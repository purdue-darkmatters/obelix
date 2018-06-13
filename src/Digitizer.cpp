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
        BOOST_LOG_TRIVIAL(fatal) << "Board " << m_iHandle << ": connected to the wrong digitizer type!";
        throw DigitizerException();
    }
    BOOST_LOG_TRIVIAL(debug) << "Board " << m_iHandle << ": connected";
    m_bRunning = false;
}

Digitizer::~Digitizer() {
    CAEN_DGTZ_SWStopAcquisition(m_iHandle);
    CAEN_DGTZ_FreeReadoutBuffer(&buffer);
    CAEN_DGTZ_ErrorCode ret = CAEN_DGTZ_CloseDigitizer(m_iHandle);
    if (ret != CAEN_DGTZ_Success) {
        BOOST_LOG_TRIVIAL(error) << "Board" << m_iHandle << ": errors during shutdown";
    }
}

void Digitizer::StartAcquisition() {
	CAEN_DGTZ_ErrorCode ret = CAEN_DGTZ_Success;
	ret = CAEN_DGTZ_SWStartAcquisition(m_iHandle);
	if (ret != CAEN_DGTZ_Success) {
		BOOST_LOG_TRIVIAL(fatal) << "Board " << m_iHandle << ": error starting acquisition. Error " << ret;
		throw DigitizerException();
	}
	m_bRunning = true;
    BOOST_LOG_TRIVIAL(debug) << "Board " << m_iHandle << ": acquisition started";
}

void Digitizer::StopAcquisition() {
	CAEN_DGTZ_ErrorCode ret = CAEN_DGTZ_Success;
    m_bRunning = false;
	ret = CAEN_DGTZ_SWStopAcquisition(m_iHandle);
	if (ret != CAEN_DGTZ_Success) {
		BOOST_LOG_TRIVIAL(fatal) << "Board " << m_iHandle << ": error stopping acquisition. Error " << ret;
		throw DigitizerException();
	}
    BOOST_LOG_TRIVIAL(debug) << "Board " << m_iHandle << ": acquisition stopped";
}

