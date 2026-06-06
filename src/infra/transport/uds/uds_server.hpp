#pragma once
#include <string>

class UdsServer {
public:
    UdsServer() = default;
    ~UdsServer();
    UdsServer(const UdsServer&) = delete;
    UdsServer& operator=(const UdsServer&) = delete;

    int listen(const std::string& path, int backlog = 5);
    int accept();                   
    int write(int connFd, const std::string& data);
    std::string readLine(int connFd);
    void closeClient(int connFd);
    void shutdown();

    const std::string& path() const { return path_; }

private:
    int serverFd_ = -1;
    std::string path_;
};
