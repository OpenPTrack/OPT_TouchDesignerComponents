//
//  OPT_CHOP.hpp
//  OPT_CHOP
//
//  Created by Peter Gusev on 3/15/18.
//  Copyright © 2018 UCLA. All rights reserved.
//

#ifndef OPT_CHOP_hpp
#define OPT_CHOP_hpp

#include <map>
#include <vector>
#include <set>

#ifdef WIN32
// nothing's here
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/ip.h>
#endif

#include "CHOP_CPlusPlusBase.h"
#include "o-base.hpp"

class OPT_CHOP : public CHOP_CPlusPlusBase,
public OBase
{
public:
	OPT_CHOP(const OP_NodeInfo * info);

	virtual ~OPT_CHOP();

	virtual void getGeneralInfo(CHOP_GeneralInfo* ginfo) override;

	virtual bool getOutputInfo(CHOP_OutputInfo * info)override;

	virtual const char * getChannelName(int index, void * reserved) override;

	virtual void execute(const CHOP_Output*, OP_Inputs*, void* reserved) override;

    virtual int32_t getNumInfoCHOPChans() override;
    virtual void getInfoCHOPChan(int index,
                                 OP_InfoCHOPChan* chan) override;
    virtual bool getInfoDATSize(OP_InfoDATSize* infoSize) override;
    virtual void getInfoDATEntries(int32_t index,
                                   int32_t nEntries,
                                   OP_InfoDATEntries* entries) override;
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
    
	const OP_NodeInfo *myNodeInfo;
    
    uint64_t heartbeat_, maxId_, nAliveIds_, nBlankRuns_;
    
    void setupSocketReader();
    void processingError(std::string m) override;
    
    void checkInputs(const CHOP_Output *, OP_Inputs *inputs, void *);
    void blankRunsTrigger();
    
    std::map<float, std::vector<float>> data;
    std::set<int> aliveIds_;
    std::map<int, std::vector<float>> lastTracks_;
    std::map<std::string, int> faceNameMap_;
};

#endif
