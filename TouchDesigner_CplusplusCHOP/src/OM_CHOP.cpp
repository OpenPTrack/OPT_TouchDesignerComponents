//
//  OM_CHOP.cpp
//  OM_CHOP
//
//  Created by Peter Gusev on 3/3/18.
//  Copyright © 2018 UCLA. All rights reserved.
//

#include "OM_CHOP.hpp"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>

#include "om-json-parser.hpp"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "defines.h"
#include "debug.h"

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

#ifdef WIN32
//    #ifdef OM_CHOP_API
        #define OM_CHOP_API __declspec(dllexport)
//    #else
//        #define OM_CHOP_API __declspec(dllimport)
//    #endif
#else
    #define OM_CHOP_API DLLEXPORT
#endif

#define PORTNUM 21235

#define OPENMOVES_MSG_BUNDLE 1
#define BLANK_RUN_THRESHOLD 60

#define PAIRWISE_MAXDIM 25
#define PAIRWISE_WIDTH (PAIRWISE_MAXDIM+1)
#define PAIRWISE_HEIGHT (PAIRWISE_MAXDIM)
#define PAIRWISE_SIZE ((PAIRWISE_WIDTH)*PAIRWISE_HEIGHT)

#define NPAR_OUTPUT 9
#define PAR_OUTPUT  "Output"
#define PAR_REINIT  "Init"
#define PAR_PORTNUM "Portnum"
#define PAR_MAXTRACKED "Maxtracked"
#define PAR_CLUSTERID "Clusterid"

#define SET_CHOP_ERROR(errexpr) {\
stringstream msg; \
errexpr; \
if (errorMessage_ == "") errorMessage_ = msg.str(); \
printf("%s\n", msg.str().c_str());\
}

#define SET_CHOP_WARN(errexpr) {\
stringstream msg; \
errexpr; \
if (warningMessage_ == "") warningMessage_ = msg.str(); \
}

using namespace std;
using namespace chrono;

static const char* DerOutNames[7] = { "id", "d1x", "d1y", "d2x", "d2y", "speed", "accel"};
static const char* StageDistNames[5] = { "id", "us", "ds", "sl", "sr"};
static const char* ClusterOutNames[4] = { "x", "y", "spread", "size" };
static const char* ClusterIdsOutNames[3] = { "id", "x", "y"};
static const char* HotspotsOutNames[3] = { "x", "y", "spread"};
static const char* GroupTargetNames[4] = { "val", "x", "y", "z"};

static const char *menuNames[] = { "Derivatives", "Pairwise", "Dtw", "Clusters", "Clusterids", "Hotspots", "Pca", "Stagedist", "Templates" };
static const char *labels[] = { "Derivatives", "Pairwise matrix", "Path similarity", "Clusters", "Cluster IDs", "Hotspots", "Group target", "Stage Distances", "Templates" };
static map<string, OM_CHOP::OutChoice> OutputMenuMap = {
    { "Unknown", OM_CHOP::OutChoice::Unknown },
    { "Derivatives", OM_CHOP::OutChoice::Derivatives },
    { "Pairwise", OM_CHOP::OutChoice::Pairwise },
    { "Dtw", OM_CHOP::OutChoice::Dtw },
    { "Clusters", OM_CHOP::OutChoice::Cluster },
    { "Clusterids", OM_CHOP::OutChoice::ClusterIds },
    { "Hotspots", OM_CHOP::OutChoice::Hotspots },
    { "Pca", OM_CHOP::OutChoice::Pca },
    { "Stagedist", OM_CHOP::OutChoice::Stagedist },
    { "Templates", OM_CHOP::OutChoice::Templates }
};

static map<OM_CHOP::OutChoice, string> OutputSubtypeMap = {
    { OM_CHOP::OutChoice::Unknown, "all" },
    { OM_CHOP::OutChoice::Derivatives, OM_JSON_SUBTYPE_DERS },
    { OM_CHOP::OutChoice::Pairwise, OM_JSON_SUBTYPE_DIST },
    { OM_CHOP::OutChoice::Dtw, OM_JSON_SUBTYPE_SIM },
    { OM_CHOP::OutChoice::Cluster, OM_JSON_SUBTYPE_CLUSTER },
    { OM_CHOP::OutChoice::ClusterIds, OM_JSON_SUBTYPE_CLUSTER },
    { OM_CHOP::OutChoice::Hotspots, OM_JSON_SUBTYPE_MDYN },
    { OM_CHOP::OutChoice::Pca, OM_JSON_SUBTYPE_MDYN },
    { OM_CHOP::OutChoice::Stagedist, OM_JSON_SUBTYPE_DIST },
    { OM_CHOP::OutChoice::Templates, OM_JSON_SUBTYPE_SIM }
};

