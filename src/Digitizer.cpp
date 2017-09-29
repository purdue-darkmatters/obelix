#include "Digitizer.h"

Digitizer::Digitizer() {}

Digitizer::Digitizer(int LinkNumber, int ConetNode, int BaseAddress) : m_iNbit(14), m_fTs(10.0), m_iNch(8), {
    CAEN_DGTZ_ErrorCode_t ret = CAEN_DGTZ_OpenDigitizer(CAEN_DGTZ_OpticalLink, LinkNumber, ConetNode, BaseAddress, &m_iHandle);
    if (ret != CAEN_DGTZ_Success) {
        throw
    }
    CAEN_DGTZ_BoardInfo_t BoardInfo;

    ret = CAEN_DGTZ_GetInfo(m_iHandle, &BoardInfo);

    if (BoardInfo.Model != CAEN_DGTZ_V1724) {
        std::cout << "Connected to the wrong digitizer type!\n";
        throw
    }

}

Digitizer::~Digitizer() {
    CAEN_DGTZ_SWStopAcquisition(m_iHandle);
    for (auto& buf : buffer) CAEN_DGTZ_FreeReadoutBuffer(&buf);
    CAEN_DGTZ_ErrorCode_t ret = CAEN_DGTZ_CloseDigitizer(m_iHandle);
    if (ret != CAEN_DGTZ_Success) {
        std::cout << "Errors in board " << m_iHandle << " during shutdown\n";
    }
}

void Digitizer::Program(ConfigSettings_t& ConfigSettings) {
    CAEN_DGTZ_ErrorCode_t ret = CAEN_DGTZ_Success;
    unsigned int val(0), AllocSize(0);

    // start with reset
    ret += CAEN_DGTZ_Reset(m_iHandle);

    if (ret != CAEN_DGTZ_Success) {
        std::cout << "Error resetting digitizer " << m_iHandle << "\nPlease reset manually and restart\n";
        throw
    }

    ret += CAEN_DGTZ_SetRecordLength(m_iHandle, CS.RecordLength);
    ret += CAEN_DGTZ_GetRecordLength(m_iHandle, &val);
    if (val != CS.RecordLength)
        std::cout << "Board " << m_iHandle << ": desired " << CS.RecordLength << " record length, got " << val << " instead\n";

    ret += CAEN_DGTZ_SetPostTriggerSize(m_iHandle, CS.PostTrigger);
    ret += CAEN_DGTZ_GetPostTriggerSize(m_iHandle, &val);
    if (val != CS.PostTrigger)
        std::cout << "Board " << m_iHandle << ": desired " << CS.PostTrigger << " post trigger, got " << val << " instead\n";

    ret += CAEN_DGTZ_SetIOLevel(m_iHandle, CS.FPIOLevel);
    ret += CAEN_DGTZ_SetMaxNumEventsBLT(m_iHandle, CS.BlockTransfer);
    ret += CAEN_DGTZ_SetAcquisitionMode(m_iHandle, CAEN_DGTZ_SW_CONTROLLED);
    ret += CAEN_DGTZ_SetExtTriggerInputMode(m_iHandle, CS.ExtTriggerMode);

    ret += CAEN_DGTZ_SetChannelEnableMask(m_iHandle, CS.EnableMask);
    ret += CAEN_DGTZ_SetChannelSelfTrigger(m_iHandle, CS.ChTriggerMode, 0xFF);

    for (auto& ch_set : CS.ChannelSettings) {
        if (!ch_set.Enabled)
            continue;
        ret += CAEN_DGTZ_SetChannelDCOffset(m_iHandle, ch_set.Channel, ch_set.DCoffset);
        ret += CAEN_DGTZ_SetChannleTriggerThreshold(m_iHandle, ch_set.Channel, ch_set.TriggerThreshold);
        ret += CAEN_DGTZ_SetTriggerPolarity(m_iHandle, ch_set.Channel, CAEN_DGTZ_TriggerOnFallingEdge);
        ret += WriteRegister(GW_t{CAEN_DGTZ_CHANNEL_ZS_THRESHOLD_BASE_ADDRESS + (0x100 * ch_set.Channel),
                                  (1 << 31) + ch_set.ZLEThreshold,
                                  THRESHOLD_MASK});
    }

    for (auto& GW : CS.GenericWrites)
        ret += WriteRegister(GW);

    if (ret != CAEN_DGTZ_Success)
        std::cout << "Board " << m_iHandle << " reports some errors in programming\n";

    ret = CAEN_DGTZ_Success;
    buffers.resize(CS.NumBuffers);
    for (auto& buffer : buffers) ret += CAEN_DGTZ_MallocReadoutBuffer(m_iHandle, &buffer, &AllocSize);

    if (ret != CAEN_DGTZ_Success) {
        std::cout << "Board " << m_iHandle << " unable to alloc readout buffers\n";
        throw
    }
    std::cout << "Board " << m_iHandle << " ready\n";
}

