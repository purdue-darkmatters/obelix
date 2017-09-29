#include "DAQ.h"

#include <csignal>
#include <chrono>
#include <sstream>
#include <utility>

static int s_interrupted = 0;
static void s_signal_handler(int signal_value) {
    s_interrupted = 1;
}

static void s_catch_signals() {
    struct sigaction action;
    action.sa_handler = s_signal_handler;
    action.sa_flags = 0;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
}

DAQ::DAQ() {
    int rc = sqlite3_open_v2(runs_m_RunsDB_addr.c_str(), &m_RunsDB, SQLITE_OPEN_READWRITE, NULL);
    if (rc != SQLITE3_OK) {
        std::cout << "Could not connect to runs database\nSQLITE complains with error " << sqlite3_errmsg(m_RunsDB) << "\n";
        throw
    }
    m_WriteThread = std::thread(DoesNothing);
    m_DecodeThread[0] = std::thread(DoesNothing);
    m_DecodeThread[1] = std::thread(DoesNothing);
    m_tReadThread = std::thread(ReadInput);
    
    m_abQuit = false;
    m_abRun = false;
    m_abSaveWaveforms = false;
    m_abWriting = false;
    m_abTestRun = true;
    m_abTriggerNow = false;
    
    m_iEventsInActiveFile = 0;
}

