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

#define PORTNUM 21235
#define OPENMOVES_MSG_BUNDLE 2
#define PAIRWISE_MAXDIM 20

#define PRINT_MESSAGES
//#define PRINT_IDS
//#define PRINT_DERIVATIVES
#define PRINT_PAIRWISE
//#define PRINT_CLUSTERS

using namespace std;

static const char* DerOutNames[5] = { "id", "d1x", "d1y", "d2x", "d2y"};
static const char* ClusterOutNames[5] = { "x", "y", "spread"};

#define NPAR_OUTPUT 4
#define PAR_OUTPUT  "Output"
static const char *names[] = { "Derivatives", "Pairwise", "Dwt", "Clusters" };
static const char *labels[] = { "Derivatives", "Pairwise matrix", "DWT", "Clusters" };
static map<std::string, OM_CHOP::OutChoice> OutputMenuMap = {
    { "Derivatives", OM_CHOP::OutChoice::Derivatives },
    { "Pairwise", OM_CHOP::OutChoice::Pairwise },
    { "Dwt", OM_CHOP::OutChoice::Dwt },
    { "Clusters", OM_CHOP::OutChoice::Cluster }
};

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
errorMessage_(""), warningMessage_(""),
outChoice_(Derivatives)
{
    setupSocketReader();
    
    for (int i = 0; i < PAIRWISE_MAXDIM; i++)
    {
        pairwiseMatrix_.push_back(vector<float>(PAIRWISE_MAXDIM+1, -1));
        pairwiseMatrix_[i][0] = 0;
    }
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
    
    switch (outChoice_) {
        case Derivatives:
            info->numSamples = info->opInputs->getParInt("Maxtracked");
            info->numChannels = 5; // id d1x d1y d2x d2y
            break;
        case Pairwise:
            info->numSamples = PAIRWISE_MAXDIM+1;
            info->numChannels = PAIRWISE_MAXDIM;
            break;
        case Dwt:
            info->numChannels = 0;
            break;
        case Cluster:
            info->numSamples = info->opInputs->getParInt("Maxclusters");
            info->numChannels = 3; // x y spread
            break;
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
        case Pairwise:
        {
            stringstream ss;
            ss << "idx" << index;
            
            return ss.str().c_str();
        }
        case Cluster:
        {
            return ClusterOutNames[index];
        }
            break;
        default:
            break;
    }
    
    return "n/a";
}

void OM_CHOP::execute(const CHOP_Output* output, OP_Inputs* inputs, void* reserved)
{
    checkInputs(output, inputs, reserved);
    processQueue();
    
    if (hasResidualData())
    {
        processResidualData(output, inputs);
    }
    else
    {
        
        std::vector<int> idOrder;
        std::map<int, std::pair<float,float>> derivatives1;
        std::map<int, std::pair<float,float>> derivatives2;
        std::map<int, std::vector<float>> dwtDistances;
        std::vector<std::vector<float>> clusters;
        std::vector<std::vector<float>> pairwiseMatrix;
        
        for (int i = 0; i < PAIRWISE_MAXDIM; i++)
        {
            pairwiseMatrix.push_back(vector<float>(PAIRWISE_MAXDIM+1, -1));
            pairwiseMatrix[i][0] = 0;
        }
        
        if (!queueBusy_)
        {
            if (messages_.size() && messages_.begin()->second.size() >= OPENMOVES_MSG_BUNDLE)
            {
                MessagesQueue::iterator msgtuple = messages_.begin();
                std::vector<rapidjson::Document>& msgs = msgtuple->second;
                
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
                                derivatives1, derivatives2,
                                dwtDistances,
                                pairwiseMatrix,//pairwiseMatrix_,
                                clusters);
                
                {
                    lock_guard<mutex> lock(messagesMutex_);
                    
                    messages_.erase(msgtuple);
                }
            } // if messages bundle
        } // if queue not busy
        
        switch (outChoice_) {
            case Derivatives:
            {
                for (auto pair:derivatives1) // { id -> <dx,dy> }
                {
                    int id = pair.first;
                    // get sample idx of this id
                    std::vector<int>::iterator it = std::find(idOrder.begin(), idOrder.end(), id);
                    assert(it != idOrder.end());
                    
                    long sampleIdx = it-idOrder.begin();
                    
                    output->channels[0][sampleIdx] = id;
                    output->channels[1][sampleIdx] = pair.second.first;
                    output->channels[2][sampleIdx] = pair.second.first;
                }
                
                for (auto pair:derivatives2) // { id -> <dx,dy> }
                {
                    int id = pair.first;
                    // get sample idx of this id
                    std::vector<int>::iterator it = std::find(idOrder.begin(), idOrder.end(), id);
                    assert(it != idOrder.end());
                    
                    long sampleIdx = it-idOrder.begin();
                    
                    output->channels[3][sampleIdx] = pair.second.first;
                    output->channels[4][sampleIdx] = pair.second.first;
                }
            }
                break;
            case Pairwise:
            {
#ifdef PRINT_PAIRWISE
                cout << "pairwise corner: " << endl;
                for (int i = 0; i < PAIRWISE_MAXDIM/5; ++i)
                {
                    for (int j = 0; j < PAIRWISE_MAXDIM/5; ++j)
                        cout << pairwiseMatrix[i][j] << " ";
                    cout << endl;
                }
#endif
                
                for (int chanIdx = 0; chanIdx < output->numChannels; chanIdx++)
                    for (int sampleIdx = 0; sampleIdx < output->numSamples; sampleIdx++)
                        output->channels[chanIdx][sampleIdx] = pairwiseMatrix[chanIdx][sampleIdx];
            }
                break;
            case Dwt:
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
                    else
                    {
                        output->channels[0][sampleIdx] = 0;
                        output->channels[1][sampleIdx] = 0;
                        output->channels[2][sampleIdx] = -1;
                    }
                }
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
    
    {
        OP_StringParameter output(PAR_OUTPUT);
        
        output.label = "Output";
        output.page = "Output";
        output.defaultValue = "Derivatives";
        
        OP_ParAppendResult res = manager->appendMenu(output, NPAR_OUTPUT, (const char**)names, (const char**)labels);
        assert(res == OP_ParAppendResult::Success);
        
        OP_NumericParameter maxClusters;
        
        //Parameter details
        maxClusters.name = "Maxclusters";
        maxClusters.label = "Max Clusters";
        maxClusters.page = "Output";
        maxClusters.defaultValues[0] = 4;
        maxClusters.minValues[0] = 1;
        
        //Add it to CHOP.
        res = manager->appendInt(maxClusters);
        assert(res == OP_ParAppendResult::Success);
    }
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
    
    {
        lock_guard<mutex> lock(documentQueueMutex_);
        queueBusy_ = true;
        documentQueue_.push(move(dcopy));
        queueBusy_ = false;
    }
}

