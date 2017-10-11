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
    DAQ(int BufferSize = 32768);
    ~DAQ();
    void Setup(const string& filename);
    void Readout();

private:
    void StartAcquisition();
    void StartRun();
    void EndRun();
    void DoesNothing() {}; // for creation of threads

    atomic<bool> m_abSaveWaveforms;
    bool m_bTestRun;
    atomic<bool> m_abIsFirstEvent;
    atomic<bool> m_abRun;

    ofstream fout;
    sqlite3* m_RunsDB;
    vector<unique_ptr<Digitizer>> digis; // make vector
    vector<thread> m_DecodeThreads;
    thread m_WriteThread;

    chrono::high_resolution_clock::time_point m_tStart;
    string m_sRunName;
    string m_sRunPath;
    vector<unsigned int> m_vEventSizes;
    vector<file_info> m_vFileInfos; // file_number, first_event, last_event, n_events
    vector<unsigned int> m_vEventSizeCum;

    vector<const char*> buffers;

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

    void AddEvents(vector<const char*>& buffer, unsigned int NumEvents);
    void DecodeEvent();
    void WriteEvent();
    void ResetPointers(); // call this while threads aren't active
    int WaitingToDecode() {return m_iToDecode;}
    int WaitingToWrite() {return m_iToWrite;}


    atomic<int> m_iInsertPtr;
    atomic<int> m_iDecodePtr;
    atomic<int> m_iWritePtr;

    atomic<int> m_iToDecode;
    atomic<int> m_iToWrite;

    vector<Event> m_vBuffer;
    const int m_iBufferLength;

};

#endif // _DAQ_H_ defined