//
//  o-base.hpp
//  OPT_CHOP
//
//  Created by Peter Gusev on 3/15/18.
//  Copyright © 2018 Derivative. All rights reserved.
//

#ifndef o_base_hpp
#define o_base_hpp

#include <stdio.h>

#include <map>
#include <vector>
#include <string>
#include <queue>
#include <set>

#include "JsonSocketReader.hpp"

class OBase : public JsonSocketReader::ISlaveReceiver
{
public:
    OBase(int msgBundleSize, int portnum);
    ~OBase();
    
protected:
    void onNewJsonObjectReceived(const rapidjson::Document&) override;
    void onSocketReaderError(const std::string&) override;
    void onSocketReaderWillReset() override;
    
    void processQueue();
    virtual void processingError(std::string m) {}
    
    std::atomic<bool> queueBusy_;
    std::mutex messagesMutex_;
    // dictionary of collected messages
    typedef std::map<int, std::pair<double, std::vector<rapidjson::Document>>> MessagesQueue;
    MessagesQueue messages_;
    
private:
    int msgBundleSize_, portnum_;
    
    
    std::mutex documentQueueMutex_;
    std::queue<rapidjson::Document> documentQueue_;
};

#endif /* o_base_hpp */
