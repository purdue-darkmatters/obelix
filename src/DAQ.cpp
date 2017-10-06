#include "DAQ.h"

#include <sstream>
#include <iomanip>
#include "mongo/db/json.h"
#include "mongo/bson/bson.h"
#include "kbhit.h"

DAQ::DAQ() {
    int rc = sqlite3_open_v2(runs_db_addr.c_str(), &m_RunsDB, SQLITE_OPEN_READWRITE, NULL);
    if (rc != SQLITE_OK) {
        cout << "Could not connect to runs database\nSQLITE complains with error " << sqlite3_errmsg(m_RunsDB) << "\n";
        throw DAQException();
    }
    m_WriteThread = thread(&DAQ::DoesNothing, this);
    m_DecodeThread[0] = thread(&DAQ::DoesNothing, this);
    m_DecodeThread[1] = thread(&DAQ::DoesNothing, this);

    m_bSaveWaveforms = false;
    m_abWriting = false;
    m_bTestRun = true;

    m_tStart = chrono::high_resolution_clock::now();
}

DAQ::~DAQ() {
    for (auto& th : m_DecodeThread) if (th.joinable()) th.join();
    if (m_WriteThread.joinable()) m_WriteThread.join();
    EndRun();
    dig.reset();
    sqlite3_close_v2(m_RunsDB);
    m_RunsDB = nullptr;
    cout << "\nShutting down DAQ\n";
}

void DAQ::Setup(const string& filename) {
    cout << "Parsing config file " << filename << "...\n";
    string temp;
    int link_number, conet_node, base_address;
    ChannelSettings_t ChanSet;
    GW_t GW;
    string json_string, str;
    ifstream fin(filename, ifstream::in);
    if (!fin.is_open()) {
        cout << "Could not open " << filename << "\n";
        throw DAQException();
    }
    while (getline(fin, str)) json_string += str;
    try {
        config_dict = mongo::fromjson(json_string);
    } catch (...) {
        cout << "Error parsing " << filename << ". Is it valid json?\n";
        throw DAQException();
    }
    // will need some more values in config json about board numbers
    ConfigSettings_t CS;
    CS.RecordLength = config_dict["record_length"]["value"].Int();
    CS.PostTrigger = config_dict["post_trigger"]["value"].Int();
    CS.BlockTransfer = config_dict["block_transfer"]["value"].Int();
    config.EventsPerFile = config_dict["events_per_file"]["value"].Int();
    temp = config_dict["is_zle"]["value"].String();
    if (temp == "no") config.IsZLE = 0;
    else if (temp == "yes") config.IsZLE = 1;
    else cout << "Invalid ZLE config setting: " << temp << "\n";

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

    for (auto& cs : config_dict["channels"].Array()) {
        ChanSet.Channel = cs["channel"].Int();
        ChanSet.Enabled = cs["enabled"].Int();
        ChanSet.DCoffset = (cs["dc_offset"].Int() + 50) * 65535/100;
        ChanSet.TriggerThreshold = cs["trigger_threshold"].Int();
        ChanSet.TriggerMode = CS.ChTriggerMode;
        ChanSet.ZLEThreshold = cs["zle_threshold"].Int();
        if (ChanSet.Enabled) CS.EnableMask |= (1 << ChanSet.Channel);
        CS.ChannelSettings.push_back(ChanSet);
        config.ChannelSettings.push_back(ChanSet);
    }

    for (auto& gw : config_dict["registers"].Array()) {
        GW.addr = stoi(gw["register"].String(), nullptr, 16);
        GW.data = stoi(gw["data"].String(), nullptr, 16);
        GW.mask = stoi(gw["mask"].String(), nullptr, 16);
        CS.GenericWrites.push_back(GW);
    }
    try {
        dig.reset(new Digitizer(link_number, conet_node, base_address));
    } catch (exception& e) {
        cout << "Could not allocate digitizer!\n" << e.what() << "\n";
        throw DAQException();
    }

    dig->ProgramDigitizer(CS);

    buffers[0] = dig->GetBuffer(0);
    buffers[1] = dig->GetBuffer(1);
}

