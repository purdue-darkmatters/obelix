#include "DAQ.h"
#include "kbhit.h"
#include <csignal>

#include <sstream>
#include <iomanip>
#include "mongo/db/json.h"
#include "mongo/bson/bson.h"

static int s_interrupted = 0;
static void s_signal_handler(int signal_value) {
    s_interrupted = 1;
    cout << "\nInterrupted!\n";
}

static void s_catch_signals() {
    struct sigaction action;
    action.sa_handler = s_signal_handler;
    action.sa_flags = 0;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
}

DAQ::DAQ(int BufferLength) : m_iBufferLength(BufferLength) {
    int rc = sqlite3_open_v2(runs_db_addr.c_str(), &m_RunsDB, SQLITE_OPEN_READWRITE, NULL);
    if (rc != SQLITE_OK) {
        cout << "Could not connect to runs database\nSQLITE complains with error " << sqlite3_errmsg(m_RunsDB) << "\n";
        throw DAQException();
    }
    m_WriteThread = thread(&DAQ::DoesNothing, this);

    try {
        m_vBuffer.assign(BufferLength, Event());
    } catch (exception& e) {
        cout << "Could not allocate memory for " << BufferLength << " events!\n";
        throw bad_alloc();
    }

    rc = sqlite3_prepare_v2(m_RunsDB,
                            "INSERT INTO runs (name, start_time, end_time, runtime, events, \
                            source, raw_size) VALUES (?, ?, ?, ?, ?, ?, ?);",
                            -1, &m_InsertStmt, NULL);
    m_BindIndex["name"] = 1;
    m_BindIndex["start_time"] = 2;
    m_BindIndex["end_time"] = 3;
    m_BindIndex["runtime"] = 4;
    m_BindIndex["events"] = 5;
    m_BindIndex["source"] = 6;
    m_BindIndex["raw_size"] = 7;
    if (rc != SQLITE_OK) {
        cout << "Could not prepare database statement\nError code " << rc << "\n";
        throw DAQException();
    }

    m_iInsertPtr = 0;
    m_iDecodePtr = 0;
    m_iWritePtr = 0;

    m_iToDecode = 0;
    m_iToWrite = 0;

    m_aiEventsInCurrentFile = 0;
    m_aiEventsInRun = 0;

    m_abSaveWaveforms = false;
    m_bTestRun = true;
    m_abRunThreads = true;

    m_tStart = chrono::high_resolution_clock::now();
    Event::SetUnixTS(m_tStart.time_since_epoch().count());

    s_catch_signals();
}

DAQ::~DAQ() {
    m_abRunThreads = false;
    m_abRun = false;
    for (auto& th : m_DecodeThreads) if (th.joinable()) th.join();
    if (m_WriteThread.joinable()) m_WriteThread.join();
    EndRun();
    for (auto& dig : digis) dig.reset();
    sqlite3_finalize(m_InsertStmt);
    sqlite3_close_v2(m_RunsDB);
    m_RunsDB = nullptr;
    cout << "\nShutting down DAQ\n";
}

