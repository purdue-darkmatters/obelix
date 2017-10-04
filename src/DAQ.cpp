#include "DAQ.h"

#include <sstream>
#include <iomanip>
#include "mongo/db/json.h"
#include "mongo/bson/bson.h"
#include "kbhit.h"

DAQ::DAQ() {
    int rc = sqlite3_open_v2(runs_db_addr.c_str(), &m_RunsDB, SQLITE_OPEN_READWRITE, NULL);
    if (rc != SQLITE_OK) {
        std::cout << "Could not connect to runs database\nSQLITE complains with error " << sqlite3_errmsg(m_RunsDB) << "\n";
        throw DAQException();
    }
    m_WriteThread = std::thread(&DAQ::DoesNothing, this);
    m_DecodeThread[0] = std::thread(&DAQ::DoesNothing, this);
    m_DecodeThread[1] = std::thread(&DAQ::DoesNothing, this);

    m_abQuit = false;
    m_abRun = false;
    m_abSaveWaveforms = false;
    m_abWriting = false;
    m_abTestRun = true;
    m_abTriggerNow = false;

    m_iEventsInActiveFile = 0;
}

DAQ::~DAQ() {
    if (m_WriteThread.joinable()) m_WriteThread.join();
    for (auto& th : m_DecodeThread) if (th.joinable()) th.join();
    EndRun();
    dig.reset();
    sqlite3_close_v2(m_RunsDB);
    m_RunsDB = nullptr;
    std::cout << "\nShutting down DAQ\n";
}

void DAQ::Setup(const std::string& filename) {
    std::cout << "Parsing config file " << filename << "...\n";
    std::string temp;
    int link_number, conet_node, base_address;
    ChannelSettings_t ChanSet;
    GW_t GW;
    std::string json_string, str;
    std::ifstream fin(filename, std::ifstream::in);
    if (!fin.is_open()) {
        std::cout << "Could not open " << filename << "\n";
        throw DAQException();
    }
    while (getline(fin, str)) json_string += str;
    try {
        config_dict = mongo::fromjson(json_string);
    } catch (...) {
        std::cout << "Error parsing " << filename << ". Is it valid json?\n";
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
    else std::cout << "Invalid ZLE config setting: " << temp << "\n";

    config.RawDataDir = config_dict["raw_data_dir"]["value"].String();
    link_number = config_dict["digitizers"]["link_number"].Int();
    conet_node = config_dict["digitizers"]["conet_node"].Int();
    base_address = config_dict["digitizers"]["base_address"].Int();

    temp = config_dict["external_trigger"]["value"].String();
    if (temp == "acquisition_only") CS.ExtTriggerMode = CAEN_DGTZ_TRGMODE_ACQ_ONLY;
    else if (temp == "acquisition_and_trgout") CS.ExtTriggerMode = CAEN_DGTZ_TRGMODE_ACQ_AND_EXTOUT;
    else if (temp == "disabled") CS.ExtTriggerMode = CAEN_DGTZ_TRGMODE_DISABLED;
    else if (temp == "trgout_only") CS.ExtTriggerMode = CAEN_DGTZ_TRGMODE_EXTOUT_ONLY;
    else std::cout << "Invalid external trigger value: " << temp << "\n";

    temp = config_dict["channel_trigger"]["value"].String();
    if (temp == "acquisition_only") CS.ChTriggerMode = CAEN_DGTZ_TRGMODE_ACQ_ONLY;
    else if (temp == "acquisition_and_trgout") CS.ChTriggerMode = CAEN_DGTZ_TRGMODE_ACQ_AND_EXTOUT;
    else if (temp == "disabled") CS.ChTriggerMode = CAEN_DGTZ_TRGMODE_DISABLED;
    else std::cout << "Invalid channel trigger value: " << temp << "\n";

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
        GW.addr = std::stoi(gw["register"].String(), nullptr, 16);
        GW.data = std::stoi(gw["data"].String(), nullptr, 16);
        GW.mask = std::stoi(gw["mask"].String(), nullptr, 16);
        CS.GenericWrites.push_back(GW);
    }

    dig.reset(new Digitizer(link_number, conet_node, base_address));

    dig->ProgramDigitizer(CS);

    buffers[0] = dig->GetBuffer(0);
    buffers[1] = dig->GetBuffer(1);
}

