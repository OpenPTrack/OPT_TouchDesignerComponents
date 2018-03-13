//
//  om-json-parser.cpp
//  OM_CHOP
//
//  Created by Peter Gusev on 3/12/18.
//  Copyright © 2018 Derivative. All rights reserved.
//

#include "om-json-parser.hpp"

#include <sstream>

#include "defines.h"

using namespace std;

#define SET_ERR_MSG(errexpr) {\
stringstream msg; \
msg << errexpr; \
if (errMsg_ == "") errMsg_ = "JSON parse error: " + msg.str() + "\n"; \
parseResult_ = false;\
}

#define CHECK_PARSE_RESULT() {\
if (!parseResult_) return parseResult_; \
}

static map<string, OmJsonParser::PacketSubtype> StringSubtypeMap = {
    { "all", OmJsonParser::PacketSubtype::All },
    { OM_JSON_SUBTYPE_DERS, OmJsonParser::PacketSubtype::Derivatives },
    { OM_JSON_SUBTYPE_SIM, OmJsonParser::PacketSubtype::Similarity },
    { OM_JSON_SUBTYPE_DIST, OmJsonParser::PacketSubtype::Distance },
    { OM_JSON_SUBTYPE_MDYN, OmJsonParser::PacketSubtype::Massdyn },
    { OM_JSON_SUBTYPE_CLUSTER, OmJsonParser::PacketSubtype::Cluster }
};

OmJsonParser::OmJsonParser(int maxMatSize):
errMsg_(""),
parseResult_(false)
{
    size_t len = maxMatSize*(maxMatSize+1);
    pairwiseMat_ = (float*)malloc(len*sizeof(float));
    dtwMat_ = (float*)malloc(len*sizeof(float));
    
    memset(pairwiseMat_, -1, len);
    memset(dtwMat_, -1, len);
}

OmJsonParser::~OmJsonParser()
{
    free(pairwiseMat_);
    free(dtwMat_);
}

bool
OmJsonParser::parse(vector<rapidjson::Document> &messages,
                    set<string> &parsedSubtypes,
                    string subtype)
{
    errMsg_ = "";
    parseResult_ = true;
//    clearAll();
    
    processIdOrder(messages, idOrder_);
    CHECK_PARSE_RESULT()
    
    set<PacketSubtype> subtypesToCheck;
    if (subtype == "all")
        for (auto& p:StringSubtypeMap)
            subtypesToCheck.insert(p.second);
    else
        subtypesToCheck.insert(StringSubtypeMap[subtype]);
    
    for (auto& subtype:subtypesToCheck)
    {
        switch (subtype) {
            case All:
            case Derivatives:
                processDerivatives(messages,
                                   idOrder_,
                                   derivatives1_,
                                   derivatives2_,
                                   speeds_,
                                   accelerations_);
                if (subtype != All)
                    break;
            case Distance:
                if (subtype != All)
                    break;
            case Cluster:
                if (subtype != All)
                    break;
            case Massdyn:
                if (subtype != All)
                    break;
            case Similarity:
                if (subtype != All)
                    break;
            default:
                break;
        }
    }
    
    return parseResult_;
}

void
OmJsonParser::processIdOrder(vector<rapidjson::Document>& messages,
                             vector<int>& idOrder)
{ // retrieving id order
    rapidjson::Value idorder;
    
    if (retireve(OM_JSON_IDS, messages, idorder))
    {
        if (!idorder.IsArray())
            SET_ERR_MSG(OM_JSON_IDS << " is not an array.")
        else
        {
            const rapidjson::Value& arr = idorder.GetArray();
            for (rapidjson::SizeType i = 0; i < arr.Size(); i++)
                idOrder.push_back(arr[i].GetInt());
        }
    }
    else
        SET_ERR_MSG("couldn't find field " << OM_JSON_IDS << " in received json messages")
        
#ifdef PRINT_IDS
        cout << "filtered ids: ";
    for (auto id:idOrder)
        cout << id << " ";
    cout << endl;
#endif
}

