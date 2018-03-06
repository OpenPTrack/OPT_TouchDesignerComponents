    //Copyright (c) 2017-2016 Ian Shelanskey ishelanskey@gmail.com
//All rights reserved.

#include <vector>
#include <map>
#include <string>
#include <iostream>
#include <sstream>

#include "OPT_CHOP.h"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#ifdef WIN32
    #include <winsock2.h>

    #pragma comment(lib,"ws2_32.lib") //Winsock Library
#else
    #include <errno.h>
    #include <unistd.h>

    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    #define WSAGetLastError() errno
#endif

#define OPT_CHOP_API

#ifdef WIN32
    #ifdef OPT_CHOP_API
        #define OPT_CHOP_API __declspec(dllexport)
    #else
        #define OPT_CHOP_API __declspec(dllimport)
    #endif
#else
    #define OPT_CHOP_API DLLEXPORT
#endif

#define SET_CHOP_ERROR(errexpr) (\
{\
    std::stringstream msg; \
    errexpr; \
    errorMessage_ = msg.str(); \
    perror(msg.str().c_str()); \
})

#define SET_CHOP_ERROR(errexpr) (\
{\
    std::stringstream msg; \
    errexpr; \
    warningMessage_ = msg.str(); \
    perror(msg.str().c_str()); \
})

#define PRINT_MSG

#define PORT 21234

using namespace std;

//Required functions.
extern "C"
{
	//Lets Touch know the API version.
	OPT_CHOP_API int GetCHOPAPIVersion(void)
	{
		// Always return CHOP_CPLUSPLUS_API_VERSION in this function.
		return CHOP_CPLUSPLUS_API_VERSION;
	}

	//Lets Touch create a new Instance of the CHOP
	OPT_CHOP_API CHOP_CPlusPlusBase* CreateCHOPInstance(const OP_NodeInfo* info)
	{
		// Return a new instance of your class every time this is called.
		// It will be called once per CHOP that is using the .dll
		return new OPT_CHOP(info);
	}

	//Let's Touch Destroy Instance of CHOP
	OPT_CHOP_API void DestroyCHOPInstance(CHOP_CPlusPlusBase* instance)
	{
		// Delete the instance here, this will be called when
		// Touch is shutting down, when the CHOP using that instance is deleted, or
		// if the CHOP loads a different DLL
		delete (OPT_CHOP*)instance;
	}
};


OPT_CHOP::OPT_CHOP(const OP_NodeInfo * info):
heartbeat(0), maxId(0)
{
	//Create a UDP Socket.
	slen = sizeof(si_other);

#ifdef WIN32
    DWORD timeout = 1;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
		printf("Failed. Error code: %d", WSAGetLastError());
		exit(EXIT_FAILURE);
	}
#else
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 100;
#endif
    
	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET)
    {
        SET_CHOP_ERROR(msg << "Socket creation failure (" << WSAGetLastError() << "): " << strerror(WSAGetLastError()));
	}

	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons(PORT);
    
	if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0)
    {
        SET_CHOP_ERROR(msg << "Socket configuration error (" << WSAGetLastError() << "): " << strerror(WSAGetLastError()));
	}
    
    if (:: bind(s, (struct sockaddr *)&server, sizeof(server)) == SOCKET_ERROR)
    {
        SET_CHOP_ERROR(msg << "Socket bind failure (" << WSAGetLastError() << "): " << strerror(WSAGetLastError()));
    }
}

OPT_CHOP::~OPT_CHOP()
{
	//Destroy UDP Socket
#ifdef WIN32
	closesocket(s);
	WSACleanup();
#else
    close(s);
#endif
}

void OPT_CHOP::getGeneralInfo(CHOP_GeneralInfo * ginfo)
{
	//Forces check on socket each time TouchDesigner cooks. 
	ginfo->cookEveryFrame = true;
}

bool OPT_CHOP::getOutputInfo(CHOP_OutputInfo * info)
{
	//OPT Streaming data (ID, isAlive age, confidence, x, y, height).
	info->numChannels = 7;

	//Sets the maximum number of people to be tracked.
	info->numSamples = info->opInputs->getParInt("Maxtracked");
	
	return true;
}

const char* OPT_CHOP::getChannelName(int index, void* reserved)
{
	//ID, age, confidence, x, y, height
	return names[index];
}

