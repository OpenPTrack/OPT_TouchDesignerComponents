//
//  OM_CHOP.cpp
//  OM_CHOP
//
//  Created by Peter Gusev on 3/3/18.
//  Copyright © 2018 UCLA. All rights reserved.
//

#include "OM_CHOP.hpp"

#include <iostream>

#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#ifdef WIN32
    #include "stdafx.h"
    #include <winsock2.h>

    #pragma comment(lib,"ws2_32.lib") //Winsock Library
#else
    #include <errno.h>
    #include <unistd.h>

    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    #define WSAGetLastError() errno
#endif

#ifdef WIN32
    #ifdef OM_CHOP_API
        #define OM_CHOP_API __declspec(dllexport)
    #else
        #define OM_CHOP_API __declspec(dllimport)
    #endif
#else
    #define OM_CHOP_API DLLEXPORT
#endif

#define PORTNUM 21234 // 21235 // openmoves

using namespace std;

//Required functions.
extern "C"
{
    //Lets Touch know the API version.
    OM_CHOP_API int GetCHOPAPIVersion(void)
    {
        // Always return CHOP_CPLUSPLUS_API_VERSION in this function.
        return CHOP_CPLUSPLUS_API_VERSION;
    }
    
    //Lets Touch create a new Instance of the CHOP
    OM_CHOP_API CHOP_CPlusPlusBase* CreateCHOPInstance(const OP_NodeInfo* info)
    {
        // Return a new instance of your class every time this is called.
        // It will be called once per CHOP that is using the .dll
        return new OM_CHOP(info);
    }
    
    //Let's Touch Destroy Instance of CHOP
    OM_CHOP_API void DestroyCHOPInstance(CHOP_CPlusPlusBase* instance)
    {
        // Delete the instance here, this will be called when
        // Touch is shutting down, when the CHOP using that instance is deleted, or
        // if the CHOP loads a different DLL
        delete (OM_CHOP*)instance;
    }
};

static shared_ptr<JsonSocketReader> SocketReader;

OM_CHOP::OM_CHOP(const OP_NodeInfo * info):
heartbeat(0), maxId(0),
errorMessage_(""), warningMessage_("")
{
    setupSocketReader();
}

OM_CHOP::~OM_CHOP()
{
    SocketReader->unregisterSlave(this);
}

void OM_CHOP::getGeneralInfo(CHOP_GeneralInfo * ginfo)
{
    //Forces check on socket each time TouchDesigner cooks.
    ginfo->cookEveryFrame = true;
}

bool OM_CHOP::getOutputInfo(CHOP_OutputInfo * info)
{
    //OPT Streaming data (ID, x, y).
    info->numChannels = 6;
    
    //Sets the maximum number of people to be tracked.
    info->numSamples = info->opInputs->getParInt("Maxtracked");
    
    return true;
}

const char* OM_CHOP::getChannelName(int index, void* reserved)
{
    //ID, age, confidence, x, y, height
    return names[index];
}

