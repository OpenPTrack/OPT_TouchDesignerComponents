//
//  JsonSocketReader.cpp
//  OPT_CHOP
//
//  Created by Peter Gusev on 3/5/18.
//  Copyright © 2018 UCLA. All rights reserved.
//

#include "JsonSocketReader.hpp"

#include <sstream>
#include <regex>
#include <iostream>

#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/error/en.h"

#ifdef WIN32
	#include <ws2tcpip.h>
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/ip.h>
    #include <errno.h>
    #include <unistd.h>

    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    #define WSAGetLastError() errno
#endif

using namespace std;
using std::string;

string do_replace( string const & in, string const & from, string const & to )
{
    return std::regex_replace( in, std::regex(from), to );
}

//******************************************************************************

JsonSocketReader::JsonSocketReader(int port):
isActive_(false)
{
    setupSocket(port);
}

JsonSocketReader::~JsonSocketReader()
{
    stop();
    destroySocket();
}

void
JsonSocketReader::start()
{
    if (!isActive_)
    {
        readThread_ = make_shared<thread>(bind(&JsonSocketReader::listenSocket, this));
    }
    else
        throw runtime_error("Socket reader is already running");
}

void
JsonSocketReader::stop()
{
    if (isActive_)
    {
        isActive_ = false;
        readThread_->join();
    }
}

void
JsonSocketReader::reset()
{
    lock_guard<mutex> lock(slavesMutex_);
    for (auto s:slaves_) s->onSocketReaderWillReset();
    slaves_.clear();
}

bool
JsonSocketReader::isRunning()
{
    return isActive_;     
}

void
JsonSocketReader::registerSlave(JsonSocketReader::ISlaveReceiver *slave)
{
    lock_guard<mutex> lock(slavesMutex_);
    slaves_.push_back(slave);
}

void
JsonSocketReader::unregisterSlave(JsonSocketReader::ISlaveReceiver *slave)
{
    std::vector<ISlaveReceiver*>::iterator it = std::find(slaves_.begin(), slaves_.end(), slave);
    if (it != slaves_.end())
    {
        lock_guard<mutex> lock(slavesMutex_);
        slaves_.erase(it);
    }
}

//******************************************************************************
void
JsonSocketReader::setupSocket(int port)
{
    //Create a UDP Socket.
    struct sockaddr_in server;
    
#ifdef WIN32
    DWORD timeout = 1000;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_) != 0)
    {
        stringstream ss;
        ss << "WSAStartup failed (" << WSAGetLastError() << "): " << strerror(WSAGetLastError());
        
		throw runtime_error(ss.str());
    }
#else
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
#endif
    
    if ((socket_ = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET)
    {
        stringstream ss;
        ss << "Could not create socket (" << WSAGetLastError() << "): " << strerror(WSAGetLastError());
        
        throw runtime_error(ss.str());
    }
    
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(port);
    
    if (setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0)
    {
        stringstream ss;
        ss << "Socket setup error (" << WSAGetLastError() << "): " << strerror(WSAGetLastError());
        
        perror(ss.str().c_str());
        throw runtime_error(ss.str());
    }
    
    if (::bind(socket_, (struct sockaddr *)&server, sizeof(server)) == SOCKET_ERROR)
    {
        stringstream ss;
        ss << "Socket bind failure (" << WSAGetLastError() << "): " << strerror(WSAGetLastError());
        
        perror(ss.str().c_str());
        throw runtime_error(ss.str());
    }
}

void
JsonSocketReader::destroySocket()
{
#ifdef WIN32
    closesocket(socket_);
    WSACleanup();
#else
    close(socket_);
#endif
}

void
JsonSocketReader::listenSocket()
{
    static struct sockaddr si_other;
    static socklen_t slen = sizeof(si_other);
    
	isActive_ = true;
    
    while (isActive_)
    {
        memset(buffer_, 0, BUFLEN);
        long recvLen = recvfrom(socket_,
                                buffer_, BUFLEN,
                                0,
                                (struct sockaddr*)&si_other, &slen);
        if (buffer_[0] != 0)
        {
            std::string buf(buffer_);
            // little hack to replace pyhton's NaN to "NaN" (strings) so that parser does not freak out
            strcpy(buffer_, do_replace(buf, "NaN", "\"Null\"").c_str());
            
            rapidjson::Document d;
            d.Parse(buffer_);
            
            if (!d.HasParseError())
            {
                // deliver document to slaves
                {
                    lock_guard<mutex> lock(slavesMutex_);
                    for (auto slave:slaves_)
                        slave->onNewJsonObjectReceived(d);
                }
            }
            else
            {
                stringstream ss;
                ss << "Error while parsing JSON (" << buffer_ << "): "
                   << rapidjson::GetParseError_En(d.GetParseError());
                
                perror(ss.str().c_str());
                {
                    lock_guard<mutex> lock(slavesMutex_);
                    for (auto slave:slaves_)
                        slave->onSocketReaderError(ss.str());
                }
            }
        }
#ifdef WIN32
		else if (recvLen == SOCKET_ERROR)
#else
        else if (recvLen == SOCKET_ERROR && (errno != EWOULDBLOCK || errno != EAGAIN))
#endif
        {
			std::cout << recvLen << std::endl;

            // socket error here
            stringstream ss;
            ss << "Socket error (" << WSAGetLastError() << ") occurred: " << strerror(WSAGetLastError());
            
            perror(ss.str().c_str());
            // deliver socket error to all slaves
            {
                lock_guard<mutex> lock(slavesMutex_);
                for (auto slave:slaves_)
                    slave->onSocketReaderError(ss.str());
            }
        }
#ifdef WIN32
        else if (recvLen == SOCKET_ERROR && errno == WSAEWOULDBLOCK)
#else
        else if (recvLen == SOCKET_ERROR && (errno == EWOULDBLOCK || errno == EAGAIN))
#endif
        {
            // let's not use this, for now
            /*
            stringstream ss;
            ss << "No data in socket (" << errno << "): " << strerror(WSAGetLastError());
            
            perror(ss.str().c_str());
            // deliver socket error to all slaves
            {
                lock_guard<mutex> lock(slavesMutex_);
                for (auto slave:slaves_)
                    slave->onSocketReaderError(ss.str());
            }
            */
        }
    }
}
