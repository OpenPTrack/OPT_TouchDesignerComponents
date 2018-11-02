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

#define NPAR_OUT 8
#define NINFOPAR_OUT 3
#define PAR_MAXTRACKED "Maxtracked"
#define PAR_MINX "Minx"
#define PAR_MAXX "Maxx"
#define PAR_MINY "Miny"
#define PAR_MAXY "Maxy"
#define PAR_MINZ "Minz"
#define PAR_MAXZ "Maxz"
#define PAR_FILTERTOGGLE "Filtertoggle"

using namespace std;

static const char* ChanNames[8] = { "id", "age", "confidence", "x", "y", "height", "isAlive", "stableId" };
static const char* InfoChanNames[3] = { "heartbeat", "maxId", "noData" };

static shared_ptr<JsonSocketReader> SocketReader;

inline bool withinBounds(float val, float min, float max)
{
    return (val >= min && val <= max);
}

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
    checkInputs(output, inputs, reserved);
    processQueue();
#ifdef WIN32
    float minX = -FLT_MAX, maxX = FLT_MAX,
          minY = -FLT_MAX, maxY = FLT_MAX,
          minZ = -FLT_MAX, maxZ = FLT_MAX;
#else
	float minX = -MAXFLOAT, maxX = MAXFLOAT,
		minY = -MAXFLOAT, maxY = MAXFLOAT,
		minZ = -MAXFLOAT, maxZ = MAXFLOAT;
#endif
    
    if (inputs->getParInt(PAR_FILTERTOGGLE))
    {
        minX = inputs->getParDouble(PAR_MINX);
        maxX = inputs->getParDouble(PAR_MAXX);
        minY = inputs->getParDouble(PAR_MINY);
        maxY = inputs->getParDouble(PAR_MAXY);
        minZ = inputs->getParDouble(PAR_MINZ);
        maxZ = inputs->getParDouble(PAR_MAXZ);
    }
    
    map<int, vector<float>> newTracks;

    bool blankRun = true;
    
    {
        processBundle([this, output, &blankRun, &newTracks,
                       minX, maxX, minY, maxY, minZ, maxZ](vector<rapidjson::Document>& msgs){
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
                                float x = tracks[i].HasMember(OPT_JSON_X) ? tracks[i][OPT_JSON_X].GetFloat() : -1;
                                float y = tracks[i].HasMember(OPT_JSON_Y) ? tracks[i][OPT_JSON_Y].GetFloat() : -1;
                                float z = tracks[i].HasMember(OPT_JSON_HEIGHT) ? tracks[i][OPT_JSON_HEIGHT].GetFloat() : -1;
                                
                                if (withinBounds(x, minX, maxX) &&
                                    withinBounds(y, minY, maxY) &&
                                    withinBounds(z, minZ, maxZ))
                                {
                                    int trackId = tracks[i][OPT_JSON_ID].GetInt();
                                    vector<float> data;
                                    
                                    data.push_back((float)trackId);
                                    data.push_back(tracks[i].HasMember(OPT_JSON_AGE) ? tracks[i][OPT_JSON_AGE].GetFloat() : -1);
                                    data.push_back(tracks[i].HasMember(OPT_JSON_CONFIDENCE) ? tracks[i][OPT_JSON_CONFIDENCE].GetFloat() : -1);
                                    data.push_back(x);
                                    data.push_back(y);
                                    data.push_back(z);
                                    // TODO: figure out this:
                                    // since heartbeat arrives every N seconds, does it make sense to
                                    // keep alive IDs array and check incoming IDs against it?
                                    data.push_back((float)(aliveIds_.find(trackId) != aliveIds_.end()));
                                    data.push_back(tracks[i].HasMember(OPT_JSON_STABLEID) ? tracks[i][OPT_JSON_STABLEID].GetFloat() : -1);
                                    
                                    if (tracks[i].HasMember(OPT_JSON_FACE_NAME))
                                    {
                                        if (tracks[i][OPT_JSON_FACE_NAME].IsString())
                                        {
                                            string faceName(tracks[i][OPT_JSON_FACE_NAME].GetString());
                                            faceNameMap_[faceName] = trackId;
                                        }
                                        else
                                            SET_CHOP_WARN(msg << OPT_JSON_FACE_NAME << " is not a string; string expected")
                                    }
                                    
                                    newTracks.insert_or_assign(trackId, data);
                                }
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
            // check, whether there are any tracks that still alive but not in newTracks
            set<int> remainingTracks(aliveIds_);
            for (auto& p:newTracks)
                if (remainingTracks.find(p.first) != remainingTracks.end())
                    remainingTracks.erase(p.first);
            
            if (remainingTracks.size())
                for (auto& id:remainingTracks)
                    if (lastTracks_[id].size() > 5 &&
                        withinBounds(lastTracks_[id][3], minX, maxX) &&
                        withinBounds(lastTracks_[id][4], minY, maxY) &&
                        withinBounds(lastTracks_[id][5], minZ, maxZ))
                        newTracks[id] = lastTracks_[id];
            
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
            
            lastTracks_ = newTracks;
        }
    }
}

int32_t
OPT_CHOP::getNumInfoCHOPChans()
{
    return NINFOPAR_OUT+(int32_t)seqs_.size(); // hearbeat, max id
}

