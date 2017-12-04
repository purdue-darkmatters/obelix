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

using namespace std;
using WORD = unsigned int;

const string runs_db_addr("/depot/darkmatter/apps/asterix/asterix_runs_db.db");
const string config_dir("/depot/darkmatter/apps/asterix/obelix/config/");

struct GW_t {
    WORD addr;
    WORD data;
    WORD mask;
};

struct ChannelSettings_t {
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

#endif // _BASE_H_ defined
