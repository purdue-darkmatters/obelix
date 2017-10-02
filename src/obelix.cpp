#include "DAQ.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " config_file\n";
        return 1;
    }
    std::cout << "Welcome to Obelix!\n";
    std::string config_file = argv[1];
    std::unique_ptr<DAQ> daq = std::unique_ptr<DAQ>(new DAQ);
    try {
        daq->Setup(config_file);
    } catch (std::exception& e) {
        std::cout << "Setup failed!\nError: " << e.what() << "\n";
        daq.reset();
        return 1;
    }
    try {
        daq->Readout();
    } catch (std::exception& e) {
        std::cout << "Runtime error!\nError: " << e.what() << "\n";
    }
    daq.reset();
    std::cout << "Exiting...\n";
    return 0;
}