#include "DAQ.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " config_file\n";
        return 1;
    }
    std::cout << "Welcome to Obelix!\n";
    std::string config_file = argv[1];
    std::unique_ptr<DAQ> daq = std::make_unique(new DAQ);
    daq->Setup(config_file);
    daq->Readout();
    daq.reset();
    std::cout << "Exiting...\n";
    return 0;
}