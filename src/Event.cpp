#include "Event.h"
#include <cstring>

int Event::s_TimestampRollovers = 0;
long Event::s_LastTimestamp = 0;

Event::Event() {}

Event::~Event() {}

void Event::Decode(const std::vector<unsigned int*>& headers, const std::vector<unsigned int*>& bodies) {
    std::vector<unsigned int> EventSizes, ChannelMasks, BoardIDs;
    unsigned long lTimestamp(0);
    unsigned int iEventCounter(0), iTimestamp(0);
    bool bIsZLE(false);
    unsigned int iEventChannelMask(0);
    int iNumWordsBody = 0;
    int iNumBytesEvent(0), iNumBytesBody(0);
    for (auto& header : headers) {
        EventSizes.push_back(header[0] & s_EventSizeMask);
        iNumWordsBody += (EventSizes.back() - 4);
        BoardIDs.push_back((header[1] & s_BoardIDMask) >> s_BoardIDShift);
        ChannelMasks.push_back(header[1] & s_ChannelMaskMask);
        iEventCounter = header[2] & s_CounterMask;
        bIsZLE = header[1] & s_ZLEMask;
        iTimestamp = header[3];
    }
    if (iTimestamp < s_LastTimestamp) s_TimestampRollovers++;
    lTimestamp = iTimestamp + s_TimestampRollovers * s_TimestampOffset;
    s_LastTimestamp = iTimestamp;
    for (unsigned i = 0; i < ChannelMasks.size(); i++) iEventChannelMask |= (ChannelMasks[i] << (NUM_CH*BoardIDs[i]));
    iNumBytesBody = iNumWordsBody * 4;
    iNumBytesEvent = iNumBytesBody + m_Header.size()*4;

    try {
        m_Body.resize(iNumBytesBody);
    } catch (std::exception& e) {
        throw std::bad_alloc();
    }
    char* cPtr(m_Body.data());
    for (unsigned i = 0; i < headers.size(); i++) {
        memcpy(cPtr, bodies[i], (EventSizes[i]-4)*4);
        cPtr += (EventSizes[i]-4)*4;
    }
    m_Header[0] = iEventCounter;
    m_Header[1] = iEventChannelMask;
    m_Header[2] = iNumBytesEvent;
    if (bIsZLE) m_Header[2] &= (1 << 31);
    m_Header[3] = lTimestamp >> 32;
    m_Header[4] = lTimestamp & (0xFFFFFFFFl);
}

int Event::Write(std::ofstream& fout) {
    fout.write((char*)m_Header.data(), m_Header.size()*4);
    fout.write(m_Body.data(), m_Body.size());
    return m_Header[2] & 0x7FFFFFFF;
}