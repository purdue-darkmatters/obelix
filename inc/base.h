#ifndef _BASE_H_
#define _BASE_H_ 1

#include <CAENDigitizer.h>
#include <CAENDigitizerType.h>

#include <cstdlib>
#include <iostream>
#include <fstream>

#include <array>
#include <vector>
#include <string>
#include <memory>
#include <stdexcept>
#include <exception>
#include <map>
#include "boost/log/trivial.hpp"

using namespace std;
namespace logging = boost::log;
using WORD = unsigned int;

const string runs_db_addr("/depot/darkmatter/apps/asterix/asterix_runs_db.db");
const string config_dir("/depot/darkmatter/apps/asterix/obelix/config/");

const int iBaselineRef(16000);

struct GW_t {
    int board;
    WORD addr;
    WORD data;
    WORD mask;
};

struct ChannelSettings_t {
    int Board;
    int Channel;
    bool Enabled;
    unsigned int DCoffset;
    unsigned int TriggerThreshold;
    unsigned int ZLEThreshold;
    int ZLE_N_LFWD;
    int ZLE_N_LBK;
    CAEN_DGTZ_TriggerMode_t TriggerMode;
};

struct ConfigSettings_t {
    unsigned int RecordLength;
    unsigned int PostTrigger;
    unsigned int EnableMask;
    unsigned int BlockTransfer;
    bool IsZLE;
    CAEN_DGTZ_IOLevel_t FPIO;
    CAEN_DGTZ_TriggerMode_t ExtTriggerMode;
    CAEN_DGTZ_TriggerMode_t ChTriggerMode;
    vector<ChannelSettings_t> ChannelSettings;
    vector<GW_t> GenericWrites;
};

const map<string, CAEN_DGTZ_TriggerMode_t> TriggerMode {
    {"acquisition_only", CAEN_DGTZ_TRGMODE_ACQ_ONLY},
    {"acquisition_and_trgout", CAEN_DGTZ_TRGMODE_ACQ_AND_EXTOUT},
    {"disabled", CAEN_DGTZ_TRGMODE_DISABLED},
    {"trgout_only", CAEN_DGTZ_TRGMODE_EXTOUT_ONLY}
};

const map<string, CAEN_DGTZ_IOLevel_t> FPIOlevel {
    {"ttl", CAEN_DGTZ_IOLevel_TTL},
    {"nim", CAEN_DGTZ_IOLevel_NIM}
};

const map<string, bool> ZLE {
    {"yes", true},
    {"no", false}
};

#endif // _BASE_H_ defined
