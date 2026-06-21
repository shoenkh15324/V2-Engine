#include "cli_app.hpp"
#include "core/common/config.h"
#include "core/common/log.hpp"
#include "core/common/time.hpp"
#include "core/common/return.hpp"
#include <iostream>

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
        std::cout.write(buf, n) << std::endl;
        V2_LOG_INFO("%s App: received response [%s]", name_.c_str(), std::string(buf, n).c_str());
    }
    return 0;
}

void CliApp::close(){
    V2_LOG_INFO("%s App Close", name_.c_str());
    client_.shutdown();
}
