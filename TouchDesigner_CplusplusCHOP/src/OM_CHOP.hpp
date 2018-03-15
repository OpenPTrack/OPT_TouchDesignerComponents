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
#include "o-base.hpp"

#define BUFLEN 65507

class OmJsonParser;

class OM_CHOP : public CHOP_CPlusPlusBase,
public OBase
{
public:
    typedef enum _OutChoice {
        Unknown,
        Derivatives,
        Pairwise,
        Dtw,
        Cluster,
        ClusterIds,
        Hotspots,
        Pca,
        Stagedist,
        Templates
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
    virtual void pulsePressed(const char *name) override;
    
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
    
    OutChoice outChoice_;
    
    const OP_NodeInfo *myNodeInfo;
    
    int seq_;
    uint64_t nAliveIds_, nBlankRuns_, nClusters_;
    std::shared_ptr<OmJsonParser> omJsonParser_;
    std::vector<int> lastAliveIds_;
    
    void setupSocketReader();
    void processingError(std::string m) override;
    
    void checkInputs(const CHOP_Output *, OP_Inputs *inputs, void *);
    void blankRunsTrigger();
};

#endif /* OM_CHOP_hpp */
