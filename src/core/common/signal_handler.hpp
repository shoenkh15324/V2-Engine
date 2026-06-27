#pragma once
#include <functional>
#include <unordered_map>
#include <vector>

class SignalHandler{
public:
    using Callback = std::function<void(int)>;

    static SignalHandler& instance();

    void listen(int signum, Callback cb);
    void ignore(int signum);

private:
    SignalHandler() = default;

    SignalHandler(const SignalHandler&) = delete;
    SignalHandler& operator=(const SignalHandler&) = delete;

    static void onSignal(int sig);
    static SignalHandler* sInstance_;

    std::unordered_map<int, std::vector<Callback>> handlers_;
};
