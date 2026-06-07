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

private:
    int clientFd_ = -1;
};
