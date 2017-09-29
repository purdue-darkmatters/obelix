#ifndef _DIGITIZER_H_
#define _DIGITIZER_H_ 1

#include "base.h"

#define THRESHOLD MASK (0x80003FFF)

class Digitizer {
public:
    Digitizer();
    Digitizer(int LinkNumber, int ConetNode, int BaseAddress);
    ~Digitizer();
    void ProgramDigitizer(ConfigSettings_t& CS);
    unsigned int ReadBuffer(unsigned int& BufferSize, int which);
    const char* GetBuffer(int which) {return buffers[which];}
    void StartAcquisition() {CAEN_DGTZ_SWStartAcquisition(m_iHandle);}
    void StopAcquisition() {CAEN_DGTZ_SWStopAcquisition(m_iHandle);}
    void SWTrigger() {CAEN_DGTZ_SendSWTrigger(m_iHandle);}

private:

    int m_iHandle;
    CAEN_DGTZ_ErrorCode_t WriteRegister(GW_t& GW);
    std::vector<char*> buffers;
};

#endif // _DIGITIZER_H_ defined