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
    std::queue<rapidjson::Document> documentQueue_;
    
    const OP_NodeInfo *myNodeInfo;
    int seq;
    const char* names[6] = { "id", "age", "confidence", "x", "y", "height"};
//    const char* names[6] = { "id", "der1x", "der1y", "der2x", "der2y"};
    std::map<float, std::vector<float>> data;
    
    uint64_t heartbeat, maxId;
    
    void setupSocketReader();
    
    void onNewJsonOnjectReceived(const rapidjson::Document&) override;
    void onSocketError(const std::string&) override;
};




#endif /* OM_CHOP_hpp */
