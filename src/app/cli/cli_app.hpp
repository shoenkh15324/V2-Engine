#pragma once
#include <string>
#include "core/common/config/platform_config.h"
#include "core/common/config/runtime_config.h"

#if V2_PLATFORM_LINUX
    #include "infra/transport/uds/uds_client.hpp"
#endif

class CliApp{
public:
    CliApp();
    ~CliApp();

    CliApp(const CliApp&) = delete;
    CliApp& operator=(const CliApp&) = delete;
    CliApp(CliApp&&) = delete;
    CliApp& operator=(CliApp&&) = delete;

    int open();
    int run(const std::string& cmd);
    void close();

    static void printLocalHelp();
    static void printLocalVersion();
    static void printLocalStatus();
    static int launchMonitor(char** argv);

private:
    void printResponse(const std::string& response);
    static bool shouldColor();

    RuntimeConfig cfg_;
    std::string name_ = "Cli";

#if V2_PLATFORM_LINUX
    UdsClient client_;
#endif    
};
