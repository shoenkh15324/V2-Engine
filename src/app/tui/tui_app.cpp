#include "tui_app.hpp"
#include "core/common/util/platform_config.h"
#include "core/common/log/log.hpp"
#include "core/common/time/time.hpp"
#include "core/common/util/return.hpp"
#include "core/common/os/signal_handler.hpp"
#include <csignal>

#if V2_PLATFORM_LINUX
    #include <unistd.h>
#endif

TuiApp::TuiApp() : root_(ftxui::CatchEvent(ftxui::Renderer([this](){ return render(); }), [this](ftxui::Event event){
    if(event == ftxui::Event::Character('q') || event == ftxui::Event::Character('Q')){
        if(screen_){
            screen_->Exit();
        }
        return true;
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
    // if(client_.connect(cfg_.monitorSocketPath) != Ok){ V2_LOG_ERROR("%s App: failed to connect to main app", name_.c_str());
    //     return Fail;
    // }
    // V2_LOG_INFO("%s App: connected to main app", name_.c_str());
#else
    V2_LOG_ERROR("%s App: Main not supported on Windows yet", name_.c_str());
    return Fail;
#endif
    screen_ = std::make_unique<ftxui::App>(ftxui::App::Fullscreen());
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
#if V2_PLATFORM_LINUX
    client_.shutdown();
#endif
    screen_.reset();
    isRunning_.store(false);
    V2_LOG_INFO("%s App Close", name_.c_str());
    return Ok;
}

void TuiApp::requestStop(){
    isRunning_.store(false);
    if(screen_) screen_->Exit();
    V2_LOG_INFO("");
}

ftxui::Element TuiApp::render(){
    using namespace ftxui;
    auto header = hbox({
        text(" V2 Engine TUI ") | bold | color(Color::Cyan),
        separator(),
        text(" v" V2_APP_VERSION " ") | dim,
        filler(),
        text((client_.fd() >= 0) ? " ● Connected" : " ○ Disconnected") | color((client_.fd() >= 0) ? Color::Green : Color::Red),
    }) | borderRounded;

    auto content = vbox({
        text("V2 Engine Monitor") | bold | hcenter,
        separator(),
        text("Status: Running") | color(Color::Green),
        text("Press 'q' or Ctrl-C to quit") | dim | hcenter,
    }) | center;

    auto footer = hbox({
        text(Time::nowDateString()) | dim,
        filler(),
        text(" TUI ") | dim,
    }) | borderRounded;

    return vbox({
        header | size(HEIGHT, GREATER_THAN, 3),
        separator(),
        content | flex,
        separator(),
        footer | size(HEIGHT, GREATER_THAN, 3),
    });
}
