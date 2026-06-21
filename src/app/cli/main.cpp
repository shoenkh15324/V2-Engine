#include "app/cli/cli_app.hpp"
#include "core/common/log.hpp"
#include <iostream>

int main(int argc, char** argv){
    if(argc < 2){
        V2_LOG_ERROR("Cli App: no command provided");
        std::cerr << "Usage: v2_cli <command>" << std::endl;
        return 1;
    }

    CliApp app;
    if(!app.open()){
        V2_LOG_ERROR("Cli App: failed to open");
        return 1;
    }
    return app.run(argv[1]);
}
