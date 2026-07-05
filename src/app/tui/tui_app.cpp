#include "tui_app.hpp"
#include "core/common/config/platform_config.h"
#include "core/common/log/log.hpp"
#include "core/common/time/time.hpp"
#include "core/common/util/return.hpp"
#include "core/common/os/signal_handler.hpp"
#include <csignal>

#include "render_util.hpp"
#include <ftxui/dom/elements.hpp>

#if V2_PLATFORM_LINUX
    #include <unistd.h>
#endif

TuiApp::TuiApp() : root_(ftxui::CatchEvent(ftxui::Renderer([this](){ return render(); }), [this](ftxui::Event event){
    if(event == ftxui::Event::Character('q') || event == ftxui::Event::Character('Q')){
        if(screen_) screen_->Exit();
        return true;
    }
    if(event.is_mouse() && (event.mouse().button == ftxui::Mouse::Left) && (event.mouse().motion == ftxui::Mouse::Pressed)){
        for(size_t i = 0; i < checkBoxes_.size(); ++i){
            if(checkBoxes_[i].Contain(event.mouse().x, event.mouse().y)){
                if(i < checkboxActorNames_.size()){
                    std::string name = checkboxActorNames_[i];
                    bool isOn = checkboxActorStates_[i];
                    setToast("toggling " + name + "...", 2);
                    std::thread([this, name, isOn](){
                        std::string cmd = isOn ? ("actor -d " + name) : ("actor -e " + name);
                        std::string rsp = sendIpcCommand(cmd);
                        screen_->Post([this, rsp]{
                            setToast(rsp, 3);
                        });
                    }).detach();
                }
                return true;
            }
        }
    }
    return false;
})){
    //
}

TuiApp::~TuiApp(){
    close();
}

int TuiApp::open(){
    cfg_ = RuntimeConfig::loadFromFile(V2_CONFIG_DIR "/v2_tui.json");
    setLogLevel(static_cast<LogLevel>(cfg_.logLevel));
    setLogAppName(std::move(name_));
    V2_LOG_INFO("%s App Open", name_.c_str());
    V2_LOG_INFO("%s App Bulid Data: %s", name_.c_str(), Time::nowDateString().c_str());
    V2_LOG_INFO("%s App Version: %s", name_.c_str(), V2_APP_VERSION);
    //
    auto& sig = SignalHandler::instance();
    sig.listen(SIGTERM, [this](int){ requestStop(); });

#if V2_PLATFORM_LINUX
    if(client_.connect(cfg_.monitorSocketPath) != Ok){ V2_LOG_ERROR("%s App: failed to connect to main app", name_.c_str());
        return Fail;
    }
    V2_LOG_INFO("%s App: connected to main app", name_.c_str());
#else
    V2_LOG_ERROR("%s App: Main not supported on Windows yet", name_.c_str());
    return Fail;
#endif
    screen_ = std::make_unique<ftxui::App>(ftxui::App::Fullscreen());
    screen_->TrackMouse(true);
    isRunning_.store(true);
    recvThread_ = std::thread(&TuiApp::recvLoop, this);
    //
    return Ok;
}

void TuiApp::run(){
    isRunning_.store(true);
#if V2_PLATFORM_LINUX
    V2_LOG_INFO("%s App Run", name_.c_str());
    if(screen_){
        screen_->Loop(root_);
    }
#endif
}

int TuiApp::close(){
    isRunning_.store(false);
#if V2_PLATFORM_LINUX
    client_.shutdown();
    if(recvThread_.joinable()) recvThread_.join();
#endif
    screen_.reset();
    V2_LOG_INFO("%s App Close", name_.c_str());
    return Ok;
}

void TuiApp::requestStop(){
    isRunning_.store(false);
    if(screen_) screen_->Exit();
    V2_LOG_INFO("");
}

void TuiApp::recvLoop(){
    std::vector<char> buf(cfg_.monitorRecvBufferSize);
    pendingSnapshot_ = MonitorSnapshot{};
    while(isRunning_.load()){
        int n = client_.recv(buf.data(), buf.size());
        if(n > 0){
            recvBuffer_.append(buf.data(), n);
            size_t pos;
            while((pos = recvBuffer_.find('\n')) != std::string::npos){
                std::string line = recvBuffer_.substr(0, pos);
                recvBuffer_.erase(0, pos + 1);
                if(!line.empty()) handleLine(line);
            }
            if(recvBuffer_.size() > 65536) recvBuffer_.clear();
            if(screen_) screen_->Post([]{}); // wake loop for redraw
        }else if(n == 0){
            break;
        }else{
            std::this_thread::sleep_for(std::chrono::milliseconds(cfg_.monitorPollIntervalMs));
        }
    }
}

void TuiApp::handleLine(const std::string& line){
    if(isSnapshotBegin(line)){
        pendingSnapshot_ = MonitorSnapshot{};
    }else if(isSnapshotEnd(line)){
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot_ = pendingSnapshot_;
    }else{
        parseSnapshotLine(line, pendingSnapshot_);
    }
}

ftxui::Element TuiApp::render(){
    using namespace ftxui;
    MonitorSnapshot snap;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snap = snapshot_;
    }

    auto& r = snap.resources;
    float memPct = (r.memoryTotalKb > 0) ? ((float)r.memoryRssKb / (float)r.memoryTotalKb * 100.0f) : 0.0f;

    {
        size_t n = 0;
        for(const auto& a : snap.actors){ if(!a.essential) ++n; }
        checkBoxes_.resize(n);
        checkboxActorNames_.resize(n);
        checkboxActorStates_.resize(n);
    }
    auto leftPanel = renderActorList(snap.actors, checkBoxes_, checkboxActorNames_, checkboxActorStates_) | flex;
    auto rightPanel = vbox({
        renderProcessInfo(r),
        separator(),
        renderSystemResources(r) | flex,
    }) | flex;

    if(screen_) screen_->RequestAnimationFrame();
    return vbox({
        renderHeader(client_.fd() >= 0, snap.actors.size(), snap.clientCount, r.uptimeMs, r.cpuPercent, memPct) | borderRounded,
        separator(),
        hbox({ leftPanel, separator(), rightPanel }) | flex,
        separator(),
        renderFooter(toastMsg_, toastExpiry_) | borderRounded
    });
}

std::string TuiApp::sendIpcCommand(const std::string& cmd){
#if V2_PLATFORM_LINUX
    UdsClient ipcClient;
    if(ipcClient.connect(cfg_.ipcSocketPath) != Ok){
        return "error: connect failed";
    }
    ipcClient.send(cmd.data(), cmd.size());
    std::vector<char> buf(cfg_.ipcRecvBufferSize);
    int n = ipcClient.recv(buf.data(), buf.size());
    ipcClient.shutdown();
    if(n > 0){
        return std::string(buf.data(), n);
    }
    return "error: no response";
#else
    (void)cmd;
    return "error: not supported";
#endif
}

void TuiApp::setToast(const std::string& msg, int durationSec){
    auto pos = msg.find('\n');
    toastMsg_ = (pos != std::string::npos) ? msg.substr(0, pos) : msg;
    if(!toastMsg_.empty() && (toastMsg_.back() == '\n')) toastMsg_.pop_back();
    toastExpiry_ = std::chrono::steady_clock::now() + std::chrono::seconds(durationSec);
    if(screen_) screen_->Post([]{});
}