void
OM_CHOP::onSocketError(const std::string &msg)
{
    warningMessage_ = msg;
}

void
OM_CHOP::processQueue()
{
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
            perror("Bad json formatting");
        
        if (seqNo >= 0)
        {
            if (messages_.find(seqNo) == messages_.end())
                messages_[seqNo] = vector<rapidjson::Document>();
            
            messages_[seqNo].push_back(move(documentQueue_.front()));
        }
        
        documentQueue_.pop();
//        cout << "document queue " << documentQueue_.size() << " messages queue " << messages_.size() << endl;
    }
}

void
OM_CHOP::processMessages(std::vector<rapidjson::Document>& messages,
                         std::vector<int>& idOrder,
                         std::map<int, std::pair<float,float>>& derivatives1,
                         std::map<int, std::pair<float,float>>& derivatives2,
                         std::map<int, std::vector<float>>&,
                         std::vector<std::vector<float>>& pairwiseMatrix,
                         std::vector<std::vector<float>>& clustersData)
{
    rapidjson::Value idorder;
    
    if (retireve("idorder", messages, idorder))
    {
        const rapidjson::Value& arr = idorder.GetArray();
        for (rapidjson::SizeType i = 0; i < arr.Size(); i++)
        {
            idOrder.push_back(arr[i].GetInt());
        }
    }
    
    // cleanupt idorder by alive ids
    rapidjson::Value aliveids;
    
    if (retireve("alive_IDs", messages, aliveids))
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
    
#ifdef PRINT_IDS
    cout << "filtered ids: ";
    for (auto id:idOrder)
        cout << id << " ";
    cout << endl;
#endif
    
    {
        rapidjson::Value firstDirs;
        if (retireve("firstdirs", messages, firstDirs))
        {
            int idx = 0;
            const rapidjson::Value& arr = firstDirs.GetArray();
            for (rapidjson::SizeType i = 0; i < arr.Size(); i++)
            {
                int id = idOrder[idx++];
                derivatives1[id] = pair<float, float>(0,0);
                
                // if value is string then it's "Null" -> skip
                if (!arr[i].GetArray()[0].IsString())
                {
                    derivatives1[id].first = arr[i].GetArray()[0].GetFloat();
                    derivatives1[id].second = arr[i].GetArray()[1].GetFloat();
                }
            } // for i
        } // if

        rapidjson::Value secondDirs;
        if (retireve("seconddirs", messages, secondDirs))
        {
            int idx = 0;
            const rapidjson::Value& arr = secondDirs.GetArray();
            for (rapidjson::SizeType i = 0; i < arr.Size(); i++)
            {
                int id = idOrder[idx++];
                derivatives2[id] = pair<float, float>(0,0);
                
                // if value is string then it's "Null" -> skip
                if (!arr[i].GetArray()[0].IsString())
                {
                    derivatives2[id].first = arr[i].GetArray()[0].GetFloat();
                    derivatives2[id].second = arr[i].GetArray()[1].GetFloat();
                }
            } // for i
        } // if
    }
    
#ifdef PRINT_DERIVATIVES
    cout << "derivatives1: " << endl;
    for (auto v:derivatives1)
        cout << "id " << v.first << " dx " << v.second.first << " dy " << v.second.second << endl;
    
    cout << "derivatives2: " << endl;
    for (auto v:derivatives2)
        cout << "id " << v.first << " dx " << v.second.first << " dy " << v.second.second << endl;
#endif
    
    {
#ifdef PRINT_PAIRWISE2
        cout << "pairwise mat: " << endl;
#endif
        rapidjson::Value pairwise;
        if (retireve("pairwise", messages, pairwise))
        {
            rapidjson::Value arr = pairwise.GetArray();
            
            for (int i = 0; i < PAIRWISE_MAXDIM; ++i)
            {
                bool hasRow = (i < arr.Size());
                rapidjson::Value row;
                
                if (hasRow)
                    row = arr[i].GetArray();
                
                for (int j = 0; j < PAIRWISE_MAXDIM+1; ++j)
                {
                    if (j == 0) // this is for ids
                    {
                        if (i < idOrder.size())
                        {
                            int id = idOrder[i];
                            pairwiseMatrix[i][j] = id;
                        }
                        else
                            pairwiseMatrix[i][j] = 0;
                    }
                    else
                    {
                        bool hasCol = (hasRow ? j-1 < row.Size() : false);
                        if (hasRow && hasCol)
                            pairwiseMatrix[i][j] = row[j-1].GetFloat();
                        else
                            pairwiseMatrix[i][j] = -1;
                    }
#ifdef PRINT_PAIRWISE2
                    cout << pairwiseMatrix[i][j] << " ";
#endif
                }
#ifdef PRINT_PAIRWISE2
                 cout << endl;
#endif
            }
        } // if
    }
    
    {
//        rapidjson::Value dtwdistances;
//
//        if (retireve("dtwdistances", messages, dtwdistances))
//        {
//            rapidjson::Value arr = dtwdistances.GetArray();
//        }
    }
    
    {
        rapidjson::Value clusters;
        
        if (retireve("clustercenters", messages, clusters))
        {
            rapidjson::Value arr = clusters.GetArray();

            for (rapidjson::SizeType i = 0; i < arr.Size(); i++)
            {
                rapidjson::Value coords = arr[i].GetArray();
                clustersData.push_back(vector<float>({ coords[0].GetFloat(), coords[1].GetFloat(), 0}));
            }
        }
        
        rapidjson::Value spreads;
        if (retireve("spreads", messages, spreads))
        {
            rapidjson::Value arr = spreads.GetArray();
            
            assert(arr.Size() == clustersData.size());
            
            for (rapidjson::SizeType i = 0; i < arr.Size(); ++i)
                clustersData[i][2] = arr[i].GetFloat();
        }
    }
    
#ifdef PRINT_CLUSTERS
    cout << "clusters: " << endl;
    for (auto v:clustersData)
        cout << "x " << v[0] << " y " << v[1] << " spread " << v[2] << endl;
#endif
}

