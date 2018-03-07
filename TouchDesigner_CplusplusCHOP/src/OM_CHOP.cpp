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

#define MESSAGE_QUEUE_THRESHOLD 500
#define MESSAGE_LIFETIME_MS 2000
#define OPENMOVES_MSG_BUNDLE 2

#define PAIRWISE_MAXDIM 25
#define PAIRWISE_WIDTH (PAIRWISE_MAXDIM+1)
#define PAIRWISE_HEIGHT (PAIRWISE_MAXDIM)
#define PAIRWISE_SIZE (PAIRWISE_WIDTH*PAIRWISE_HEIGHT)

using namespace std;
using namespace chrono;

static const char* DerOutNames[6] = { "id", "d1x", "d1y", "d2x", "d2y", "speed"};
static const char* StageDistNames[5] = { "id", "us", "ds", "sl", "sr"};
static const char* ClusterOutNames[3] = { "x", "y", "spread"};
static const char* HotspotsOutNames[3] = { "x", "y", "spread"};
static const char* GroupTargetNames[4] = { "val", "x", "y", "z"};

#define NPAR_OUTPUT 8
#define PAR_OUTPUT  "Output"
#define PAR_REINIT  "Init"
#define PAR_PORTNUM "Portnum"
#define PAR_MAXTRACKED "Maxtracked"

static const char *menuNames[] = { "Derivatives", "Pairwise", "Dtw", "Clusters", "Hotspots", "Pca", "Stagedist", "Templates" };
static const char *labels[] = { "Derivatives", "Pairwise matrix", "Path similarity", "Clusters", "Hotspots", "Group target", "Stage Distances", "Templates" };
static map<string, OM_CHOP::OutChoice> OutputMenuMap = {
    { "Uknown", OM_CHOP::OutChoice::Unknown },
    { "Derivatives", OM_CHOP::OutChoice::Derivatives },
    { "Pairwise", OM_CHOP::OutChoice::Pairwise },
    { "Dtw", OM_CHOP::OutChoice::Dtw },
    { "Clusters", OM_CHOP::OutChoice::Cluster },
    { "Hotspots", OM_CHOP::OutChoice::Hotspots },
    { "Pca", OM_CHOP::OutChoice::Pca },
    { "Stagedist", OM_CHOP::OutChoice::Stagedist },
    { "Templates", OM_CHOP::OutChoice::Templates }
};

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

string bundleToString(const vector<rapidjson::Document>& bundle)
{
    stringstream ss;
    
    for (auto& d:bundle)
    {
        rapidjson::StringBuffer buffer;
        buffer.Clear();
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        d.Accept(writer);
        
        ss << buffer.GetString() << endl;
    }
    
    return ss.str();
}

//******************************************************************************
OM_CHOP::OM_CHOP(const OP_NodeInfo * info):
errorMessage_(""), warningMessage_(""),
outChoice_(Derivatives)
{
    pairwiseMat_ = (float*)malloc(PAIRWISE_SIZE*sizeof(float));
    dtwMat_ = (float*)malloc(PAIRWISE_SIZE*sizeof(float));
    
    memset(pairwiseMat_, -1, PAIRWISE_SIZE);
    memset(dtwMat_, -1, PAIRWISE_SIZE);
    
    setupSocketReader();
}

OM_CHOP::~OM_CHOP()
{
    if (SocketReader)
        SocketReader->unregisterSlave(this);
    
    free(pairwiseMat_);
}

void OM_CHOP::getGeneralInfo(CHOP_GeneralInfo * ginfo)
{
    //Forces check on socket each time TouchDesigner cooks.
    ginfo->cookEveryFrame = true;
}