void
OmJsonParser::processDerivatives(vector<rapidjson::Document>& messages,
                            vector<int>& idOrder,
                            map<int, vector<float>>& derivatives1,
                            map<int, vector<float>>& derivatives2,
                            map<int, float>& speed,
                            map<int, float>& acceleration)
{ // retrieving derivatives
    rapidjson::Value values;
    if (retireve(OM_JSON_VALUES, messages, values))
    {   
        retrieveOrdered(values, OM_JSON_FIRSTDERS, idOrder, derivatives1);
        retrieveOrdered(values, OM_JSON_SECONDDERS, idOrder, derivatives2);
        retrieveOrdered(values, OM_JSON_SPEEDS, idOrder, speed);
        retrieveOrdered(values, OM_JSON_ACCELERATIONS, idOrder, acceleration);
    }
    else
        SET_ERR_MSG("derivatives subtype; can't find " << OM_JSON_VALUES)
    
#ifdef PRINT_DERIVATIVES
    cout << "derivatives1: " << endl;
    for (auto v:derivatives1)
    {
        cout << "id " << v.first << " d1x " << v.second.first << " d1y " << v.second.second;
        
        if (derivatives2.find(v.first) != derivatives2.end())
            cout << " d2x " << derivatives2[v.first].first << " d2y " << derivatives2[v.first].second;
        else
            cout << " no d2 data ";
        if (speed.find(v.first) != speed.end())
            cout << " speed " << speed[v.first];
        else
            cout <<  " no speed data ";
        if (acceleration.find(v.first) != acceleration.end())
            cout << " accel " << acceleration[v.first] << endl;
        else
            cout << " no acceleration data " << endl;
    }
#endif
}

bool
OmJsonParser::retireve(const string& key,
                  vector<rapidjson::Document>& messages,
                  rapidjson::Value& val)
{
    
    for (auto &m:messages)
    {
#if 0
        {
            rapidjson::StringBuffer buffer;
            buffer.Clear();
            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
            m.Accept(writer);
            
            cout << "---> message: " << buffer.GetString() << endl;
        }
#endif
        
        if (m.HasMember(key.c_str()))
        {
            val = m[key.c_str()];
            return true;
        }
    }
    
    return false;
}

bool
OmJsonParser::retrieveOrdered(const rapidjson::Value& document,
                              const char *key,
                              const vector<int> &idOrder,
                              map<int, vector<float> > &listOfLists)
{
    parseResult_ = true;
    
    if (document.HasMember(key))
    {
        const rapidjson::Value& masterList = document[key];
        
        if (!masterList.IsArray())
            SET_ERR_MSG(key << " is not an array")
        else
        {
            const rapidjson::Value::ConstArray& arr = masterList.GetArray();
            for (rapidjson::SizeType i = 0; i < arr.Size(); i++)
            {
                if (i >= idOrder.size())
                    SET_ERR_MSG(key << " - id list size doesn't match list size")
                {
                    if (!arr[i].IsArray())
                        SET_ERR_MSG(key << " expected to be list of lists")
                    else
                    {
                        int id = idOrder[i];
                        const rapidjson::Value::ConstArray& subArr = arr[i].GetArray();
                        
                        listOfLists[id] = vector<float>();
                        
                        for (rapidjson::SizeType k = 0; k < subArr.Size(); ++k)
                        {
                            float val = 0;
                            if (!subArr[k].IsFloat())
                                SET_ERR_MSG(key << " bad type for " << i << " sublist element "
                                            << k << ": float expected")
                            else
                                val = subArr[k].GetFloat();
                            
                            listOfLists[id].push_back(val);
                        }
                    } // else - arr is list
                } // if i >= idOrder
            } // for i
        } // else - masterList is array
    }
    else
        SET_ERR_MSG("can't find " << key)

   return parseResult_;
}

bool
OmJsonParser::retrieveOrdered(const rapidjson::Value& document,
                              const char* key,
                              const std::vector<int>& idOrder,
                              std::map<int, float>& list)
{
    parseResult_ = true;
    
    if (document.HasMember(key))
    {
        if (!document[key].IsArray())
            SET_ERR_MSG(key << " is not a list")
        else
        {
            const rapidjson::Value::ConstArray& arr = document[key].GetArray();
            
            for (rapidjson::SizeType i = 0; i < arr.Size(); ++i)
            {
                if (i >= idOrder.size())
                    SET_ERR_MSG(key << " - id list size doesn't match list size")
                else
                {
                    int id = idOrder[i];
                    float val = 0;
                    
                    if (!arr[i].IsFloat())
                        SET_ERR_MSG(key << " bad type for " << i << " element: float expected")
                    else
                        val = arr[i].GetFloat();
                    
                    list[id] = val;
                } // if i >= idorder
            } // for
        } // else document[key] is array
    }
    else
        SET_ERR_MSG("can't find " << key)
        
    return parseResult_;
}
