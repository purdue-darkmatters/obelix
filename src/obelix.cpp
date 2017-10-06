#include "DAQ.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        cout << "Usage: " << argv[0] << " config_file\n";
        return 1;
    }
    cout << "Welcome to Obelix!\n";
    string config_file = argv[1];
    unique_ptr<DAQ> daq;
    try {
    	daq = unique_ptr<DAQ>(new DAQ);
    } catch (exception& e) {
    	cout << "Why did this fail?" << e.what() << "\n";
    	return 1;
    }
    try {
        daq->Setup(config_file);
    } catch (exception& e) {
        cout << "Setup failed!\nError: " << e.what() << "\n";
        daq.reset();
        return 1;
    }
    try {
        daq->Readout();
    } catch (exception& e) {
        cout << "Runtime error!\nError: " << e.what() << "\n";
    }
    daq.reset();
    cout << "Exiting...\n";
    return 0;
}
