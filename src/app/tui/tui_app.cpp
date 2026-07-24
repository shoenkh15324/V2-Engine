#include "tui_app.hpp"
#include "core/common/config/platform_config.h"
#include "core/common/log/log.hpp"
#include "core/common/time/time.hpp"
#include "core/common/util/return.hpp"
#include "core/common/os/signal_handler.hpp"
#include "app/tui/render_util.hpp"
#include <csignal>
#include <ftxui/dom/elements.hpp>

#if V2_PLATFORM_LINUX
    #include <unistd.h>
#endif

using namespace tui;

TuiApp::TuiApp(){
    splitSize_ = ftxui::Terminal::Size().dimx / 2;
    footerWidget_ = ftxui::Make<FooterWidget>();
    headerWidget_ = ftxui::Make<HeaderWidget>();
    systemPanelWidget_ = ftxui::Make<SystemPanelWidget>();
    actorListWidget_ = ftxui::Make<ActorListWidget>();
    pmuPanelWidget_ = ftxui::Make<PmuPanelWidget>();

    actorListWidget_->setOnToggle([this](const std::string& name, bool wasOn){
        setToast("toggling " + name + "...", 2);
        std::thread([this, name, wasOn](){
            std::string cmd = wasOn ? ("actor -d " + name) : ("actor -e " + name);
            std::string rsp = sendIpcCommand(cmd);
            screen_->Post([this, rsp]() { setToast(rsp, 3); });
        }).detach();
    });

    pmuSplitSize_ = ftxui::Terminal::Size().dimy / 2;
    leftContent_ = ftxui::ResizableSplitTop(
        actorListWidget_,
        pmuPanelWidget_,
        &pmuSplitSize_
    );

    mainContent_ = ftxui::ResizableSplitLeft(
        leftContent_,
        systemPanelWidget_,
        &splitSize_
    );
    auto scene = ftxui::Container::Vertical({
        headerWidget_,
        mainContent_,
        footerWidget_,
    });

    root_ = ftxui::CatchEvent(
        ftxui::Renderer(std::move(scene), [this](){ return render(); }),
        [this](ftxui::Event event){
            if((event == ftxui::Event::Character('q')) || (event == ftxui::Event::Character('Q'))){
                if(screen_) screen_->Exit();
                return true;
            }
            return false;
        }
    );
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
    V2_LOG_INFO("%s App Version: %s", name_.c_str(), V2_ENGINE_VERSION);
    //
    SignalHandler::instance().init();
    SignalHandler::instance().install(SIGTERM, [this](int){ requestStop(); });
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
    std::string lineBuffer;
    while(isRunning_.load()){
        int n = client_.recv(buf.data(), buf.size());
        if(n > 0){
            lineBuffer.append(buf.data(), n);
            size_t pos;
            while((pos = lineBuffer.find('\n')) != std::string::npos){
                std::string line = lineBuffer.substr(0, pos);
                lineBuffer.erase(0, pos + 1);
                if(!line.empty()){
                    try{
                        MonitorSnapshot snap = nlohmann::json::parse(line);
                        std::lock_guard<std::mutex> lock(mutex_);
                        snapshot_ = std::move(snap);
                    }catch(const nlohmann::json::exception& e){
                        V2_LOG_ERROR("TuiApp: failed to parse snapshot: %s", e.what());
                    }
                }
            }
            if(lineBuffer.size() > 65536) lineBuffer.clear();
            if(screen_) screen_->Post([]{});
        }else if(n == 0){
            break;
        }else{
            std::this_thread::sleep_for(std::chrono::milliseconds(cfg_.monitorPollIntervalMs));
        }
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

    actorListWidget_->setActors(snap.actors);
    systemPanelWidget_->setResources(r);
    pmuPanelWidget_->setPmuData(snap.pmu);

    headerWidget_->setConnected(client_.fd() >= 0);
    headerWidget_->setActorCount(snap.actors.size());
    headerWidget_->setClientCount(snap.clientCount);
    headerWidget_->setUptime(r.uptimeMs);

    if(screen_) screen_->RequestAnimationFrame();
    return vbox({
        headerWidget_->Render() | borderRounded,
        separator(),
        mainContent_->Render() | flex,
        separator(),
        footerWidget_->Render() | borderRounded
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
    std::string m = (pos != std::string::npos) ? msg.substr(0, pos) : msg;
    footerWidget_->setToast(m, Time::now() + std::chrono::seconds(durationSec));
    if(screen_) screen_->Post([]{});
}