void DAQ::StartRun() {
    if (fout.is_open()) return; // run already going
    m_tStart = chrono::high_resolution_clock::now(); // nanosecond precision!
    time_t rawtime;
    time(&rawtime);
    char temp[32];
    strftime(temp, sizeof(temp), "%Y%m%d_%H%M", localtime(&rawtime));
    config.RunName = temp;
    cout << "\nStarting run " << config.RunName << "\n";
    if (m_bSaveWaveforms) {
        string command = "mkdir " + config.RawDataDir + config.RunName;
        int ignore = system(command.c_str());
        ignore++;
        stringstream fullfilename;
        fullfilename << config.RawDataDir << config.RunName << "/" << config.RunName << "_" << setw(6) << setfill('0') << m_vFileInfos.size() << flush << ".ast";
        fout.open(fullfilename.str(), ofstream::binary | ofstream::out);
        if (!fout.is_open()) {
            cout << "Could not open " << fullfilename.str() << "\n";
            throw DAQException();
        }
        m_vFileInfos.push_back(file_info{0,0,0,0});
    }
}

void DAQ::EndRun() {
    cout << "\nEnding run " << config.RunName << "\n";
    if (fout.is_open()) fout.close();
    chrono::high_resolution_clock::time_point tEnd = chrono::high_resolution_clock::now();
    stringstream command;
    char* errmsg(nullptr);
    if (!m_bTestRun) {
        command << "INSERT INTO runs (name, start_time, end_time, runtime, events, raw_status, source) VALUES ('"
            << config.RunName << "'," << m_tStart.time_since_epoch().count() << "," << tEnd.time_since_epoch().count() << ","
            << chrono::duration_cast<chrono::duration<double>>(tEnd-m_tStart).count() << "," << m_vEventSizes.size()
            << ",'acquired','" << (config.IsZLE ? "none" : "LED") << "');";
        int rc = sqlite3_exec(m_RunsDB, command.str().c_str(), nullptr, nullptr, &errmsg);
        if (rc != SQLITE_OK) {
            cout << "Couldn't add entry to runs databse\nError: " << errmsg << "\n";
            sqlite3_free(errmsg);
        }
    }
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

    m_vEventSizes.clear();
    m_vFileInfos.clear();
    m_vEventSizeCum.clear();

    // Reset digitizer timestamps?
}

void DAQ::DecodeEvents(const bool which, const unsigned int iNumEvents) {
    const unsigned int iSizeMask (0xFFFFFF);
    const int iNumBytesHeader(4 * sizeof(WORD));
    // each digitizer has a buffer we need to process (only 1 digitizer right now), holding NumEvents
    vector<vector<WORD*> > vHeaders;
    vector<vector<WORD*> > vBodies;
    WORD* pHeader(nullptr);
    WORD* pBody(nullptr);
    m_vEvents[which].assign(iNumEvents, Event());
    int offset(0);
    unsigned int iWordsInThisEvent(0);
    for (unsigned i = 0; i < iNumEvents; i++) {
        vHeaders.push_back(vector<WORD*>());
        vBodies.push_back(vector<WORD*>());
        iWordsInThisEvent = iSizeMask & *(WORD*)(buffers[which] + offset);
        pHeader = (WORD*)(buffers[which] + offset);
        pBody = (WORD*)(buffers[which] + offset + iNumBytesHeader);
        vHeaders.back().push_back(pHeader);
        vBodies.back().push_back(pBody);
        // add headers and bodies from other buffers
        offset += iWordsInThisEvent * sizeof(WORD);
        m_vEvents[which][i].Decode(vHeaders.back(), vBodies.back());
    }
}

void DAQ::WriteToDisk(const bool which) {
    m_abWriting = true;
    int NumBytes(0);
    unsigned int EvNum(0);
    for (auto& event : m_vEvents[which]) {
        if (m_vFileInfos.back()[n_events] >= config.EventsPerFile) {
            fout.close();
            m_vFileInfos.push_back(file_info{});
            stringstream ss;
            ss << config.RawDataDir << config.RunName << "/" << config.RunName << "_" << setw(6) << setfill('0') << m_vFileInfos.size() << flush << ".ast";
            fout.open(ss.str(), ofstream::binary | ofstream::out);
            m_vFileInfos.back()[file_number] = m_vFileInfos.size();
        }
        NumBytes = event.Write(fout, EvNum);
        m_vEventSizes.push_back(NumBytes);

        if (m_vFileInfos.back()[n_events] == 0) {
            m_vFileInfos.back()[first_event] = EvNum;
            m_vEventSizeCum.push_back(0);
        } else if (m_vFileInfos.back()[n_events] == config.EventsPerFile-1) {
            m_vFileInfos.back()[last_event] = EvNum;
            m_vEventSizeCum.push_back(m_vEventSizeCum.back() + NumBytes);
        } else {
            m_vEventSizeCum.push_back(m_vEventSizeCum.back() + NumBytes);
        }

        m_vFileInfos.back()[n_events]++;
    }
    m_abWriting = false;
}

