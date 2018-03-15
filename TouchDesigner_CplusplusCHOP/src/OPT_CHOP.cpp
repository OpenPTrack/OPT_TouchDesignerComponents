    //Copyright (c) 2017-2016 Ian Shelanskey ishelanskey@gmail.com
//All rights reserved.

#include <vector>
#include <map>
#include <string>
#include <iostream>
#include <sstream>

#include "OPT_CHOP.h"
#include "defines.h"

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

#define SET_CHOP_ERROR(errexpr) {\
stringstream msg; \
errexpr; \
errorMessage_ = msg.str(); \
printf("%s\n", msg.str().c_str());\
}

#define SET_CHOP_WARN(errexpr) {\
stringstream msg; \
errexpr; \
warningMessage_ = msg.str(); \
}

#define PORTNUM 21234
#define NPAR_OUT 7
#define PAR_MAXTRACKED "Maxtracked"

using namespace std;

static const char* ChanNames[7] = { "id", "isAlive", "age", "confidence", "x", "y", "height"};

static shared_ptr<JsonSocketReader> SocketReader;

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
OBase(1, PORTNUM),
errorMessage_(""), warningMessage_(""),
heartbeat_(0)
{
    setupSocketReader();
}

OPT_CHOP::~OPT_CHOP()
{
    if (SocketReader)
        SocketReader->unregisterSlave(this);
}

void OPT_CHOP::getGeneralInfo(CHOP_GeneralInfo * ginfo)
{
	ginfo->cookEveryFrame = true;
}

bool OPT_CHOP::getOutputInfo(CHOP_OutputInfo * info)
{
	info->numChannels = NPAR_OUT;
	info->numSamples = info->opInputs->getParInt(PAR_MAXTRACKED);
	
	return true;
}

const char* OPT_CHOP::getChannelName(int index, void* reserved)
{
	return ChanNames[index];
}

void OPT_CHOP::execute(const CHOP_Output* output, OP_Inputs* inputs, void* reserved)
{
    warningMessage_ = "";
    errorMessage_ = "";
    checkInputs(output, inputs, reserved);
    processQueue();

    bool blankRun = true;
    
    {
        processBundle([this, output](vector<rapidjson::Document>& msgs){
            
            if (msgs.size() == 0)
                return ;
            
            // for OPT, expecting bundle size of 1 message only
            rapidjson::Document& d = msgs[0];
            
            if (d.HasMember(OPT_JSON_HEADER) &&
                d[OPT_JSON_HEADER].HasMember(OPT_JSON_FRAMEID))
            {
                string frameId(d[OPT_JSON_HEADER][OPT_JSON_FRAMEID].GetString());
                
                if (frameId == OPT_JSON_HEARTBEAT)
                {
                    heartbeat_++;
                    
                    if (!d.HasMember(OPT_JSON_MAXID))
                        SET_CHOP_WARN(msg << "can't find " << OPT_JSON_MAXID << " field in heartbeat message")
                    else
                        maxId_ = d[OPT_JSON_MAXID].GetInt();
                        
                    if (!d.HasMember(OPT_JSON_ALIVEIDS))
                        SET_CHOP_WARN(msg << "can't find " << OPT_JSON_ALIVEIDS << " field in heartbead message")
                    else
                    {
                        aliveIds_.clear();
                        
                        const rapidjson::Value& arr = d[OPT_JSON_ALIVEIDS].GetArray();
                        
                        for (int i = 0; i < arr.Size(); ++i)
                            aliveIds_.insert(arr[i].GetInt());
                    }
                } // if heartbeat
                else
                {
                    if (d.HasMember("people_tracks"))
                    {
                        vector<float> NewTracks;
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
                }
            }
            else
                SET_CHOP_WARN(msg << "can't locate " << OPT_JSON_FRAMEID << " field")
        });
    }
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
        chan->value = (float)heartbeat_;
    }
    
    if (index == 1)
    {
        chan->name = "maxId";
        chan->value = (float)maxId_;
    }
    
    if (index == 2)
    {
        chan->name = "seq";
        chan->value = (float)seq_;
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

//******************************************************************************
void
OPT_CHOP::setupSocketReader()
{
    try
    {
        errorMessage_ = "";
        
        if (!SocketReader)
            SocketReader = make_shared<JsonSocketReader>(PORTNUM);
        
        if (!SocketReader->isRunning())
            SocketReader->start();
        
        SocketReader->registerSlave(this);
    }
    catch (runtime_error& e)
    {
        SET_CHOP_ERROR(msg << e.what())
    }
}

void
OPT_CHOP::processingError(string m)
{
    SET_CHOP_WARN(msg << m);
}

void
OPT_CHOP::checkInputs(const CHOP_Output *, OP_Inputs *inputs, void *)
{
    
}

void
OPT_CHOP::blankRunsTrigger()
{
    nAliveIds_ = 0;
}

