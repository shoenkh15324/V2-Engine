#include "infra/transport/uds/uds_server.hpp"
#include "core/common/log.hpp"
#include "core/common/return.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

UdsServer::~UdsServer() { shutdown(); }

int UdsServer::listen(const std::string& path, int backlog) {
    unlink(path.c_str()); // 기존 소켓 파일 제거
    serverFd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if(serverFd_ < 0){ V2_LOG_ERROR("socket() failed");
        return Fail;
    }
    sockaddr_un addr{.sun_family = AF_UNIX,};
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if(bind(serverFd_, (sockaddr*)&addr, sizeof(addr)) < 0){ V2_LOG_ERROR("bind(%s) failed", path.c_str());
        close(serverFd_);
        return Fail;
    }
    if(::listen(serverFd_, backlog) < 0){ V2_LOG_ERROR("listen() failed");
        close(serverFd_);
        return Fail;
    }
    path_ = path;
    V2_LOG_INFO("UDS listening on %s", path.c_str());
    return Ok;
}

int UdsServer::accept(){
    sockaddr_un peer{};
    socklen_t len = sizeof(peer);
    int connFd = ::accept(serverFd_, (sockaddr*)&peer, &len);
    if(connFd < 0){ 
        if(errno != EINVAL){ 
            V2_LOG_ERROR("accept() failed");
        }
    }
    return connFd;
}

int UdsServer::write(int connFd, const std::string& data){
    ssize_t n = ::write(connFd, data.data(), data.size());
    if(n < 0){ V2_LOG_ERROR("write() failed");
        return Fail;
    }
    return Ok;
}

std::string UdsServer::readLine(int connFd){
    std::string line;
    char ch;
    while(::read(connFd, &ch, 1) == 1){
        if(ch == '\n'){
            break;
        }
        line += ch;
    }
    return line;
}

void UdsServer::closeClient(int connFd){
    ::close(connFd);
}

void UdsServer::shutdown(){
    if(serverFd_ > 0){
        ::close(serverFd_);
        serverFd_ = -1;
    }
    if(!path_.empty()){
        unlink(path_.c_str());
        path_.clear();
    }
}
