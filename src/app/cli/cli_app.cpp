#include "cli_app.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include "core/common/log/log.hpp"
#include "core/common/time/time.hpp"
#include "core/common/util/return.hpp"

#if V2_PLATFORM_LINUX
    #include <sys/ioctl.h>
    #include <unistd.h>
#endif

CliApp::CliApp()
    : subs_{
        {"actor", "Actor management", {
            {"list", "List actors"},
            {"enable", "Enable actor"},
            {"disable", "Disable actor"},
        }},
        {"wifi", "Wi-Fi management", {
            {"scan", "Scan access points"},
            {"list", "List scanned APs"},
            {"connect", "Connect to a network"},
            {"disconnect", "Disconnect current network"},
            {"status", "Show connection status"},
        }},
        {"pmu", "PMU operations", {
            {"status", "Show PMU status"},
        }},
        {"test", "Test command parsing"},
    }
{}

CliApp::~CliApp(){
    close();
}

int CliApp::open(){
    cfg_ = RuntimeConfig::loadFromFile(V2_CONFIG_DIR "/v2_cli.json");
    setLogLevel(static_cast<LogLevel>(cfg_.logLevel));
    setLogAppName(appName_);
    V2_LOG_INFO("%s App Open", appName_.c_str());
    V2_LOG_INFO("%s App Build Data: %s", appName_.c_str(), Time::nowDateString().c_str());
    V2_LOG_INFO("%s App Version: %s", appName_.c_str(), V2_ENGINE_VERSION);
#if V2_PLATFORM_LINUX
    if(client_.connect(cfg_.ipcSocketPath) != Ok){ V2_LOG_ERROR("%s App: failed to connect to main app", appName_.c_str());
        return Fail;
    }
    V2_LOG_INFO("%s App: connected to main app", appName_.c_str());
#else
    V2_LOG_ERROR("%s App: CLI not supported on Windows yet", appName_.c_str());
    return Fail;
#endif
    return Ok;
}

int CliApp::close(){
#if V2_PLATFORM_LINUX
    client_.shutdown();
#endif
    V2_LOG_INFO("%s App Close", appName_.c_str());
    return Ok;
}

int CliApp::run(int argc, char** argv){
    if(argc < 2){
        std::cout << helpText();
        return 0;
    }
    if(std::string(argv[1]) == "help"){
        if(argc > 2) printHelp(argv[2]);
        else printHelp();
        return 0;
    }

    std::string sub;
    std::vector<std::string> args;
    bool helpSeen = false;

    for(int i = 1; i < argc; ++i){
        std::string s = argv[i];
        if(s == "-h" || s == "--help")   { helpSeen = true; continue; }
        if(s == "-v" || s == "--version"){ printVersion(); return 0; }
        if(s == "-s" || s == "--status") { printStatus(); return 0; }
        if(s == "-m" || s == "--monitor"){ return launchTui(argv); }

        if(sub.empty() && s[0] != '-'){
            sub = s;
            continue;
        }
        if(!sub.empty() && (s == "-h" || s == "--help")){
            printHelp(sub);
            return 0;
        }
        if(s[0] == '-' && sub.empty()){
            std::cerr << "Unknown option: " << s << "\n";
            return 1;
        }
        args.push_back(s);
    }

    if(helpSeen){
        if(sub.empty()){
            printHelp();
        }else{
            printHelp(sub);
        }
        return 0;
    }
    if(sub == "version"){ printVersion(); return 0; }
    if(sub == "status") { printStatus(); return 0; }
    if(sub == "monitor"){ return launchTui(argv); }

    std::string cmd;
    if(!sub.empty()){
        cmd = sub;
        for(auto& a : args)
            cmd += " " + a;
    }else{
        for(size_t i = 0; i < args.size(); ++i){
            if(i > 0) cmd += " ";
            cmd += args[i];
        }
    }
    if(!cmd.empty()) sendToDaemon(cmd);
    return 0;
}

// ── Help text ──────────────────────────────────────────────────

