#pragma once

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

class OPT_CHOP : public CHOP_CPlusPlusBase
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

	virtual void setupParameters(OP_ParameterManager * manager) override;

private:
	const OP_NodeInfo *myNodeInfo;
	char buf[BUFLEN];
	bool listening;
	int seq;
	const char* names[6] = { "id", "age", "confidence", "x", "y", "height"};
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


