#include "header.hpp"
#include "app/tui/render_util.hpp"
#include <ftxui/dom/elements.hpp>

namespace tui{

HeaderWidget::HeaderWidget() = default;

ftxui::Element HeaderWidget::OnRender(){
    using namespace ftxui;
    Elements elems;
    return hbox({
        text(" ■ V2 Engine TUI ") | bold | color(cCyan()),
        separator(),
        text(" v" V2_APP_VERSION " ") | color(cDim()),
        separator(),
        text(" " + fmtUptime(uptimeMs_) + " ") | color(cDim()),
        filler(),
        text(std::to_string(actorCount_) + " actors ") | color(cDim()),
        separator(),
        text(std::to_string(clientCount_) + " client ") | color(cDim()),
        separator(),
        text(connected_ ? " ● Connected" : " ○ Disconnected") | color(connected_ ? cGreen() : cRed()),
    });
}

void HeaderWidget::setConnected(bool v){
    connected_ = v;
}

void HeaderWidget::setActorCount(int v){
    actorCount_ = v;
}

void HeaderWidget::setClientCount(int v){
    clientCount_ = v;
}

void HeaderWidget::setUptime(uint64_t ms){
    uptimeMs_ = ms;
}



} // namespace tui
