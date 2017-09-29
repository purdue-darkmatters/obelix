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
    void Decode(const std::vector<unsigned int*>& headers, const std::vector<unsigned int*>& bodies);
    int Write(const std::ofstream& fout);

private:
    std::array<unsigned int, 5> m_Header;
    std::vector<unsigned int> m_Body;

    static int s_TimestampRollovers;
    static long s_LastTimestamp;

    const unsigned int m_iEventSizeMask;
    const unsigned int m_iBoardIDMask;
    const unsigned int m_iZLEMask;
    const unsigned int m_iChannelMaskMask;
    const unsigned int m_iCounterMask;
    const unsigned int m_iBoardIDShift;
    const unsigned int m_iTimestampOffset;
};

#endif // _EVENT_H_ defined