unsigned int Digitizer::ReadBuffer(unsigned int& BufferSize, int which) {
    unsigned int NumEvents(0);
    CAEN_DGTZ_ErrorCode_t ret = CAEN_DGTZ_Success;
    ret = CAEN_DGTZ_ReadData(m_iHandle, CAEN_DGTZ_SLAVE_TERMINATED_READOUT_MBLT, buffer, &BufferSize);
    if (ret != CAEN_DGTZ_Success) {
        std::cout << "Readout error from board " << m_iHandle << "\n";
        throw
    }
    if (BufferSize != 0) {
        ret = CAEN_DGTZ_GetNumEvents(m_iHandle, buffer, BufferSize, &NumEvents);
        if (ret != CAEN_DGTZ_Success) {
            std::cout << "Readout error on board " << m_iHandle << "\n";
            throw
        }
    }

    return NumEvents;
}

CAEN_DGTZ_ErrorCode_t Digitizer::WriteRegister(GW_t& GW) {
    unsigned int temp = 0;
    CAEN_DGTZ_ErrorCode_t ret = CAEN_DGTZ_ReadRegister(m_iHandle, GW.addr, &temp);
    if (ret != CAEN_DGTZ_Success)
        return ret;

    temp &= ~GW.mask;
    temp |= GW.data;
    ret = CAEN_DGTZ_WriteRegister(m_iHandle, GW.addr, temp);
    return ret;
}

void Digitizer::DetermineBaselines() {
    // 
}


