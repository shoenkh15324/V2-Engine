#pragma once
#include <string>
#include "core/common/util/platform_config.h"
#include "core/common/config/runtime_config.h"

#if V2_PLATFORM_LINUX
    #include "infra/transport/uds/uds_client.hpp"
#endif

class CliApp{
public:
    CliApp();
    ~CliApp();
    bool open();
    int run(const std::string& cmd);
    void close();

    static void printLocalHelp();
    static void printLocalVersion();
    static void printLocalStatus();

private:
    void printResponse(const std::string& response);
    static bool shouldColor();

    RuntimeConfig cfg_;
#if V2_PLATFORM_LINUX
    UdsClient client_;
#endif
    std::string name_ = "Cli";
};