void Digitizer::ProgramDigitizer(ConfigSettings_t& CS) {
    CAEN_DGTZ_ErrorCode ret = CAEN_DGTZ_Success;
    unsigned int val(0), AllocSize(0);
    unsigned int address(0), data(0);

    // start with reset
    ret = CAEN_DGTZ_Reset(m_iHandle);

    if (ret != CAEN_DGTZ_Success) {
        BOOST_LOG_TRIVIAL(fatal) << "Board " << m_iHandle << ": error resetting digitizer. Please reset manually and restart";
        throw DigitizerException();
    }
    ret = CAEN_DGTZ_SetRecordLength(m_iHandle, CS.RecordLength);
    if (ret != CAEN_DGTZ_Success) BOOST_LOG_TRIVIAL(error) << "Board " << m_iHandle << ": error setting record length: " << ret;
    else BOOST_LOG_TRIVIAL(debug) << "Board " << m_iHandle << ": set record length: " << CS.RecordLength;
    ret = CAEN_DGTZ_GetRecordLength(m_iHandle, &val);
    if (ret != CAEN_DGTZ_Success) BOOST_LOG_TRIVIAL(error) << "Board " << m_iHandle << ": error getting record length: " << ret;
    if (val != CS.RecordLength) BOOST_LOG_TRIVIAL(warning) << "Board " << m_iHandle << ": record length, wanted " << CS.RecordLength << ", got " << val;
    else BOOST_LOG_TRIVIAL(debug) << "Board " << m_iHandle << ": record length, wanted " << CS.RecordLength << ", got " << val;

    ret = CAEN_DGTZ_SetDecimationFactor(m_iHandle, 1);
    if (ret != CAEN_DGTZ_Success) BOOST_LOG_TRIVIAL(error) << "Problem setting decimation factor";
    else BOOST_LOG_TRIVIAL(debug) << "Set decimation factor";

    ret = CAEN_DGTZ_SetPostTriggerSize(m_iHandle, CS.PostTrigger);
    if (ret != CAEN_DGTZ_Success) BOOST_LOG_TRIVIAL(error) << "Board " << m_iHandle << ": error setting post trigger: " << ret;
    else BOOST_LOG_TRIVIAL(debug) << "Board " << m_iHandle << ": set post trigger: " << CS.PostTrigger;
    ret = CAEN_DGTZ_GetPostTriggerSize(m_iHandle, &val);
    if (ret != CAEN_DGTZ_Success) BOOST_LOG_TRIVIAL(error) << "Board " << m_iHandle << ": error getting post trigger: " << ret;
    if (val != CS.PostTrigger) BOOST_LOG_TRIVIAL(warning) << "Board " << m_iHandle << ": post trigger, wanted " << CS.PostTrigger << ", got " << val;
    else BOOST_LOG_TRIVIAL(debug) << "Board " << m_iHandle << ": post trigger, wanted " << CS.PostTrigger << ", got " << val;

    ret = CAEN_DGTZ_SetIOLevel(m_iHandle, CS.FPIO);
    if (ret != CAEN_DGTZ_Success) BOOST_LOG_TRIVIAL(error) << "Board " << m_iHandle << ": error setting IO level: " << ret;
    else BOOST_LOG_TRIVIAL(debug) << "Board " << m_iHandle << ": set IO level: " << CS.FPIO;

    ret = CAEN_DGTZ_SetMaxNumEventsBLT(m_iHandle, CS.BlockTransfer);
    if (ret != CAEN_DGTZ_Success) BOOST_LOG_TRIVIAL(error) << "Board " << m_iHandle << ": error setting block block transfer: " << ret;
    else BOOST_LOG_TRIVIAL(debug) << "Board " << m_iHandle << ": set block transfer: " << CS.BlockTransfer;

    ret = CAEN_DGTZ_SetAcquisitionMode(m_iHandle, CAEN_DGTZ_SW_CONTROLLED);
    if (ret != CAEN_DGTZ_Success) BOOST_LOG_TRIVIAL(error) << "Board " << m_iHandle << ": error setting aquisition mode: " << ret;
    ret = CAEN_DGTZ_SetExtTriggerInputMode(m_iHandle, CS.ExtTriggerMode);
    if (ret != CAEN_DGTZ_Success) BOOST_LOG_TRIVIAL(error) << "Board " << m_iHandle << ": error setting external trigger mode: " << ret;
    else BOOST_LOG_TRIVIAL(debug) << "Board " << m_iHandle << ": set external trigger mode: " << CS.ExtTriggerMode;

    ret = CAEN_DGTZ_SetChannelEnableMask(m_iHandle, CS.EnableMask);
    if (ret != CAEN_DGTZ_Success) BOOST_LOG_TRIVIAL(error) << "Board " << m_iHandle << ": error setting channel mask: " << ret;
    ret = CAEN_DGTZ_GetChannelEnableMask(m_iHandle, &val);
    if (ret != CAEN_DGTZ_Success) BOOST_LOG_TRIVIAL(error) << "Board " << m_iHandle << ": error checking channel mask: " << ret;
    if (val != CS.EnableMask) BOOST_LOG_TRIVIAL(warning) << "Board " << m_iHandle << ": desired " << CS.EnableMask << " mask, got " << val;
    else BOOST_LOG_TRIVIAL(debug) << "Board " << m_iHandle << ": desired " << CS.EnableMask << " mask, got " << val;

    ret = CAEN_DGTZ_SetChannelSelfTrigger(m_iHandle, CS.ChTriggerMode, 0xFF);
    if (ret != CAEN_DGTZ_Success) BOOST_LOG_TRIVIAL(error) << "Board " << m_iHandle << ": error setting channel trigger mode: " << ret;
    else BOOST_LOG_TRIVIAL(debug) << "Board " << m_iHandle << ": set channel trigger mode: " << CS.ChTriggerMode;

    for (auto& ch_set : CS.ChannelSettings) {
        if (!ch_set.Enabled)
            continue;
        ret = CAEN_DGTZ_SetChannelDCOffset(m_iHandle, ch_set.Channel, ch_set.DCoffset);
        if (ret != CAEN_DGTZ_Success) BOOST_LOG_TRIVIAL(error) << "Board " << m_iHandle << ": error setting channel " << ch_set.Channel << " DC offset: " << ret;
        else BOOST_LOG_TRIVIAL(debug) << "Board " << m_iHandle << ": set channel " << ch_set.Channel << " DC offset to " << ch_set.DCoffset;

        ret = CAEN_DGTZ_SetChannelTriggerThreshold(m_iHandle, ch_set.Channel, iBaselineRef - ch_set.TriggerThreshold);
        if (ret != CAEN_DGTZ_Success) BOOST_LOG_TRIVIAL(error) << "Board " << m_iHandle << ": error setting channel " << ch_set.Channel << " trigger threshold: " << ret;
        else BOOST_LOG_TRIVIAL(debug) << "Board " << m_iHandle << ": set channel " << ch_set.Channel << " trigger threshold to " << ch_set.TriggerThreshold;

        ret = CAEN_DGTZ_SetTriggerPolarity(m_iHandle, ch_set.Channel, CAEN_DGTZ_TriggerOnFallingEdge);
        if (ret != CAEN_DGTZ_Success) BOOST_LOG_TRIVIAL(error) << "Board " << m_iHandle << ": error setting channel " << ch_set.Channel << " trigger polarity to falling:" << ret;
        else BOOST_LOG_TRIVIAL(debug) << "Board " << m_iHandle << ": set channel " << ch_set.Channel << " trigger polarity to falling";

        ret = CAEN_DGTZ_SetChannelPulsePolarity(m_iHandle, ch_set.Channel, CAEN_DGTZ_PulsePolarityNegative);
        if (ret != CAEN_DGTZ_Success) BOOST_LOG_TRIVIAL(error) << "Board " << m_iHandle << ": error setting channel " << ch_set.Channel << " pulse polarity to negative";
        else BOOST_LOG_TRIVIAL(debug) << "Board " << m_iHandle << ": set channel " << ch_set.Channel << " pulse polarity to negative";

        if (CS.IsZLE) {
            ret = CAEN_DGTZ_SetZeroSuppressionMode(m_iHandle, CAEN_DGTZ_ZS_ZLE);
            if (ret != CAEN_DGTZ_Success) BOOST_LOG_TRIVIAL(error) << "Board " << m_iHandle << ": error setting channel " << ch_set.Channel << " ZLE mode: " << ret;
            else BOOST_LOG_TRIVIAL(debug) << "Board " << m_iHandle << ": set channel " << ch_set.Channel << " ZLE mode";

            ret = CAEN_DGTZ_SetChannelZSParams(m_iHandle, ch_set.Channel, CAEN_DGTZ_ZS_FINE, iBaselineRef - ch_set.ZLEThreshold, 0);
            if (ret != CAEN_DGTZ_Success) BOOST_LOG_TRIVIAL(error) << "Board " << m_iHandle << ": error setting channel " << ch_set.Channel << " ZLE threshold: " << ret;
            else BOOST_LOG_TRIVIAL(debug) << "Board " << m_iHandle << ": set channel " << ch_set.Channel << " ZLE threshold";

            address = CAEN_DGTZ_CHANNEL_ZS_NSAMPLE_BASE_ADDRESS + (0x100 * ch_set.Channel);
            data = (ch_set.ZLE_N_LBK << 16) + ch_set.ZLE_N_LFWD;
            BOOST_LOG_TRIVIAL(debug) << "Board " << m_iHandle << ": writing 0x" << setbase(16) << data << " to 0x" << address;
            ret = WriteRegister(GW_t{m_iHandle, address, data, 0xFFFFFFFF});
            if (ret != CAEN_DGTZ_Success) BOOST_LOG_TRIVIAL(error) << "Board " << m_iHandle << ": error setting channel " << ch_set.Channel << " ZLE parameters: " << ret;
            else BOOST_LOG_TRIVIAL(debug) << "Board " << m_iHandle << ": set channel " << ch_set.Channel << " ZLE parameters";
        }
    }

    for (auto& GW : CS.GenericWrites) {
        ret = WriteRegister(GW);
        if (ret != CAEN_DGTZ_Success) BOOST_LOG_TRIVIAL(error) << "Board " << m_iHandle << ": Error with register write: " << ret << setbase(16) << ", tried to write value 0x" << GW.data << " to 0x" << GW.addr << " with mask 0x" << GW.mask;
        else BOOST_LOG_TRIVIAL(debug) << "Board " << m_iHandle << " wrote " << setbase(16) << "0x" << GW.data << " to 0x" << GW.addr << " with mask 0x" << GW.mask << setbase(10);
    }
    buffer = nullptr;
    ret = CAEN_DGTZ_MallocReadoutBuffer(m_iHandle, &buffer, &AllocSize);

    if (ret != CAEN_DGTZ_Success) {
        BOOST_LOG_TRIVIAL(fatal) << "Board " << m_iHandle << " unable to alloc readout buffer: " << ret << "\n";
        throw DigitizerException();
    }
    BOOST_LOG_TRIVIAL(info) << "Board " << m_iHandle << " ready with mask " << CS.EnableMask << "\n";
}

