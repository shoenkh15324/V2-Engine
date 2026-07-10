#pragma once
#include <string>
#include <vector>
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
    int close();
    int run(int argc, char** argv);

private:
    struct SubDef{
        std::string name;
        std::string usage;
        std::string desc;
        std::vector<SubDef> children;
    };

    void printHelp(const std::string& sub = {}) const;
    std::string helpText(const SubDef* sub = nullptr) const;
    static void printVersion();
    static void printStatus();
    static int launchTui(char** argv);
    void sendToDaemon(const std::string& cmd);
    static int terminalWidth();

    RuntimeConfig cfg_;
    std::string appName_ = "Cli";
    std::vector<SubDef> subCmds_;

#if V2_PLATFORM_LINUX
    UdsClient client_;
#endif
};
