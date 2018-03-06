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
        Dwt,
        Cluster
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
    typedef std::map<int, std::vector<rapidjson::Document>> MessagesQueue;
    std::mutex messagesMutex_;
    std::map<int, std::vector<rapidjson::Document>> messages_;
    
    OutChoice outChoice_;
    
    const OP_NodeInfo *myNodeInfo;
    int seq;
    
    std::map<float, std::vector<float>> data;
    
    uint64_t nAliveIds_;
    std::vector<std::vector<float>> pairwiseMatrix_;
    
    void setupSocketReader();
    
    void onNewJsonOnjectReceived(const rapidjson::Document&) override;
    void onSocketError(const std::string&) override;
    
    void processQueue();
    
    // outputs:
    // - id order
    // - derivatives
    // - dwt distances
    // - pairwise matrix
    void processMessages(std::vector<rapidjson::Document>&,
                         std::vector<int>& idOrder,
                         std::map<int, std::pair<float,float>>&,
                         std::map<int, std::pair<float,float>>&,
                         std::map<int, std::vector<float>>&,
                         std::vector<std::vector<float>>&,
                         std::vector<std::vector<float>>&);
    
    bool retireve(const std::string& key,
                  std::vector<rapidjson::Document>&,
                  rapidjson::Value&);
    
    bool hasResidualData();
    void processResidualData(const CHOP_Output*, OP_Inputs*);
    
    void checkInputs(const CHOP_Output *, OP_Inputs *inputs, void *);
};




#endif /* OM_CHOP_hpp */