bool OM_CHOP::getOutputInfo(CHOP_OutputInfo * info)
{
    info->numSamples = info->opInputs->getParInt(PAR_MAXTRACKED);
    
    switch (outChoice_) {
        case Derivatives:
            info->numChannels = 6; // id speed d1x d1y d2x d2y
            break;
        case Pairwise: // fallthrough
        case Dtw:
            info->numSamples = info->opInputs->getParInt(PAR_MAXTRACKED)+1;
            info->numChannels = info->opInputs->getParInt(PAR_MAXTRACKED);
            break;
        case Cluster:
            info->numChannels = 3; // x y spread
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
    double nowTs = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    
    checkInputs(output, inputs, reserved);
    processQueue();
    
    {
        string bundleStr;
        vector<int> idOrder;
        map<int, pair<float,float>> derivatives1;
        map<int, pair<float,float>> derivatives2;
        map<int, float> speeds;
        map<int, vector<float>> dwtDistances;
        vector<vector<float>> clusters;
        map<int, vector<float>> stageDistances;
        vector<vector<float>> hotspotsData;
        vector<vector<float> > groupTarget;
        map<string, vector<float>> templates;
        
        if (!queueBusy_)
        {
            lock_guard<mutex> lock(messagesMutex_);
            
            for (MessagesQueue::iterator it = messages_.begin(); it != messages_.end(); /* NO INCREMENT HERE */)
            {
                if ((*it).second.second.size() >= OPENMOVES_MSG_BUNDLE)
                {
                    vector<rapidjson::Document>& msgs = (*it).second.second;
                    bundleStr = bundleToString(msgs);
                    
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
                    
                    processMessages(msgs, idOrder,
                                    derivatives1, derivatives2, speeds,
                                    pairwiseMat_,
                                    clusters,
                                    stageDistances,
                                    hotspotsData,
                                    dtwMat_,
                                    groupTarget,
                                    templates);
                    messages_.erase(it++);
                    
                    break;
                } // if messages bundle
                else
                {
                    // check message timestamp and delete it if it's too old
                    if (nowTs-(*it).second.first >= MESSAGE_LIFETIME_MS)
                    {
                        SET_CHOP_WARN(msg << "Cleaning up old unprocessed message bundle (id " << (*it).first
                                      << "). This normally should not happen, check incoming messages bundle length. Deleted: "
                                      << bundleStr)
                        messages_.erase(it++);
                    }
                    else
                        ++it;
                }
            }
        } // if queue not busy
        
        switch (outChoice_) {
            case Derivatives:
            {
                for (auto pair:derivatives1) // { id -> <dx,dy> }
                {
                    int id = pair.first;
                    // get sample idx of this id
                    vector<int>::iterator it = find(idOrder.begin(), idOrder.end(), id);
                    
                    if (it == idOrder.end())
                    {
                        SET_CHOP_WARN(msg << "1st Derivatives id (" << id << ") was not found in available id"
                                      " list. Message bundle: " << bundleStr)
//                        assert(it != idOrder.end());
                    }
                    else
                    {
                        long sampleIdx = it-idOrder.begin();
                        
                        output->channels[0][sampleIdx] = id;
                        output->channels[1][sampleIdx] = pair.second.first;
                        output->channels[2][sampleIdx] = pair.second.second;
                    }
                }
                
                for (auto pair:derivatives2) // { id -> <dx,dy> }
                {
                    int id = pair.first;
                    // get sample idx of this id
                    vector<int>::iterator it = find(idOrder.begin(), idOrder.end(), id);
                    
                    if (it == idOrder.end())
                    {
                        SET_CHOP_WARN(msg << "2nd Derivatives id (" << id << ") was not found in available id"
                                      " list. Message bundle: " << bundleStr)
//                        assert(it != idOrder.end());
                    }
                    else
                    {
                        long sampleIdx = it-idOrder.begin();
                        
                        output->channels[3][sampleIdx] = pair.second.first;
                        output->channels[4][sampleIdx] = pair.second.second;
                    }
                }
                
                for (auto pair:speeds)
                {
                    int id = pair.first;
                    vector<int>::iterator it = find(idOrder.begin(), idOrder.end(), id);
                    
                    if (it == idOrder.end())
                    {
                        SET_CHOP_WARN(msg << "Speed id (" << id << ") was not found in available id"
                                      " list. Message bundle: " << bundleStr)
                    }
                    else
                    {
                        long sampleIdx = it-idOrder.begin();
                        
                        output->channels[5][sampleIdx] = pair.second;
                    }
                }
            }
                break;
            case Pairwise:
            {
#ifdef PRINT_PAIRWISE
                cout << "pairwise: " << endl;
                for (int i = 0; i < output->numSamples; ++i)
                {
                    for (int j = 0; j < output->numSamples; ++j)
                        cout << pairwiseMat_[i*PAIRWISE_WIDTH+j] << " ";
                    cout << endl;
                }
#endif
                
                for (int chanIdx = 0; chanIdx < output->numChannels; chanIdx++)
                    for (int sampleIdx = 0; sampleIdx < output->numSamples; sampleIdx++)
                        output->channels[chanIdx][sampleIdx] = pairwiseMat_[chanIdx*PAIRWISE_WIDTH+sampleIdx];
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
                    if (sampleIdx < clusters.size())
                    {
                        output->channels[0][sampleIdx] = clusters[sampleIdx][0];
                        output->channels[1][sampleIdx] = clusters[sampleIdx][1];
                        output->channels[2][sampleIdx] = clusters[sampleIdx][2];
                    }
                }
            }
                break;
            case Stagedist:
            {
                for (auto pair:stageDistances) // { id -> <dx,dy> }
                {
                    int id = pair.first;
                    // get sample idx of this id
                    vector<int>::iterator it = find(idOrder.begin(), idOrder.end(), id);
                    
                    if (it == idOrder.end())
                        SET_CHOP_WARN(msg << "stagedist id (" << id << ") was not found in available id"
                                      " list. Message bundle: " << bundleStr)
                    else
                    {
                        long sampleIdx = it-idOrder.begin();
                        
                        if (pair.second.size() >= 4)
                        {
                            output->channels[0][sampleIdx] = id;
                            
                            output->channels[1][sampleIdx] = pair.second[0];
                            output->channels[2][sampleIdx] = pair.second[1];
                            output->channels[3][sampleIdx] = pair.second[2];
                            output->channels[4][sampleIdx] = pair.second[3];
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
                    if (sampleIdx < hotspotsData.size())
                    {
                        output->channels[0][sampleIdx] = hotspotsData[sampleIdx][0];
                        output->channels[1][sampleIdx] = hotspotsData[sampleIdx][1];
                        output->channels[2][sampleIdx] = hotspotsData[sampleIdx][2];
                    }
                }
            }
                break;
            case Pca:
            {
                assert(groupTarget.size() <= 2);
                
                int sampleIdx = 0;
                for (auto v:groupTarget)
                {
                    for (int i = 0; i < v.size(); ++i)
                        output->channels[i][sampleIdx] = v[i];
                    sampleIdx++;
                }
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
}

int32_t
OM_CHOP::getNumInfoCHOPChans()
{
    return 1; // aliveIds num
}

void
OM_CHOP::getInfoCHOPChan(int32_t index,
                          OP_InfoCHOPChan* chan)
{
    if (index == 0)
    {
        chan->name = "aliveIds";
        chan->value = (float)nAliveIds_;
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
    
        OP_NumericParameter MaxTracked;
        
        MaxTracked.name = PAR_MAXTRACKED;
        MaxTracked.label = "Max Tracked";
        MaxTracked.page = "Output";
        MaxTracked.defaultValues[0] = 1;
        MaxTracked.minValues[0] = 1;
        MaxTracked.maxValues[0] = PAIRWISE_MAXDIM;
        
        res = manager->appendInt(MaxTracked);
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
OM_CHOP::onNewJsonObjectReceived(const rapidjson::Document &d)
{
    errorMessage_ = "";
    warningMessage_ = "";
    
    // check thread safety for vector
    rapidjson::Document dcopy;
    dcopy.CopyFrom(d, dcopy.GetAllocator());
    
    {
        lock_guard<mutex> lock(documentQueueMutex_);
        queueBusy_ = true;
        documentQueue_.push(move(dcopy));
        queueBusy_ = false;
    }
}

void
OM_CHOP::onSocketReaderError(const string &m)
{
    SET_CHOP_WARN(msg << m)
}

void
OM_CHOP::onSocketReaderWillReset()
{
    // clear everything and wait for socket reader to be re-created
}

void
OM_CHOP::processQueue()
{
    double nowTs = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    
    lock_guard<mutex> lock(documentQueueMutex_);
    // this function goes over queue and moves them over to messages dict
    while (documentQueue_.size())
    {
        int seqNo = -1;
        
        if (documentQueue_.front().HasMember("seq"))
            seqNo = documentQueue_.front()["seq"].GetInt();
        else if (documentQueue_.front().HasMember("header") &&
                 documentQueue_.front()["header"].HasMember("seq"))
        {
            seqNo = documentQueue_.front()["header"]["seq"].GetInt();
        }
        else
            SET_CHOP_WARN(msg << "Bad json formatting: can't locate 'seq' field")
        
        if (seqNo >= 0)
        {
            if (messages_.find(seqNo) == messages_.end())
                messages_[seqNo] = pair<double, vector<rapidjson::Document>>(nowTs, vector<rapidjson::Document>());
            
            messages_[seqNo].second.push_back(move(documentQueue_.front()));
        }
        
        documentQueue_.pop();
#ifdef PRINT_MSG_QUEUE
        float avgBundleLen = 0;
        for (auto& t:messages_) avgBundleLen += t.second.second.size();
        avgBundleLen /= (float)messages_.size();
        
        cout << "document queue " << documentQueue_.size() << " messages queue "
            << messages_.size() << " avg bundle len " << avgBundleLen << endl;
#endif
        if (messages_.size() >= MESSAGE_QUEUE_THRESHOLD)
            SET_CHOP_WARN(msg << "Incoming message queue is too large - " << messages_.size()
                          << " (data is received, but not processed). Check openmoves message "
                          "bundle size (current bundle size is "
                          << OPENMOVES_MSG_BUNDLE << "; was the protocol updated?)")
    }
}

void
OM_CHOP::processMessages(vector<rapidjson::Document>& messages,
                         vector<int>& idOrder,
                         map<int, pair<float,float>>& derivatives1,
                         map<int, pair<float,float>>& derivatives2,
                         map<int, float>& speeds,
                         float* pairwiseMatrix,
                         vector<vector<float>>& clustersData,
                         map<int, vector<float>>& stageDistances,
                         vector<vector<float>>& hotspotsData,
                         float* dtwMatrix,
                         vector<vector<float>>& groupTarget,
                         map<string, vector<float>>& templates)
{
    processIdOrder(messages, idOrder);
    processDerivatives(messages, idOrder, derivatives1, derivatives2, speeds);
    processPairwise(messages, idOrder, pairwiseMatrix);
    processClusters(messages, clustersData);
    processDtw(messages, idOrder, dtwMatrix);
    processStageDistances(messages, idOrder, stageDistances);
    processHotspots(messages, hotspotsData);
    processGroupTarget(messages, groupTarget);
    processTemplates(messages, idOrder, templates);
}

bool
OM_CHOP::retireve(const string& key,
                  vector<rapidjson::Document>& messages,
                  rapidjson::Value& val)
{
    
    for (auto &m:messages)
    {
#if 0
        {
            rapidjson::StringBuffer buffer;
            buffer.Clear();
            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
            m.Accept(writer);
            
            cout << "---> message: " << buffer.GetString() << endl;
        }
#endif
        
        if (m.HasMember(key.c_str()))
        {
            val = m[key.c_str()];
            return true;
        }
    }
    
    return false;
}

void
OM_CHOP::checkInputs(const CHOP_Output *outputs, OP_Inputs *inputs, void *)
{
    string outputChoice(inputs->getParString(PAR_OUTPUT));
    outChoice_ = OutputMenuMap[outputChoice];
    
//    inputs->enablePar("Maxclusters", false);
    
    // update UI
    switch (outChoice_) {
        case Derivatives:
            
            break;
            
        case Pairwise:
            break;
            
        case Dtw:
            break;
            
        case Cluster:
//            inputs->enablePar("Maxclusters", true);
            break;
        default:
            break;
    }
}

void
OM_CHOP::processIdOrder(vector<rapidjson::Document>& messages,
                    vector<int>& idOrder)
{ // retrieving id order
    rapidjson::Value idorder;
    
    if (retireve(OM_JSON_IDORDER, messages, idorder))
    {
        if (!idorder.IsArray())
            SET_CHOP_WARN(msg << "JSON format error: " << OM_JSON_IDORDER << " is not an array." )
        else
        {
            const rapidjson::Value& arr = idorder.GetArray();
            for (rapidjson::SizeType i = 0; i < arr.Size(); i++)
                idOrder.push_back(arr[i].GetInt());
        }
    }
    else
        SET_CHOP_WARN(msg << "JSON format error: couldn't find field "
                      << OM_JSON_IDORDER << " in received json messages")
    
    // cleanupt idorder by alive ids
    rapidjson::Value aliveids;
    
    if (retireve(OM_JSON_ALIVEIDS, messages, aliveids))
    {
        set<int> aliveIds;
        const rapidjson::Value& arr = aliveids.GetArray();
        
        for (rapidjson::SizeType i = 0; i < arr.Size(); i++)
            aliveIds.insert(arr[i].GetInt());
        
        nAliveIds_ = aliveIds.size();
        
        int idx = 0;
        while (idOrder.size() != aliveIds.size() && idx < idOrder.size())
        {
            int searchId = *(idOrder.begin()+idx);
            
            if (aliveIds.find(searchId) == aliveIds.end()) // id is not in aliveIds -> delete it from idOrder
                idOrder.erase(idOrder.begin()+idx);
            else
                idx++;
        }
    }
    else
        SET_CHOP_WARN(msg << "JSON format error: couldn't find field "
                      << OM_JSON_ALIVEIDS << " in received json messages")
    
#ifdef PRINT_IDS
    cout << "filtered ids: ";
    for (auto id:idOrder)
        cout << id << " ";
    cout << endl;
#endif
}

void
OM_CHOP::processDerivatives(vector<rapidjson::Document>& messages,
                            vector<int>& idOrder,
                            map<int, pair<float,float>>& derivatives1,
                            map<int, pair<float,float>>& derivatives2,
                            map<int, float>& speed)
{ // retrieving derivatives
    rapidjson::Value firstDirs;
    if (retireve(OM_JSON_FIRSTDERS, messages, firstDirs))
    {
        if (!firstDirs.IsArray())
            SET_CHOP_WARN(msg << "JSON format error: " << OM_JSON_FIRSTDERS << " is not an array")
        else
        {
//            int idx = 0;
            const rapidjson::Value& arr = firstDirs.GetArray();
            for (rapidjson::SizeType i = 0; i < arr.Size(); i++)
            {
                int id = idOrder[i];
                derivatives1[id] = pair<float, float>(0,0);
                
                // if value is string then it's "Null" -> skip
                if (!arr[i].GetArray()[0].IsString())
                {
                    derivatives1[id].first = arr[i].GetArray()[0].GetFloat();
                    derivatives1[id].second = arr[i].GetArray()[1].GetFloat();
                }
            } // for i
        }
    } // if
    else
        SET_CHOP_WARN(msg << "JSON format error: couldn't find field "
                      << OM_JSON_FIRSTDERS << " in received json messages")
    
    rapidjson::Value secondDirs;
    if (retireve(OM_JSON_SECONDDERS, messages, secondDirs))
    {
        if (!firstDirs.IsArray())
            SET_CHOP_WARN(msg << "JSON format error: " << OM_JSON_FIRSTDERS << " is not an array")
        else
        {
//            int idx = 0;
            const rapidjson::Value& arr = secondDirs.GetArray();
            for (rapidjson::SizeType i = 0; i < arr.Size(); i++)
            {
                int id = idOrder[i];
                derivatives2[id] = pair<float, float>(0,0);
                
                // if value is string then it's "Null" -> skip
                if (!arr[i].GetArray()[0].IsString())
                {
                    derivatives2[id].first = arr[i].GetArray()[0].GetFloat();
                    derivatives2[id].second = arr[i].GetArray()[1].GetFloat();
                }
            } // for i
        }
    } // if
    else
        SET_CHOP_WARN(msg << "JSON format error: couldn't find field "
                      << OM_JSON_SECONDDERS << " in received json messages")
    
    rapidjson::Value speeds;
    if (retireve(OM_JSON_SPEEDS, messages, speeds))
    {
        if (!speeds.IsArray())
            SET_CHOP_WARN(msg << "JSON format error: 'speeds' is expected to be an array")
        else
        {
            const rapidjson::Value& arr = speeds.GetArray();
            for (rapidjson::SizeType i = 0; i < arr.Size(); ++i)
            {
                int id = idOrder[i];
                speed[id] = arr[i].GetFloat();
            }
        }
    }
    
#ifdef PRINT_DERIVATIVES
    cout << "derivatives1: " << endl;
    for (auto v:derivatives1)
        cout << "id " << v.first << " dx " << v.second.first << " dy " << v.second.second << endl;
    
    cout << "derivatives2: " << endl;
    for (auto v:derivatives2)
        cout << "id " << v.first << " dx " << v.second.first << " dy " << v.second.second << endl;
#endif
}

void
OM_CHOP::processPairwise(vector<rapidjson::Document>& messages,
                         vector<int>& idOrder,
                         float* pairwiseMatrix)
{
    rapidjson::Value pairwise;
    if (retireve(OM_JSON_PAIRWISE, messages, pairwise))
    {
        if (!pairwise.IsArray())
            SET_CHOP_WARN(msg << "JSON format error: " << OM_JSON_PAIRWISE << " is not an array")
        else
        {
            rapidjson::Value arr = pairwise.GetArray();
            
            for (int i = 0; i < PAIRWISE_HEIGHT; ++i)
            {
                bool hasRow = (i < arr.Size());
                rapidjson::Value row;
                
                if (hasRow)
                    row = arr[i].GetArray();
                
                for (int j = 0; j < PAIRWISE_WIDTH; ++j)
                {
                    if (j == 0) // this is for ids
                    {
                        if (i < idOrder.size())
                        {
                            int id = idOrder[i];
                            pairwiseMatrix[i*PAIRWISE_WIDTH+j] = id;
                        }
                    }
                    else
                    {
                        bool hasCol = (hasRow ? j-1 < row.Size() : false);
                        if (hasRow && hasCol)
                            pairwiseMatrix[i*PAIRWISE_WIDTH+j] = row[j-1].GetFloat();
                    }
                }
            }
        }
    } // if
    else
        SET_CHOP_WARN(msg << "JSON format error: couldn't find field "
                      << OM_JSON_PAIRWISE << " in received json messages")
}

void
OM_CHOP::processClusters(vector<rapidjson::Document>& messages,
                         vector<vector<float>>& clustersData)
{ //  retrieve clusters
    rapidjson::Value clusters;
    
    if (retireve(OM_JSON_CLUSTERCENTERS, messages, clusters))
    {
        if (!clusters.IsArray())
            SET_CHOP_WARN(msg << "JSON format error: " << OM_JSON_FIRSTDERS << " is not an array")
        else
        {
            rapidjson::Value arr = clusters.GetArray();
            
            for (rapidjson::SizeType i = 0; i < arr.Size(); i++)
            {
                rapidjson::Value coords = arr[i].GetArray();
                clustersData.push_back(vector<float>({ coords[0].GetFloat(), coords[1].GetFloat(), 0}));
            }
        }
    }
    else
        SET_CHOP_WARN(msg << "JSON format error: couldn't find field "
                      << OM_JSON_CLUSTERCENTERS << " in received json messages")
    
    rapidjson::Value spreads;
    if (retireve(OM_JSON_CLUSTERSPREADS, messages, spreads))
    {
        if (!spreads.IsArray())
            SET_CHOP_WARN(msg << "JSON format error: " << OM_JSON_FIRSTDERS << " is not an array")
        else
        {
            rapidjson::Value arr = spreads.GetArray();
            
            if (arr.Size() != clustersData.size())
            {
                SET_CHOP_WARN(msg << "Array length of cluster spreads (" << arr.Size()
                              << ") does not equal number of clusters (" << clustersData.size() << ").")
            }
            else
            {
                for (rapidjson::SizeType i = 0; i < arr.Size(); ++i)
                    clustersData[i][2] = arr[i].GetFloat();
            }
        }
    }
    else
        SET_CHOP_WARN(msg << "JSON format error: couldn't find field "
                      << OM_JSON_CLUSTERSPREADS << " in received json messages")
    
#ifdef PRINT_CLUSTERS
    cout << "clusters: " << endl;
    for (auto v:clustersData)
        cout << "x " << v[0] << " y " << v[1] << " spread " << v[2] << endl;
#endif
}

void
OM_CHOP::processStageDistances(vector<rapidjson::Document>& messages,
                               vector<int>& idOrder,
                               map<int, vector<float>>& stageDistances)
{ // retrieving stage distances
    rapidjson::Value stageDist;
    if (retireve(OM_JSON_STAGEDIST, messages, stageDist))
    {
        if (!stageDist.IsArray())
            SET_CHOP_WARN(msg << "JSON format error: " << OM_JSON_STAGEDIST << " is not an array")
        else
        {
            int idx = 0;
            const rapidjson::Value& arr = stageDist.GetArray();
            for (rapidjson::SizeType i = 0; i < arr.Size(); i++)
            {
                int id = idOrder[idx++];
                stageDistances[id] = vector<float>();
                
                if (!arr[i].IsObject())
                    SET_CHOP_WARN(msg << "JSON format error: " << OM_JSON_STAGEDIST << " element is not a dictionary")
                else
                {
                    if (arr[i].HasMember(OM_JSON_STAGEDIST_US))
                        stageDistances[id].push_back(arr[i][OM_JSON_STAGEDIST_US].GetFloat());
                    else
                        SET_CHOP_WARN(msg << "JSON format error: can't find key "
                                      << OM_JSON_STAGEDIST_US << " in stagedist object")
                    if (arr[i].HasMember(OM_JSON_STAGEDIST_DS))
                        stageDistances[id].push_back(arr[i][OM_JSON_STAGEDIST_DS].GetFloat());
                    else
                        SET_CHOP_WARN(msg << "JSON format error: can't find key "
                                      << OM_JSON_STAGEDIST_DS << " in stagedist object")
                    if (arr[i].HasMember(OM_JSON_STAGEDIST_SL))
                        stageDistances[id].push_back(arr[i][OM_JSON_STAGEDIST_SL].GetFloat());
                    else
                        SET_CHOP_WARN(msg << "JSON format error: can't find key "
                                      << OM_JSON_STAGEDIST_SL << " in stagedist object")
                    if (arr[i].HasMember(OM_JSON_STAGEDIST_SR))
                        stageDistances[id].push_back(arr[i][OM_JSON_STAGEDIST_SR].GetFloat());
                    else
                        SET_CHOP_WARN(msg << "JSON format error: can't find key "
                                      << OM_JSON_STAGEDIST_SR << " in stagedist object")
                }
            } // for i
        }
    } // if
    else
        SET_CHOP_WARN(msg << "JSON format error: couldn't find field "
                      << OM_JSON_STAGEDIST << " in received json messages")
}

void
OM_CHOP::processHotspots(vector<rapidjson::Document>& messages,
                     vector<vector<float>>& hotspotsData)
{ // retrieveing hotspots
    rapidjson::Value hotspots;
    
    if (retireve(OM_JSON_HOTSPOTS, messages, hotspots))
    {
        if (!hotspots.IsArray())
            SET_CHOP_WARN(msg << "JSON format error: " << OM_JSON_HOTSPOTS << " is not an array")
        else
        {
            rapidjson::Value arr = hotspots.GetArray();
            
            for (rapidjson::SizeType i = 0; i < arr.Size(); i++)
            {
                rapidjson::Value coords = arr[i].GetArray();
                if (coords.Size() < 2)
                {
                    SET_CHOP_WARN(msg << "Hotspots centers don't have enough data (at least "
                                  "2 coordinates expected, but " << coords.Size() << " given")
                }
                else
                {
                    hotspotsData.push_back(vector<float>({ coords[0].GetFloat(), coords[1].GetFloat()}));
                    if (coords.Size() >= 3)
                        hotspotsData.back().push_back(coords[2].GetFloat());
                }
            }
        }
    }
    else
        SET_CHOP_WARN(msg << "JSON format error: couldn't find field "
                      << OM_JSON_HOTSPOTS << " in received json messages")
}

void
OM_CHOP::processDtw(vector<rapidjson::Document>& messages,
                    vector<int>& idOrder,
                    float* dtwMatrix)
{
    rapidjson::Value dtwdistances;
    
    if (retireve(OM_JSON_DTW, messages, dtwdistances))
    {
        if (!dtwdistances.IsArray())
            SET_CHOP_WARN(msg << "JSON format error: " << OM_JSON_DTW << " is not an array")
        else
        {
            rapidjson::Value arr = dtwdistances.GetArray();
            
            for (int i = 0; i < PAIRWISE_HEIGHT && arr.Size(); ++i)
            {
                if (!arr[i].IsArray())
                    SET_CHOP_WARN(msg << "JSON format error: array expected as element of " << OM_JSON_DTW)
                else
                {
                    bool hasRow = (i < arr.Size());
                    rapidjson::Value row;
                    
                    if (hasRow)
                        row = arr[i].GetArray();
                    
                    for (int j = 0; j < PAIRWISE_WIDTH; ++j)
                    {
                        if (j == 0) // this is for ids
                        {
                            if (i < idOrder.size())
                            {
                                int id = idOrder[i];
                                dtwMatrix[i*PAIRWISE_WIDTH+j] = id;
                            }
                        }
                        else
                        {
                            bool hasCol = (hasRow ? j-1 < row.Size() : false);
                            if (hasRow && hasCol)
                                dtwMatrix[i*PAIRWISE_WIDTH+j] = row[j-1].GetFloat();
                        }
                    }
                }
            }
            
            if (arr.Size() == 0)
                SET_CHOP_WARN(msg << "Dtw array is empty")
        }
    }
    else
        SET_CHOP_WARN(msg << "JSON format error: couldn't find field "
                      << OM_JSON_DTW << " in received json messages")
}

void
OM_CHOP::processGroupTarget(vector<rapidjson::Document> &messages,
                            vector<vector<float>> &groupTarget)
{
    {
        rapidjson::Value eigenVals;
        
        if (retireve(OM_JSON_PCA1, messages, eigenVals))
        {
            if (!eigenVals.IsArray())
                SET_CHOP_WARN(msg << "JSON format error: "
                              << OM_JSON_PCA1 << " it not an array")
            else
            {
                rapidjson::Value arr = eigenVals.GetArray();
                
                if (arr.Size() != 2)
                    SET_CHOP_WARN(msg << "JSON format error: "
                                  << OM_JSON_PCA1 << " expected 2 elements in the array")
                else
                {
                    groupTarget.push_back(vector<float>(arr[0].GetFloat()));
                    
                    if (!arr[1].IsArray())
                        SET_CHOP_WARN(msg << "JSON format errors: "
                                      << OM_JSON_PCA1 << " second element must be a vector")
                    else
                    {
                        const rapidjson::Value& v = arr[1].GetArray();
                        
                        if (v.Size() != 3)
                            SET_CHOP_WARN(msg << "JSON format error: "
                                          << OM_JSON_PCA1 << " expected to have 3 elements for eigen  vector")
                        else
                        {
                            groupTarget[0].push_back(v[0].GetFloat());
                            groupTarget[0].push_back(v[1].GetFloat());
                            groupTarget[0].push_back(v[2].GetFloat());
                        }
                    }
                }
            }
        }
        else
            SET_CHOP_WARN(msg << "JSON format error: " << OM_JSON_PCA1 << " not found")
    }
    {
        rapidjson::Value eigenVals;
        
        if (retireve(OM_JSON_PCA2, messages, eigenVals))
        {
            if (!eigenVals.IsArray())
                SET_CHOP_WARN(msg << "JSON format error: "
                              << OM_JSON_PCA2 << " it not an array")
            else
            {
                rapidjson::Value arr = eigenVals.GetArray();
                
                if (arr.Size() != 2)
                    SET_CHOP_WARN(msg << "JSON format error: "
                                  << OM_JSON_PCA2 << " expected 2 elements in the array")
                else
                {
                    groupTarget.push_back(vector<float>(arr[0].GetFloat()));
                    
                    if (!arr[1].IsArray())
                        SET_CHOP_WARN(msg << "JSON format errors: "
                                      << OM_JSON_PCA2 << " second element must be a vector")
                    else
                    {
                        const rapidjson::Value& v = arr[1].GetArray();
                        
                        if (v.Size() != 3)
                            SET_CHOP_WARN(msg << "JSON format error: "
                                          << OM_JSON_PCA2 << " expected to have 3 elements for eigen  vector")
                        else
                        {
                            groupTarget[1].push_back(v[0].GetFloat());
                            groupTarget[1].push_back(v[1].GetFloat());
                            groupTarget[1].push_back(v[2].GetFloat());
                        }
                    }
                }
            }
        }
        else
            SET_CHOP_WARN(msg << "JSON format error: " << OM_JSON_PCA2 << " not found")
    }
}

void
OM_CHOP::processTemplates(vector<rapidjson::Document> &messages,
                          vector<int> &idOrder, map<string, vector<float> > &templates)
{
    
}
