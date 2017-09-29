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

//using namespace std;

const std::string runs_db_addr("/depot/darkmatter/apps/asterix/asterix_runs_database.db");
const std::string config_dir("/depot/darkmatter/apps/asterix/obelix/config/");

struct GW_t {
    unsigned int addr;
    unsigned int data;
    unsigned int mask;
};

struct ChannelSettings_t {
    int Channel;
    bool Enabled;
    unsigned int DCoffset;
    unsigned int TriggerThreshold;
    unsigned int ZLEThreshold;
    CAEN_DGTZ_TriggerMode_t TriggerMode;
};

struct ConfigSettings_t {
    unsigned int RecordLength;
    unsigned int PostTrigger;
    unsigned int EnableMask;
    unsigned int BlockTransfer;
    CAEN_DGTZ_IOLevel_t FPIOLevel;
    CAEN_DGTZ_TriggerMode_t ExtTriggerMode;
    CAEN_DGTZ_TriggerMode_t ChTriggerMode;
    std::vector<ChannelSettings_t> ChannelSettings;
    std::vector<GW_t> GenericWrites;
};

#endif // _BASE_H_ defined