#include "cli_app.hpp"
#include "core/common/util/platform_config.h"
#include "core/common/log/log.hpp"
#include "core/common/time/time.hpp"
#include "core/common/util/return.hpp"
#include <iostream>
#include <sstream>
#include <vector>
#include <cstdio>

#if V2_PLATFORM_LINUX
    #include <unistd.h>
#endif

#define CLR(c) (shouldColor() ? (c) : "")

CliApp::CliApp(){}

CliApp::~CliApp(){
    close();
}

int CliApp::open(){
    cfg_ = RuntimeConfig::loadFromFile(V2_CONFIG_DIR "/v2_cli.json");
    setLogLevel(static_cast<LogLevel>(cfg_.logLevel));
    setLogAppName(std::move(name_));
    V2_LOG_INFO("%s App Open", name_.c_str());
    V2_LOG_INFO("%s App Bulid Data: %s", name_.c_str(), Time::nowDateString().c_str());
    V2_LOG_INFO("%s App Version: %s", name_.c_str(), V2_APP_VERSION);
    //
#if V2_PLATFORM_LINUX
    if(client_.connect(cfg_.ipcSocketPath) != Ok){ V2_LOG_ERROR("%s App: failed to connect to main app", name_.c_str());
        return Fail;
    }
    V2_LOG_INFO("%s App: connected to main app", name_.c_str());
#else
    V2_LOG_ERROR("%s App: CLI not supported on Windows yet", name_.c_str());
    return Fail;
#endif
    //
    return Ok;
}

int CliApp::run(const std::string& cmd){
#if V2_PLATFORM_LINUX
    V2_LOG_INFO("%s App Run", name_.c_str());
    V2_LOG_INFO("%s App: sending command [%s]", name_.c_str(), cmd.c_str());

    if(client_.send(cmd.data(), cmd.size()) != Ok){
        V2_LOG_ERROR("%s App: send failed", name_.c_str());
        return 1;
    }

    std::vector<char> buf(cfg_.ipcRecvBufferSize);
    int n = client_.recv(buf.data(), buf.size());
    if(n > 0){
        std::string resp(buf.data(), n);
        printResponse(resp);
        V2_LOG_INFO("%s App: received response [%s]", name_.c_str(), resp.c_str());
    }
    return 0;
#else
    (void)cmd;
    return 1;
#endif
}

void CliApp::close(){
#if V2_PLATFORM_LINUX
    client_.shutdown();
#endif
    V2_LOG_INFO("%s App Close", name_.c_str());
}

bool CliApp::shouldColor(){
#if V2_PLATFORM_LINUX
    static bool color = isatty(STDOUT_FILENO);
    return color;
#else
    return false;
#endif
}

void CliApp::printLocalHelp(){
    std::cout
        << CLR("\033[1m\033[36m") << "  V2 Engine \342\200\224 Control Interface" << CLR("\033[0m") << "\n"
        << CLR("\033[33m") << "  Usage:" << CLR("\033[0m") << " v2 <command>\n"
        << "\n"
        << CLR("\033[33m") << "  Commands:" << CLR("\033[0m") << "\n"
        << "    " << CLR("\033[32m") << "help" << CLR("\033[0m") << "      Show this help message\n"
        << "    " << CLR("\033[32m") << "info" << CLR("\033[0m") << "      Show system information\n"
        << "    " << CLR("\033[32m") << "version" << CLR("\033[0m") << "   Show version information\n"
        << "    " << CLR("\033[32m") << "status" << CLR("\033[0m") << "    Show daemon status\n"
        << "    " << CLR("\033[32m") << "monitor" << CLR("\033[0m") << "   Open TUI monitor\n";
}

void CliApp::printLocalVersion(){
    std::cout << CLR("\033[36m\033[1m") << "  V2 Engine " << CLR("\033[0m") << V2_APP_VERSION << "\n";
}

void CliApp::printLocalStatus(){
#if V2_PLATFORM_LINUX
    std::string socketPath = RuntimeConfig{}.ipcSocketPath;
    bool sockExists = (access(socketPath.c_str(), F_OK) == 0);

    std::cout << CLR("\033[36m\033[1m") << "  V2 Engine \342\200\224 Status" << CLR("\033[0m") << "\n";

    if(!sockExists){
        std::cout << "    Daemon: " << CLR("\033[31m") << "stopped" << CLR("\033[0m") << "\n"
                  << "    Socket: " << socketPath << " (not found)\n";
        return;
    }

    UdsClient probe;
    bool alive = (probe.connect(socketPath) == Ok);
    probe.shutdown();

    if(alive){
        std::cout << "    Daemon: " << CLR("\033[32m") << "running" << CLR("\033[0m") << "\n";
    }else{
        std::cout << "    Daemon: " << CLR("\033[31m") << "dead (stale socket)" << CLR("\033[0m") << "\n";
    }

    std::cout << "    Socket: " << socketPath << "\n";

    FILE* fp = ::popen("pidof v2_main 2>/dev/null", "r");
    if(fp){
        char buf[16] = {};
        if(std::fgets(buf, sizeof(buf), fp)){
            std::cout << "    PID:    " << buf;
        }
        ::pclose(fp);
    }
#else
    std::cout << "  V2 Engine \342\200\224 Status\n"
              << "    Daemon: not supported on Windows yet\n";
#endif
}

int CliApp::launchMonitor(char** argv){
#if V2_PLATFORM_LINUX
    std::string self = argv[0];
    auto seq = self.rfind('/');
    if(seq != std::string::npos){
        std::string tuiPath = self.substr(0, seq + 1) + "v2_tui";
        execv(tuiPath.c_str(), argv);
    }
    execvp("v2_tui", argv);
#endif
    V2_LOG_ERROR("Cli App: failed to launch v2_tui");
    std::cerr << "Error: failed to launch v2_tui (is it installed?)\n";
    return Ok;
}

void CliApp::printResponse(const std::string& resp){
    std::istringstream stream(resp);
    std::string line;
    bool first = true;

    while(std::getline(stream, line)){
        if(line.empty()) continue;

        auto colon = line.find(':');
        if(colon == std::string::npos){
            std::cout << "  " << line << CLR("\033[0m") << "\n";
            continue;
        }

        std::string key = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        if(!value.empty() && value[0] == ' ') value.erase(0, 1);

        if(key == "uptime"){
            auto ms = std::stoll(value);
            int s = static_cast<int>(ms / 1000);
            int m = s / 60; s %= 60;
            int h = m / 60; m %= 60;
            int d = h / 24; h %= 24;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%dd %02dh %02dm %02ds", d, h, m, s);
            value = buf;
        }

        if(key == "error"){
            std::cout << CLR("\033[31m") << "  Error: " << CLR("\033[0m") << value << "\n";
        }else if(first){
            std::cout << CLR("\033[36m\033[1m") << "  \342\226\272 " << value << CLR("\033[0m") << "\n";
            first = false;
        }else{
            std::cout << "    " << CLR("\033[33m") << key << CLR("\033[0m") << ": " << value << "\n";
        }
    }
    std::cout << std::flush;
}

#undef CLR