static shared_ptr<JsonSocketReader> SocketReader;

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

//******************************************************************************
OM_CHOP::OM_CHOP(const OP_NodeInfo * info):
OBase(OPENMOVES_MSG_BUNDLE, PORTNUM),
errorMessage_(""), warningMessage_(""),
outChoice_(Derivatives),
nAliveIds_(0),nClusters_(0),
omJsonParser_(make_shared<OmJsonParser>(PAIRWISE_MAXDIM))
{
    setupSocketReader();
}

OM_CHOP::~OM_CHOP()
{
    if (SocketReader)
        SocketReader->unregisterSlave(this);
}

void OM_CHOP::getGeneralInfo(CHOP_GeneralInfo * ginfo)
{
    ginfo->cookEveryFrame = true;
}

bool OM_CHOP::getOutputInfo(CHOP_OutputInfo * info)
{
    info->numSamples = info->opInputs->getParInt(PAR_MAXTRACKED);
    
    switch (outChoice_) {
        case Derivatives:
            info->numChannels = 7; // id d1x d1y d2x d2y speed acceleration
            break;
        case Pairwise: // fallthrough
        case Dtw:
            info->numSamples = info->opInputs->getParInt(PAR_MAXTRACKED)+1;
            info->numChannels = info->opInputs->getParInt(PAR_MAXTRACKED);
            break;
        case Cluster:
            info->numChannels = 4; // x y spread size
            break;
        case ClusterIds:
            info->numChannels = 3; // id x y of - a person
            break;
        case Stagedist:
            info->numChannels = 5; // id US DS SL SR
            break;
        case Hotspots:
            info->numChannels = 3; // x y z
            break;
        case Pca:
            info->numSamples = 2;
            info->numChannels = 4;
            break;
        case Templates:
            break;
        case Unknown:
        default:
            break;
    }
    
    return true;
}

const char* OM_CHOP::getChannelName(int index, void* reserved)
{
    switch (outChoice_) {
        case Derivatives:
        {
            return DerOutNames[index];
        }
            break;
        case Pairwise: // fallthrough
        case Dtw:
        {
            stringstream ss;
            ss << "row" << index;
            
            return ss.str().c_str();
        }
        case Cluster:
        {
            return ClusterOutNames[index];
        }
            break;
        case ClusterIds:
        {
            return ClusterIdsOutNames[index];
        }
            break;
        case Stagedist:
        {
            return StageDistNames[index];
        }
            break;
        case Hotspots:
        {
            return HotspotsOutNames[index];
        }
            break;
        case Pca:
        {
            return GroupTargetNames[index];
        }
            break;
        default:
            break;
    }
    
    return "n/a";
}

