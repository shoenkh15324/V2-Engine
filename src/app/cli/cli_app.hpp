#pragma once
#include <string>
#include "infra/transport/uds/uds_client.hpp"
#include "core/common/config/runtime_config.h"

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
    UdsClient client_;
    std::string name_ = "Cli";
};
