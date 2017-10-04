#include "Digitizer.h"

Digitizer::Digitizer() {}

Digitizer::Digitizer(int LinkNumber, int ConetNode, int BaseAddress) {
    CAEN_DGTZ_ErrorCode ret = CAEN_DGTZ_OpenDigitizer(CAEN_DGTZ_OpticalLink, LinkNumber, ConetNode, BaseAddress, &m_iHandle);
    if (ret != CAEN_DGTZ_Success) {
        throw DigitizerException();
    }
    CAEN_DGTZ_BoardInfo_t BoardInfo;

    ret = CAEN_DGTZ_GetInfo(m_iHandle, &BoardInfo);

    if (BoardInfo.Model != CAEN_DGTZ_V1724) {
        std::cout << "Connected to the wrong digitizer type!\n";
        throw DigitizerException();
    }

}

Digitizer::~Digitizer() {
    CAEN_DGTZ_SWStopAcquisition(m_iHandle);
    for (auto& buf : buffers) CAEN_DGTZ_FreeReadoutBuffer(&buf);
    CAEN_DGTZ_ErrorCode ret = CAEN_DGTZ_CloseDigitizer(m_iHandle);
    if (ret != CAEN_DGTZ_Success) {
        std::cout << "Errors in board " << m_iHandle << " during shutdown\n";
    }
}

void Digitizer::ProgramDigitizer(ConfigSettings_t& CS) {
    CAEN_DGTZ_ErrorCode ret = CAEN_DGTZ_Success;
    unsigned int val(0), AllocSize(0);

    // start with reset
    ret = CAEN_DGTZ_Reset(m_iHandle);

    if (ret != CAEN_DGTZ_Success) {
        std::cout << "Error resetting digitizer " << m_iHandle << "\nPlease reset manually and restart\n";
        throw DigitizerException();
    }

    ret = CAEN_DGTZ_SetRecordLength(m_iHandle, CS.RecordLength);
    if (ret != CAEN_DGTZ_Success) std::cout << "Error setting record length\n";
    ret = CAEN_DGTZ_GetRecordLength(m_iHandle, &val);
    if (ret != CAEN_DGTZ_Success) std::cout << "Error checking record length\n";
    if (val != CS.RecordLength)
        std::cout << "Board " << m_iHandle << ": desired " << CS.RecordLength << " record length, got " << val << " instead\n";

    ret = CAEN_DGTZ_SetPostTriggerSize(m_iHandle, CS.PostTrigger);
    if (ret != CAEN_DGTZ_Success) std::cout << "Error setting post trigger\n";
    ret = CAEN_DGTZ_GetPostTriggerSize(m_iHandle, &val);
    if (ret != CAEN_DGTZ_Success) std::cout << "Error checking post trigger\n";
    if (val != CS.PostTrigger)
        std::cout << "Board " << m_iHandle << ": desired " << CS.PostTrigger << " post trigger, got " << val << " instead\n";

    ret = CAEN_DGTZ_SetIOLevel(m_iHandle, CS.FPIOLevel);
    if (ret != CAEN_DGTZ_Success) std::cout << "Error setting IO level\n";
    ret = CAEN_DGTZ_SetMaxNumEventsBLT(m_iHandle, CS.BlockTransfer);
    if (ret != CAEN_DGTZ_Success) std::cout << "Error setting block block transfer\n";
    ret = CAEN_DGTZ_SetAcquisitionMode(m_iHandle, CAEN_DGTZ_SW_CONTROLLED);
    ret = CAEN_DGTZ_SetExtTriggerInputMode(m_iHandle, CS.ExtTriggerMode);
    if (ret != CAEN_DGTZ_Success) std::cout << "Error setting external trigger\n";

    ret = CAEN_DGTZ_SetChannelEnableMask(m_iHandle, CS.EnableMask);
    if (ret != CAEN_DGTZ_Success) std::cout << "Error setting channel mask\n";
    ret = CAEN_DGTZ_SetChannelSelfTrigger(m_iHandle, CS.ChTriggerMode, 0xFF);
    if (ret != CAEN_DGTZ_Success) std::cout << "Error setting channel trigger\n";

    for (auto& ch_set : CS.ChannelSettings) {
        if (!ch_set.Enabled)
            continue;
        ret = CAEN_DGTZ_SetChannelDCOffset(m_iHandle, ch_set.Channel, ch_set.DCoffset);
        if (ret != CAEN_DGTZ_Success) std::cout << "Error setting channel " << ch_set.Channel << " DC offset\n";
        ret = CAEN_DGTZ_SetChannelTriggerThreshold(m_iHandle, ch_set.Channel, ch_set.TriggerThreshold);
        if (ret != CAEN_DGTZ_Success) std::cout << "Error setting channel " << ch_set.Channel << " trigger threshold\n";
        ret = CAEN_DGTZ_SetTriggerPolarity(m_iHandle, ch_set.Channel, CAEN_DGTZ_TriggerOnFallingEdge);
        ret = WriteRegister(GW_t{CAEN_DGTZ_CHANNEL_ZS_THRESHOLD_BASE_ADDRESS + (0x100 * ch_set.Channel),
                                  (1 << 31) + ch_set.ZLEThreshold,
                                  THRESHOLD_MASK});
        if (ret != CAEN_DGTZ_Success) std::cout << "Error setting channel " << ch_set.Channel << " ZLE threshold\n";
    }

    for (auto& GW : CS.GenericWrites) {
        ret = WriteRegister(GW);
        if (ret != CAEN_DGTZ_Success) std::cout << "Error with register write\n";
    }

    if (ret != CAEN_DGTZ_Success)
        std::cout << "Board " << m_iHandle << " reports some errors in programming\n";

    ret = CAEN_DGTZ_Success;
    for (auto& buffer : buffers) ret = CAEN_DGTZ_MallocReadoutBuffer(m_iHandle, &buffer, &AllocSize);

    if (ret != CAEN_DGTZ_Success) {
        std::cout << "Board " << m_iHandle << " unable to alloc readout buffers\n";
        throw DigitizerException();
    }
    std::cout << "Board " << m_iHandle << " ready\n";
}

unsigned int Digitizer::ReadBuffer(unsigned int& BufferSize, bool which) {
    unsigned int NumEvents(0);
    CAEN_DGTZ_ErrorCode ret = CAEN_DGTZ_Success;
    ret = CAEN_DGTZ_ReadData(m_iHandle, CAEN_DGTZ_SLAVE_TERMINATED_READOUT_MBLT, buffers[which], &BufferSize);
    if (ret != CAEN_DGTZ_Success) {
        std::cout << "Readout error from board " << m_iHandle << "\n";
        throw DigitizerException();
    }
    if (BufferSize != 0) {
        ret = CAEN_DGTZ_GetNumEvents(m_iHandle, buffers[which], BufferSize, &NumEvents);
        if (ret != CAEN_DGTZ_Success) {
            std::cout << "Readout error on board " << m_iHandle << "\n";
            throw DigitizerException();
        }
    }

    return NumEvents;
}

CAEN_DGTZ_ErrorCode Digitizer::WriteRegister(GW_t GW) {
    unsigned int temp = 0;
    CAEN_DGTZ_ErrorCode ret = CAEN_DGTZ_ReadRegister(m_iHandle, GW.addr, &temp);
    if (ret != CAEN_DGTZ_Success)
        return ret;

    temp &= ~GW.mask;
    temp |= GW.data;
    ret = CAEN_DGTZ_WriteRegister(m_iHandle, GW.addr, temp);
    return ret;
}