void OM_CHOP::execute(const CHOP_Output* output, OP_Inputs* inputs, void* reserved)
{
    warningMessage_ = "";
    errorMessage_ = "";
    checkInputs(output, inputs, reserved);
    processQueue();
    
    bool blankRun = true;
    
    {
        string bundleStr;
        shared_ptr<OmJsonParser> parser = omJsonParser_;
        string subtypeToParse = OutputSubtypeMap[outChoice_];
        
        processBundle([&bundleStr, &blankRun, this,
                       subtypeToParse, parser](vector<rapidjson::Document>& msgs){
#ifdef PRINT_MESSAGES
            for (auto& m:msgs)
            {
                rapidjson::StringBuffer buffer;
                buffer.Clear();
                rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
                m.Accept(writer);
                cout << "got message: " << buffer.GetString() << endl;
            }
#endif
            
            set<string> parsedSubtypes;
            
            if (!parser->parse(msgs, parsedSubtypes, subtypeToParse))
                SET_CHOP_WARN(msg << "Failed to parse subtype " << subtypeToParse
                              << " due to error: " << omJsonParser_->getParseError())
                
                blankRun = (parsedSubtypes.size() == 0);
                this->nAliveIds_ = parser->getIdOrder().size();
        });
        
        switch (outChoice_) {
            case Derivatives:
            {
                set<int> fixedSamples;
                long sampleIdx = 0;
                
                for (auto pair:omJsonParser_->getD1()) // { id -> <dx,dy> }
                {
                    int id = pair.first;
                    // get sample idx of this id
                    vector<int>::const_iterator it = find(omJsonParser_->getIdOrder().begin(),
                                                    omJsonParser_->getIdOrder().end(), id);
                    
                    if (it == omJsonParser_->getIdOrder().end())
                    {
                        SET_CHOP_WARN(msg << "1st Derivatives id (" << id << ") was not found in available id"
                                      " list. Message bundle: " << bundleStr)
//                        assert(it != idOrder.end());
                    }
                    else
                    {
                        sampleIdx = it-omJsonParser_->getIdOrder().begin();
                        if (sampleIdx < output->numChannels)
                        {
                            fixedSamples.insert(sampleIdx);
                            output->channels[0][sampleIdx] = id;
                            output->channels[1][sampleIdx] = pair.second[0];
                            output->channels[2][sampleIdx] = pair.second[1];
                        }
                    }
                }
                
                // TODO: this prob need to be smarter than just a loop
                for (int i = 0; i < output->numSamples; ++i)
                    if (fixedSamples.find(i) == fixedSamples.end())
                        output->channels[0][i] = -1;
                
                for (auto pair:omJsonParser_->getD2()) // { id -> <dx,dy> }
                {
                    int id = pair.first;
                    // get sample idx of this id
                    vector<int>::const_iterator it = find(omJsonParser_->getIdOrder().begin(),
                                                    omJsonParser_->getIdOrder().end(), id);
                    
                    if (it == omJsonParser_->getIdOrder().end())
                    {
                        SET_CHOP_WARN(msg << "2nd Derivatives id (" << id << ") was not found in available id"
                                      " list. Message bundle: " << bundleStr)
//                        assert(it != idOrder.end());
                    }
                    else
                    {
                        sampleIdx = it-omJsonParser_->getIdOrder().begin();
                        
                        if (sampleIdx < output->numSamples)
                        {
                            output->channels[3][sampleIdx] = pair.second[0];
                            output->channels[4][sampleIdx] = pair.second[1];
                        }
                    }
                }
                
                for (auto pair:omJsonParser_->getSpeeds())
                {
                    int id = pair.first;
                    vector<int>::const_iterator it = find(omJsonParser_->getIdOrder().begin(),
                                                    omJsonParser_->getIdOrder().end(), id);
                    
                    if (it == omJsonParser_->getIdOrder().end())
                    {
                        SET_CHOP_WARN(msg << "Speed id (" << id << ") was not found in available id"
                                      " list. Message bundle: " << bundleStr)
                    }
                    else
                    {
                        long sampleIdx = it-omJsonParser_->getIdOrder().begin();
                        if (sampleIdx < output->numSamples)
                            output->channels[5][sampleIdx] = pair.second;
                    }
                }
                for (auto pair:omJsonParser_->getAccelerations())
                {
                    int id = pair.first;
                    vector<int>::const_iterator it = find(omJsonParser_->getIdOrder().begin(),
                                                    omJsonParser_->getIdOrder().end(), id);
                    
                    if (it == omJsonParser_->getIdOrder().end())
                    {
                        SET_CHOP_WARN(msg << "Acceleration id (" << id << ") was not found in available id"
                                      " list. Message bundle: " << bundleStr)
                    }
                    else
                    {
                        long sampleIdx = it-omJsonParser_->getIdOrder().begin();
                        if (sampleIdx < output->numChannels)
                            output->channels[6][sampleIdx] = pair.second;
                    }
                }
            }
                break;
            case Pairwise:
            {   
                for (int chanIdx = 0; chanIdx < output->numChannels; chanIdx++)
                    for (int sampleIdx = 0; sampleIdx < output->numSamples; sampleIdx++)
                        output->channels[chanIdx][sampleIdx] = omJsonParser_->getPairwiseMat()[chanIdx*PAIRWISE_WIDTH+sampleIdx];
            }
                break;
            case Dtw:
            {
                
            }
                break;
            case Cluster:
            {
                for (int sampleIdx = 0; sampleIdx < output->numSamples; sampleIdx++)
                {
                    if (sampleIdx < omJsonParser_->getClusters().size())
                    {
                        output->channels[0][sampleIdx] = omJsonParser_->getClusters()[sampleIdx][0];
                        output->channels[1][sampleIdx] = omJsonParser_->getClusters()[sampleIdx][1];
                        output->channels[2][sampleIdx] = omJsonParser_->getClusters()[sampleIdx][2];
                        output->channels[3][sampleIdx] = omJsonParser_->getClusterIds()[sampleIdx].size();
                    }
                    else if (!blankRun)
                    {
                        output->channels[0][sampleIdx] = 0;
                        output->channels[1][sampleIdx] = 0;
                        output->channels[2][sampleIdx] = 0;
                        output->channels[3][sampleIdx] = 0;
                    }
                }
                
                if (!blankRun) nClusters_ = omJsonParser_->getClusters().size();
            }
                break;
            case ClusterIds:
            {
                int clusterIdx = inputs->getParInt(PAR_CLUSTERID);
                for (int sampleIdx = 0; sampleIdx < output->numSamples; sampleIdx++)
                {
                    bool hasData = (clusterIdx < omJsonParser_->getClusterIds().size() &&
                                    sampleIdx < omJsonParser_->getClusterIds()[clusterIdx].size());
                    
                    if (hasData)
                    {
                        output->channels[0][sampleIdx] = omJsonParser_->getClusterIds()[clusterIdx][sampleIdx][0];
                        output->channels[1][sampleIdx] = omJsonParser_->getClusterIds()[clusterIdx][sampleIdx][1];
                        output->channels[2][sampleIdx] = omJsonParser_->getClusterIds()[clusterIdx][sampleIdx][2];
                    }
                    else if (!blankRun)
                    {
                        output->channels[0][sampleIdx] = 0;
                        output->channels[1][sampleIdx] = 0;
                        output->channels[2][sampleIdx] = 0;
                    }
                }
            }
                break;
            case Stagedist:
            {
                for (auto pair:omJsonParser_->getStageDists()) // { id -> <dx,dy> }
                {
                    int id = pair.first;
                    // get sample idx of this id
                    vector<int>::const_iterator it = find(omJsonParser_->getIdOrder().begin(),
                                                    omJsonParser_->getIdOrder().end(), id);
                    
                    if (it == omJsonParser_->getIdOrder().end())
                        SET_CHOP_WARN(msg << "stagedist id (" << id << ") was not found in available id"
                                      " list. Message bundle: " << bundleStr)
                    else
                    {
                        long sampleIdx = it-omJsonParser_->getIdOrder().begin();
                        
                        if (pair.second.size() >= 4)
                        {
                            if (sampleIdx < output->numSamples)
                            {
                                output->channels[0][sampleIdx] = id;
                                
                                output->channels[1][sampleIdx] = pair.second[0];
                                output->channels[2][sampleIdx] = pair.second[1];
                                output->channels[3][sampleIdx] = pair.second[2];
                                output->channels[4][sampleIdx] = pair.second[3];
                            }
                        }
                        else
                            SET_CHOP_WARN(msg << "Insufficient data on stage dists (only "
                                          << pair.second.size() << " elements instead of 4)")
                    }
                }
            }
                break;
            case Hotspots:
            {
                for (int sampleIdx = 0; sampleIdx < output->numSamples; sampleIdx++)
                {
                    if (sampleIdx < omJsonParser_->getHotspots().size() &&
                        sampleIdx < output->numSamples)
                    {
                        output->channels[0][sampleIdx] = omJsonParser_->getHotspots()[sampleIdx][0];
                        output->channels[1][sampleIdx] = omJsonParser_->getHotspots()[sampleIdx][1];
                        output->channels[2][sampleIdx] = omJsonParser_->getHotspots()[sampleIdx][2];
                    }
                }
            }
                break;
            case Pca:
            {
//                assert(omJsonParser_->getGroupTarget().size() <= 2);
//
//                int sampleIdx = 0;
//                for (auto v:omJsonParser_->getGroupTarget())
//                {
//                    for (int i = 0; i < v.size(); ++i)
//                        output->channels[i][sampleIdx] = v[i];
//                    sampleIdx++;
//                }
            }
                break;
            case Templates:
            {
                
            }
                break;
            default:
                break;
        }
    }
    
    if (blankRun)
    {
        nBlankRuns_++;
        if (nBlankRuns_ >= BLANK_RUN_THRESHOLD)
            blankRunsTrigger();
    }
    else
        nBlankRuns_ = 0;
}

