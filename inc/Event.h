#ifndef _EVENT_H_
#define _EVENT_H_ 1

#include "base.h"
#include <atomic>

#define NUM_CH 8

/* Event header format:
 * word0: Event number
 * word1: channel mask
 * word2: bit[31] = zle, bits [0:30] = event size (total bytes on disk, header plus body)
 * word3: timestamp (bits [32:63])
 * word4: timestamp (bits [0:31])
*/

class Event {
public:
    Event();
    ~Event();
    void Add(const vector<WORD*>& headers, const vector<WORD*>& bodies, bool IsFirstEvent = false); // should handle multiple digitizers (up to 32 total channels)
    void Decode();
    int Write(ofstream& fout, unsigned int& EvNum);
    static void SetUnixTS(long ts);

private:
    array<WORD, 5> m_Header;
    vector<char> m_Body;

    static int s_TimestampRollovers;
    static long s_LastTimestamp;
    static long s_FirstEventTimestamp;
    static int s_FirstEventNumber;
    static atomic<long> s_UnixTSStart;

    static const unsigned int s_EventSizeMask = (0xFFFFFFF);
    static const unsigned int s_BoardIDMask = (0xF8000000);
    static const unsigned int s_ZLEMask = (0x1000000);
    static const unsigned int s_ChannelMaskMask = (0xFF);
    static const unsigned int s_CounterMask = (0xFFFFFF);
    static const unsigned int s_BoardIDShift = (0x18);
    static const unsigned int s_TimestampOffset = (0x80000000);
    static const unsigned int s_NsPerTriggerClock = (0x14);
    static const unsigned int s_HeaderStartIndicator = (0xC0000000);
};

#endif // _EVENT_H_ defined
