#ifndef _DAQ_H_
#define _DAQ_H_ 1

#include "Digitizer.h"
#include "Event.h"
#include "kbhit.h"

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
    DAQ(int BufferSize = 1024);
    ~DAQ();
    void Setup(const string& filename);
    void Readout();
    void SetRunComment(const string& in) {m_sRunComment = in;}

private:
    void StartAcquisition();
    void StopAcquisition();
    void StartRun();
    void EndRun();
    void GetNewRunComment();
    void DoesNothing() {}; // for creation of threads

    atomic<bool> m_abSaveWaveforms;
    bool m_bTestRun;
    atomic<bool> m_abIsFirstEvent;
    atomic<bool> m_abRun;
    atomic<bool> m_abRunThreads;
    atomic<bool> m_abSuppressOutput;
    atomic<int> m_aiEventsInCurrentFile;
    atomic<int> m_aiEventsInRun;

    ofstream fout;
    sqlite3* m_RunsDB;
    sqlite3_stmt* m_InsertStmt;
    string m_sRunComment;
    map<string, int> m_BindIndex;
    vector<unique_ptr<Digitizer>> digis;
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
        int RecordLength;
        int BlockTransfer;
        unsigned int EventsPerFile;
        int IsZLE;
        string RawDataDir;
        string RunName;
        vector<ChannelSettings_t> ChannelSettings;
        int PostTrigger;
        vector<GW_t> GWs;
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

    atomic<int> m_iInsertPtr;
    atomic<int> m_iDecodePtr;
    atomic<int> m_iWritePtr;

    atomic<int> m_iToDecode;
    atomic<int> m_iToWrite;

    vector<Event> m_vBuffer;
    const int m_iBufferLength;
    const int m_iMaxEventsInRun = 1000000;
    const float m_fMaxFileRunTime = 3600.;

    KBHIT kb;
};

#endif // _DAQ_H_ defined
