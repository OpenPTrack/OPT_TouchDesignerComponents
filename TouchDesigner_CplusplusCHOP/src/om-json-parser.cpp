//
//  om-json-parser.cpp
//  OM_CHOP
//
//  Created by Peter Gusev on 3/12/18.
//  Copyright © 2018 Derivative. All rights reserved.
//

#include "om-json-parser.hpp"

#include <sstream>
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include <iostream>

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
parseResult_(false),
pairwiseStride_(maxMatSize)
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
    clearAll();
    
    processIdOrder(messages, idOrder_);
    CHECK_PARSE_RESULT()
    
    set<PacketSubtype> subtypesToCheck;
    if (subtype == "all")
        for (auto& p:StringSubtypeMap)
            subtypesToCheck.insert(p.second);
    else
        subtypesToCheck.insert(StringSubtypeMap[subtype]);
    
    for (auto& st:subtypesToCheck)
    {
        // TODO: this should be var not vector
        std::vector<rapidjson::Document> subTypeMsg;
        if (!hasSubType(messages, subtype, subTypeMsg))
            continue;
        
        switch (st) {
            case All:
            case Derivatives:
            {
                    processDerivatives(subTypeMsg,
                                       idOrder_,
                                       derivatives1_,
                                       derivatives2_,
                                       speeds_,
                                       accelerations_);
            }
                if (st != All)
                    break;
            case Distance:
            {
                    processDistances(subTypeMsg,
                                     idOrder_,
                                     pairwiseMat_,
                                     stageDistances_);
            }
                if (st != All)
                    break;
            case Cluster:
            {
                    processClusters(subTypeMsg, clustersData_, clusterIds_);
            }
                if (st != All)
                    break;
            case Massdyn:
            {
                    processHotspots(subTypeMsg, hotspotsData_);
                    processGroupTarget(subTypeMsg, groupTarget_);
            }
                if (st != All)
                    break;
            case Similarity:
                {
                    processDtw(subTypeMsg, idOrder_, dtwMat_);
                    processTemplates(subTypeMsg, idOrder_, templatesData_);
                }
                if (st != All)
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

void
OmJsonParser::processDistances(std::vector<rapidjson::Document>& messages,
                               std::vector<int>& idOrder,
                               float* pairwiseMatrix,
                               std::map<int, std::vector<float>>& stageDistances)
{
    rapidjson::Value values;
    if (retireve(OM_JSON_VALUES, messages, values))
    {
        retrieveOrdered(values, OM_JSON_PAIRWISE, idOrder, pairwiseMatrix);
        retrieveStageDistances(values, OM_JSON_STAGEDIST, idOrder, stageDistances);
    }
    else
        SET_ERR_MSG("distances subtype; can't fund " << OM_JSON_VALUES);
    
#ifdef PRINT_PAIRWISE
    cout << "pairwise: " << endl;
    for (int i = 0; i < output->numSamples; ++i)
    {
        for (int j = 0; j < output->numSamples; ++j)
            cout << pairwiseMatrix[i*PAIRWISE_WIDTH+j] << " ";
        cout << endl;
    }
#endif
}

void
OmJsonParser::processClusters(std::vector<rapidjson::Document> &messages,
                              std::vector<std::vector<float> > &clustersData,
                              std::vector<std::vector<float>>& clusterIds)
{
    rapidjson::Value values;
    if (retireve(OM_JSON_VALUES, messages, values))
    {
        if (retrieveUnordered(values, OM_JSON_CLUSTERCENTERS, clustersData))
        {   
            if (!values.HasMember(OM_JSON_CLUSTERSPREADS) || !values[OM_JSON_CLUSTERCENTERS].IsArray())
                SET_ERR_MSG("can't find field " << OM_JSON_CLUSTERSPREADS << " or field is not a list")
            else
            {
                const rapidjson::Value& arr = values[OM_JSON_CLUSTERSPREADS].GetArray();
                
                if (arr.Size() != clustersData.size())
                    SET_ERR_MSG("cluster spreads list size does not match cluster centers list; attempting to proceed anyways")
                for (int i = 0; i < clustersData.size(); ++i)
                {
                    if (i < arr.Size())
                        clustersData[i].push_back(arr[i].GetFloat());
                    else
                        clustersData[i].push_back(0);
                }
            }
        }
        
        if (values.HasMember(OM_JSON_CLUSTER_POINTS) && values[OM_JSON_CLUSTER_POINTS].IsArray())
        {
            const rapidjson::Value& clusters = values[OM_JSON_CLUSTER_POINTS].GetArray();
            
            for (rapidjson::SizeType i = 0; i < clusters.Size(); ++i)
            {
                if (!clusters[i].IsArray())
                    SET_ERR_MSG("cluster array element is not a list")
                else
                {
                    const rapidjson::Value::ConstArray& cluster = clusters[i].GetArray();
                    clusterIds.push_back(vector<float>());
                    
                    for (rapidjson::SizeType k = 0; k < cluster.Size(); ++k)
                    {
                        if (!cluster[k].IsArray())
                            SET_ERR_MSG("cluster point is not a list")
                        else
                        {
                            const rapidjson::Value::ConstArray& clusterPoint = cluster[k].GetArray();
                            
                            if (clusterPoint.Size() < 3)
                                SET_ERR_MSG("cluster point list size is less than 3 (expected)")
                            else
                            {
                                for (int idx = 0; idx < 3; ++idx)
                                {
                                    float val = 0;
                                    if (!(clusterPoint[idx].IsFloat() || clusterPoint[idx].IsInt()))
                                        SET_ERR_MSG("cluster point contains elements other than float type")
                                    else
                                        val = clusterPoint[idx].GetFloat();
                                    clusterIds.back().push_back(val);
                                }
                            }
                        } // cluster point is array
                    } // for k
                } // else cluster is array
            } // for i
        } // if has cluster field
        else
            SET_ERR_MSG(OM_JSON_CLUSTER_POINTS << " element not found or is not a list")
    }
    else
        SET_ERR_MSG("clusters subtype; can't find " << OM_JSON_VALUES)
        
#ifdef PRINT_CLUSTERS
    cout << "clusters: " << endl;
    if (clustersData.size() == 0)
        cout << "EMPTY" << endl;
    else
        for (auto v:clustersData)
            cout << "x " << v[0] << " y " << v[1] << " spread " << v[2] << endl;
#endif
}

void
OmJsonParser::processHotspots(std::vector<rapidjson::Document>& messages,
                              std::vector<std::vector<float>>& hotspotsData)
{
    rapidjson::Value values;
    if (retireve(OM_JSON_VALUES, messages, values))
    {
        
    }
    else
        SET_ERR_MSG("massdynamics subtype; can't find " << OM_JSON_VALUES)
}

void
OmJsonParser::processGroupTarget(std::vector<rapidjson::Document>& messages,
                        std::vector<std::vector<float>>& groupTarget)
{
    rapidjson::Value values;
    if (retireve(OM_JSON_VALUES, messages, values))
    {
        
    }
    else
        SET_ERR_MSG("massdynamics subtype; can't find " << OM_JSON_VALUES)
}

void
OmJsonParser::processDtw(std::vector<rapidjson::Document>& messages,
                         std::vector<int>& idOrder,
                         float* dtwMatrix)
{
    rapidjson::Value values;
    if (retireve(OM_JSON_VALUES, messages, values))
    {
        
    }
    else
        SET_ERR_MSG("similarity subtype; can't find " << OM_JSON_VALUES)
}

void
OmJsonParser::processTemplates(std::vector<rapidjson::Document>& messages,
                      std::vector<int>& idOrder,
                      std::map<std::string, std::vector<float>>& templates)
{
    rapidjson::Value values;
    if (retireve(OM_JSON_VALUES, messages, values))
    {
        
    }
    else
        SET_ERR_MSG("similarity subtype; can't find " << OM_JSON_VALUES)
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

bool
OmJsonParser::retrieveOrdered(const rapidjson::Value &document,
                              const char *key,
                              const std::vector<int> &idOrder,
                              float *mat)
{
    parseResult_ = true;
    
    if (document.HasMember(key))
    {
        if (!document[key].IsArray())
            SET_ERR_MSG(key << " is not a list")
        else
        {
            const rapidjson::Value::ConstArray& arr = document[key].GetArray();
            
            for (int i = 0; i < pairwiseStride_; ++i)
            {
                bool hasRow = (i < arr.Size());
                
                if (hasRow && !arr[i].IsArray())
                    SET_ERR_MSG(key << " is expected to be a list of lists")
                else
                {
                    int w = pairwiseStride_+1;
                    for (int j = 0; j < w; ++j)
                    {
                        if (j == 0) // this is for ids
                        {
                            if (i < idOrder.size())
                            {
                                int id = idOrder[i];
                                mat[i*w+j] = id;
                            }
                            else
                                if (hasRow)
                                    SET_ERR_MSG(key << " can't find matching id")
                        }
                        else
                        {
                            bool hasCol = (hasRow ? j-1 < arr[i].Size() : false);
                            if (hasRow && hasCol)
                            {
                                float val = 0;
                                if (!arr[i][j-1].IsFloat())
                                    SET_ERR_MSG(key << " expected to be list of lists of floats")
                                else
                                    val = arr[i][j-1].GetFloat();
                                mat[i*w+j] = val;
                            } // if hascol hasrow
                        } // else - values
                    } // for j
                } // else arr[i] is array
            } // for i
        } // if document[key] is array
    }
    else
        SET_ERR_MSG("can't find " << key)
    
    return parseResult_;
}

bool
OmJsonParser::retrieveStageDistances(const rapidjson::Value &document,
                                     const char *key,
                                     const std::vector<int> &idOrder,
                                     std::map<int, std::vector<float> > &stageDistances)
{
    parseResult_ = true;
    if (document.HasMember(key))
    {
        const rapidjson::Value::ConstArray& arr = document[key].GetArray();
        
        for (rapidjson::SizeType i = 0; i < arr.Size(); ++i)
        {
            if (i >= idOrder.size())
                SET_ERR_MSG(key << " id list size doesn't match list size")
            else
            {
                int id = idOrder[i];
                stageDistances[id] = vector<float>();
                
                if (!arr[i].IsObject())
                    SET_ERR_MSG(key << " expected list of objects")
                else
                {
                    if (arr[i].HasMember(OM_JSON_STAGEDIST_US))
                        stageDistances[id].push_back(arr[i][OM_JSON_STAGEDIST_US].GetFloat());
                    else
                        SET_ERR_MSG(key << " can't find key " << OM_JSON_STAGEDIST_US);
                    if (arr[i].HasMember(OM_JSON_STAGEDIST_US))
                        stageDistances[id].push_back(arr[i][OM_JSON_STAGEDIST_DS].GetFloat());
                    else
                        SET_ERR_MSG(key << " can't find key " << OM_JSON_STAGEDIST_DS);
                    if (arr[i].HasMember(OM_JSON_STAGEDIST_US))
                        stageDistances[id].push_back(arr[i][OM_JSON_STAGEDIST_SL].GetFloat());
                    else
                        SET_ERR_MSG(key << " can't find key " << OM_JSON_STAGEDIST_SL);
                    if (arr[i].HasMember(OM_JSON_STAGEDIST_US))
                        stageDistances[id].push_back(arr[i][OM_JSON_STAGEDIST_SR].GetFloat());
                    else
                        SET_ERR_MSG(key << " can't find key " << OM_JSON_STAGEDIST_SR);
                }
            }
        }
    }
    else
        SET_ERR_MSG("can't find " << key)
    
    return parseResult_;
}

bool
OmJsonParser::hasSubType(std::vector<rapidjson::Document> &messages,
                         std::string subType,
                         std::vector<rapidjson::Document> &subtypeMsg) const
{
    for (auto& d:messages)
        if (d.HasMember(OM_JSON_PACKET) && d[OM_JSON_PACKET].IsObject())
        {
            if (d[OM_JSON_PACKET].HasMember(OM_JSON_SUBTYPE))
            {
                const char* st = d[OM_JSON_PACKET][OM_JSON_SUBTYPE].GetString();
                if (subType == string(st))
                {
                    subtypeMsg.push_back(move(d));
                    return true;
                }
            }
        }
    
    return false;
}

bool
OmJsonParser::retrieveUnordered(const rapidjson::Value& document,
                       const char* key,
                       std::vector<std::vector<float>>& listOfLists)
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
                if (!arr[i].IsArray())
                    SET_ERR_MSG(key << " expected to be list of lists")
                else
                {
                    const rapidjson::Value::ConstArray& subArr = arr[i].GetArray();
                    
                    listOfLists.push_back(vector<float>());
                    
                    for (rapidjson::SizeType k = 0; k < subArr.Size(); ++k)
                    {
                        float val = 0;
                        if (!subArr[k].IsFloat())
                            SET_ERR_MSG(key << " bad type for " << i << " sublist element "
                                        << k << ": float expected")
                        else
                            val = subArr[k].GetFloat();
                        
                        listOfLists.back().push_back(val);
                    }
                } // else - arr is list
            } // for i
        } // else - masterList is array
    }
    else
        SET_ERR_MSG("can't find " << key)
        
    return parseResult_;
}

void
OmJsonParser::clearAll()
{
    idOrder_.clear();
    derivatives1_.clear();
    derivatives2_.clear();
    speeds_.clear();
    accelerations_.clear();
    stageDistances_.clear();
    clustersData_.clear();
    clusterIds_.clear();
    hotspotsData_.clear();
    groupTarget_.clear();
    templatesData_.clear();
}
