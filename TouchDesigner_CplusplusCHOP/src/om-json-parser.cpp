//
//  om-json-parser.cpp
//  OM_CHOP
//
//  Created by Peter Gusev on 3/12/18.
//  Copyright © 2018 Derivative. All rights reserved.
//

#include "om-json-parser.hpp"

using namespace std;

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
OmJsonParser::parse(vector<rapidjson::Document> &message,
                    set<string> &parsedSubtypes,
                    string subtype)
{
    errMsg_ = "Not implemented";
    return false;
}
