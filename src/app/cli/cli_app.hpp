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

private:
    UdsClient client_;
    std::string name_ = "Cli";
};
