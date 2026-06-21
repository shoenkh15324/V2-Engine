#include "app/cli/cli_app.hpp"
#include "core/common/log.hpp"
#include <iostream>
#include <string>

int main(int argc, char** argv){
    if(argc < 2){
        CliApp::printLocalHelp();
        return 0;
    }

    std::string cmd = argv[1];
    if(cmd == "--help" || cmd == "help"){
        CliApp::printLocalHelp();
        return 0;
    }
    if(cmd == "--version" || cmd == "version"){
        CliApp::printLocalVersion();
        return 0;
    }
    if(cmd == "--status" || cmd == "status"){
        CliApp::printLocalStatus();
        return 0;
    }

    CliApp app;
    if(!app.open()){
        V2_LOG_ERROR("Cli App: failed to open");
        return 1;
    }
    return app.run(cmd);
}
