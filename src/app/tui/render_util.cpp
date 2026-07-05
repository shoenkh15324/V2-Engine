#include "render_util.hpp"
#include "core/common/time/time.hpp"
#include <sstream>
#include <iomanip>
#include <unistd.h>
#include <thread>
#include <ftxui/screen/terminal.hpp>

namespace tui{

std::string fmtPercent(float val){
    std::ostringstream os;
    os << std::fixed << std::setprecision(1) << val;
    return os.str();
}

std::string fmtUptime(uint64_t ms){
    uint64_t sec = ms / 1000;
    uint64_t min = sec / 60; sec %= 60;
    uint64_t hr = min / 60; min %= 60;
    uint64_t day = hr / 24; hr %= 24;
    std::ostringstream os;
    if(day > 0) os << day << "d ";
    os << hr << "h " << min << "m " << sec << "s";
    return os.str();
}

ftxui::Color gaugeColor(float f){
    if(f >= 0.8f) return cRed();
    if(f >= 0.5f) return cYellow();
    return cGreen();
}

int gaugeCells(){
    auto size = ftxui::Terminal::Size();
    int cells = size.dimx / 4 - 3;
    if(cells < 15) cells = 15;
    if(cells > 35) cells = 35;
    return cells;
}

ftxui::Element cellGauge(float progress, ftxui::Color col, int cells){
    using namespace ftxui;
    int filled = static_cast<int>(progress * cells);
    if(filled > cells) filled = cells;
    if(filled < 0) filled = 0;
    std::string filledStr, emptyStr;
    for(int i = 0; i < filled; i++){
        filledStr += "|";
    }
    for(int i = filled; i < cells; i++){
        emptyStr += "|";
    }
    Elements elems;
    elems.push_back(text(" "));
    if(filled > 0) elems.push_back(text(filledStr) | color(col));
    if(filled < cells) elems.push_back(text(emptyStr) | color(cGaugeBg()));
    elems.push_back(text(" "));
    return hbox(std::move(elems));
}

} // namespace tui
