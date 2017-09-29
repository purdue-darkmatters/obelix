#ifndef _DAQ_H_
#define _DAQ_H_ 1

#include "Digitizer.h"
#include "Event.h"

#include <thread>
#include <atomic>
#include <mutex>

#include <utility>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <ctime>
#include <chrono>
#include <unistd.h>
#include <cstdint>
#include <cctype>
#include <sys/time.h>

namespace pt = boost::property_tree;

class DAQ {
public:
    DAQ();
    ~DAQ();
    void Setup(const std::string& filename);
    void Readout();

private:
    void StartRun();
    void EndRun();
    void ReadInput();
    void DecodeEvents(const bool which, const int iNumEvents);
    void WriteToDisk(const bool which);
    void DoesNothing() {}; // for creation of threads

    std::atomic<bool> m_abRun;
    std::atomic<bool> m_abQuit;
    std::atomic<bool> m_abTriggerNow;
    std::atomic<bool> m_abSaveWaveforms;
    std::atomic<bool> m_abTestRun;

    std::ofstream fout;
    sqlite3* m_RunsDB;
    std::unique_ptr<Digitizer> Digi;
    std::array<std::thread, 2> m_DecodeThread;
    std::thread m_WriteThread;
    std::thread m_ReadThread;

    unsigned long t_start;
    std::string m_sRunName;
    std::string m_sRunPath;
    int m_iFileCounter;
    std::atomic<int> m_iEventsInActiveFile;
    std::atomic<int> m_iEventsInRun;
    std::vector<unsigned int> m_vEventSizes;

    bool m_bWhich;
    std::array<const char*, 2> buffers;
    std::atomic<bool> m_abWriting;
    std::array<std::vector<Event>, 2> m_vEvents;

    struct {
        int EventsPerFile;
        int IsZLE;
        std::string RawDataDir;
        std::string RunName;
        int NumFiles;
        std::vector<ChannelSettings_t> ChannelSettings;
        int PostTrigger;
    } config;
}

#endif // _DAQ_H_ defined