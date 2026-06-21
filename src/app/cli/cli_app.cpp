#include "cli_app.hpp"
#include "core/common/config.h"
#include "core/common/log.hpp"
#include "core/common/time.hpp"
#include "core/common/return.hpp"
#include <iostream>
#include <sstream>
#include <cstdio>
#include <unistd.h>

#define CLR(c) (shouldColor() ? (c) : "")

CliApp::CliApp(){}

CliApp::~CliApp(){
    close();
}

bool CliApp::open(){
    setLogAppName(name_);
    V2_LOG_INFO("%s App Open", name_.c_str());
    V2_LOG_INFO("%s App Bulid Data: %s", name_.c_str(), Time::nowDateString().c_str());
    V2_LOG_INFO("%s App Version: %s", name_.c_str(), V2_APP_VERSION);

    if(client_.connect(V2_DEFAULT_IPC_SOCKET_PATH) != Ok){
        V2_LOG_ERROR("%s App: failed to connect to main app", name_.c_str());
        return false;
    }
    V2_LOG_INFO("%s App: connected to main app", name_.c_str());
    return true;
}

int CliApp::run(const std::string& cmd){
    V2_LOG_INFO("%s App Run", name_.c_str());
    V2_LOG_INFO("%s App: sending command [%s]", name_.c_str(), cmd.c_str());

    if(client_.send(cmd.data(), cmd.size()) != Ok){
        V2_LOG_ERROR("%s App: send failed", name_.c_str());
        return 1;
    }

    char buf[V2_IPC_RECV_BUFFER_SIZE];
    int n = client_.recv(buf, sizeof(buf));
    if(n > 0){
        std::string resp(buf, n);
        printResponse(resp);
        V2_LOG_INFO("%s App: received response [%s]", name_.c_str(), resp.c_str());
    }
    return 0;
}

void CliApp::close(){
    V2_LOG_INFO("%s App Close", name_.c_str());
    client_.shutdown();
}

bool CliApp::shouldColor(){
    static bool color = isatty(STDOUT_FILENO);
    return color;
}

void CliApp::printLocalHelp(){
    std::cout
        << CLR("\033[1m\033[36m") << "  V2 Engine \342\200\224 Control Interface" << CLR("\033[0m") << "\n"
        << CLR("\033[33m") << "  Usage:" << CLR("\033[0m") << " v2 <command>\n"
        << "\n"
        << CLR("\033[33m") << "  Commands:" << CLR("\033[0m") << "\n"
        << "    " << CLR("\033[32m") << "info" << CLR("\033[0m") << "      Show engine information\n"
        << "    " << CLR("\033[32m") << "help" << CLR("\033[0m") << "      Show this help message\n"
        << "\n"
        << CLR("\033[33m") << "  Options:" << CLR("\033[0m") << "\n"
        << "    " << CLR("\033[32m") << "--version" << CLR("\033[0m") << "  Show version information\n";
}

void CliApp::printLocalVersion(){
    std::cout
        << CLR("\033[36m\033[1m") << "  V2 Engine " << CLR("\033[0m") << V2_APP_VERSION << "\n";
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
