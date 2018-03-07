//
//  OM_CHOP.hpp
//  OM_CHOP
//
//  Created by Peter Gusev on 3/3/18.
//  Copyright © 2018 UCLA. All rights reserved.
//

#ifndef OM_CHOP_hpp
#define OM_CHOP_hpp

#include <map>
#include <vector>
#include <string>
#include <queue>
#include <set>

#ifdef WIN32
    #include <winsock2.h>
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/ip.h>
#endif

#include "rapidjson/document.h"

#include "CHOP_CPlusPlusBase.h"
#include "JsonSocketReader.hpp"

#define BUFLEN 65507

class OM_CHOP : public CHOP_CPlusPlusBase, public JsonSocketReader::ISlaveReceiver
{
public:
    typedef enum _OutChoice {
        Unknown,
        Derivatives,
        Pairwise,
        Dtw,
        Cluster,
        Hotspots,
        Pca,
        Stagedist
    } OutChoice;
    
    OM_CHOP(const OP_NodeInfo * info);
    
    virtual ~OM_CHOP();
    
    virtual void getGeneralInfo(CHOP_GeneralInfo* ginfo) override;
    
    virtual bool getOutputInfo(CHOP_OutputInfo * info)override;
    
    virtual const char * getChannelName(int index, void * reserved) override;
    
    virtual void execute(const CHOP_Output*, OP_Inputs*, void* reserved) override;
    
    virtual int32_t getNumInfoCHOPChans() override;
    virtual void getInfoCHOPChan(int index,
                                 OP_InfoCHOPChan* chan) override;
    
    virtual void setupParameters(OP_ParameterManager * manager) override;
    
    virtual const char* getWarningString() override
    {
        return (warningMessage_.size() ? warningMessage_.c_str() : NULL);
    }
    
    virtual const char* getErrorString() override
    {
        return (errorMessage_.size() ? errorMessage_.c_str() : NULL);
    }
    
private:
    
    std::string errorMessage_, warningMessage_;
    
    std::atomic<bool> queueBusy_;
    std::mutex documentQueueMutex_;
    std::queue<rapidjson::Document> documentQueue_;
    
    // dictionary of collected messages
    typedef std::map<int, std::pair<double, std::vector<rapidjson::Document>>> MessagesQueue;
    std::mutex messagesMutex_;
    MessagesQueue messages_;
    
    OutChoice outChoice_;
    
    const OP_NodeInfo *myNodeInfo;
    
    int seq_;
    uint64_t nAliveIds_;
    float *pairwiseMat_, *dtwMat_;
    
    void setupSocketReader();
    
    void onNewJsonObjectReceived(const rapidjson::Document&) override;
    void onSocketReaderError(const std::string&) override;
    
    void processQueue();
    
    // outputs:
    // - id order
    // - derivatives
    // - dwt distances
    // - pairwise matrix
    // - clusters
    // - stage distances
    // - hotspots
    // - dtw
    void processMessages(std::vector<rapidjson::Document>& messages,
                         std::vector<int>& idOrder,
                         std::map<int, std::pair<float,float>>& derivatives1,
                         std::map<int, std::pair<float,float>>& derivatives2,
                         float* pairwiseMatrix,
                         std::vector<std::vector<float>>& clustersData,
                         std::map<int, std::vector<float>>& stageDistances,
                         std::vector<std::vector<float>>& hotspotsData,
                         float* dtwMatrix);
    void processIdOrder(std::vector<rapidjson::Document>& messages,
                        std::vector<int>& idOrder);
    void processDerivatives(std::vector<rapidjson::Document>& messages,
                            std::vector<int>& idOrder,
                            std::map<int, std::pair<float,float>>& derivatives1,
                            std::map<int, std::pair<float,float>>& derivatives2);
    void processPairwise(std::vector<rapidjson::Document>& messages,
                         std::vector<int>& idOrder,
                         float* pairwiseMatrix);
    void processClusters(std::vector<rapidjson::Document>& messages,
                         std::vector<std::vector<float>>& clustersData);
    void processStageDistances(std::vector<rapidjson::Document>& messages,
                               std::vector<int>& idOrder,
                               std::map<int, std::vector<float>>& stageDistances);
    void processHotspots(std::vector<rapidjson::Document>& messages,
                         std::vector<std::vector<float>>& hotspotsData);
    void processDtw(std::vector<rapidjson::Document>& messages,
                    std::vector<int>& idOrder,
                    float* dtwMatrix);
    
    bool retireve(const std::string& key,
                  std::vector<rapidjson::Document>&,
                  rapidjson::Value&);
    
    void checkInputs(const CHOP_Output *, OP_Inputs *inputs, void *);
};




#endif /* OM_CHOP_hpp */
