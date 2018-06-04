#ifndef _DIGITIZER_H_
#define _DIGITIZER_H_ 1

#include "base.h"

#define THRESHOLD_MASK (0x80003FFF)

class DigitizerException : public exception {
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
    void ProgramDigitizer(ConfigSettings_t& CS); // will need stuff for syncing
    unsigned int ReadBuffer(unsigned int& BufferSize);
    const char* GetBuffer() {return buffer;}
    void StartAcquisition();
    void StopAcquisition();
    void SWTrigger() {CAEN_DGTZ_SendSWtrigger(m_iHandle);}
    bool IsRunning() {return m_bRunning;}

private:
    CAEN_DGTZ_ErrorCode WriteRegister(GW_t GW, bool bForce = false);

    int m_iHandle;
    bool m_bRunning;
    char* buffer;
};

#endif // _DIGITIZER_H_ defined