int32_t
OM_CHOP::getNumInfoCHOPChans()
{
    return 3; // aliveIds seq numClusters_
}

void
OM_CHOP::getInfoCHOPChan(int32_t index,
                          OP_InfoCHOPChan* chan)
{
    switch (index) {
        case 0:
            chan->name = "aliveIds";
            chan->value = (float)nAliveIds_;
            break;
        case 1:
            chan->name = "seq";
            chan->value = (float)seq_;
            break;
        case 2:
            chan->name = "nClusters";
            chan->value = (float)nClusters_;
            break;
        default:
            break;
    }
}

void OM_CHOP::setupParameters(OP_ParameterManager* manager)
{
    {
        OP_NumericParameter reinit(PAR_REINIT), portnum(PAR_PORTNUM);
        
        reinit.label = "Init";
        reinit.page = "General";
        
        portnum.label = "Port";
        portnum.page = "General";
        portnum.defaultValues[0] = PORTNUM;
        
        OP_ParAppendResult res = manager->appendPulse(reinit);
        assert(res == OP_ParAppendResult::Success);
        res = manager->appendFloat(portnum);
        assert(res == OP_ParAppendResult::Success);
    }
    {
        OP_StringParameter output(PAR_OUTPUT);
        
        output.label = "Output";
        output.page = "Output";
        output.defaultValue = "Derivatives";
        
        OP_ParAppendResult res = manager->appendMenu(output, NPAR_OUTPUT, (const char**)menuNames,
                                                     (const char**)labels);
        assert(res == OP_ParAppendResult::Success);
    
        OP_NumericParameter maxTracked(PAR_MAXTRACKED);
        
        maxTracked.label = "Max Tracked";
        maxTracked.page = "Output";
        maxTracked.defaultValues[0] = 1;
        maxTracked.minValues[0] = 1;
        maxTracked.maxValues[0] = PAIRWISE_MAXDIM;
        
        OP_NumericParameter clusterId(PAR_CLUSTERID);
        
        clusterId.label = "Cluster Id";
        clusterId.page = "Output";
        clusterId.defaultValues[0] = 0;
        clusterId.minValues[0] = 1;
        clusterId.maxValues[0] = PAIRWISE_MAXDIM;
        
        res = manager->appendInt(maxTracked);
        assert(res == OP_ParAppendResult::Success);
        res = manager->appendInt(clusterId);
        assert(res == OP_ParAppendResult::Success);
    }
}

void OM_CHOP::pulsePressed(const char *name)
{
    if (!strcmp(name, "Init"))
    {
        
    }
}

//******************************************************************************
void
OM_CHOP::setupSocketReader()
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
OM_CHOP::processingError(std::string m)
{
    SET_CHOP_WARN(msg << m)
}

void
OM_CHOP::checkInputs(const CHOP_Output *outputs, OP_Inputs *inputs, void *)
{
    string outputChoice(inputs->getParString(PAR_OUTPUT));
    outChoice_ = OutputMenuMap[outputChoice];
    
    if (outChoice_ == ClusterIds)
        inputs->enablePar(PAR_CLUSTERID, true);
    else
        inputs->enablePar(PAR_CLUSTERID, false);
}

void
OM_CHOP::blankRunsTrigger()
{
    nAliveIds_ = 0;
    nClusters_ = 0;
#ifdef PRINT_BLANKRUN_TRIGGER
    cout << "------------------------------" << endl;
    cout << endl;
    cout << "Blank run!" << endl;
    cout << "------------------------------" << endl;
#endif
}
