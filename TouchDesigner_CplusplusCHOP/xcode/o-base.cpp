//
//  o-base.cpp
//  OPT_CHOP
//
//  Created by Peter Gusev on 3/15/18.
//  Copyright © 2018 Derivative. All rights reserved.
//

#include "o-base.hpp"

#include <iostream>
#include <sstream>

#include "defines.h"
#include "debug.h"

#define MESSAGE_QUEUE_THRESHOLD 500

using namespace std;
using namespace chrono;

OBase::OBase(int msgBundleSize, int portnum):
msgBundleSize_(msgBundleSize), portnum_(portnum)
{
    
}

OBase::~OBase()
{
    
}

//******************************************************************************
void
OBase::onNewJsonObjectReceived(const rapidjson::Document &d)
{
    // check thread safety for vector
    rapidjson::Document dcopy;
    dcopy.CopyFrom(d, dcopy.GetAllocator());
    
    {
        lock_guard<mutex> lock(documentQueueMutex_);
        queueBusy_ = true;
        documentQueue_.push(move(dcopy));
        queueBusy_ = false;
    }
}

void
OBase::onSocketReaderError(const std::string &m)
{
    processingError(m);
}

void
OBase::onSocketReaderWillReset()
{}

void
OBase::processQueue()
{
    double nowTs = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    
    lock_guard<mutex> lock(documentQueueMutex_);
    // this function goes over queue and moves them over to messages dict
    while (documentQueue_.size())
    {
        int seqNo = -1;
        
        if (documentQueue_.front().HasMember(OM_JSON_SEQ))
            seqNo = documentQueue_.front()[OM_JSON_SEQ].GetInt(); // deprecated for v1
        else if (documentQueue_.front().HasMember(OM_JSON_HEADER) &&
                 documentQueue_.front()[OM_JSON_HEADER].HasMember(OM_JSON_SEQ))
        {
            seqNo = documentQueue_.front()[OM_JSON_HEADER][OM_JSON_SEQ].GetInt();
        }
        else
            processingError("Bad json formatting: can't locate 'seq' field");
        
        if (seqNo >= 0)
        {
            if (messages_.find(seqNo) == messages_.end())
                messages_[seqNo] = pair<double, vector<rapidjson::Document>>(nowTs, vector<rapidjson::Document>());
            
            messages_[seqNo].second.push_back(move(documentQueue_.front()));
        }
        
        documentQueue_.pop();
#ifdef PRINT_MSG_QUEUE
        float avgBundleLen = 0;
        for (auto& t:messages_) avgBundleLen += t.second.second.size();
        avgBundleLen /= (float)messages_.size();
        
        cout << "document queue " << documentQueue_.size() << " messages queue "
        << messages_.size() << " avg bundle len " << avgBundleLen << endl;
#endif
        if (messages_.size() >= MESSAGE_QUEUE_THRESHOLD)
        {
            stringstream ss;
            
            ss << "Incoming message queue is too large - " << messages_.size()
            << " (data is received, but not processed). Check message "
            "bundle size (current bundle size is "
            << msgBundleSize_ << "; was the protocol updated?)";
            
            processingError(ss.str());
        }
    }
}
