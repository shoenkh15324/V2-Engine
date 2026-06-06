#pragma once
#include <string>
#include <cstdint>

class UdsServer {
public:
    UdsServer() = default;
    ~UdsServer();
    UdsServer(const UdsServer&) = delete;
    UdsServer& operator=(const UdsServer&) = delete;
    UdsServer(UdsServer&&) noexcept;
    UdsServer& operator=(UdsServer&&) noexcept;

    int start(const std::string& path, int backlog = 5);
    int accept();                   
    int send(int fd, const void* data, size_t size);
    int recv(int fd, void* data, size_t size);
    void closeClient(int connFd);
    void shutdown();

private:
    int serverFd_ = -1;
    std::string path_;
};
