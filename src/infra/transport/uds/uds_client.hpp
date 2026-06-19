#pragma once
#include <string>
#include <cstdint>

class UdsClient{
public:
    UdsClient() = default;
    ~UdsClient();
    UdsClient(const UdsClient&) = delete;
    UdsClient& operator=(const UdsClient&) = delete;
    UdsClient(UdsClient&&) noexcept;
    UdsClient& operator=(UdsClient&&) noexcept;

    int connect(const std::string& path);
    int send(const void* data, size_t size);
    int recv(void* data, size_t size);
    void disconnect();
    void shutdown();

#if defined (__linux__)
    int fd() const { return clientFd_; }
#endif

private:
#if defined (__linux__)
    int clientFd_ = -1;
#endif
};
