#include "uds_client.hpp"

#if !V2_PLATFORM_WINDOWS
#include "core/common/log/log.hpp"
#include "core/common/util/return.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

UdsClient::~UdsClient(){
    shutdown();
}

UdsClient::UdsClient(UdsClient&& other) noexcept : clientFd_(other.clientFd_){
    other.clientFd_ = -1;
}

UdsClient& UdsClient::operator=(UdsClient&& other) noexcept{
    if(this != &other){
        shutdown();
        clientFd_ = other.clientFd_;
        other.clientFd_ = -1;
    }
    return *this;
}

int UdsClient::connect(const std::string& path){
    clientFd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if(clientFd_ < 0){ V2_LOG_ERROR("socket() failed");
        return Fail;
    }
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';
    if(::connect(clientFd_, (sockaddr*)&addr, sizeof(addr)) < 0){ V2_LOG_ERROR("connect(%s) failed", path.c_str());
        ::close(clientFd_);
        clientFd_ = -1;
        return Fail;
    }
    V2_LOG_INFO("UDS connected to %s", path.c_str());
    return Ok;
}

int UdsClient::send(const void* data, size_t size){
    if(clientFd_ < 0){ V2_LOG_ERROR("Invalid Arg");
        return InvalidArg;
    }
    const uint8_t* ptr = static_cast<const uint8_t*>(data);
    size_t sent = 0;
    while(sent < size){
        ssize_t n = ::send(clientFd_, ptr + sent, size - sent, MSG_NOSIGNAL);
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

int UdsClient::recv(void* data, size_t size){
    if(clientFd_ < 0){ V2_LOG_ERROR("Invalid Arg");
        return InvalidArg;
    }
    ssize_t n = ::recv(clientFd_, data, size, 0);
    if(n == 0) return 0;
    if(n < 0){
        if(errno == EINTR) return 0;
        return Fail;
    }
    return static_cast<int>(n);
}

void UdsClient::disconnect(){
    if(clientFd_ >= 0){
        ::close(clientFd_);
        clientFd_ = -1;
    }
}

void UdsClient::shutdown(){
    disconnect();
}

#endif
