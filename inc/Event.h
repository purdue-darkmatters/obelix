#ifndef _EVENT_H_
#define _EVENT_H_ 1

#include "base.h"

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
    void Decode(const std::vector<unsigned int*>& headers, const std::vector<unsigned int*>& bodies); // should handle multiple digitizers (up to 32 total channels)
    int Write(std::ofstream& fout);

private:
    std::array<unsigned int, 5> m_Header;
    std::vector<char> m_Body;

    static int s_TimestampRollovers;
    static long s_LastTimestamp;

    static const unsigned int s_EventSizeMask = (0xFFFFFFF);
    static const unsigned int s_BoardIDMask = (0xF8000000);
    static const unsigned int s_ZLEMask = (0x1000000);
    static const unsigned int s_ChannelMaskMask = (0xFF);
    static const unsigned int s_CounterMask = (0xFFFFFF);
    static const unsigned int s_BoardIDShift = (24);
    static const unsigned int s_TimestampOffset = (2147483648);
};

#endif // _EVENT_H_ defined