void DAQ::StartRun() {
    m_tStart = std::chrono::high_resolution_clock::now(); // nanosecond precision!
    time_t rawtime;
    time(&rawtime);
    char temp[32];
    strftime(temp, sizeof(temp), "%Y%m%d_%H%M", localtime(&rawtime));
    config.RunName = temp;
    std::cout << "\nStarting run " << config.RunName << "\n";
    if (m_abSaveWaveforms) {
        std::string command = "mkdir " + config.RawDataDir + config.RunName;
        int ignore = system(command.c_str());
        ignore = 0;
        std::stringstream fullfilename;
        fullfilename << config.RawDataDir << config.RunName << "/" << config.RunName << "_" << std::setw(6) << std::setfill('0') << config.NumFiles << std::flush << ".ast";
        fout.open(fullfilename.str(), std::ofstream::binary | std::ofstream::out);
    }
}

void DAQ::EndRun() {
    std::cout << "\nEnding run " << config.RunName << "\n";
    if (fout.is_open()) fout.close();
    std::chrono::high_resolution_clock::time_point tEnd = std::chrono::high_resolution_clock::now();
    std::stringstream command;
    char* errmsg(nullptr);
    if (!m_abSaveWaveforms) {
        command << "INSERT INTO runs (name, start_time, end_time, runtime, events, raw_status, source) VALUES ('"
            << config.RunName << "'," << m_tStart.time_since_epoch().count() << "," << tEnd.time_since_epoch().count() << ","
            << std::chrono::duration_cast<std::chrono::duration<double>>(tEnd-m_tStart).count() << "," << m_vEventSizes.size()
            << ",'acquired','" << (config.IsZLE ? "none" : "LED") << "');";
        int rc = sqlite3_exec(m_RunsDB, command.str().c_str(), nullptr, nullptr, &errmsg);
        if (rc != SQLITE_OK) {
            std::cout << "Couldn't add entry to runs databse\nError: " << errmsg << "\n";
            sqlite3_free(errmsg);
        }
    }
    mongo::BSONObjBuilder builder;

    builder.append("events_per_file", config.EventsPerFile);
    builder.append("is_zle", config.IsZLE);
    builder.append("run_name", config.RunName);
    builder.append("num_files", config.NumFiles+1);
    builder.append("post_trigger", config.PostTrigger);
    builder.append("events", (int)m_vEventSizes.size());

    std::vector<mongo::BSONObj> chan_sets;
    for (auto& cs : config.ChannelSettings) {
        mongo::BSONObjBuilder ch;
        ch.append("channel", cs.Channel);
        ch.append("enabled", cs.Enabled);
        ch.append("dc_offset", cs.DCoffset);
        ch.append("trigger_threshold", cs.TriggerThreshold);
        ch.append("zle_threshold", cs.ZLEThreshold);

        chan_sets.push_back(ch.obj());
    }
    builder.append("channel_settings", chan_sets);

    builder.append("event_size_bytes", m_vEventSizes);

    std::stringstream ss;
    ss << config.RawDataDir << config.RunName << "/config.json";
    std::ofstream fheader(ss.str(), std::ofstream::out);
    if (!fheader.is_open()) {
        std::cout << "Could not open file header: " << ss.str() << "\n";
        throw DAQException();
    }
    fheader << tojson(builder.obj());
    fheader.close();

    m_vEventSizes.clear();
    m_iEventsInActiveFile = 0;
    config.NumFiles = 0;

    // Reset digitizer timestamps?
}

void DAQ::DecodeEvents(const bool which, const unsigned int iNumEvents) {
    const unsigned int iSizeMask (0xFFFFFFF);
    const int iNumBytesHeader(16);
    // each digitizer has a buffer we need to process (only 1 digitizer right now), holding NumEvents
    std::vector<std::vector<unsigned int*> > vHeaders(iNumEvents, std::vector<unsigned int*>());
    std::vector<std::vector<unsigned int*> > vBodies(iNumEvents, std::vector<unsigned int*>());
    unsigned int* pHeader(nullptr);
    unsigned int* pBody(nullptr);
    m_vEvents[which].assign(iNumEvents, Event());
    int offset(0);
    unsigned int iWordsInThisEvent(0);
    for (unsigned i = 0; i < iNumEvents; i++) {
        iWordsInThisEvent = iSizeMask & *(buffers[which] + offset);
        pHeader = (unsigned int*)(buffers[which] + offset);
        pBody = (unsigned int*)(buffers[which] + offset + iNumBytesHeader);
        vHeaders[i].push_back(pHeader);
        vBodies[i].push_back(pBody);
        // add headers and bodies from other buffers
        offset += iWordsInThisEvent * 4;
        m_vEvents[which][i].Decode(vHeaders[i], vBodies[i]);
    }
}

