#include "Digitizer.h"
#include <iomanip>

Digitizer::Digitizer() {}

Digitizer::Digitizer(int LinkNumber, int ConetNode, int BaseAddress) {
    CAEN_DGTZ_ErrorCode ret = CAEN_DGTZ_OpenDigitizer(CAEN_DGTZ_OpticalLink, LinkNumber, ConetNode, BaseAddress, &m_iHandle);
    if (ret != CAEN_DGTZ_Success) {
        throw DigitizerException();
    }
    CAEN_DGTZ_BoardInfo_t BoardInfo;

    ret = CAEN_DGTZ_GetInfo(m_iHandle, &BoardInfo);

    if (BoardInfo.Model != CAEN_DGTZ_V1724) {
        cout << "Connected to the wrong digitizer type!\n";
        throw DigitizerException();
    }
    m_bRunning = false;
}

Digitizer::~Digitizer() {
    CAEN_DGTZ_SWStopAcquisition(m_iHandle);
    CAEN_DGTZ_FreeReadoutBuffer(&buffer);
    CAEN_DGTZ_ErrorCode ret = CAEN_DGTZ_CloseDigitizer(m_iHandle);
    if (ret != CAEN_DGTZ_Success) {
        cout << "Errors in board " << m_iHandle << " during shutdown\n";
    }
}

void Digitizer::StartAcquisition() {
	CAEN_DGTZ_ErrorCode ret = CAEN_DGTZ_Success;
	ret = CAEN_DGTZ_SWStartAcquisition(m_iHandle);
	if (ret != CAEN_DGTZ_Success) {
		cout << "Error starting acquisition on board " << m_iHandle << ". Error " << ret << "\n";
		throw DigitizerException();
	}
	m_bRunning = true;
}

void Digitizer::StopAcquisition() {
	CAEN_DGTZ_ErrorCode ret = CAEN_DGTZ_Success;
    m_bRunning = false;
	ret = CAEN_DGTZ_SWStopAcquisition(m_iHandle);
	if (ret != CAEN_DGTZ_Success) {
		cout << "Error stopping acquisition on board " << m_iHandle << ". Error " << ret << "\n";
		throw DigitizerException();
	}
}

