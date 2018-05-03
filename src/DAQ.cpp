#include "DAQ.h"
#include <csignal>
#include <cmath>

#include <sstream>
#include <iomanip>
#include <bsoncxx/json.hpp>
#include <bsoncxx/document/value.hpp>
#include <bsoncxx/document/view.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/builder/basic/kvp.hpp>

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
        BOOST_LOG_TRIVIAL(fatal) << "Could not connect to runs database. SQLITE complains with error " << sqlite3_errmsg(m_RunsDB);
        throw DAQException();
    } else BOOST_LOG_TRIVIAL(debug) << "Runs db connection made";

    m_WriteThread = thread(&DAQ::DoesNothing, this);

    try {
        m_vBuffer.assign(BufferLength, Event());
    } catch (exception& e) {
        BOOST_LOG_TRIVIAL(fatal) << "Could not allocate memory for " << BufferLength << " events!";
        throw bad_alloc();
    }

    rc = sqlite3_prepare_v2(m_RunsDB,
                            "INSERT INTO runs (name, start_time, end_time, runtime, events, \
                            source, raw_size, comments) VALUES (?, ?, ?, ?, ?, ?, ?, ?);",
                            -1, &m_InsertStmt, NULL);
    m_BindIndex["name"] = 1;
    m_BindIndex["start_time"] = 2;
    m_BindIndex["end_time"] = 3;
    m_BindIndex["runtime"] = 4;
    m_BindIndex["events"] = 5;
    m_BindIndex["source"] = 6;
    m_BindIndex["raw_size"] = 7;
    m_BindIndex["comments"] = 8;
    if (rc != SQLITE_OK) {
        BOOST_LOG_TRIVIAL(fatal) << "Could not prepare database statement, error code " << rc;
        throw DAQException();
    } else BOOST_LOG_TRIVIAL(debug) << "Database statement prepared";

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
    m_abSuppressOutput = false;

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
    BOOST_LOG_TRIVIAL(info) << "Shutting down DAQ";
}