DAQ::~DAQ() {
    m_tReadThread = std::thread(DoesNothing);
    if (m_tWriteThread.joinable()) m_tWriteThread.join();
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
    int ChannelMask(0);
    ChannelSettings_t ChanSet;
    GW_t GW;
    pt::ptree config_dict;
    pt::read_json(filename, config_dict);

    ConfigSettings_t CS;

    CS.RecordLength = config_dict.get<int>("record_length.value");
    CS.PostTrigger = config_dict.get<int>("post_trigger.value");
    CS.BlockTransfer = config_dict.get<int>("block_transfer.value");
    config.EventsPerFile = config_dict.get<int>("events_per_file.value");
    temp = config_dict.get<std::string>("is_zle.value");
    if (temp == "no") config.IsZLE = 0;
    else if (temp == "yes") config.IsZLE = 1;
    else std::cout << "Invalid ZLE config setting: " << temp << "\n";

    config.RawDataDir = config_dict.get<std::string>("raw_data_dir.value");
    link_number = config_dict.get<int>("digitizers.link_number");
    conet_node = config_dict.get<int>("digitizers.conet_node");
    base_address = config_dict.get<int>("digitizers.base_address");

    temp = config_dict.get<std::string>("external_trigger.value");
    if (temp == "acquisition_only") CS.ExtTriggerMode = CAEN_DGTZ_;
    else if (temp == "acquisition_and_trgout") CS.ExtTriggerMode = ;
    else if (temp == "disabled") CS.ExtTriggerMode = ;
    else if (temp == "trgout_only") CS.ExtTriggerMode = ;
    else std::cout << "Invalid external trigger value: " << temp << "\n";

    temp = config_dict.get<std::string>("channel_trigger.value");
    if (temp == "acquisition_only") CS.ChTriggerMode = ;
    else if (temp == "acquisition_and_trgout") CS.ChTriggerMode = ;
    else if (temp == "disabled") CS.ChTriggerMode = ;
    else std::cout << "Invalid channel trigger value: " << temp << "\n";

    for (pt::ptree:value_type& cs : config_dict.get_child("channels")) {
        ChanSet.Channel = cs.get<int>("channel");
        ChanSet.Enabled = cs.get<bool>("enabled");
        ChanSet.DCoffset = cs.get<int>("dc_offset");
        ChanSet.TriggerThreshold = cs.get<int>("trigger_threshold");
        ChanSet.TriggerMode = CS.ChTriggerMode;
        ChanSet.ZLEThreshold = cs.get<int>("zle_threshold");
        if (ChanSet.Enabled) CS.EnableMask |= (1 << ChanSet.Channel);
        CS.ChannelSettings.push_back(ChanSet);
    }

    for (pt::ptree::value_type& gw : config_dict.get_child("registers")) {
        temp = gw.get<std::string>("register");
        GW.addr = stoi(temp, nullptr, 16);
        temp = gw.get<std::string>("data");
        GW.data = stoi(temp, nullptr, 16);
        temp = gw.get<std::string>("mask");
        GW.mask = stoi(temp, nullptr, 16);
        CS.GenericWrites.push_back(GW);
    }

    dig.reset(new Digitizer(link_number, conet_node, base_address);

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
    std::string command = "mkdir " + config.RawDataDir + config.RunName;
    system(command.c_str());
    std::stringstream fullfilename;
    fullfilename << config.RawDataDir << config.RunName << "/" << config.RunName << "_" << std::setw(6) << std::setfill('0') << config.NumFiles << std::flush << ".ast";
    fout.open(fullfilename.str(), std::ofstream::binary | std::ofstream::out);
}

void DAQ::EndRun() {
    std::cout << "\nEnding run " << config.RunName << "\n";
    fout.close();
    tEnd = std::chrono::high_resolution_clock::now();
    stringstream command;
    char* errmsg(nullptr)
    command << "INSERT INTO runs (name, start_time, end_time, runtime, events, raw_status, source) VALUES ('" << config.RunName << "',"
        << m_tStart.time_since_epoch().count() << "," << tEnd.time_since_epoch().count() << ","
        << std::chrono::duration_cast<std::chrono::duration<double>>(tEnd-m_tStart) << "," << m_vEventSizes.size() << ",'acquired','"
        << (m_IsZLE ? "none" : "LED") << "');";
    int rc = sqlite3_exec(m_RunsDB, command.str().c_str(), nullptr, nullptr, &errmsg);
    if (rc != SQLITE3_OK) {
        cout << "Couldn't add entry to runs databse\nError: " << errmsg << "\n";
        sqlite3_free(errmsg);
    }
    
    pt::ptree file_header, channel_settings, chanset;

    file_header.put("events_per_file", config.EventsPerFile);
    file_header.put("is_zle", config.IsZLE);
    file_header.put("run_name", config.RunName);
    file_header.put("num_files", config.NumFiles+1);
    file_header.put("post_trigger", config.PostTrigger);
    file_header.put("num_events", m_vEventSizes.size());
    
    for (auto& cs : config.ChannelSettings) {
        chanset.put("channel", cs.Channel);
        chanset.put("enabled", cs.Enabled);
        chanset.put("dc_offset", cs.DCoffset);
        chanset.put("trigger_threshold", cs.TriggerThreshold);
        chanset.put("zle_threshold", cs.ZLEThreshold);
        
        channel_settings.put("",chanset);
        channel_settings.push_back(std::make_pair("",chanset));
    }
    
    file_header.add_child("channel_settings", channel_settings);
    stringstream ss;
    ss << config.RawDataDir << config.RunName << "/config.json";
    pt::write_json(ss.str().c_str(), file_header)
    
    m_vEventSizes.clear();
    m_iEventsInActiveFile = 0;
    config.NumFiles = 0;

    // Reset digitizer timestamps?
}

void DecodeEvents(const bool which, const unsigned int iNumEvents) {
    const unsigned int iSizeMask (0xFFFFFFF);
    const int iNumBytesHeader(16);
    // each digitizer has a buffer we need to process (only 1 digitizer right now), holding NumEvents
    std::vector<std::vector<unsigned int*> > vHeaders(iNumEvents, std::vector<unsigned int*>(1));
    std::vector<std::vector<unsigned int*> > vBodies(iNumEvents, std::vector<unsigned int*>(1));
    unsigned int* pHeader(nullptr);
    unsigned int* pBody(nullptr);
    m_vEvents[which].assign(iNumEvents, Event());
    int offset(0);
    unsigned int iWordsInThisEvent(0);
    for (int i = 0; i < iNumEvents; i++) {
        iWordsInThisEvent = iSizeMask & *(buffer[which] + offset);
        pHeader = (unsigned int*)(buffer[which] + offset);
        pBody = (unsigned int*)(buffer[which] + offset + iNumBytesHeader);
        vHeaders[i].push_back(pHeader);
        vBodies[i].push_back(pBody);
        // add headers and bodies from other buffers
        offset += iWordsInThisEvent * 4;
        m_vEvents[which][i].Decode(vHeaders[i], vBodies[i]);
    }
}

void WriteToDisk(const bool which) {
    m_abWriting = true;
    // add stuff here for new files
    for (auto& event : m_vEvents[which]) {
        if (m_iEventsInActiveFile >= config.EventsPerFile) {
            fout.close();
            config.NumFiles++;
            std::stringstream ss;
            ss << config.RawDataDir << config.RunName << "/" << config.RunName << "_" << std::setw(6) << std::setfill('0') << config.NumFiles << std::flush << ".ast";
            fout.open(ss.str(), std::ofstream::binary | std::ofstream::out);
            m_iEventsInActiveFile = 0;
        }
        vEventSizes.push_back(event.Write(fout));
        m_iEventsInActiveFile++;
    }
    m_abWriting = false;
}

void ReadInput() {
    char in;
    while (!m_abQuit) {
        cin >> in;
        switch (in) {
            case 's' : m_abRun = !m_abRun;
                       if (m_abRun) StartRun(); break;
            case 't' : m_abTriggerNow = true; break;
            case 'w' : m_abSaveWaveforms = !m_abSaveWaveforms;
                       std::cout << "Writing to disk " << (m_abSaveWaveforms ? "en" : "dis") << "abled\n"; break;
            case 'T' : m_abTestRun = !m_abTestRun;
                       std::cout << "Test run " << (m_abTestRun ? "en" : "dis") << "abled\n"; break;
            case 'q' : m_abQuit = true; break;
            default: break;
        }
        in = '\0';
    }
}

void Readout() {
    std::cout << "Ready to go.\n";
    std::cout << "Commands:"
              << " [s] Start/stop\n"
              << " [t] Force trigger\n"
              << " [w] Write events to disk (otherwise discard)\n"
              << " [T] En/disable automatic runs database interfacing\n"
              << " [q] Quit\n"
              << " [Return] submits input\n";
    unsigned int iNumEvents(0), iBufferSize(0), iTotalBuffer(0), iTotalEvents(0);
    bool bWhich(false); // which buffer to use
    auto PrevPrintTime = std::chrono::system_clock::now();
    std::chrono::system_clock::time_point ThisLoop;
    int FileRunTime;
    int OutputWidth(88);
    double dReadRate, dTrigRate, dLoopTime;

    while (!m_abQuit) {
        while (!m_abRun) {
            std::this_thread::yield();
        }
        if (m_abTriggerNow) {
            Digi->SWTrigger();
            m_abTriggerNow = false;
        }
        // read from digitizer
        iNumEvents = dig->ReadBuffer(bWhich, iBufferSize);
        iTotalBuffer += iBufferSize;
        if (m_abSaveWaveforms) {
            // decode events in one buffer
            if (m_tDecodeThread[bWhich].joinable()) m_tDecodeThread[bWhich].join();
            m_tDecodeEvents[bWhich] = std::thread(DecodeEvents, bWhich, iNumEvents);
            iTotalEvents += iNumEvents;

            // write to disk from the other
            if (m_abWriting) std::cout << "\nDeadtime warning: disk output is too slow\n";
            if (m_tWriteOutput.joinable()) m_tWriteOutput.join();
            m_tWriteOutput = std::thread(WriteToDisk, !bWhich);

            bWhich = !bWhich;
        }
        ThisLoop = std::chrono::system_clock::now();
        dLoopTime = std::chrono::duration_cast<std::chrono::duration<double>>(ThisLoop - PrevPrintTime).count();
        if (dLoopTime > 1.0) {
            dReadRate = (iTotalBuffer>>20)/dLoopTime; // MB/s
            dTrigRate = iTotalEvents/dLoopTime; // Hz
            FileRunTime = std::chrono::duration_cast<std::chrono::seconds> (ThisLoop - m_tStart).count();
            std::stringstream ss;
            ss << std::setprecision(3) << "\rStatus: " << dReadRate << " MB/s, " << dTrigRate << " Hz, "
               << std::setprecision(4) << FileRunTime << " sec, "
               << std::setprecision(6) << m_iEventsInActiveFile << "/" << m_vEventSizes.size() << " ev";
            std::cout << std::left << std::setw(55) << ss.str() << std::flush;
            std::cout << "| >>> " << std::flush;
            iTotalBuffer = 0;
            iTotalEvents = 0;
            if (FileRunTime >= 3600) {
                if (m_tWriteOutput.joinable()) m_tWriteOutput.join();
                EndRun();
                StartRun();
            }
            PrevLoop = std::chrono::system_clock::now();
        }
        if (s_interrupted) {
            m_abQuit = true;
        }
    }
}