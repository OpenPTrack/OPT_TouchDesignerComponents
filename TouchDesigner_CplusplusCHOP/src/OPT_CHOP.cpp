//
//  OPT_CHOP.hpp
//  OPT_CHOP
//
//  Created by Peter Gusev on 3/15/18.
//
//  Copyright (c) 2017-2016 Ian Shelanskey ishelanskey@gmail.com
//  Copyright © 2018 UCLA. All rights reserved.
//

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

static const char* ChanNames[7] = { "id", "age", "confidence", "x", "y", "height", "isAlive"};

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
    
    map<int, vector<float>> newTracks;

    bool blankRun = true;
    
    {
        processBundle([this, output, &blankRun, &newTracks](vector<rapidjson::Document>& msgs){
            if (msgs.size() == 0)
                return ;
            
            string bundleStr = bundleToString(msgs);
            
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
                        SET_CHOP_WARN(msg << "can't find " << OPT_JSON_MAXID
                                      << " field in heartbeat message")
                    else
                        maxId_ = d[OPT_JSON_MAXID].GetInt();
                        
                    if (!d.HasMember(OPT_JSON_ALIVEIDS))
                        SET_CHOP_WARN(msg << "can't find " << OPT_JSON_ALIVEIDS
                                      << " field in heartbeat message: " << bundleStr)
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
                    if (frameId == OPT_JSON_WORLD &&
                        d.HasMember(OPT_JSON_PEOPLE_TRACKS) &&
                        d[OPT_JSON_PEOPLE_TRACKS].IsArray())
                    {
                        vector<float> NewTracks;
                        const rapidjson::Value& tracks = d[OPT_JSON_PEOPLE_TRACKS].GetArray();
                        
                        //For each new track.
                        for (rapidjson::SizeType i = 0; i < tracks.Size(); i++)
                        {
                            if (!tracks[i].HasMember(OPT_JSON_ID))
                                SET_CHOP_WARN(msg << "track doesn't have " << OPT_JSON_ID << " field")
                            else
                            {
                                int trackId = tracks[i][OPT_JSON_ID].GetInt();
                                vector<float> data;
                                
                                data.push_back((float)trackId);
                                data.push_back(tracks[i].HasMember(OPT_JSON_AGE) ? tracks[i][OPT_JSON_AGE].GetFloat() : -1);
                                data.push_back(tracks[i].HasMember(OPT_JSON_CONFIDENCE) ? tracks[i][OPT_JSON_CONFIDENCE].GetFloat() : -1);
                                data.push_back(tracks[i].HasMember(OPT_JSON_X) ? tracks[i][OPT_JSON_X].GetFloat() : -1);
                                data.push_back(tracks[i].HasMember(OPT_JSON_Y) ? tracks[i][OPT_JSON_Y].GetFloat() : -1);
                                data.push_back(tracks[i].HasMember(OPT_JSON_HEIGHT) ? tracks[i][OPT_JSON_HEIGHT].GetFloat() : -1);
                                data.push_back((float)(aliveIds_.find(trackId) != aliveIds_.end()));
                                
                                newTracks.insert_or_assign(trackId, data);
                            }
                        } // for tracks
                        
                        blankRun = false;
                    } // if not world frameid
                    else
                        SET_CHOP_WARN(msg << "no " << OPT_JSON_PEOPLE_TRACKS
                                      << " field or it's not a list in incoming message: " << bundleStr)
                } // if not heartbeat
            }
            else
                SET_CHOP_WARN(msg << "can't locate " << OPT_JSON_FRAMEID << " field")
        });
        
        if (!blankRun)
        {
            map<int, vector<float>>::iterator it = newTracks.begin();
            for (int i = 0; i < output->numSamples; i++) {
                
                if (it != newTracks.end())
                    assert(it->second.size() == NPAR_OUT);
                
                for (int chanIdx = 0; chanIdx < output->numChannels; ++chanIdx)
                    if (it != newTracks.end() && chanIdx < it->second.size())
                    {
                        output->channels[chanIdx][i] = it->second[chanIdx];
                    }
                    else
                        output->channels[chanIdx][i] = 0;
                
                if (it != newTracks.end())
                    it++;
            } // for i
        }
    }
}

int32_t
OPT_CHOP::getNumInfoCHOPChans()
{
    return 2+(int32_t)seqs_.size(); // hearbeat, max id
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
    
    if (index >= 2)
    {
        stringstream ss;
        map<string, int>::iterator it = seqs_.begin();
        
        advance(it, index-2);
        ss << "seq_" << it->first;
        
        chan->name = ss.str().c_str();
        chan->value = it->second;
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