void DAQ::Setup(const string& filename) {
    BOOST_LOG_TRIVIAL(info) << "Parsing config file " << filename << "...";
    string pmt_config_file(filename.substr(0, filename.find_last_of('/')) + "/pmt_config.json");
    int link_number(0), conet_node(0), base_address(0), board(-1);
    ChannelSettings_t ChanSet;
    GW_t GW;
    string json_string(""), str("");
    ifstream fin(filename, ifstream::in);
    bsoncxx::document::view config_dict{};
    if (!fin.is_open()) {
        BOOST_LOG_TRIVIAL(fatal) << "Could not open " << filename;
        throw DAQException();
    } else BOOST_LOG_TRIVIAL(debug) << "Opened " << filename;
    while (getline(fin, str)) json_string += str;
    try {
        bsoncxx::document::value temp = bsoncxx::from_json(json_string);
        config_dict = temp.view();
    } catch (exception& e) {
        BOOST_LOG_TRIVIAL(fatal) << "Error parsing " << filename << ". Is it valid json? " << e.what();
        throw DAQException();
    }
    fin.close();

    vector<ConfigSettings_t> CS;
    try {
        config.RawDataDir = config_dict["raw_data_dir"]["value"].get_utf8().value.to_string();
        for (auto& d : config_dict["digitizers"].get_array().value) {
            link_number = d["link_number"].get_int32();
            conet_node = d["conet_node"].get_int32();
            base_address = d["base_address"].get_int32();
            try {
                digis.push_back(unique_ptr<Digitizer>(new Digitizer(link_number, conet_node, base_address)));
                CS.push_back(ConfigSettings_t{});
            } catch (exception& e) {
                BOOST_LOG_TRIVIAL(fatal) << "Could not allocate digitizer! " << e.what();
                throw DAQException();
            }
        }

        for (auto& gw : config_dict["registers"].get_array().value) {
            GW.board = gw["board"].get_int32();
            GW.addr = stoi(gw["register"].get_utf8().value.to_string(), nullptr, 16);
            GW.data = stoi(gw["data"].get_utf8().value.to_string(), nullptr, 16);
            GW.mask = stoi(gw["mask"].get_utf8().value.to_string(), nullptr, 16);
            if (board == -1) for (auto& cs : CS) cs.GenericWrites.push_back(GW);
            else CS[board].GenericWrites.push_back(GW);
            config.GWs.push_back(GW);
        }
    } catch (exception& e) {
        BOOST_LOG_TRIVIAL(fatal) << "Error in config file block 1: " << e.what();
        throw DAQException();
    }

    try {
        for (auto& cs : CS) {
            cs.RecordLength = config_dict["record_length"]["value"].get_int32();
            cs.PostTrigger = config_dict["post_trigger"]["value"].get_int32();
            cs.BlockTransfer = config_dict["block_transfer"]["value"].get_int32();
            cs.IsZLE = ZLE.at(config_dict["is_zle"]["value"].get_utf8().value.to_string()); // operator[] throws esoteric errors
            cs.FPIO = FPIOlevel.at(config_dict["fpio_level"]["value"].get_utf8().value.to_string());
            cs.ExtTriggerMode = TriggerMode.at(config_dict["external_trigger"]["value"].get_utf8().value.to_string());
            cs.ChTriggerMode = TriggerMode.at(config_dict["channel_trigger"]["value"].get_utf8().value.to_string());
            cs.EnableMask = 0;
        }
        config.EventsPerFile = config_dict["events_per_file"]["value"].get_int32();
        config.RecordLength = config_dict["record_length"]["value"].get_int32();
        config.BlockTransfer = config_dict["block_transfer"]["value"].get_int32();
        config.IsZLE = ZLE.at(config_dict["is_zle"]["value"].get_utf8().value.to_string());
        BOOST_LOG_TRIVIAL(debug) << "Events per file: " << config.EventsPerFile;
        BOOST_LOG_TRIVIAL(debug) << "Record length: " << config.RecordLength;
        BOOST_LOG_TRIVIAL(debug) << "Block transfer: " << config.BlockTransfer;
        BOOST_LOG_TRIVIAL(debug) << "Is ZLE: " << config.IsZLE;

    } catch (exception& e) {
        BOOST_LOG_TRIVIAL(fatal) << "Error in config file block 2: " << e.what();
        throw DAQException();
    }

    try {
        for (int i = 0; i < config_dict["decode_threads"]["value"].get_int32(); i++) m_DecodeThreads.push_badk(thread(&DAQ::DoesNothing, this));
    } catch (exception& e) {
        BOOST_LOG_TRIVIAL(fatal) << "Error starting decode threads. " << e.what();
        throw DAQException();
    }

    fin.open(pmt_config_file, ifstream::in);
    if (!fin.is_open()) {
        BOOST_LOG_TRIVIAL(fatal) << "Could not open " << pmt_config_file;
        throw DAQException();
    } else BOOST_LOG_TRIVIAL(debug) << "Opened " << pmt_config_file;
    json_string = "\0";
    while (getline(fin, str)) json_string += str;
    fin.close();
    try {
        config_dict = bsoncxx::from_json(json_string);
    } catch (exception& e) {
        BOOST_LOG_TRIVIAL(fatal) << "Error parsing " << pmt_config_file << ". Is it valid json? " << e.what();
        throw DAQException();
    }

    try {
        for (auto& cs : config_dict["channels"].get_array().value) {
            ChanSet.Board               = cs["board"].get_int32();
            ChanSet.Channel             = cs["channel"].get_int32();
            ChanSet.Enabled             = cs["enabled"].get_int32();
            ChanSet.DCoffset            = cs["dc_offset"].get_int32();
            ChanSet.TriggerThreshold    = cs["trigger_threshold"].get_int32();
            ChanSet.TriggerMode         = CS[board].ChTriggerMode;
            ChanSet.ZLEThreshold        = cs["zle_threshold"].get_int32();
            ChanSet.ZLE_N_LFWD          = cs["zle_lfwd_samples"].get_int32();
            ChanSet.ZLE_N_LBK           = cs["zle_lbk_samples"].get_int32();
            if (ChanSet.Enabled) CS[ChanSet.Board].EnableMask |= (1 << ChanSet.Channel);
            CS[ChanSet.Board].ChannelSettings.push_back(ChanSet);
            config.ChannelSettings.push_back(ChanSet);
            BOOST_LOG_TRIVIAL(debug) << "Board " << ChanSet.Board << " ch " << ChanSet.Channel << " dc " << ChanSet.DCoffset << " trig " << ChanSet.TriggerThreshold
                << " ZLE " << ChanSet.ZLEThreshold;
        }
    } catch (exception& e) {
        cout << "Error in config file block 3: " << e.what() << "\n";
        throw DAQException();
    }

    for (unsigned i = 0; i < digis.size(); i++) {
        digis[i]->ProgramDigitizer(CS[i]);
        buffers.push_back(digis[i]->GetBuffer());
    }
    BOOST_LOG_TRIVIAL(debug) << "Setup done";
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
    BOOST_LOG_TRIVIAL(info) << "Starting run " << config.RunName;

    m_vFileInfos.push_back(file_info{0,0,0,0});
    string command = "mkdir " + config.RawDataDir + config.RunName;
    system(command.c_str());
    stringstream fullfilename;
    fullfilename << config.RawDataDir << config.RunName << "/" << config.RunName << "_" << setw(6) << setfill('0') << m_vFileInfos.size()-1 << flush << ".ast";
    fout.open(fullfilename.str(), ofstream::binary | ofstream::out);
    if (!fout.is_open()) {
        BOOST_LOG_TRIVIAL(fatal) << "Could not open " << fullfilename.str();
        throw DAQException();
    } else BOOST_LOG_TRIVIAL(debug) << "Opened " << fullfilename.str();
}