unsigned int Digitizer::ReadBuffer(unsigned int& BufferSize) {
    unsigned int NumEvents(0);
    CAEN_DGTZ_ErrorCode ret = CAEN_DGTZ_Success;
    ret = CAEN_DGTZ_ReadData(m_iHandle, CAEN_DGTZ_SLAVE_TERMINATED_READOUT_MBLT, buffer, &BufferSize);
    if (ret != CAEN_DGTZ_Success) {
        BOOST_LOG_TRIVIAL(fatal) << "Readout error from board " << m_iHandle << ": " << ret << "\n";
        throw DigitizerException();
    }
    if (BufferSize != 0) {
        ret = CAEN_DGTZ_GetNumEvents(m_iHandle, buffer, BufferSize, &NumEvents);
        if (ret != CAEN_DGTZ_Success) {
            BOOST_LOG_TRIVIAL(fatal) << "Readout error on board " << m_iHandle << ": " << ret << "\n";
            throw DigitizerException();
        }
    }

    return NumEvents;
}

CAEN_DGTZ_ErrorCode Digitizer::WriteRegister(GW_t GW, bool bForce) {
    WORD temp = 0;
    CAEN_DGTZ_ErrorCode ret = CAEN_DGTZ_ReadRegister(m_iHandle, GW.addr, &temp);
    if (ret != CAEN_DGTZ_Success) {
        BOOST_LOG_TRIVIAL(error) << "Error reading board " << m_iHandle << " register " << setbase(16) << "0x" << GW.addr << setbase(10);
        if (!bForce) return ret;
    }

    temp &= ~GW.mask;
    temp |= GW.data;
    ret = CAEN_DGTZ_WriteRegister(m_iHandle, GW.addr, temp);
    return ret;
}

