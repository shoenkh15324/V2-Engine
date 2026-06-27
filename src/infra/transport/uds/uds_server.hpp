#pragma once
#include "core/common/platform_config.h"
#include <string>
#include <cstdint>

class UdsServer{
public:
    UdsServer() = default;
    ~UdsServer();
    
    UdsServer(const UdsServer&) = delete;
    UdsServer& operator=(const UdsServer&) = delete;
    UdsServer(UdsServer&&) noexcept;
    UdsServer& operator=(UdsServer&&) noexcept;

    int start(const std::string& path, int backlog);
    int accept();                   
    int send(int fd, const void* data, size_t size);
    int recv(int fd, void* data, size_t size);
    void closeClient(int connFd);
    void shutdown();

#if V2_PLATFORM_LINUX
    int fd() const { return serverFd_; }
#endif

private:
    std::string path_;
#if V2_PLATFORM_LINUX
    int serverFd_ = -1;
#endif
};