std::string CliApp::helpText(const SubDef* sub) const {
    int tw = terminalWidth();
    int cw = std::clamp(tw * 2 / 5, 20, 50);
    std::stringstream out;

    if(sub){
        out << sub->desc << "\n";
    }else{
        out << "V2 Engine CLI\n";
    }

    out << "Usage: v2";
    if(sub) out << " " << sub->name;
    out << " [OPTIONS]";
    if((sub && !sub->children.empty()) || (!sub && !subs_.empty())) out << " [SUBCOMMANDS]";
    out << "\n";
    out << "\nOPTIONS:\n";

    auto optLine = [&](const std::string& name, const std::string& desc){
        out << "  " << std::left << std::setw(cw) << name << "  " << desc << "\n";
    };

    optLine("-h, --help", "Print this help message and exit");
    if(!sub){
        optLine("-v, --version", "Show version information");
        optLine("-s, --status", "Show daemon connection status");
        optLine("-m, --monitor", "Open TUI monitor application");
    }

    const auto& items = sub ? sub->children : subs_;
    if(!items.empty()){
        out << "\nSubcommands:\n";
        for(auto& item : items) {
            out << "  " << std::left << std::setw(cw) << item.name << "  " << item.desc << "\n";
        }
    }
    return out.str();
}

void CliApp::printHelp(const std::string& sub) const {
    if(sub.empty()){
        std::cout << helpText();
        return;
    }
    for(auto& s : subs_){
        if(s.name == sub){
            std::cout << helpText(&s);
            return;
        }
    }
    std::cerr << "Unknown subcommand: " << sub << "\n";
}

// ── Local handlers ────────────────────────────────────────────

void CliApp::printVersion(){
    std::cout << "v2 version " << V2_ENGINE_VERSION << "\n";
}

void CliApp::printStatus(){
#if V2_PLATFORM_LINUX
    std::string socketPath = RuntimeConfig{}.ipcSocketPath;
    bool sockExists = (access(socketPath.c_str(), F_OK) == 0);
    if(!sockExists){
        std::cout << "Daemon: stopped\n" << "  Socket: " << socketPath << " (not found)\n";
        return;
    }

    UdsClient probe;
    bool alive = (probe.connect(socketPath) == Ok);
    probe.shutdown();

    if(alive){
        std::cout << "Daemon: running\n";
    }else{
        std::cout << "Daemon: dead (stale socket)\n";
    }
    std::cout << "  Socket: " << socketPath << "\n";

    FILE* fp = ::popen("pidof v2_main 2>/dev/null", "r");
    if(fp){
        char buf[16] = {};
        if(std::fgets(buf, sizeof(buf), fp)) std::cout << "  PID: " << buf;
        ::pclose(fp);
    }
#else
    std::cout << "V2 Engine -- Status\n" << "  Daemon: not supported on Windows yet\n";
#endif
}

int CliApp::launchTui(char** argv){
#if V2_PLATFORM_LINUX
    std::string self = argv[0];
    auto sep = self.rfind('/');
    if(sep != std::string::npos){
        std::string tuiPath = self.substr(0, sep + 1) + "v2_tui";
        execv(tuiPath.c_str(), argv);
    }
    execvp("v2_tui", argv);
#endif
    V2_LOG_ERROR("Cli App: failed to launch v2_tui");
    std::cerr << "Error: failed to launch v2_tui (is it installed?)\n";
    return Ok;
}

// ── Daemon forwarding ─────────────────────────────────────────

void CliApp::sendToDaemon(const std::string& cmd){
#if V2_PLATFORM_LINUX
    if(client_.send(cmd.data(), cmd.size()) != Ok){
        V2_LOG_ERROR("%s App: send failed", appName_.c_str());
        return;
    }
    V2_LOG_INFO("%s App: sending command [%s]", appName_.c_str(), cmd.c_str());
    std::vector<char> buf(cfg_.ipcRecvBufferSize);
    int n = client_.recv(buf.data(), buf.size());
    if(n > 0){
        std::string resp(buf.data(), n);
        std::cout << resp << std::flush;
        V2_LOG_INFO("%s App: received response [%s]", appName_.c_str(), resp.c_str());
    }
#else
    (void)cmd;
#endif
}

// ── Terminal utilities ────────────────────────────────────────

int CliApp::terminalWidth(){
#if V2_PLATFORM_LINUX
    struct winsize w;
    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0)
        return w.ws_col;
#endif
    if(const char* cols = std::getenv("COLUMNS")){
        int n = std::atoi(cols);
        if(n > 0) return n;
    }
    return 80;
}
