#ifndef _DAQ_H_
#define _DAQ_H_ 1

#include "Digitizer.h"
#include "Event.h"

#include <sqlite3.h>
#include "mongo/bson/bson.h"

#include <thread>
#include <mutex>

#include <ctime>
#include <chrono>
#include <unistd.h>
#include <cstdint>
#include <cctype>
#include <chrono>

using file_info = array<unsigned int, 4>;

class DAQException : public exception {
public:
    const char* what() const throw () {
        return "DAQ error";
    }
};

class DAQ {
public:
    DAQ();
    ~DAQ();
    void Setup(const string& filename);
    void Readout();

private:
    void StartRun();
    void EndRun();
    void DecodeEvents(const bool which, const unsigned int iNumEvents);
    void WriteToDisk(const bool which);
    void DoesNothing() {}; // for creation of threads

    bool m_bSaveWaveforms;
    bool m_bTestRun;
    atomic<bool> m_abIsFirstEvent;

    ofstream fout;
    sqlite3* m_RunsDB;
    unique_ptr<Digitizer> dig; // make vector
    array<thread, 2> m_DecodeThread;
    thread m_WriteThread;

    chrono::high_resolution_clock::time_point m_tStart;
    string m_sRunName;
    string m_sRunPath;
    int m_iFileCounter;
    vector<unsigned int> m_vEventSizes;
    vector<file_info> m_vFileInfos; // file_number, first_event, last_event, n_events
    vector<unsigned int> m_vEventSizeCum;

    bool m_bWhich;
    array<const char*, 2> buffers; // make vector
    atomic<bool> m_abWriting;
    array<vector<Event>, 2> m_vEvents;

    mongo::BSONObj config_dict;
    struct {
        int EventsPerFile;
        int IsZLE;
        string RawDataDir;
        string RunName;
        vector<ChannelSettings_t> ChannelSettings;
        int PostTrigger;
    } config;

    enum file_info_vals {
        file_number = 0,
        first_event,
        last_event,
        n_events,
    };
};

#endif // _DAQ_H_ defined