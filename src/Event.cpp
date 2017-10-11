#include "Event.h"
#include <cstring>

int Event::s_TimestampRollovers = 0;
long Event::s_LastTimestamp = 0;
long Event::s_FirstEventTimestamp = 0;
int Event::s_FirstEventNumber = 0;
atomic<long> Event::s_UnixTSStart;

Event::Event() {}

Event::~Event() {}

void Event::Add(const vector<WORD*>& headers, const vector<WORD*>& bodies, bool IsFirstEvent) {
    vector<unsigned int> EventSizes, ChannelMasks, BoardIDs;
    unsigned long lTimestamp(0);
    unsigned int iEventCounter(0), iTimestamp(0);
    bool bIsZLE(false);
    unsigned int iEventChannelMask(0);
    int iNumWordsBody(0), iNumWordsHeader(4);
    int iNumBytesEvent(0), iNumBytesBody(0);
    for (auto& header : headers) {
        EventSizes.push_back(header[0] & s_EventSizeMask);
        iNumWordsBody += (EventSizes.back() - iNumWordsHeader);
        BoardIDs.push_back((header[1] & s_BoardIDMask) >> s_BoardIDShift);
        ChannelMasks.push_back(header[1] & s_ChannelMaskMask);
        iEventCounter = header[2] & s_CounterMask;
        bIsZLE = header[1] & s_ZLEMask;
        iTimestamp = header[3];
    }
    if (IsFirstEvent) {
        Event::s_FirstEventNumber = iEventCounter;
        Event::s_FirstEventTimestamp = iTimestamp;
    }
    if (iTimestamp < s_LastTimestamp) s_TimestampRollovers++; // CAEN timestamp rolls over every 43 seconds
    lTimestamp = iTimestamp + s_TimestampRollovers * s_TimestampOffset;
    s_LastTimestamp = iTimestamp;
    lTimestamp = s_UnixTSStart + (lTimestamp - Event::s_FirstEventTimestamp)*s_NsPerTriggerClock;
    iEventCounter = iEventCounter - Event::s_FirstEventNumber;
    for (unsigned i = 0; i < ChannelMasks.size(); i++) iEventChannelMask |= (ChannelMasks[i] << (NUM_CH*BoardIDs[i]));
    iNumBytesBody = iNumWordsBody * sizeof(WORD);
    iNumBytesEvent = iNumBytesBody + m_Header.size()*sizeof(WORD);

    try {
        m_Body.resize(iNumBytesBody);
    } catch (exception& e) {
        throw bad_alloc();
    }
    char* cPtr(m_Body.data());
    for (unsigned i = 0; i < headers.size(); i++) {
        memcpy(cPtr, bodies[i], (EventSizes[i]-4)*sizeof(WORD));
        cPtr += (EventSizes[i]-4)*sizeof(WORD);
    }
    m_Header[0] = iEventCounter | Event::s_HeaderStartIndicator; // assuming we don't get 1 << 30 events in a run ;)
    m_Header[1] = iEventChannelMask;
    m_Header[2] = bIsZLE ? iNumBytesEvent | (1 << 31) : iNumBytesEvent;
    m_Header[3] = lTimestamp >> 32;
    m_Header[4] = lTimestamp & (0xFFFFFFFFl);
}

void Event::Decode() {
    // nothing here, but we have the option
}

int Event::Write(ofstream& fout, unsigned int& EvNum) {
    fout.write((char*)m_Header.data(), m_Header.size()*sizeof(WORD));
    fout.write(m_Body.data(), m_Body.size());
    EvNum = m_Header[0] & (0x3FFFFFFF);
    return m_Header[2] & 0x7FFFFFFF;
}

void Event::SetUnixTS(long ts) {
    Event::s_UnixTSStart = ts;
}
