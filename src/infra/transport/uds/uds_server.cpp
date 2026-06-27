#include "infra/transport/uds/uds_server.hpp"

#if !V2_PLATFORM_WINDOWS
#include "core/common/log/log.hpp"
#include "core/common/util/return.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>

UdsServer::~UdsServer(){ 
    shutdown();
}

UdsServer::UdsServer(UdsServer&& other) noexcept : serverFd_(other.serverFd_), path_(std::move(other.path_)){
    other.serverFd_ = -1;
}

UdsServer& UdsServer::operator=(UdsServer&& other) noexcept{
    if(this != &other){
        shutdown();
        serverFd_ = other.serverFd_;
        path_ = std::move(other.path_);
        other.serverFd_ = -1;
    }
    return *this;
}

int UdsServer::start(const std::string& path, int backlog){
    unlink(path.c_str());
    serverFd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if(serverFd_ < 0){ V2_LOG_ERROR("socket() failed");
        return Fail;
    }
    sockaddr_un addr{.sun_family = AF_UNIX,};
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';
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
    if(serverFd_ < 0) return InvalidState;
    while(true){
        int connFd = ::accept(serverFd_, nullptr, nullptr);
        if(connFd >= 0){
            return connFd;
        }
        if(errno == EINTR){
            continue;
        }
        return Fail;
    }
}

int UdsServer::send(int fd, const void* data, size_t size){
    const uint8_t* ptr = static_cast<const uint8_t*>(data);
    size_t sent = 0;
    while(sent < size){
        ssize_t n = ::send(fd, ptr + sent, size - sent, MSG_NOSIGNAL);
        if(n < 0){
            if(errno == EINTR){
                continue;
            }
            return Fail;
        }
        sent += static_cast<size_t>(n);
    }
    return Ok;
}

int UdsServer::recv(int fd, void* data, size_t size){
    uint8_t* ptr = static_cast<uint8_t*>(data);
    size_t received = 0;
    while(received < size){
        ssize_t n = ::recv(fd, ptr + received, size - received, 0);
        if(n == 0){
            return static_cast<int>(received);
        }
        if(n < 0){
            if(errno == EINTR){
                continue;
            }
            return Fail;
        }
        received += static_cast<size_t>(n);
    }
    return static_cast<int>(received);
}

void UdsServer::closeClient(int connFd){
    if(connFd >= 0){
        ::close(connFd);
    }
}

void UdsServer::shutdown(){
    if(serverFd_ >= 0){
        ::close(serverFd_);
        serverFd_ = -1;
    }
    if(!path_.empty()){
        unlink(path_.c_str());
        path_.clear();
    }
}

#endif
