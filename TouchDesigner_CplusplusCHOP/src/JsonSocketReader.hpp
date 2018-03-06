//
//  JsonSocketReader.hpp
//  OPT_CHOP
//
//  Created by Peter Gusev on 3/5/18.
//  Copyright © 2018 UCLA. All rights reserved.
//

#ifndef SocketReader_hpp
#define SocketReader_hpp

#include <stdio.h>
#include <vector>
#include <thread>

#include "rapidjson/document.h"

#ifdef WIN32
    #include <winsock2.h>
	#include <atomic>
	#include <mutex>
#endif

#define BUFLEN 65507

/**
 * JSON Socket reader - provides asynchronous reading from UDP socket
 * and formatting read data as a JSON object.
 * Whenever new JSON object is retrieved from the socket, all registered
 * receivers are notified. They must copy data if they need to and return
 * as quickly as possible.
 */
class JsonSocketReader {
public:
    
    class ISlaveReceiver {
    public:
        virtual void onNewJsonObjectReceived(const rapidjson::Document&) = 0;
        virtual void onSocketReaderError(const std::string&) = 0;
    };
    
    JsonSocketReader(int port);
    ~JsonSocketReader();
    
    // start listening socket in a separate thread
    // throws if already started
    void start();
    
    // stops listening socket
    void stop();
    
    bool isRunning();
    
    void registerSlave(ISlaveReceiver*);
    void unregisterSlave(ISlaveReceiver*);
    
private:
    char buffer_[BUFLEN];
    
#ifdef WIN32
    SOCKET socket_;
    WSADATA wsa_;
#else
    int socket_;
#endif
    std::atomic<bool> isActive_;
    std::shared_ptr<std::thread> readThread_;
    
    std::mutex slavesMutex_;
    std::vector<ISlaveReceiver*> slaves_;
    
    void setupSocket(int port);
    void destroySocket();
    void listenSocket();
};

#endif /* SocketReader_hpp */
