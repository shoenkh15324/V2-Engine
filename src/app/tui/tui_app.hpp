#pragma once
#include "core/common/config/platform_config.h"
#include "core/common/config/runtime_config.h"
#include "service/monitor/monitor_data.hpp"
#include "ftxui/component/app.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/screen/terminal.hpp"
#include "widgets/footer.hpp"
#include "widgets/header.hpp"
#include "widgets/system_panel.hpp"
#include "widgets/pmu_panel.hpp"
#include "widgets/actor_list/actor_list.hpp"
#include <string>
#include <atomic>
#include <memory>
#include <thread>
#include <mutex>
#include <vector>

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
    ftxui::Element render();
    std::string sendIpcCommand(const std::string& cmd);
    void setToast(const std::string& msg, int durationSec);

    RuntimeConfig cfg_;
    MonitorSnapshot snapshot_;
    mutable std::mutex mutex_;
    std::thread recvThread_;
    std::string name_ = "Tui";
    std::atomic<bool> isRunning_{false};
    std::unique_ptr<ftxui::App> screen_;
    ftxui::Component root_;
    ftxui::Component mainContent_;
    ftxui::Component leftContent_;
    std::shared_ptr<tui::FooterWidget> footerWidget_;
    std::shared_ptr<tui::HeaderWidget> headerWidget_;
    std::shared_ptr<tui::SystemPanelWidget> systemPanelWidget_;
    std::shared_ptr<tui::PmuPanelWidget> pmuPanelWidget_;
    std::shared_ptr<tui::ActorListWidget> actorListWidget_;

    bool mouseEnabled_ = true;
    int splitSize_ = 35;
    int pmuSplitSize_ = 12;
#if V2_PLATFORM_LINUX
    UdsClient client_;
#endif
};