void DAQ::Setup(const string& filename) {
    cout << "Parsing config file " << filename << "...\n";
    string temp(""), pmt_config_file(filename.substr(0, filename.find_last_of('/')) + "/pmt_config.json");
    int link_number(0), conet_node(0), base_address(0);
    ChannelSettings_t ChanSet;
    GW_t GW;
    string json_string(""), str("");
    ifstream fin(filename, ifstream::in);
    if (!fin.is_open()) {
        cout << "Could not open " << filename << "\n";
        throw DAQException();
    }
    while (getline(fin, str)) json_string += str;
    try {
        config_dict = mongo::fromjson(json_string);
    } catch (exception& e) {
        cout << "Error parsing " << filename << ". Is it valid json?\n" << e.what() << "\n";
        throw DAQException();
    }
    fin.close();
    // will need some more values in config json about board numbers
    ConfigSettings_t CS;
    try {
        CS.RecordLength = config_dict["record_length"]["value"].Int();
        CS.PostTrigger = config_dict["post_trigger"]["value"].Int();
        CS.BlockTransfer = config_dict["block_transfer"]["value"].Int();
        config.EventsPerFile = config_dict["events_per_file"]["value"].Int();
        temp = config_dict["is_zle"]["value"].String();
        if (temp == "no") {CS.IsZLE = false; config.IsZLE = false;}
        else if (temp == "yes") {CS.IsZLE = true; config.IsZLE = true;}
        else cout << "Invalid ZLE config setting: " << temp << "\n";

        temp = config_dict["fpio_level"]["value"].String();
        if (temp == "ttl") CS.FPIO = CAEN_DGTZ_IOLevel_TTL;
        else if (temp == "nim") CS.FPIO = CAEN_DGTZ_IOLevel_NIM;
        else cout << "Invalid front panel setting: " << temp << "\n";
    } catch (exception& e) {
        cout << "Error in config file block 1: " << e.what() << "\n";
        throw DAQException();
    }

    try {
        config.RawDataDir = config_dict["raw_data_dir"]["value"].String();
        link_number = config_dict["digitizers"]["link_number"].Int();
        conet_node = config_dict["digitizers"]["conet_node"].Int();
        base_address = config_dict["digitizers"]["base_address"].Int();

        temp = config_dict["external_trigger"]["value"].String();
        if (temp == "acquisition_only") CS.ExtTriggerMode = CAEN_DGTZ_TRGMODE_ACQ_ONLY;
        else if (temp == "acquisition_and_trgout") CS.ExtTriggerMode = CAEN_DGTZ_TRGMODE_ACQ_AND_EXTOUT;
        else if (temp == "disabled") CS.ExtTriggerMode = CAEN_DGTZ_TRGMODE_DISABLED;
        else if (temp == "trgout_only") CS.ExtTriggerMode = CAEN_DGTZ_TRGMODE_EXTOUT_ONLY;
        else cout << "Invalid external trigger value: " << temp << "\n";

        temp = config_dict["channel_trigger"]["value"].String();
        if (temp == "acquisition_only") CS.ChTriggerMode = CAEN_DGTZ_TRGMODE_ACQ_ONLY;
        else if (temp == "acquisition_and_trgout") CS.ChTriggerMode = CAEN_DGTZ_TRGMODE_ACQ_AND_EXTOUT;
        else if (temp == "disabled") CS.ChTriggerMode = CAEN_DGTZ_TRGMODE_DISABLED;
        else cout << "Invalid channel trigger value: " << temp << "\n";

        for (auto& gw : config_dict["registers"].Array()) {
            GW.addr = stoi(gw["register"].String(), nullptr, 16);
            GW.data = stoi(gw["data"].String(), nullptr, 16);
            GW.mask = stoi(gw["mask"].String(), nullptr, 16);
            CS.GenericWrites.push_back(GW);
        }
    } catch (exception& e) {
        cout << "Error in config file block 2: " << e.what() << "\n";
        throw DAQException();
    }

    try {
        for (int i = 0; i < config_dict["decode_threads"]["value"].Int(); i++) m_DecodeThreads.push_back(thread(&DAQ::DoesNothing, this));
    } catch (exception& e) {
        cout << "Error starting decode threads\n" << e.what() << "\n";
        throw DAQException();
    }

    fin.open(pmt_config_file, ifstream::in);
    if (!fin.is_open()) {
        cout << "Could not open " << pmt_config_file << "\n";
        throw DAQException();
    }
    json_string = "\0";
    while (getline(fin, str)) json_string += str;
    fin.close();
    try {
        config_dict = mongo::fromjson(json_string);
    } catch (exception& e) {
        cout << "Error parsing " << pmt_config_file << ". Is it valid json?\n" << e.what() << "\n";
        throw DAQException();
    }

    try {
        CS.EnableMask = 0;
        for (auto& cs : config_dict["channels"].Array()) {
            ChanSet.Channel             = cs["channel"].Int();
            ChanSet.Enabled             = cs["enabled"].Int();
            ChanSet.DCoffset            = cs["dc_offset"].Int();
            ChanSet.TriggerThreshold    = cs["trigger_threshold"].Int();
            ChanSet.TriggerMode         = CS.ChTriggerMode;
            ChanSet.ZLEThreshold        = cs["zle_threshold"].Int();
            ChanSet.ZLE_N_LFWD          = cs["zle_lfwd_samples"].Int();
            ChanSet.ZLE_N_LBK           = cs["zle_lbk_samples"].Int();
            if (ChanSet.Enabled) CS.EnableMask |= (1 << ChanSet.Channel);
            CS.ChannelSettings.push_back(ChanSet);
            config.ChannelSettings.push_back(ChanSet);
        }
    } catch (exception& e) {
        cout << "Error in config file block 3: " << e.what() << "\n";
        throw DAQException();
    }

    try {
        digis.push_back(unique_ptr<Digitizer>(new Digitizer(link_number, conet_node, base_address)));
    } catch (exception& e) {
        cout << "Could not allocate digitizer!\n" << e.what() << "\n";
        throw DAQException();
    }

    for (auto& dig : digis) {
        dig->ProgramDigitizer(CS);
        buffers.push_back(dig->GetBuffer());
    }

}

