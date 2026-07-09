#include "cli_app.hpp"
#include "core/common/config/platform_config.h"
#include "core/common/log/log.hpp"
#include "core/common/time/time.hpp"
#include "core/common/util/return.hpp"
#include <iostream>
#include <vector>
#include <cstdio>
#include <cstring>

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
    V2_LOG_INFO("%s App Version: %s", name_.c_str(), V2_ENGINE_VERSION);
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

int CliApp::run(int argc, char** argv){
#if V2_PLATFORM_LINUX
    V2_LOG_INFO("%s App Run", name_.c_str());
    if(argc < 2){ printLocalHelp(); return 0; }

    // 로컬에서 처리할 플래그 (v2 바로 다음에만 유효)
    if(strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "help") == 0){ printLocalHelp(); return 0; }
    if(strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "version") == 0){ printLocalVersion(); return 0; }
    if(strcmp(argv[1], "-s") == 0 || strcmp(argv[1], "--status") == 0 || strcmp(argv[1], "status") == 0){ printLocalStatus(); return 0; }
    if(strcmp(argv[1], "-m") == 0 || strcmp(argv[1], "--monitor") == 0 || strcmp(argv[1], "monitor") == 0){ return launchMonitor(argv); }

    // 그 외 모든 인자는 데몬으로 전달
    std::string cmd;
    for(int i = 1; i < argc; ++i){
        if (i > 1) cmd += " ";
        cmd += argv[i];
    }

    // 명령어 전송 (UDS)
    if(client_.send(cmd.data(), cmd.size()) != Ok){
        V2_LOG_ERROR("%s App: send failed", name_.c_str());
        return 1;
    }
    V2_LOG_INFO("%s App: sending command [%s]", name_.c_str(), cmd.c_str());

    // 응답 수신
    std::vector<char> buf(cfg_.ipcRecvBufferSize);
    int n = client_.recv(buf.data(), buf.size());
    if(n > 0){
        std::string resp(buf.data(), n);
        std::cout << resp << std::flush;
        V2_LOG_INFO("%s App: received response [%s]", name_.c_str(), resp.c_str());
    }
    return Ok;
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
        << CLR("\033[33m") << "  Usage:" << CLR("\033[0m") << " v2 <command> [options]\n"
        << "\n"
        << CLR("\033[33m") << "  Information:" << CLR("\033[0m") << "\n"
        << "    " << CLR("\033[32m") << "help, -h, --help" << CLR("\033[0m") << "                   Show this help message\n"
        << "    " << CLR("\033[32m") << "version, -v, --version" << CLR("\033[0m") << "             Show version\n"
        << "    " << CLR("\033[32m") << "status, -s, --status" << CLR("\033[0m") << "               Show daemon status\n"
        << "    " << CLR("\033[32m") << "info" << CLR("\033[0m") << "                               Show system information\n"
        << "\n"
        << CLR("\033[33m") << "  Monitoring:" << CLR("\033[0m") << "\n"
        << "    " << CLR("\033[32m") << "monitor, -m, --monitor" << CLR("\033[0m") << "             Open TUI monitor\n"
        << "\n"
        << CLR("\033[33m") << "  Actor Control:" << CLR("\033[0m") << "\n"
        << "    " << CLR("\033[32m") << "actor -l" << CLR("\033[0m") << "                           List actors\n"
        << "    " << CLR("\033[32m") << "actor -d <name>" << CLR("\033[0m") << "                    Disable actor\n"
        << "    " << CLR("\033[32m") << "actor -e <name>" << CLR("\033[0m") << "                    Enable actor\n"
        << "\n"
        << CLR("\033[33m") << "  Pmu:" << CLR("\033[0m") << "\n"
        << "    " << CLR("\033[32m") << "pmu -s" << CLR("\033[0m") << "                             Show Pmu Status\n"
        << "\n"
        << CLR("\033[33m") << "  Wifi:" << CLR("\033[0m") << "\n"
        << "    " << CLR("\033[32m") << "wifi scan" << CLR("\033[0m") << "                          Scan for access points\n"
        << "    " << CLR("\033[32m") << "wifi list, -l" << CLR("\033[0m") << "                      List scanned access points\n"
        << "    " << CLR("\033[32m") << "wifi connect <ssid> [pw], -c <ssid> [pw]" << CLR("\033[0m") << "  Connect to a network\n"
        << "    " << CLR("\033[32m") << "wifi disconnect, -d" << CLR("\033[0m") << "                  Disconnect current network\n"
        << "    " << CLR("\033[32m") << "wifi status, -s" << CLR("\033[0m") << "                     Show connection status\n"
        << "\n"
        << CLR("\033[33m") << "  Development:" << CLR("\033[0m") << "\n"
        << "    " << CLR("\033[32m") << "test [options]" << CLR("\033[0m") << "                     Test command parsing\n";
}

void CliApp::printLocalVersion(){
    std::cout << "V2 Engine:  v" << V2_ENGINE_VERSION << "\n";
}

void CliApp::printLocalStatus(){
#if V2_PLATFORM_LINUX
    std::string socketPath = RuntimeConfig{}.ipcSocketPath;
    bool sockExists = (access(socketPath.c_str(), F_OK) == 0);
    if(!sockExists){
        std::cout << "Daemon: " << CLR("\033[31m") << "stopped" << CLR("\033[0m") << "\n" << "    Socket: " << socketPath << " (not found)\n";
        return;
    }

    UdsClient probe;
    bool alive = (probe.connect(socketPath) == Ok);
    probe.shutdown();
    if(alive){
        std::cout << "Daemon: " << CLR("\033[32m") << "running" << CLR("\033[0m") << "\n";
    }else{
        std::cout << "Daemon: " << CLR("\033[31m") << "dead (stale socket)" << CLR("\033[0m") << "\n";
    }
    std::cout << "Socket: " << socketPath << "\n";
    FILE* fp = ::popen("pidof v2_main 2>/dev/null", "r");
    if(fp){
        char buf[16] = {};
        if(std::fgets(buf, sizeof(buf), fp)) std::cout << "PID: " << buf;
        ::pclose(fp);
    }
#else
    std::cout << "  V2 Engine \342\200\224 Status\n" << "    Daemon: not supported on Windows yet\n";
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

#undef CLR
