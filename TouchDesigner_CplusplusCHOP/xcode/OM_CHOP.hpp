//
//  OM_CHOP.hpp
//  OM_CHOP
//
//  Created by Peter Gusev on 3/3/18.
//  Copyright © 2018 UCLA. All rights reserved.
//

#ifndef OM_CHOP_hpp
#define OM_CHOP_hpp

#define BUFLEN 65507
#include "CHOP_CPlusPlusBase.h"
#include <map>
#include <vector>

#ifdef WIN32
#include <winsock2.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#endif

class OM_CHOP : public CHOP_CPlusPlusBase
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
    
private:
    const OP_NodeInfo *myNodeInfo;
    char buf[BUFLEN];
    bool listening;
    int seq;
    const char* names[6] = { "id", "der1x", "der1y", "der2x", "der2y"};
    std::map<float, std::vector<float>> data;
    
#ifdef WIN32
    SOCKET s;
    WSADATA wsa;
#else
    int s;
#endif
    
    struct sockaddr_in server, si_other;
    unsigned int slen;
    int recv_len;
    uint64_t heartbeat, maxId;
};




#endif /* OM_CHOP_hpp */