void DAQ::Readout() {
    cout << "Ready to go.\n";
    cout << "Commands:"
              << " [s] Start/stop\n"
              << " [t] Force trigger\n"
              << " [w] Write events to disk (otherwise discard)\n"
              << " [T] En/disable automatic runs database interfacing\n"
              << " [q] Quit\n";
    unsigned int iNumEvents(0), iBufferSize(0), iTotalBuffer(0), iTotalEvents(0);
    bool bWhich(false); // which buffer to use
    bool bTriggerNow(false), bQuit(false), bRun(false);
    auto PrevPrintTime = chrono::system_clock::now();
    chrono::system_clock::time_point ThisLoop;
    int FileRunTime(0);
    int OutputWidth(55);
    double dReadRate(0), dTrigRate(0), dLoopTime(0);
    char input('0');

    while (!bQuit) {
        if (kbhit()) {
            cin.get(input);
            switch(input) {
                case 's' : bRun = !bRun;
                if (bRun) {
                    dig->StartAcquisition();
                    if (m_bSaveWaveforms) StartRun();
                } else {
                    dig->StopAcquisition();
                } break;
                case 't' : bTriggerNow = true; break;
                case 'w' : m_bSaveWaveforms = !m_bSaveWaveforms;
                    cout << "Writing to disk " << (m_bSaveWaveforms ? "en" : "dis") << "abled\n";
                    if ((m_bSaveWaveforms) && (bRun)) StartRun();
                    break;
                case 'T' : m_bTestRun = !m_bTestRun;
                    cout << "Test run " << (m_bTestRun ? "en" : "dis") << "abled\n"; break;
                case 'q' : bQuit = true; break;
                default: break;
            }
            input = '0';
        }
        if (!bRun) {
            continue;
        }
        if (bTriggerNow) {
            cout << "Triggering\n";
            dig->SWTrigger();
            bTriggerNow = false;
        }

            // check to make sure buffer is available
        if (m_DecodeThread[bWhich].joinable()) m_DecodeThread[bWhich].join();

        // read from digitizer into open buffer
        iNumEvents = dig->ReadBuffer(iBufferSize, bWhich);
        if (iNumEvents > 0) m_DecodeThread[bWhich] = thread(&DAQ::DecodeEvents, this, bWhich, iNumEvents);

        iTotalBuffer += iBufferSize;
        iTotalEvents += iNumEvents;

        if (m_bSaveWaveforms) {
            // write to disk from the other
            //if (m_abWriting) cout << "\nDeadtime warning\n";
            if (m_WriteThread.joinable()) m_WriteThread.join();
            m_WriteThread = thread(&DAQ::WriteToDisk, this, !bWhich);
        }
        // switch buffers
        bWhich = !bWhich;

        ThisLoop = chrono::system_clock::now();
        dLoopTime = chrono::duration_cast<chrono::duration<double>>(ThisLoop - PrevPrintTime).count();
        if (dLoopTime > 1.0) {
            dReadRate = (iTotalBuffer>>20)/dLoopTime; // MB/s
            dTrigRate = iTotalEvents/dLoopTime; // Hz
            FileRunTime = chrono::duration_cast<chrono::seconds>(ThisLoop - m_tStart).count();
            stringstream ss;
            ss << setprecision(3) << "\rStatus: " << dReadRate << " MB/s, " << dTrigRate << " Hz, ";
            ss << setprecision(4) << FileRunTime << " sec, ";
            if (m_bSaveWaveforms) {
                ss << setprecision(6) << m_vFileInfos.back()[n_events] << "/" << m_vEventSizes.size() << " ev";
            }
            cout << left << setw(OutputWidth) << ss.str() << flush;
            iTotalBuffer = 0;
            iTotalEvents = 0;
            if (FileRunTime >= 3600.) {
                if (m_WriteThread.joinable()) m_WriteThread.join();
                EndRun();
                StartRun();
            }
            PrevPrintTime = chrono::system_clock::now();
        } // print loop
    } // run loop
} // Readout()