void DAQ::EndRun() {
    if (!fout.is_open()) return;
    BOOST_LOG_TRIVIAL(info) << "Ending run " << config.RunName;
    if (fout.is_open()) fout.close();
    chrono::high_resolution_clock::time_point tEnd = chrono::high_resolution_clock::now();

    long run_size_bytes(0);
    int log_size(0);
    char run_size[16];
    bsoncxx::builder::basic::document doc{};
    const string sBlockSize = " MMGTP";
    using bsoncxx::builder::basic::sub_document;
    using bsoncxx::builder::basic::sub_array;

    doc.append(kvp("is_zle", config.IsZLE));
    doc.append(kvp("run_name", config.RunName));
    doc.append(kvp("post_trigger", config.PostTrigger));
    doc.append(kvp("events", (int)m_vEventSizes.size()));
    doc.append(kvp("start_time_ns", m_tStart.time_since_epoch().count()));
    doc.append(kvp("end_time_ns", tEnd.time_since_epoch().count()));

/*    doc.append(kvp("subdocument key", [](sub_document subdoc) {
                       subdoc.append(kvp("subdoc key", "subdoc value"),
                                     kvp("another subdoc key", types::b_int64{1212}));
                   }), */

    doc.append(kvp("channel_settings", [](sub_array subarr) {
        for (auto& cs : config.ChannelSettings) {
            subarr.append([](sub_document subdoc) {
                subdoc.append(kvp("board", cs.Board));
                subdoc.append(kvp("channel", cs.Channel));
                subdoc.append(kvp("enabled", cs.Enabled));
                subdoc.append(kvp("trigger_threshold", cs.TriggerThreshold));
                subdoc.append(kvp("zle_threshold", cs.ZLEThreshold));
                subdoc.append(kvp("zle_lbk", cs.ZLE_N_LBK));
                subdoc.append(kvp("zle_lfw", cs.ZLE_N_LFWD));
            });
        }
    }));

    doc.append(kvp("generic_writes", [](sub_array subarr) {
        for (auto& gw : config.GWs) {
            subarr.append([](sub_document subdoc) {
                subdoc.append(kvp("board", gw.board));
                subdoc.append(kvp("address", gw.addr));
                subdoc.append(kvp("data", gw.data));
                subdoc.append(kvp("mask", gw.mask));
            });
        }
    }));

    doc.append(kvp("file_info", [](sub_array subarr) {
        for (auto& f : m_vFileInfos) {
            subarr.append([](sub_document subdoc) {
                subdoc.append(kvp("file_number", f[file_number]));
                subdoc.append(kvp("first_event", f[first_event]));
                subdoc.append(kvp("last_event", f[last_event]));
                subdoc.append(kvp("n_events", f[n_events]));
            });
        }
    }));

    doc.append(kvp("event_size_bytes", [](sub_array subarr) {
        for (auto& i : m_vEventSizes) subarr.append(i);
    }));
    doc.append(kvp("event_size_cum", [](sub_array subarr) {
        for (auto& i : m_vEventSizeCum) subarr.append(i);
    }));

    stringstream ss;
    ss << config.RawDataDir << config.RunName << "/pax_info.json";
    ofstream fheader(ss.str(), ofstream::out);
    if (!fheader.is_open()) {
        BOOST_LOG_TRIVIAL(fatal) << "Could not open file header " << ss.str();
        throw DAQException();
    }
    fheader << bsoncxx::to_json(doc.view(), bsoncxx::ExtendedJsonMode::k_canonical);
    fheader.close();

    if (!m_bTestRun) {
        for (auto& x : m_vEventSizes) run_size_bytes += x;
        log_size = log(run_size_bytes)/log(2)/10;
        log_size = max(log_size, 0);
        sprintf(run_size, "%li%c", max(1l, run_size_bytes >> 10*log_size), sBlockSize[log_size]);
        sqlite3_bind_text(m_InsertStmt, m_BindIndex["name"], config.RunName.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(m_InsertStmt, m_BindIndex["start_time"], m_tStart.time_since_epoch().count());
        sqlite3_bind_int64(m_InsertStmt, m_BindIndex["end_time"], tEnd.time_since_epoch().count());
        sqlite3_bind_int64(m_InsertStmt, m_BindIndex["runtime"], chrono::duration_cast<chrono::duration<double>>(tEnd-m_tStart).count());
        sqlite3_bind_int(m_InsertStmt, m_BindIndex["events"], m_vEventSizes.size());
        sqlite3_bind_text(m_InsertStmt, m_BindIndex["source"], (config.IsZLE ? "none" : "LED"), -1, SQLITE_STATIC);
        sqlite3_bind_text(m_InsertStmt, m_BindIndex["raw_size"], run_size, -1, SQLITE_STATIC);
        sqlite3_bind_text(m_InsertStmt, m_BindIndex["comments"], m_sRunComment.c_str(), -1, SQLITE_STATIC);

        int rc = sqlite3_step(m_InsertStmt);
        if (rc != SQLITE_DONE) {
            BOOST_LOG_TRIVIAL(error) << "Couldn't add entry to runs databse, error code " << rc;
        } else BOOST_LOG_TRIVIAL(debug) << "Statement stepped";
        rc = sqlite3_reset(m_InsertStmt);
        if (rc != SQLITE_OK) {
            BOOST_LOG_TRIVIAL(error) << "Couldn't reset statement, error code " << rc;
        } else BOOST_LOG_TRIVIAL(debug) << "Statement reset";
        rc = sqlite3_clear_bindings(m_InsertStmt);
        if (rc != SQLITE_OK) {
            BOOST_LOG_TRIVIAL(error) << "Couldn't clear bindings, error code " << rc;
        } else BOOST_LOG_TRIVIAL(debug) << "Bindings cleared";
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
    digis.front()->StartAcquisition();
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

void DAQ::GetNewRunComment() {
    cout << "Enter new comment for run:\n";
    kb.deinit();
    getline(cin, m_sRunComment);
    kb.init();
    m_abSuppressOutput = false;
}

void DAQ::Readout() {
    cout << setbase(10) << flush;
    BOOST_LOG_TRIVIAL(info) << "Ready to go";
    cout << "Commands:"
              << " [s] Start/stop\n"
              << " [t] Force trigger\n"
              << " [w] Toggle writing events to disk\n"
              << " [T] Toggle automatic runs database interfacing\n"
              << " [c] Set run comment\n"
              << " [q] Quit\n";
    unsigned int iNumEvents(0), iBufferSize(0), iTotalBuffer(0), iTotalEvents(0);
    bool bTriggerNow(false), bQuit(false);
    auto PrevPrintTime = chrono::system_clock::now();
    chrono::system_clock::time_point ThisLoop;
    int FileRunTime(0), iLogReadSize(0), OutputWidth(60);
    char input('0');
    double dLoopTime(0);
    char sOutput[128];
    const string sBlockSize = " kMGT";
    const int iMaxLogSize = sBlockSize.size()-1;
    thread tCommentThread = thread(&DAQ::DoesNothing, this);
    kb.init();
    m_abRun = false;

    while (!bQuit) {
        if ((!m_abSuppressOutput) && (kb.kbhit())) {
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
                    if (m_abRun) {
                        BOOST_LOG_TRIVIAL(error) << "Please stop acquisition first with 's'";
                    } else {
                        m_abSaveWaveforms = !m_abSaveWaveforms;
                        BOOST_LOG_TRIVIAL(info) << "Writing to disk " << (m_abSaveWaveforms ? "en" : "dis") << "abled";
                    }
                    if (m_abSaveWaveforms) StartAcquisition();
                    break;
                case 'T' :
                    m_bTestRun = !m_bTestRun;
                    BOOST_LOG_TRIVIAL(info) << "Test run " << (m_bTestRun ? "en" : "dis") << "abled";
                    break;
                case 'q' :
                    if (m_abRun) StopAcquisition();
                    bQuit = true;
                    break;
                case 'c' :
                    if (tCommentThread.joinable()) tCommentThread.join();
                    m_abSuppressOutput = true;
                    tCommentThread = thread(&DAQ::GetNewRunComment, this);
                    break;
                default: break;
            }
            input = '0';
        }
        if (!m_abRun) {
            continue;
        }
        if (bTriggerNow) {
            BOOST_LOG_TRIVIAL(info) << "Triggering";
            digis.front()->SWTrigger();
            bTriggerNow = false;
        }
        // read from digitizer into buffer
        iNumEvents = 0;
        for (auto& dig : digis) {
            iNumEvents = dig->ReadBuffer(iBufferSize); // all digitizers should read same number of events, don't want to double-count
            iTotalBuffer += iBufferSize;
        }
        iTotalEvents += iNumEvents;
        if (iNumEvents > 0) AddEvents(buffers, iNumEvents);

        ThisLoop = chrono::system_clock::now();
        dLoopTime = chrono::duration_cast<chrono::duration<double>>(ThisLoop - PrevPrintTime).count();
        if ((dLoopTime > 1.0) && (!m_abSuppressOutput)) {
            iLogReadSize = log(iTotalBuffer)/log(2)/10;
            iLogReadSize = max(0, iLogReadSize);
            iLogReadSize = min(iLogReadSize, iMaxLogSize);
            FileRunTime = chrono::duration_cast<chrono::seconds>(ThisLoop - m_tStart).count();
            if (m_abSaveWaveforms) sprintf(sOutput, "\rStatus: %.1f %cB/s | %.1f Hz | %i sec | %i/%i | %i/%i ev |",
                                                    (iTotalBuffer >> (iLogReadSize*10))/dLoopTime,
                                                    sBlockSize[iLogReadSize],
                                                    iTotalEvents/dLoopTime,
                                                    FileRunTime,
                                                    m_iToDecode.load(),
                                                    m_iToWrite.load(),
                                                    m_aiEventsInCurrentFile.load(),
                                                    m_aiEventsInRun.load());
            else sprintf(sOutput, "\rStatus: %.1f %cB/s | %.1f Hz | %i sec | %i",
                                                    (iTotalBuffer >> (iLogReadSize*10))/dLoopTime,
                                                    sBlockSize[iLogReadSize],
                                                    iTotalEvents/dLoopTime,
                                                    FileRunTime,
                                                    m_iToDecode.load());
            cout << left << setw(OutputWidth) << sOutput << flush;
            iTotalBuffer = 0;
            iTotalEvents = 0;
            if ((FileRunTime >= m_fMaxFileRunTime) || (m_aiEventsInRun >= m_iMaxEventsInRun)) {
                StopAcquisition();
                StartAcquisition();
            }
            PrevPrintTime = chrono::system_clock::now();
        } // print loop

        if (s_interrupted) {
            StopAcquisition();
            bQuit = true;
        }
    } // run loop
    if (tCommentThread.joinable()) tCommentThread.join();
    kb.deinit();
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
                BOOST_LOG_TRIVIAL(warning) << "Deadtime warning";
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
        m_vBuffer[m_iDecodePtr].Decode();
        BOOST_LOG_TRIVIAL(debug) << "Event decoded at ptr " << m_iDecodePtr;
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
        char outfilename[256];

        if ((!m_abRunThreads) || (s_interrupted)) return;

        if (m_vFileInfos.back()[n_events] >= config.EventsPerFile) {
            fout.close();
            m_vFileInfos.push_back(file_info{0,0,0,0});
            sprintf(outfilename, "%s%s/%s_%06i.ast", config.RawDataDir.c_str(), config.RunName.c_str(), config.RunName.c_str(), int(m_vFileInfos.size()-1));
            fout.open(outfilename, ofstream::binary | ofstream::out);
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
        BOOST_LOG_TRIVIAL(debug) << "Event written at ptr " << m_iWritePtr;
        m_iWritePtr = (m_iWritePtr+1) % m_iBufferLength;
    }
}

void DAQ::ResetPointers() {
    m_iToDecode = 0;
    m_iToWrite = 0;
    m_iWritePtr.store(m_iInsertPtr.load());
    m_iDecodePtr.store(m_iInsertPtr.load());
}