void OM_CHOP::execute(const CHOP_Output* output, OP_Inputs* inputs, void* reserved)
{
    
    if (documentQueue_.size())
    {
        rapidjson::Document d(move(documentQueue_.front()));
        documentQueue_.pop();
        
        //Create objects to hold incoming data
        std::vector<float> NewTracks;
        std::vector<float> incoming;
        
        
//        if (d.HasMember("firstdirs"))
//        {
//            const rapidjson::Value& tracks = d["firstdirs"].GetArray();
//
//            for (rapidjson::SizeType i = 0; i < tracks.Size(); i++)
//            {
//                //Create a vector of new tracks data.
//                incoming = { float(tracks[i]["x"].GetFloat()),
//                    float(tracks[i]["y"].GetFloat()) };
//
//                //Get id of track.
//                float newid = float(tracks[i]["id"].GetInt());
//
//                //Add id to vector of ids to use in find and replace operation later.
//                NewTracks.push_back(newid);
//
//                //Map data to its id.
//                data.insert_or_assign(newid, incoming);
//            } // for
//        }
        
//        if (d.HasMember("seconddirs"))
//        {
//            const rapidjson::Value& tracks = d["seconddirs"].GetArray();
//
//            for (rapidjson::SizeType i = 0; i < tracks.Size(); i++)
//            {
//                //Create a vector of new tracks data.
//                incoming.push_back(float(tracks[i]["x"].GetFloat()));
//                incoming.push_back(float(tracks[i]["y"].GetFloat()));
//
//                //Get id of track.
//                float newid = float(tracks[i]["id"].GetInt());
//
//                //Add id to vector of ids to use in find and replace operation later.
//                NewTracks.push_back(newid);
//
//                //Map data to its id.
//                data.insert_or_assign(newid, incoming);
//            } // for
//        }
        
        const char* frameId = d["header"]["frame_id"].GetString();
        
        if (std::string(frameId) == "heartbeat")
        {
            heartbeat++;
            maxId = d["max_ID"].GetInt();
        }
        else //Get all of the tracks from JSON.
        {
            const rapidjson::Value& tracks = d["people_tracks"].GetArray();

            //For each new track.
            for (rapidjson::SizeType i = 0; i < tracks.Size(); i++)
            {
                //Create a vector of new tracks data.
                std::vector<float> incoming = {    float(tracks[i]["age"].GetFloat()),
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
                
#if 1
                if (lookupid < 0) {
                    //Set any open slots to 0.
                    output->channels[1][i] = 0;
                    output->channels[2][i] = 0;
                    output->channels[3][i] = 0;
                    output->channels[4][i] = 0;
                    output->channels[5][i] = 0;
                }
                else {
                    if (data.find(lookupid) != data.end())
                    {
                        //Lookup track data based on ID in data map.
                        output->channels[1][i] = data[lookupid][0]; //age
                        output->channels[2][i] = data[lookupid][1]; //confidence
                        output->channels[3][i] = data[lookupid][2]; //x
                        output->channels[4][i] = data[lookupid][3]; //y
                        output->channels[5][i] = data[lookupid][4]; //height
                    }
                }
#else
                
                if (lookupid < 0) {
                    //Set any open slots to 0.
                    output->channels[1][i] = 0;
                    output->channels[2][i] = 0;
                }
                else {
                    if (data.find(lookupid) != data.end())
                    {
                        //Lookup track data based on ID in data map.
                        output->channels[1][i] = data[lookupid][0]; //x
                        output->channels[2][i] = data[lookupid][1]; //y
                    }
                }
#endif
            } // for
        } // if not heartbeat
    } // if buffer is not empty
}

int32_t
OM_CHOP::getNumInfoCHOPChans()
{
    return 2; // hearbeat, max id
}

void
OM_CHOP::getInfoCHOPChan(int32_t index,
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

void OM_CHOP::setupParameters(OP_ParameterManager* manager)
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
OM_CHOP::setupSocketReader()
{
    if (!SocketReader)
    {
        try
        {
            errorMessage_ = "";
            SocketReader = make_shared<JsonSocketReader>(PORTNUM);
        }
        catch (runtime_error& e)
        {
            errorMessage_ = e.what();
        }
    }
    
    if (!SocketReader->isRunning())
        SocketReader->start();
    
    SocketReader->registerSlave(this);
}

void
OM_CHOP::onNewJsonOnjectReceived(const rapidjson::Document &d)
{
    errorMessage_ = "";
    warningMessage_ = "";
    
    // check thread safety for vector
    rapidjson::Document dcopy;
    dcopy.CopyFrom(d, dcopy.GetAllocator());
    
    documentQueue_.push(move(dcopy));
    
    cout << "received new json" << endl;
}

void
OM_CHOP::onSocketError(const std::string &msg)
{
    warningMessage_ = msg;
}
