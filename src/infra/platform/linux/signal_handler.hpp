#pragma once
#include <array>
#include <vector>
#include <functional>

class SignalHandler{
public:
    using Callback = std::function<void(int)>;

    static SignalHandler& instance();

    int init();
    int fd() const;

    bool install(int signum, Callback cb);
    bool uninstall(int signum);
    void dispatch(int signum);

private:
    SignalHandler() = default;
    ~SignalHandler();

    SignalHandler(const SignalHandler&) = delete;
    SignalHandler& operator=(const SignalHandler&) = delete;
    SignalHandler(SignalHandler&&) = delete;
    SignalHandler& operator=(SignalHandler&&) = delete;

    static void onSignal(int sig);
    
    static SignalHandler* sInstance_;
    std::array<std::vector<Callback>, 65> callbacks_;
    int pipeFd_[2] = {-1, -1};
};