void Digitizer::ProgramDigitizer(ConfigSettings_t& CS) {
    CAEN_DGTZ_ErrorCode ret = CAEN_DGTZ_Success;
    unsigned int val(0), AllocSize(0);
    unsigned int address(0), data(0);

    // start with reset
    ret = CAEN_DGTZ_Reset(m_iHandle);

    if (ret != CAEN_DGTZ_Success) {
        cout << "Error resetting digitizer " << m_iHandle << "\nPlease reset manually and restart\n";
        throw DigitizerException();
    }

    ret = CAEN_DGTZ_SetRecordLength(m_iHandle, CS.RecordLength);
    if (ret != CAEN_DGTZ_Success) cout << "Error setting record length: " << ret << "\n";
    ret = CAEN_DGTZ_GetRecordLength(m_iHandle, &val);
    if (ret != CAEN_DGTZ_Success) cout << "Error checking record length: " << ret << "\n";
    if (val != CS.RecordLength)
        cout << "Board " << m_iHandle << ": desired " << CS.RecordLength << " record length, got " << val << " instead\n";

    ret = CAEN_DGTZ_SetPostTriggerSize(m_iHandle, CS.PostTrigger);
    if (ret != CAEN_DGTZ_Success) cout << "Error setting post trigger: " << ret << "\n";
    ret = CAEN_DGTZ_GetPostTriggerSize(m_iHandle, &val);
    if (ret != CAEN_DGTZ_Success) cout << "Error checking post trigger: " << ret << "\n";
    if (val != CS.PostTrigger)
        cout << "Board " << m_iHandle << ": desired " << CS.PostTrigger << " post trigger, got " << val << " instead\n";

    ret = CAEN_DGTZ_SetIOLevel(m_iHandle, CS.FPIO);
    if (ret != CAEN_DGTZ_Success) cout << "Board " << m_iHandle << ": Error setting IO level: " << ret << "\n";
    ret = CAEN_DGTZ_SetMaxNumEventsBLT(m_iHandle, CS.BlockTransfer);
    if (ret != CAEN_DGTZ_Success) cout << "Board " << m_iHandle << ": Error setting block block transfer: " << ret << "\n";
    ret = CAEN_DGTZ_SetAcquisitionMode(m_iHandle, CAEN_DGTZ_SW_CONTROLLED);
    ret = CAEN_DGTZ_SetExtTriggerInputMode(m_iHandle, CS.ExtTriggerMode);
    if (ret != CAEN_DGTZ_Success) cout << "Board " << m_iHandle << ": Error setting external trigger: " << ret << "\n";

    ret = CAEN_DGTZ_SetChannelEnableMask(m_iHandle, CS.EnableMask);
    if (ret != CAEN_DGTZ_Success) cout << "Board " << m_iHandle << ": Error setting channel mask: " << ret << "\n";
    ret = CAEN_DGTZ_GetChannelEnableMask(m_iHandle, &val);
    if (ret != CAEN_DGTZ_Success) cout << "Board " << m_iHandle << ": Error checking channel mask: " << ret << "\n";
    if (val != CS.EnableMask)
        cout << "Board " << m_iHandle << ": desired " << CS.EnableMask << " mask, got " << val << " instead\n";
    ret = CAEN_DGTZ_SetChannelSelfTrigger(m_iHandle, CS.ChTriggerMode, 0xFF);
    if (ret != CAEN_DGTZ_Success) cout << "Board " << m_iHandle << ": Error setting channel trigger: " << ret << "\n";

    for (auto& ch_set : CS.ChannelSettings) {
        if (!ch_set.Enabled)
            continue;
        ret = CAEN_DGTZ_SetChannelDCOffset(m_iHandle, ch_set.Channel, ch_set.DCoffset);
        if (ret != CAEN_DGTZ_Success) cout << "Board " << m_iHandle << ": Error setting channel " << ch_set.Channel << " DC offset: " << ret << "\n";
        ret = CAEN_DGTZ_SetChannelTriggerThreshold(m_iHandle, ch_set.Channel, iBaselineRef - ch_set.TriggerThreshold);
        if (ret != CAEN_DGTZ_Success) cout << "Board " << m_iHandle << ": Error setting channel " << ch_set.Channel << " trigger threshold: " << ret << "\n";
        ret = CAEN_DGTZ_SetTriggerPolarity(m_iHandle, ch_set.Channel, CAEN_DGTZ_TriggerOnFallingEdge);
        ret = CAEN_DGTZ_SetChannelPulsePolarity(m_iHandle, ch_set.Channel, CAEN_DGTZ_PulsePolarityNegative);
        if (CS.IsZLE) {
            ret = CAEN_DGTZ_SetZeroSuppressionMode(m_iHandle, CAEN_DGTZ_ZS_ZLE);
            if (ret != CAEN_DGTZ_Success) cout << "Board " << m_iHandle << ": Error setting channel " << ch_set.Channel << " ZLE mode: " << ret << "\n";
            ret = CAEN_DGTZ_SetChannelZSParams(m_iHandle, ch_set.Channel, CAEN_DGTZ_ZS_FINE, iBaselineRef - ch_set.ZLEThreshold, 0);
            address = CAEN_DGTZ_SAM_REG_VALUE + (0x100 * ch_set.Channel);
            data = (ch_set.ZLE_N_LBK << 16) + ch_set.ZLE_N_LFWD;
            ret = WriteRegister(GW_t{address, data, 0xFFFFFFFF});
            if (ret != CAEN_DGTZ_Success) cout << "Board " << m_iHandle << ": Error setting channel " << ch_set.Channel << " ZLE parameters: " << ret << "\n";
        }
    }

    for (auto& GW : CS.GenericWrites) {
        ret = WriteRegister(GW);
        if (ret != CAEN_DGTZ_Success) cout << "Board " << m_iHandle << ": Error with register write: " << ret << "\n" << setbase(16) << "Tried to write value 0x" << GW.data << " to 0x" << GW.addr << " with mask 0x" << GW.mask << '\n';
    }
    buffer = nullptr;
    ret = CAEN_DGTZ_MallocReadoutBuffer(m_iHandle, &buffer, &AllocSize);

    if (ret != CAEN_DGTZ_Success) {
        cout << "Board " << m_iHandle << " unable to alloc readout buffer: " << ret << "\n";
        throw DigitizerException();
    }
    cout << "Board " << m_iHandle << " ready with mask " << CS.EnableMask << "\n";
}

unsigned int Digitizer::ReadBuffer(unsigned int& BufferSize) {
    unsigned int NumEvents(0);
    CAEN_DGTZ_ErrorCode ret = CAEN_DGTZ_Success;
    ret = CAEN_DGTZ_ReadData(m_iHandle, CAEN_DGTZ_SLAVE_TERMINATED_READOUT_MBLT, buffer, &BufferSize);
    if (ret != CAEN_DGTZ_Success) {
        cout << "Readout error from board " << m_iHandle << ": " << ret << "\n";
        throw DigitizerException();
    }
    if (BufferSize != 0) {
        ret = CAEN_DGTZ_GetNumEvents(m_iHandle, buffer, BufferSize, &NumEvents);
        if (ret != CAEN_DGTZ_Success) {
            cout << "Readout error on board " << m_iHandle << ": " << ret << "\n";
            throw DigitizerException();
        }
    }

    return NumEvents;
}

CAEN_DGTZ_ErrorCode Digitizer::WriteRegister(GW_t GW) {
    WORD temp = 0;
    CAEN_DGTZ_ErrorCode ret = CAEN_DGTZ_ReadRegister(m_iHandle, GW.addr, &temp);
    if (ret != CAEN_DGTZ_Success)
        return ret;

    temp &= ~GW.mask;
    temp |= GW.data;
    ret = CAEN_DGTZ_WriteRegister(m_iHandle, GW.addr, temp);
    return ret;
}
