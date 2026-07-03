#include "app/cli/cli_app.hpp"
#include "core/common/log/log.hpp"
#include <iostream>
#include <string>

int main(int argc, char** argv){
    CliApp app;
    if(app.open()){ V2_LOG_ERROR("Cli App: failed to open");
        return 1;
    }
    return app.run(argc, argv);
}