void OPT_CHOP::execute(const CHOP_Output* output, OP_Inputs* inputs, void* reserved)
{
	fflush(stdout);

	//clear buffer
	memset(buf, '\0', BUFLEN);

	if ((recv_len = recvfrom(s, buf, BUFLEN, 0, (struct sockaddr *) &si_other, &slen)) == SOCKET_ERROR) {
		//Should be an error here. Touch doesn't like that.
	}

	//If buffer isn't empty then parse and update channels.
    if (buf[0] != '\0') {
        
#ifdef PRINT_MSG
        cout << "received msg: " << buf << endl;
#endif
        
        //Create objects to hold incoming data
        std::vector<float> NewTracks;
        rapidjson::Document d;
        
        //Parse json from UDP buffer.
        d.Parse(buf);
        
        const char* frameId = d["header"]["frame_id"].GetString();
        
        if (std::string(frameId) == "heartbeat")
        {
            heartbeat++;
            maxId = d["max_ID"].GetInt();
            
            const rapidjson::Value& arr = d["alive_IDs"].GetArray();
            aliveIds_.clear();
            
            for (int i = 0; i < arr.Size(); ++i)
                aliveIds_.insert(arr[i].GetInt());
        }
        else //Get all of the tracks from JSON.
        {
            if (d.HasMember("people_tracks"))
            {
                const rapidjson::Value& tracks = d["people_tracks"].GetArray();
                
                //For each new track.
                for (rapidjson::SizeType i = 0; i < tracks.Size(); i++)
                {
                    //Create a vector of new tracks data.
                    std::vector<float> incoming = {
                        float(tracks[i]["age"].GetFloat()),
                        float(tracks[i]["confidence"].GetFloat()),
                        float(tracks[i]["x"].GetFloat()),
                        float(tracks[i]["y"].GetFloat()),
                        float(tracks[i]["height"].GetFloat()) };
                    
                    //Get id of track.
                    float newid = float(tracks[i]["id"].GetInt());
                    
                    //Add id to vector of ids to use in find and replace operation later.
                    NewTracks.push_back(newid);
                    
                    //Map data to its id.
                    data.insert_or_assign(newid, incoming);
                } // for
                
                
                //Create counters for ID matching and sorting.
                int offset = 0; //New data
                int i = 0; //Old data
                
                //Go through each Track ID.
                while(offset < NewTracks.size())
                {
                    //Find tracks not being used.
                    if (i < output->numSamples) {
                        //If the previous track is not in the new frame of data.
                        if (NewTracks[offset] != output->channels[0][i]) {
                            //Change its id to -1.
                            output->channels[0][i] = -1;
                            //Increase old track counter.
                            i++;
                            continue;
                        }
                        else {
                            //We are still tracking this old ID.
                            i++;
                            offset++;
                        }
                    }
                    else {
                        //Look for any ids that are -1 to replace with overflow track data.
                        float * p;
                        p = std::find(output->channels[0], output->channels[0]+output->numSamples, -1);
                        if (p == output->channels[0] + output->numSamples) {
                            //There are no empty slots for new data.
                            break;
                        }
                        else {
                            //Replace empty slot with overflow track data.
                            if (NewTracks.size() - offset > 0 ) {
                                //TODO add filters - age, confidence.
                                *p = NewTracks[offset];
                            }
                            else {
                                //No overflow track data exists. Leave them as -1.
                                break;
                            }
                        }
                        offset++;
                    }
                } // while
                
                //Iterate through IDs.
                for (int i = 0; i < output->numSamples; i++) {
                    float lookupid = output->channels[0][i];
                    
                    if (lookupid < 0) {
                        //Set any open slots to 0.
                        output->channels[1][i] = 0;
                        output->channels[2][i] = 0;
                        output->channels[3][i] = 0;
                        output->channels[4][i] = 0;
                        output->channels[5][i] = 0;
                        output->channels[6][i] = 0;
                    }
                    else {
                        if (data.find(lookupid) != data.end())
                        {
                            //Lookup track data based on ID in data map.
                            output->channels[1][i] = (float)(aliveIds_.find(lookupid) != aliveIds_.end()); // isAlive
                            output->channels[2][i] = data[lookupid][0]; //age
                            output->channels[3][i] = data[lookupid][1]; //confidence
                            output->channels[4][i] = data[lookupid][3]; //x
                            output->channels[5][i] = data[lookupid][4]; //y
                            output->channels[6][i] = data[lookupid][5]; //height
                        }
                    }
                } // for
            }
        } // if not heartbeat
    } // if buffer is not empty
}

int32_t
OPT_CHOP::getNumInfoCHOPChans()
{
    return 2; // hearbeat, max id
}

void
OPT_CHOP::getInfoCHOPChan(int32_t index,
                          OP_InfoCHOPChan* chan)
{
    if (index == 0)
    {
        chan->name = "heartbeat";
        chan->value = (float)heartbeat;
    }
    
    if (index == 1)
    {
        chan->name = "maxId";
        chan->value = (float)maxId;
    }
}

void OPT_CHOP::setupParameters(OP_ParameterManager* manager) 
{
	//Create new parameter 
	OP_NumericParameter MaxTracked;

	//Parameter details
	MaxTracked.name = "Maxtracked";
	MaxTracked.label = "Max Tracked";
	MaxTracked.page = "OPT General";
	MaxTracked.defaultValues[0] = 1;
	MaxTracked.minValues[0] = 1;

	//Add it to CHOP.
    OP_ParAppendResult res = manager->appendInt(MaxTracked);
    assert(res == OP_ParAppendResult::Success);

}