void DAQ::StartRun() {
    m_tStart = chrono::high_resolution_clock::now(); // nanosecond precision!
    Event::SetUnixTS(m_tStart.time_since_epoch().count());
    m_abIsFirstEvent = true;
    time_t rawtime;
    time(&rawtime);
    char temp[32];
    strftime(temp, sizeof(temp), "%Y%m%d_%H%M", localtime(&rawtime));
    config.RunName = temp;
    cout << "\nStarting run " << config.RunName << "\n";

    m_vFileInfos.push_back(file_info{0,0,0,0});
    string command = "mkdir " + config.RawDataDir + config.RunName;
    system(command.c_str());
    stringstream fullfilename;
    fullfilename << config.RawDataDir << config.RunName << "/" << config.RunName << "_" << setw(6) << setfill('0') << m_vFileInfos.size()-1 << flush << ".ast";
    fout.open(fullfilename.str(), ofstream::binary | ofstream::out);
    if (!fout.is_open()) {
        cout << "Could not open " << fullfilename.str() << "\n";
        throw DAQException();
    }
}

void DAQ::EndRun() {
    if (!fout.is_open()) return;
    cout << "\nEnding run " << config.RunName << "\n";
    if (fout.is_open()) fout.close();
    chrono::high_resolution_clock::time_point tEnd = chrono::high_resolution_clock::now();

    mongo::BSONObjBuilder builder;

    builder.append("is_zle", config.IsZLE);
    builder.append("run_name", config.RunName);
    builder.append("post_trigger", config.PostTrigger);
    builder.append("events", (int)m_vEventSizes.size());
    builder.append("start_time_ns", (long long)m_tStart.time_since_epoch().count());
    builder.append("end_time_ns", (long long)tEnd.time_since_epoch().count());

    vector<mongo::BSONObj> chan_sets;
    for (auto& cs : config.ChannelSettings) {
        mongo::BSONObjBuilder ch;
        ch.append("channel", cs.Channel);
        ch.append("enabled", cs.Enabled);
        ch.append("trigger_threshold", cs.TriggerThreshold);
        ch.append("zle_threshold", cs.ZLEThreshold);

        chan_sets.push_back(ch.obj());
    }
    builder.append("channel_settings", chan_sets);

    vector<mongo::BSONObj> file_nos;
    for (auto& f : m_vFileInfos) {
        mongo::BSONObjBuilder ff;
        ff.append("file_number", f[file_number]);
        ff.append("first_event", f[first_event]);
        ff.append("last_event", f[last_event]);
        ff.append("n_events", f[n_events]);

        file_nos.push_back(ff.obj());
    }
    builder.append("file_info", file_nos);

    builder.append("event_size_bytes", m_vEventSizes);
    builder.append("event_size_cum", m_vEventSizeCum);

    stringstream ss;
    ss << config.RawDataDir << config.RunName << "/pax_info.json";
    ofstream fheader(ss.str(), ofstream::out);
    if (!fheader.is_open()) {
        cout << "Could not open file header: " << ss.str() << "\n";
        throw DAQException();
    }
    fheader << tojson(builder.obj(), mongo::Strict, true);
    fheader.close();

    if (!m_bTestRun) {
        long run_size_bytes(0);
        int log_size(0);
        stringstream run_size;
        for (auto& x : m_vEventSizes) run_size_bytes += x;
        for (log_size = 63; log_size >= 0; log_size--) if ((1 << log_size) & run_size_bytes) break;
        if (log_size < 20) { // < 1 MB
            run_size << "1M";
        } else if (log_size < 30) { // < 1 GB
            run_size << (run_size_bytes >> 20) << "M";
        } else if (log_size < 40) { // < 1 TB
            run_size << (run_size_bytes >> 30) << "G";
        } else {
            run_size << (run_size_bytes >> 40) << "T";
        }
        sqlite3_bind_text(m_InsertStmt, m_BindIndex["name"], config.RunName.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(m_InsertStmt, m_BindIndex["start_time"], m_tStart.time_since_epoch().count());
        sqlite3_bind_int64(m_InsertStmt, m_BindIndex["end_time"], tEnd.time_since_epoch().count());
        sqlite3_bind_int64(m_InsertStmt, m_BindIndex["runtime"], chrono::duration_cast<chrono::duration<double>>(tEnd-m_tStart).count());
        sqlite3_bind_int(m_InsertStmt, m_BindIndex["events"], m_vEventSizes.size());
        sqlite3_bind_text(m_InsertStmt, m_BindIndex["source"], (config.IsZLE ? "none" : "LED"), -1, SQLITE_STATIC);
        sqlite3_bind_text(m_InsertStmt, m_BindIndex["raw_size"], run_size.str().c_str(), -1, SQLITE_STATIC);

        int rc = sqlite3_step(m_InsertStmt);
        if (rc != SQLITE_DONE) {
            cout << "Couldn't add entry to runs databse\nError code " << rc << "\n";
        }
        rc = sqlite3_reset(m_InsertStmt);
        if (rc != SQLITE_OK) {
            cout << "Couldn't reset statement\nError code " << rc << "\n";
        }
        rc = sqlite3_clear_bindings(m_InsertStmt);
        if (rc != SQLITE_OK) {
            cout << "Couldn't clear bindings\nError code " << rc << "\n";
        }
    }

    m_vEventSizes.clear();
    m_vFileInfos.clear();
    m_vEventSizeCum.clear();

    m_aiEventsInCurrentFile = 0;
    m_aiEventsInRun = 0;

    // reset digitizer timestamps?
}

void DAQ::StartAcquisition() {
    m_abRunThreads = false;
    for (auto& th : m_DecodeThreads) if (th.joinable()) th.join();
    if (m_WriteThread.joinable()) m_WriteThread.join();
    digis.front()->StartAcquisition(); // signal propagates
    m_abRun = true;
    ResetPointers();
    m_abIsFirstEvent = true;
    m_abRunThreads = true;
    if (m_abSaveWaveforms) StartRun();
    for (auto& th : m_DecodeThreads) th = thread(&DAQ::DecodeEvent, this);
    m_WriteThread = thread(&DAQ::WriteEvent, this);
}

void DAQ::StopAcquisition() {
    digis.front()->StopAcquisition();
    m_abRunThreads = false;
    m_abRun = false;
    for (auto& th : m_DecodeThreads) if (th.joinable()) th.join();
    if (m_WriteThread.joinable()) m_WriteThread.join();
    ResetPointers();
    if (m_abSaveWaveforms) EndRun();
}

void DAQ::Readout() {
    cout << setbase(10) << flush;
    cout << "Ready to go.\n";
    cout << "Commands:"
              << " [s] Start/stop\n"
              << " [t] Force trigger\n"
              << " [w] Toggle writing events to disk\n"
              << " [T] Toggle automatic runs database interfacing\n"
              << " [q] Quit\n";
    unsigned int iNumEvents(0), iBufferSize(0), iTotalBuffer(0), iTotalEvents(0);
    bool bTriggerNow(false), bQuit(false);
    auto PrevPrintTime = chrono::system_clock::now();
    chrono::system_clock::time_point ThisLoop;
    int FileRunTime(0), iLogReadSize(0), OutputWidth(60);
    char input('0');
    double dLoopTime(0);
    array<stringstream, 5> sOutputs;
    stringstream sOutput;
    const string delim(", ");

    while (!bQuit) {
        if (kbhit()) {
            cin.get(input);
            switch(input) {
                case 's' :
                    m_abRun = !m_abRun;
                    if (m_abRun) {
                        StartAcquisition();
                    } else {
                        StopAcquisition();
                    } break;
                case 't' :
                    bTriggerNow = true;
                    break;
                case 'w' :
                    if (m_abRun)
                        cout << "Please stop acquisition first with 's'\n";
                    else {
                        m_abSaveWaveforms = !m_abSaveWaveforms;
                        cout << "Writing to disk " << (m_abSaveWaveforms ? "en" : "dis") << "abled\n";
                        StartAcquisition();
                    }
                    break;
                case 'T' :
                    m_bTestRun = !m_bTestRun;
                    cout << "Test run " << (m_bTestRun ? "en" : "dis") << "abled\n";
                    break;
                case 'q' :
                    if (m_abRun)
                        cout << "Please stop acquisition first with 's'\n";
                    else
                        bQuit = true;
                    break;
                default: break;
            }
            input = '0';
        }
        if (!m_abRun) {
            continue;
        }
        if (bTriggerNow) {
            cout << "Triggering\n";
            digis.front()->SWTrigger();
            bTriggerNow = false;
        }

        // read from digitizer into open buffer
        iNumEvents = 0;
        for (auto& dig : digis) {
            iNumEvents = dig->ReadBuffer(iBufferSize);
            iTotalBuffer += iBufferSize;
            iTotalEvents += iNumEvents;
        }
        if (iNumEvents > 0) AddEvents(buffers, iNumEvents);


        ThisLoop = chrono::system_clock::now();
        dLoopTime = chrono::duration_cast<chrono::duration<double>>(ThisLoop - PrevPrintTime).count();
        if (dLoopTime > 1.0) {
            for (iLogReadSize = 31; iLogReadSize >= 0; iLogReadSize--) if ((1 << iLogReadSize) & iTotalBuffer) break;
            if (iLogReadSize < 10) { // ~B/s
                sOutputs[0] << setprecision(3) << iTotalBuffer/dLoopTime << " B/s";
            } else if (iLogReadSize < 20) { // ~kB/s
                sOutputs[0] << setprecision(3) << (iTotalBuffer >> 10)/dLoopTime << " kB/s";
            } else { // ~MB/s
                sOutputs[0] << setprecision(3) << (iBufferSize >> 20)/dLoopTime << " MB/s";
            }
            sOutputs[1] << setprecision(3) << iTotalEvents/dLoopTime << " Hz";
            FileRunTime = chrono::duration_cast<chrono::seconds>(ThisLoop - m_tStart).count();
            sOutputs[2] << FileRunTime << " sec";
            sOutputs[3] << m_iToDecode;
            if (m_abSaveWaveforms) {
                sOutputs[3] << "/" << m_iToWrite;
                sOutputs[4] << m_aiEventsInCurrentFile << "/" << m_aiEventsInRun << " ev";
            }
            sOutput << "\rStatus: ";
            for (auto& s : sOutputs) {
                if (s.str() != "") sOutput << s.str() << delim;
                s.str("");
            }
            cout << left << setw(OutputWidth) << sOutput.str() << flush;
            sOutput.str("");
            iTotalBuffer = 0;
            iTotalEvents = 0;
            if ((FileRunTime >= 3600.) || (m_aiEventsInRun >= m_iMaxEventsInRun)) {
                StopAcquisition();
                StartAcquisition();
            }
            PrevPrintTime = chrono::system_clock::now();
        } // print loop

        if (s_interrupted) {
            StopAcquisition();
            bQuit = true;
            m_abRunThreads = false;
        }
    } // run loop
} // Readout()

void DAQ::AddEvents(vector<const char*>& buffer, unsigned int NumEvents) {
    // this runs in the main thread
    const unsigned int iSizeMask (0xFFFFFFF), iNumBytesHeader(4*sizeof(WORD));
    vector<vector<WORD*> > vHeaders;
    vector<vector<WORD*> > vBodies;
    WORD* pHeader(nullptr);
    WORD* pBody(nullptr);
    vector<int> offset(buffer.size());
    unsigned int iWordsInThisEvent(0);
    bool bCallForHelp(true);
    for (unsigned i = 0; i < NumEvents; i++) {
        vHeaders.push_back(vector<WORD*>());
        vBodies.push_back(vector<WORD*>());
        for (unsigned int b = 0; b < buffer.size(); b++) {
            iWordsInThisEvent = iSizeMask & *(WORD*)(buffer[b] + offset[b]);
            pHeader = (WORD*)(buffer[b] + offset[b]);
            pBody = (WORD*)(buffer[b] + offset[b] + iNumBytesHeader);
            vHeaders.back().push_back(pHeader);
            vBodies.back().push_back(pBody);
            offset[b] += iWordsInThisEvent * sizeof(WORD);
        }
        m_vBuffer[m_iInsertPtr].Add(vHeaders.back(), vBodies.back(), m_abIsFirstEvent);
        m_abIsFirstEvent = false;
        m_iToDecode++;
        while (m_abSaveWaveforms && (m_iWritePtr == (m_iInsertPtr+1)%m_iBufferLength) && (s_interrupted == 0) && (m_iToWrite != 0)) {
            if (bCallForHelp) {
                cout << "Deadtime warning\n";
                bCallForHelp = false;
            }
            this_thread::yield();
        }
        bCallForHelp = true;
        m_iInsertPtr = (m_iInsertPtr+1) % m_iBufferLength;
    }
}

void DAQ::DecodeEvent() {
    while (m_abRun) {
        while ((m_iToDecode == 0) && (m_abRunThreads) && (s_interrupted == 0)) this_thread::yield();

        if ((!m_abRunThreads) || (s_interrupted)) return;
        //cout << "Decoded an event\n";
        m_vBuffer[m_iDecodePtr].Decode();

        m_iToDecode--;
        m_iToWrite++;
        m_iDecodePtr = (m_iDecodePtr+1) % m_iBufferLength;
    }
}

void DAQ::WriteEvent() {
    while (m_abRun) {
        while ((m_iToWrite == 0 || !m_abSaveWaveforms) && (m_abRunThreads) && (s_interrupted == 0)) this_thread::yield();
        int NumBytes(0);
        unsigned int EvNum(0);

        if ((!m_abRunThreads) || (s_interrupted)) return;

        if (m_vFileInfos.back()[n_events] >= config.EventsPerFile) {
            fout.close();
            m_vFileInfos.push_back(file_info{0,0,0,0});
            stringstream ss;
            ss << config.RawDataDir << config.RunName << "/" << config.RunName << "_" << setw(6) << setfill('0') << m_vFileInfos.size()-1 << flush << ".ast";
            fout.open(ss.str(), ofstream::binary | ofstream::out);
            m_vFileInfos.back()[file_number] = m_vFileInfos.size()-1;
        }

        NumBytes = m_vBuffer[m_iWritePtr].Write(fout, EvNum);

        if (m_vFileInfos.back()[n_events] == 0) {
            m_vFileInfos.back()[first_event] = EvNum;
            m_vEventSizeCum.push_back(0);
        } else {
            m_vFileInfos.back()[last_event] = EvNum;
            m_vEventSizeCum.push_back(m_vEventSizeCum.back() + m_vEventSizes.back());
        }
        m_vEventSizes.push_back(NumBytes);

        m_vFileInfos.back()[n_events]++;
        m_aiEventsInCurrentFile = m_vFileInfos.back()[n_events];
        m_aiEventsInRun = m_vEventSizes.size();

        m_iToWrite--;

        m_iWritePtr = (m_iWritePtr+1) % m_iBufferLength;
    }
}

void DAQ::ResetPointers() {
    m_iToDecode = 0;
    m_iToWrite = 0;
    m_iWritePtr.store(m_iInsertPtr.load());
    m_iDecodePtr.store(m_iInsertPtr.load());
}
