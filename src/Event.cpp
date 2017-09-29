#include "Event.h"

int Event::s_TimestampRollovers = 0;
int Event::s_LastTimestamp;

Event::Event() : m_iEventSizeMask(0xFFFFFFF), m_iBoardIDMask(0xF8000000), m_iZLEMask(0x1000000), m_iChannelMaskMask(0xFF), m_iCounterMask(0xFFFFFF),
                 m_iBoardIDShift(24), m_iTimestampOffset(2147483648) {}

Event::~Event() {
    m_Body.reset();
}

void Event::Decode(const vector<unsigned int*>& headers, const vector<unsigned int*>& bodies) {
    vector<unsigned int> EventSizes, ChannelMasks, BoardIDs;
    unsigned long lTimestamp(0);
    unsigned int iEventCounter(0), iTimestamp;
    bool bIsZLE;
    unsigned int iEventChannelMask(0);
    int iNumWordsHeader = 4*headers.size();
    int iNumWordsBody = 0;
    int iNumBytesEvent(0), iNumBytesBody(0);
    for (auto& header : headers) {
        EventSizes.push_back(header[0] & m_iEventSizeMask);
        iNumWordsBody += (EventSizes.back() - 4);
        BoardIDs.push_back((header[1] & m_iBoardIDMask) >> m_iBoardIDShift);
        ChannelMasks.push_back(header[1] & m_iChannelMask);
        iEventCounter = header[2] & m_iCounterMask;
        bIsZLE = header[1] & m_iZLEMask;
        iTimestamp = ((header[1] & m_lTSMask) << m_iTSShift) & (header[3]);
    }
    if (iTimestamp < s_LastTimestamp) s_TimestampRollovers++;
    lTimestamp = iTimestamp + s_TimestampRollovers * m_iTimestampOffset;
    s_LastTimestamp = iTimestamp;
    for (unsigned i = 0; i < ChannelMasks.size(); i++) iEventChannelMask |= (ChannelMasks[i] << (NUM_CH*BoardIDs[i]));
    m_iNumBytesBody = iNumWordsBody * 4;
    iNumBytesEvent = iNumBytesBody + m_Header.size()*4;
    try {
        m_Body.resize(iNumBytesBody);
    } catch (std::exception& e) {
        throw
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

int Event::Write(const std::ofstream& fout) {
    fout.write((char*)m_Header.data(), m_Header.size()*4);
    fout.write(m_Body.data(), m_Body.size());
    return m_Header[2] & 0x7FFFFFFF;
}