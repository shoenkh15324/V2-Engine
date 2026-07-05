#pragma once
#include "core/common/config/platform_config.h"
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
    std::string sendIpcCommand(const std::string& cmd);
    void setToast(const std::string& msg, int durationSec);

    RuntimeConfig cfg_;
    MonitorSnapshot snapshot_, pendingSnapshot_;
    mutable std::mutex mutex_;
    std::string recvBuffer_;
    std::thread recvThread_;
    std::string name_ = "Tui";
    std::atomic<bool> isRunning_{false};
    std::unique_ptr<ftxui::App> screen_;
    ftxui::Component root_;

    // 체크박스 Box 저장 (render()에서 채워지고, CatchEvent에서 읽힘)
    std::vector<ftxui::Box> checkBoxes_;
    // 현재 렌더링된 actor 리스트 (토글 대상 찾기용)
    std::vector<std::string> checkboxActorNames_;
    std::vector<bool> checkboxActorStates_;

    // Toast 메시지
    std::string toastMsg_;
    std::chrono::steady_clock::time_point toastExpiry_;

    // 마우스 클릭 활성화
    bool mouseEnabled_ = true;

#if V2_PLATFORM_LINUX
    UdsClient client_;
#endif
};