bool
OM_CHOP::retireve(const std::string& key,
                  std::vector<rapidjson::Document>& messages,
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

bool
OM_CHOP::hasResidualData()
{
//    if (outChoice_ == Pairwise && pairwiseMatrix_.size())
//        return true;
    return false;
}

void
OM_CHOP::processResidualData(const CHOP_Output* output, OP_Inputs* inputs)
{
//    assert(output->numChannels == pairwiseMatrix_.size());
//
//    for (int chanIdx = 0; chanIdx < output->numChannels; chanIdx++)
//        for (int sampleIdx = 0; sampleIdx < output->numSamples; sampleIdx++)
//            output->channels[chanIdx][sampleIdx] = pairwiseMatrix_[chanIdx][sampleIdx];
//
//    pairwiseMatrix_.clear();
}

void
OM_CHOP::checkInputs(const CHOP_Output *outputs, OP_Inputs *inputs, void *)
{
    std::string outputChoice(inputs->getParString(PAR_OUTPUT));
    
    outChoice_ = OutputMenuMap[outputChoice];
    
    inputs->enablePar("Maxclusters", false);
    
    // update UI
    switch (outChoice_) {
        case Derivatives:
            
            break;
            
        case Pairwise:
            break;
            
        case Dwt:
            break;
            
        case Cluster:
            inputs->enablePar("Maxclusters", true);
            break;
        default:
            break;
    }
}