int Digitizer::DetermineBaselines()
//Rewrite of baseline routine from Marc S
//Updates to C++, makes compatible with new FW
{
    // First thing's first: Reset the board
    WriteReg32(CBV1724_BoardResetReg, 0x1);
    
    // If there are old baselines we can use them as a starting point
    vector <int> DACValues;
    //if(GetBaselines(DACValues,true)!=0) {
    DACValues.resize(8,0xFFFF - fIdealBaseline);
    //}
    
    //Load the old baselines into the board
    if(LoadDAC(DACValues)!=0) {
        LogError("Can't load to DAC!");
        return -1;
    }
    
    //Get the firmware revision (for data formats)
    if(InitForPreProcessing()!=0){
        LogError("Can't load registers for baselines!");
        return -1;
    }
    //u_int32_t datasize = ( 524288 );  // 4byte/word, 8ch/digi, 16byte head
    u_int32_t fwRev=0;
    ReadReg32(0x118C,fwRev);
    int fwVERSION = ((fwRev>>8)&0xFF); //0 for old FW, 137 for new FW   
    LogMessage("Baselines for firmware version " + koHelper::IntToString(fwVERSION) );
    
    //Do the magic
    double idealBaseline = (double)fIdealBaseline;
    double maxDev = 5.;
    //vector<bool> channelFinished(8,false);
    vector<int> channelFinished(8, 0);
    
    int maxIterations = 1000;
    int currentIteration = 0;
    
    while(currentIteration<=maxIterations){
        stringstream message;
        message << "Baseline iteration " << currentIteration << "/" << maxIterations << endl;
        LogMessage(message.str());
        currentIteration++;
        
        //get out if all channels done
        bool getOut=true;
        for(unsigned int x=0;x<channelFinished.size();x++){
            //if(channelFinished[x]==false) getOut=false;
            if(channelFinished[x]<5) getOut=false;
        }
        if(getOut) break;
        
        // Enable to board
        WriteReg32(CBV1724_AcquisitionControlReg,0x24);
        //usleep(5000); //
        //Set Software Trigger
        WriteReg32(CBV1724_SoftwareTriggerReg,0x1);
        usleep(50); //
        
        //Disable the board
        WriteReg32(CBV1724_AcquisitionControlReg,0x0);
        //usleep(5000);
        
        //Read the data                    
        unsigned int readout = 0, thisread =0, counter=0;
        do{
            thisread = 0;
            thisread = ReadMBLT();
            readout+=thisread;
            usleep(10);
            counter++;
        } while( counter < 1000 && (readout == 0 || thisread != 0));
        // Either the timer times out or the readout is non zero but 
        //the current read is finished  
        if(readout == 0){
            LogError("Read failed in baseline function on iteration" + to_string(currentIteration) + ".");
            continue;
            //return -1;
        }
        
        // Use main kodiaq parsing     
        unsigned int rc=0;
        u_int32_t ht=0;
        vector <u_int32_t> *dsizes;
        LockDataBuffer();
        vector<u_int32_t*> *buff= ReadoutBuffer(dsizes, rc, ht);
        
        vector <u_int32_t> *dchannels = new vector<u_int32_t>;
        vector <u_int32_t> *dtimes = new vector<u_int32_t>;
        
        bool berr; string serr;
        if(fwVERSION!=0)
            DataProcessor::SplitChannelsNewFW(buff,dsizes,
                                              dtimes,dchannels,berr,serr);
            else
                DataProcessor::SplitChannels(buff,dsizes,dtimes,dchannels,NULL,false);
            
            
            //loop through channels
            for(unsigned int x=0;x<dchannels->size();x++){
                if(channelFinished[(*dchannels)[x]]>=5 || (*dsizes)[x]==0) {
                    delete[] (*buff)[x];
                    continue;
                }
                
                //compute baseline
                double baseline=0.,bdiv=0.;
                int maxval=-1,minval=17000;
                
                // Loop through data
                for(unsigned int y=0;y<(*dsizes)[x]/4;y++){
                    // Second loop for first/second sample in word
                    for(int z=0;z<2;z++){
                        int dbase=0;
                        if(z==0) 
                            dbase=(((*buff)[x][y])&0xFFFF);
                        else 
                            dbase=(((*buff)[x][y]>>16)&0xFFFF);
                        if(dbase == 0 || dbase == 4) 
                            continue;
                        baseline+=dbase;
                        bdiv+=1.;
                        if(dbase>maxval) 
                            maxval=dbase;
                        if(dbase<minval) 
                            minval=dbase;
                    }      
                }
                baseline/=bdiv;
                if(abs(maxval-minval) > 150) {
                    //stringstream error;
                    //error<<"Channel "<<(*dchannels)[x]<<" signal in baseline?";
                    //LogMessage( error.str() );    
                    stringstream message;
                    message<<"maxval - minval for about " <<
                    fBID.id << "." << (*dchannels)[x] << " is " << 
                    abs(maxval-minval) << ", max " << 
                    maxval << " min " << minval <<
                    " maybe there's a signal in the baseline." <<
                    " Event length " << (*dsizes)[x]/4 << " words.";
                    LogMessage(message.str());
                    delete[] (*buff)[x];
                    continue; //signal in baseline?
                }
                
                // shooting for 16000. best is if we UNDERshoot 
                // and then can more accurately adjust DAC
                double discrepancy = baseline-idealBaseline;      
                //LogMessage("Discrepancy is " + koHelper::IntToString(discrepancy));
                if(abs(discrepancy)<=maxDev) { 
                    
                    if(channelFinished[(*dchannels)[x]]>=5){
                        stringstream message;
                        message<<"Board "<<fBID.id<< " Channel "<< (*dchannels)[x]
                        <<" finished with value "<<baseline
                        <<" discrepancy: "<<discrepancy<<" and value "
                        <<DACValues[(*dchannels)[x]]<<endl;
                        LogMessage(message.str());
                    }
                    
                    //channelFinished[(*dchannels)[x]]=true;
                    channelFinished[(*dchannels)[x]]+=1;
                    delete[] (*buff)[x];
                    continue;
                }
                channelFinished[(*dchannels)[x]]=0;
                
                
                // Have a range of 0xFFFF
                u_int32_t offset = 1000;
                if(abs(discrepancy)<1000)
                    offset = fabs(discrepancy);
                if(abs(discrepancy) < 500)
                    offset = 100;
                if(abs(discrepancy) < 100)
                    offset = 10;
                if(abs(discrepancy) < 50)
                    offset = 5;
                if(abs(discrepancy) < 20)
                    offset = 2;
                if(abs(discrepancy) < 5)
                    offset = 1;
                
                if(discrepancy < 0)
                    DACValues[(*dchannels)[x]] -= offset;
                else 
                    DACValues[(*dchannels)[x]] += offset;
                
                // Check out of bounds
                if(DACValues[(*dchannels)[x]] <= 0)
                    DACValues[(*dchannels)[x]] = 0x0;
                if(DACValues[(*dchannels)[x]] >= 0xFFFF)
                    DACValues[(*dchannels)[x]] = 0xFFFF;
                
                delete[] (*buff)[x];
            } //end loop through channels
            LoadDAC(DACValues);
            
            delete buff;
            delete dsizes;
            delete dchannels;
            delete dtimes;
            
    }//end while through iterations
    
    //write baselines to file
    ofstream outfile;
    stringstream filename; 
    filename<<"baselines/XeBaselines_"<<fBID.id<<".ini";                         
    outfile.open(filename.str().c_str());
    outfile<<koHelper::CurrentTimeInt()<<endl;
    for(unsigned int x=0;x<DACValues.size();x++)  {
        outfile<<x+1<<"  "<<hex<<setw(4)<<setfill('0')<<
        ((DACValues[x])&0xFFFF)<<endl;                  
    }
    outfile.close();  
    
    int retval=0;
    for(unsigned int x=0;x<channelFinished.size();x++){
        //if(channelFinished[x]=false) {
        if(channelFinished[x]<5){
            stringstream errstream;
            errstream<<"Didn't finish channel "<<x;
            LogError(errstream.str());
            retval=-1;
        }
    }
    
    return retval;
    
}
