//
//  om-json-parser.hpp
//  OM_CHOP
//
//  Created by Peter Gusev on 3/12/18.
//  Copyright © 2018 Derivative. All rights reserved.
//

#ifndef om_json_parser_hpp
#define om_json_parser_hpp

#include <stdio.h>
#include <map>
#include <vector>
#include <string>
#include <queue>
#include <set>

#include "rapidjson/document.h"

class OmJsonParser {
public:
    typedef enum _PacketSubtype {
        All,
        Derivatives,
        Distance,
        Cluster,
        Massdyn,
        Similarity
    } PacketSubtype;
    
    OmJsonParser(int maxMatSize);
    ~OmJsonParser();
    
    bool parse(std::vector<rapidjson::Document>& message,
               std::set<std::string>& parsedSubtypes,
               std::string subtype = "all");

    const std::string& getParseError() const { return errMsg_; }
    const bool getParseResult() const { return parseResult_; }
    
    const std::vector<int>& getIdOrder() const { return idOrder_; }
    const std::map<int, std::vector<float>>& getD1() const { return derivatives1_; }
    const std::map<int, std::vector<float>>& getD2() const { return derivatives2_; }
    const std::map<int, float>& getSpeeds() const { return speeds_; }
    const std::map<int, float>& getAccelerations() const { return accelerations_; }
    const float* const getPairwiseMat() const { return pairwiseMat_; }
    const float* const getDtwMat() const { return dtwMat_; }
    const std::vector<std::vector<float>>& getClusters() const { return clustersData_; }
    const std::map<int, std::vector<float>>& getStageDists() const { return stageDistances_; }
    const std::vector<std::vector<float>>& getHotspots() const { return hotspotsData_; }
    const std::vector<std::vector<float>>& getGroupTarget() const { return groupTarget_; }
    const std::map<std::string, std::vector<float>>& getTemplates() const { return templatesData_; }
    
private:
    std::string errMsg_;
    bool parseResult_;
    
    std::vector<int> idOrder_;
    std::map<int, std::vector<float>> derivatives1_;
    std::map<int, std::vector<float>> derivatives2_;
    std::map<int, float> speeds_;
    std::map<int, float> accelerations_;
    float* pairwiseMat_, *dtwMat_;
    std::vector<std::vector<float>> clustersData_;
    std::map<int, std::vector<float>> stageDistances_;
    std::vector<std::vector<float>> hotspotsData_;
    std::vector<std::vector<float>> groupTarget_;
    std::map<std::string, std::vector<float>> templatesData_;
    
    void processIdOrder(std::vector<rapidjson::Document>& messages,
                        std::vector<int>& idOrder);
    void processDerivatives(std::vector<rapidjson::Document>& messages,
                            std::vector<int>& idOrder,
                            std::map<int, std::vector<float>>& derivatives1,
                            std::map<int, std::vector<float>>& derivatives2,
                            std::map<int, float>& speeds,
                            std::map<int, float>& accelerations);
//    void processPairwise(std::vector<rapidjson::Document>& messages,
//                         std::vector<int>& idOrder,
//                         float* pairwiseMatrix);
//    void processClusters(std::vector<rapidjson::Document>& messages,
//                         std::vector<std::vector<float>>& clustersData);
//    void processStageDistances(std::vector<rapidjson::Document>& messages,
//                               std::vector<int>& idOrder,
//                               std::map<int, std::vector<float>>& stageDistances);
//    void processHotspots(std::vector<rapidjson::Document>& messages,
//                         std::vector<std::vector<float>>& hotspotsData);
//    void processDtw(std::vector<rapidjson::Document>& messages,
//                    std::vector<int>& idOrder,
//                    float* dtwMatrix);
//    void processGroupTarget(std::vector<rapidjson::Document>& messages,
//                            std::vector<std::vector<float>>& groupTarget);
//    void processTemplates(std::vector<rapidjson::Document>& messages,
//                          std::vector<int>& idOrder,
//                          std::map<std::string, std::vector<float>>& templates);
//
    bool retireve(const std::string& key,
                  std::vector<rapidjson::Document>&,
                  rapidjson::Value&);
    
    bool retrieveOrdered(const rapidjson::Value& document,
                         const char* key,
                         const std::vector<int>& idOrder,
                         std::map<int, std::vector<float>>& listOfLists);
    bool retrieveOrdered(const rapidjson::Value& document,
                         const char* key,
                         const std::vector<int>& idOrder,
                         std::map<int, float>& list);
};

#endif /* om_json_parser_hpp */
