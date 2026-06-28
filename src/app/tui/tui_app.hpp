#pragma once
#include "core/common/util/platform_config.h"
#include "core/common/config/runtime_config.h"
#include "service/monitor/monitor_data.hpp"
#include <string>
#include <atomic>
#include <memory>
#include <thread>
#include <mutex>
#include <vector>


#include "ftxui/component/app.hpp"
#include "ftxui/component/component.hpp"

#if V2_PLATFORM_LINUX
    #include "infra/transport/uds/uds_client.hpp"
#endif

class TuiApp{
public:
    TuiApp();
    ~TuiApp();

    TuiApp(const TuiApp&) = delete;
    TuiApp& operator=(const TuiApp&) = delete;
    TuiApp(TuiApp&&) = delete;
    TuiApp& operator=(TuiApp&&) = delete;

    int open();
    void run();
    int close();

private:
    void requestStop();
    void recvLoop();
    void handleLine(const std::string& line);
    ftxui::Element render();

    RuntimeConfig cfg_;
    MonitorSnapshot snapshot_, pendingSnapshot_;
    mutable std::mutex mutex_;
    std::string recvBuffer_;
    std::thread recvThread_;
    std::string name_ = "Tui";
    std::atomic<bool> isRunning_{false};
    std::unique_ptr<ftxui::App> screen_;
    ftxui::Component root_;

#if V2_PLATFORM_LINUX
    UdsClient client_;
#endif
};
