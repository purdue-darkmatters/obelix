#ifndef _DIGITIZER_H_
#define _DIGITIZER_H_ 1

#include "base.h"

#define THRESHOLD_MASK (0x80003FFF)

class DigitizerException : public std::exception {
public:
    const char* what() const throw () {
        return "Digitizer error";
    }
};

class Digitizer {
public:
    Digitizer();
    Digitizer(int LinkNumber, int ConetNode, int BaseAddress);
    ~Digitizer();
    void ProgramDigitizer(ConfigSettings_t& CS);
    unsigned int ReadBuffer(unsigned int& BufferSize, bool which);
    const char* GetBuffer(int which) {return buffers[which];}
    void StartAcquisition() {CAEN_DGTZ_SWStartAcquisition(m_iHandle);}
    void StopAcquisition() {CAEN_DGTZ_SWStopAcquisition(m_iHandle);}
    void SWTrigger() {CAEN_DGTZ_SendSWtrigger(m_iHandle);}

private:

    int m_iHandle;
    CAEN_DGTZ_ErrorCode WriteRegister(GW_t GW);
    std::array<char*, 2> buffers;
};

#endif // _DIGITIZER_H_ defined