#pragma once
#include <string>
#include "infra/transport/uds/uds_client.hpp"

class CliApp{
public:
    CliApp();
    ~CliApp();
    bool open();
    int run(const std::string& cmd);
    void close();

    static void printLocalHelp();
    static void printLocalVersion();

private:
    void printResponse(const std::string& response);
    static bool shouldColor();

    UdsClient client_;
    std::string name_ = "Cli";
};
