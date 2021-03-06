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

#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#include "defines.h"
#include "debug.h"

#define MESSAGE_QUEUE_THRESHOLD 500
#define MESSAGE_LIFETIME_MS 2000
#define DEFAULT_FRAMEID "default"
#define NODATA_THRES    1000     // threshold for no data detection

using namespace std;
using namespace chrono;

OBase::OBase(int msgBundleSize, int portnum):
msgBundleSize_(msgBundleSize),
lastDataTs_(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count()),
noData_(false)
{}

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
    
    if (nowTs - lastDataTs_ > NODATA_THRES)
        noData_ = documentQueue_.size() == 0;
    lastDataTs_ = documentQueue_.size() > 0 ? nowTs : lastDataTs_;
    
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

void
OBase::processBundle(OnNewBundle handler)
{
    double nowTs = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    
    if (!queueBusy_)
    {
        lock_guard<mutex> lock(messagesMutex_);
        nDropped_ = 0;
        std::map<std::string, int> seqs;
        
        for (MessagesQueue::iterator it = messages_.begin();
             it != messages_.end(); /* NO INCREMENT HERE */)
        {
            if ((*it).second.second.size() >= msgBundleSize_)
            {
                vector<rapidjson::Document>& msgs = (*it).second.second;
                string frameId = retrieveFrameId(msgs[0]);
                int thisSeqNo = (*it).first;
                
                if (seqs.find(frameId) == seqs.end())
                    seqs[frameId] = (*it).first;
                
                bool oldMessage = (thisSeqNo < seqs[frameId]);
                
                if (oldMessage)
                    nDropped_++;
                else
                {
                    handler(msgs);
                    seqs[frameId] = (*it).first;
                }
                
                messages_.erase(it++);
                
                // using continue instead of break makes sense for OPT
                // TODO: test openmoves whether continue works
//                continue; // we'll continue to process the whole queue
//                break;
            } // if messages bundle
            else
            {
                // check message timestamp and delete it if it's too old
                if (nowTs-(*it).second.first >= MESSAGE_LIFETIME_MS)
                {
                    stringstream ss;
                    ss << "Cleaning up old unprocessed message bundle (id " << (*it).first
                    << "). This normally should not happen, check incoming messages bundle length. Deleted: ";
                    processingError(ss.str());
                    messages_.erase(it++);
                }
                else
                    ++it;
            }
        } // for msg in queue
        
        seqs.insert(lastProcessedSeqs_.begin(), lastProcessedSeqs_.end());
        lastProcessedSeqs_ = seqs;
        
        //if (nDropped_)
        //{
        //    stringstream ss;
        //    ss << "Dropped " << nDropped_ << " old messages" << std::endl;
        //    processingError(ss.str());
        //}
    } // if queue not busy
}

string
OBase::bundleToString(const vector<rapidjson::Document>& bundle)
{
    stringstream ss;
    
    for (auto& d:bundle)
    {
        rapidjson::StringBuffer buffer;
        buffer.Clear();
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        d.Accept(writer);
        
        ss << buffer.GetString() << endl;
    }
    
    return ss.str();
}

string
OBase::retrieveFrameId(const rapidjson::Document &d)
{
    if (d.HasMember(OPT_JSON_HEADER) &&
        d[OPT_JSON_HEADER].HasMember(OPT_JSON_FRAMEID))
        return string(d[OPT_JSON_HEADER][OPT_JSON_FRAMEID].GetString());
    return DEFAULT_FRAMEID; // frame ids not supported
}