void DAQ::WriteToDisk(const bool which) {
    m_abWriting = true;
    int NumBytes(0);
    for (auto& event : m_vEvents[which]) {
        if (m_iEventsInActiveFile >= config.EventsPerFile) {
            fout.close();
            config.NumFiles++;
            std::stringstream ss;
            ss << config.RawDataDir << config.RunName << "/" << config.RunName << "_" << std::setw(6) << std::setfill('0') << config.NumFiles << std::flush << ".ast";
            fout.open(ss.str(), std::ofstream::binary | std::ofstream::out);
            m_iEventsInActiveFile = 0;
        }
        NumBytes = event.Write(fout);
        m_vEventSizes.push_back(NumBytes);
        m_iEventsInActiveFile++;
    }
    m_abWriting = false;
}

void DAQ::Readout() {
    std::cout << "Ready to go.\n";
    std::cout << "Commands:"
              << " [s] Start/stop\n"
              << " [t] Force trigger\n"
              << " [w] Write events to disk (otherwise discard)\n"
              << " [T] En/disable automatic runs database interfacing\n"
              << " [q] Quit\n";
    unsigned int iNumEvents(0), iBufferSize(0), iTotalBuffer(0), iTotalEvents(0);
    bool bWhich(false); // which buffer to use
    auto PrevPrintTime = std::chrono::system_clock::now();
    std::chrono::system_clock::time_point ThisLoop;
    int FileRunTime;
    int OutputWidth(55);
    double dReadRate, dTrigRate, dLoopTime;
    char input;

    while (!m_abQuit) {
        if (kbhit()) {
            std::cin.get(input);
            switch(input) {
                case 's' : m_abRun = !m_abRun;
                if (m_abRun) {
                    dig->StartAcquisition();
                    StartRun();
                } else {
                    dig->StopAcquisition();
                } break;
                case 't' : m_abTriggerNow = true; break;
                case 'w' : m_abSaveWaveforms = !m_abSaveWaveforms;
                    std::cout << "Writing to disk " << (m_abSaveWaveforms ? "en" : "dis") << "abled\n"; break;
                case 'T' : m_abTestRun = !m_abTestRun;
                    std::cout << "Test run " << (m_abTestRun ? "en" : "dis") << "abled\n"; break;
                case 'q' : m_abQuit = true; break;
                default: break;
            }
        }
        if (!m_abRun) {
            continue;
        }
        if (m_abTriggerNow) {
            std::cout << "\nTriggering\n";
            dig->SWTrigger();
            m_abTriggerNow = false;
        }
        // read from digitizer
        iNumEvents = dig->ReadBuffer(iBufferSize, bWhich);
        iTotalBuffer += iBufferSize;
        if (m_abSaveWaveforms) {
            // decode events in one buffer
            if (m_DecodeThread[bWhich].joinable()) m_DecodeThread[bWhich].join();
            m_DecodeThread[bWhich] = std::thread(&DAQ::DecodeEvents, this, bWhich, iNumEvents);
            iTotalEvents += iNumEvents;

            // write to disk from the other
            if (m_abWriting) std::cout << "\nDeadtime warning\n";
            if (m_WriteThread.joinable()) m_WriteThread.join();
            m_WriteThread = std::thread(&DAQ::WriteToDisk, this, !bWhich);

            bWhich = !bWhich;
        }
        ThisLoop = std::chrono::system_clock::now();
        dLoopTime = std::chrono::duration_cast<std::chrono::duration<double>>(ThisLoop - PrevPrintTime).count();
        if (dLoopTime > 1.0) {
            dReadRate = (iTotalBuffer>>20)/dLoopTime; // MB/s
            dTrigRate = iTotalEvents/dLoopTime; // Hz
            FileRunTime = std::chrono::duration_cast<std::chrono::seconds>(ThisLoop - m_tStart).count();
            std::stringstream ss;
            ss << std::setprecision(3) << "\rStatus: " << dReadRate << " MB/s, " << dTrigRate << " Hz, "
               << std::setprecision(4) << FileRunTime << " sec, "
               << std::setprecision(6) << m_iEventsInActiveFile << "/" << m_vEventSizes.size() << " ev";
            std::cout << std::left << std::setw(OutputWidth) << ss.str() << std::flush;
        //  std::cout << "| >>> " << std::flush;
            iTotalBuffer = 0;
            iTotalEvents = 0;
            if (FileRunTime >= 3600.) {
                if (m_WriteThread.joinable()) m_WriteThread.join();
                EndRun();
                StartRun();
            }
            PrevPrintTime = std::chrono::system_clock::now();
        }
//        break;
    }
}
