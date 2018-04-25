#include "DAQ.h"
#include "boost/program_options.hpp"

namespace po = boost::program_options;

int main(int argc, char** argv) {
    const string version("1.3.2");
    const int default_buffer_size(1024);
    int buffer_length(0);
    string run_comment("");
    po::options_description general_options("Allowed arguments");
    general_options.add_options()
        ("help,h", "produce this message")
        ("config,c", po::value<string>(), "specify config file (requried)")
        ("version,v", "output current version and return")
        ("comment,C", po::value<string>()->default_value(run_comment), "specify comment for runs DB")
    ;

    po::options_description secret_options("Secret arguments");
    secret_options.add_options()
        ("buffer,b", po::value<int>()->default_value(default_buffer_size), "circular buffer size")
    ;

    po::options_description all_options("Allowed options");
    all_options.add(general_options).add(secret_options);
    po::variables_map vm;
    try {
        po::store(po::parse_command_line(argc, argv, all_options), vm);
    } catch (exception& e) {
        cout << e.what() << "\n";
        cout << general_options << "\n";
        return 1;
    }

    cout << "Welcome to Obelix!\n";
    if (vm.count("help")) {
        cout << general_options << "\n";
        return 0;
    }
    if (vm.count("version")) {
        cout << "Version " << version << "\n";
        return 0;
    }
    if (not vm.count("config")) {
        cout << "Config file required\n";
        return 1;
    }
    if (vm.count("buffer")) {
        buffer_length = vm["buffer"].as<int>();
    }
    if (vm.count("comment")) {
        run_comment = vm["comment"].as<string>();
    }
    const string& config_file = vm["config"].as<string>();
    unique_ptr<DAQ> daq;
    try {
        daq = unique_ptr<DAQ>(new DAQ(buffer_length));
        daq->SetRunComment(run_comment);
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
