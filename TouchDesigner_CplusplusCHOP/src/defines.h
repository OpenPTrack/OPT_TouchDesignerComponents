//
//  defines.h
//  OM_CHOP
//
//  Created by Peter Gusev on 3/6/18.
//  Copyright © 2018 UCLA. All rights reserved.
//

#ifndef defines_h
#define defines_h

#define OM_JSON_HEADER          "header"
#define OM_JSON_SEQ             "seq"
#define OM_JSON_IDS             "ids"
#define OM_JSON_DIMS            "dims"

#define OM_JSON_PACKET          "packet"
#define OM_JSON_PACKET_TYPE     "type"
#define OM_JSON_PACKET_VER      "version"
#define OM_JSON_SUBTYPE         "subtype"
#define OM_JSON_VALUES          "values"

#define OM_JSON_SUBTYPE_DERS    "derivatives"
#define OM_JSON_FIRSTDERS       "d1"
#define OM_JSON_SECONDDERS      "d2"
#define OM_JSON_SPEEDS          "speed"
#define OM_JSON_ACCELERATIONS   "acceleration"

#define OM_JSON_SUBTYPE_DIST    "distance"
#define OM_JSON_POI             "poi"
#define OM_JSON_PAIRWISE        "pairwise"
#define OM_JSON_STAGEDIST       "stage"
#define OM_JSON_STAGEDIST_US    "US"
#define OM_JSON_STAGEDIST_DS    "DS"
#define OM_JSON_STAGEDIST_SL    "SL"
#define OM_JSON_STAGEDIST_SR    "SR"

#define OM_JSON_SUBTYPE_CLUSTER "cluster"
#define OM_JSON_CLUSTER_POINTS  "cluster"
#define OM_JSON_CLUSTERCENTERS  "center"
#define OM_JSON_CLUSTERSPREADS  "spreads"

#define OM_JSON_SUBTYPE_MDYN    "massdynamics"
#define OM_JSON_TREND           "trend"
#define OM_JSON_EIGENVALUE      "eigenvalue"
#define OM_JSON_EIGENVECTOR     "eigenvector"
#define OM_JSON_HOTSPOTS        "hotspot"

#define OM_JSON_SUBTYPE_SIM     "similarity"
#define OM_JSON_PREDICTIONS     "predictions"
#define OM_JSON_SIMILARITY      "similarity"

#endif /* defines_h */