void
OPT_CHOP::getInfoCHOPChan(int32_t index,
                          OP_InfoCHOPChan* chan)
{
    switch (index) {
        case 0:
            chan->name = InfoChanNames[index];
            chan->value = (float)heartbeat_;
            break;
        case 1:
            chan->name = InfoChanNames[index];
            chan->value = (float)maxId_;
            break;
        case 2:
            chan->name = InfoChanNames[index];
            chan->value = (float)noData_;
            break;
        default:
        {
            map<string, int>::iterator it = seqs_.begin();
            stringstream ss;
            advance(it, index-2);
            ss << "seq_" << it->first;
            
            chan->name = ss.str().c_str();
            chan->value = it->second;
        }
            break;
    }
}

bool
OPT_CHOP::getInfoDATSize(OP_InfoDATSize *infoSize)
{
    infoSize->rows = (int32_t)faceNameMap_.size()+1;
    infoSize->cols = 2;
    infoSize->byColumn = false;
    
    return true;
}

void
OPT_CHOP::getInfoDATEntries(int32_t index, int32_t nEntries, OP_InfoDATEntries *entries)
{
    if (index == 0)
    {
        entries->values[0] = (char*)"face name";
        entries->values[1] = (char*)"track id";
    }
    else
    {
        map<string, int>::iterator it = faceNameMap_.begin();
        advance(it, index-1);
        stringstream ss;
        ss << it->second;
        
        entries->values[0] = (char*)it->first.c_str();
        entries->values[1] = (char*)ss.str().c_str();
    }
}

void OPT_CHOP::setupParameters(OP_ParameterManager* manager) 
{
    {
        OP_NumericParameter maxTracked(PAR_MAXTRACKED);
        
        maxTracked.label = "Max Tracked";
        maxTracked.page = "General";
        maxTracked.defaultValues[0] = 1;
		maxTracked.minValues[0] = 1;
		maxTracked.maxValues[0] = 25;
        maxTracked.minSliders[0] = 1;
		maxTracked.maxSliders[0] = 25;
        
        OP_ParAppendResult res = manager->appendInt(maxTracked);
        assert(res == OP_ParAppendResult::Success);
    }
    {
        OP_NumericParameter filterToggle(PAR_FILTERTOGGLE);
        OP_NumericParameter minX(PAR_MINX), maxX(PAR_MAXX),
        minY(PAR_MINY), maxY(PAR_MAXY),
        minZ(PAR_MINZ), maxZ(PAR_MAXZ);
        
        minX.label = "Min X";
        minX.page = "Filtering";
        minX.defaultValues[0] = -5;
        minX.minSliders[0] = -100;
        minX.maxSliders[0] = 100;
        
        maxX.label = "Max X";
        maxX.page = "Filtering";
        maxX.defaultValues[0] = 5;
        maxX.minSliders[0] = -100;
        maxX.maxSliders[0] = 100;
        
        minY.label = "Min Y";
        minY.page = "Filtering";
        minY.defaultValues[0] = -5;
        minY.minSliders[0] = -100;
        minY.maxSliders[0] = 100;
        
        maxY.label = "Max Y";
        maxY.page = "Filtering";
        maxY.defaultValues[0] = 5;
        maxY.minSliders[0] = -100;
        maxY.maxSliders[0] = 100;
        
        minZ.label = "Min Z";
        minZ.page = "Filtering";
        minZ.defaultValues[0] = -5;
        minZ.minSliders[0] = -100;
        minZ.maxSliders[0] = 100;
        
        maxZ.label = "Max Z";
        maxZ.page = "Filtering";
        maxZ.defaultValues[0] = 5;
        maxZ.minSliders[0] = -100;
        maxZ.maxSliders[0] = 100;
        
        filterToggle.label = "Filter";
        filterToggle.page = "Filtering";
        
        OP_ParAppendResult res = manager->appendToggle(filterToggle);
        assert(res == OP_ParAppendResult::Success);
        res = manager->appendFloat(minX);
        assert(res == OP_ParAppendResult::Success);
        res = manager->appendFloat(maxX);
        assert(res == OP_ParAppendResult::Success);
        res = manager->appendFloat(minY);
        assert(res == OP_ParAppendResult::Success);
        res = manager->appendFloat(maxY);
        assert(res == OP_ParAppendResult::Success);
        res = manager->appendFloat(minZ);
        assert(res == OP_ParAppendResult::Success);
        res = manager->appendFloat(maxZ);
        assert(res == OP_ParAppendResult::Success);
    }
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
    
    bool filteringEnabled = inputs->getParInt(PAR_FILTERTOGGLE);
    
    inputs->enablePar(PAR_MINX, filteringEnabled);
    inputs->enablePar(PAR_MAXX, filteringEnabled);
    inputs->enablePar(PAR_MINY, filteringEnabled);
    inputs->enablePar(PAR_MAXY, filteringEnabled);
    inputs->enablePar(PAR_MINZ, filteringEnabled);
    inputs->enablePar(PAR_MAXZ, filteringEnabled);
}

void
OPT_CHOP::blankRunsTrigger()
{
    nAliveIds_ = 0;
}

