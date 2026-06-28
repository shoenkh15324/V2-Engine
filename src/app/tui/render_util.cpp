#include "render_util.hpp"
#include "core/common/time/time.hpp"
#include <mutex>
#include <sstream>
#include <iomanip>
#include <unistd.h>
#include <ftxui/screen/terminal.hpp>

namespace {

// Soft pastel color palette
ftxui::Color cCyan, cGreen, cYellow, cRed, cBlue, cWhite, cDim, cGaugeBg;
std::once_flag colorInitFlag;
void initColors(){
    std::call_once(colorInitFlag, []{
        cCyan    = ftxui::Color::RGB(153, 229, 229);
        cGreen   = ftxui::Color::RGB(153, 255, 153);
        cYellow  = ftxui::Color::RGB(255, 255, 153);
        cRed     = ftxui::Color::RGB(255, 153, 153);
        cBlue    = ftxui::Color::RGB(153, 204, 255);
        cWhite   = ftxui::Color::RGB(230, 230, 230);
        cDim     = ftxui::Color::RGB(192, 192, 192);
        cGaugeBg = ftxui::Color::RGB(102, 102, 110);
    });
}

int gaugeCells(){
    auto size = ftxui::Terminal::Size();
    int cells = size.dimx / 4 - 3;
    if(cells < 15) cells = 15;
    if(cells > 35) cells = 35;
    return cells;
}

} // namespace

std::string fmtPct(float val){
    std::ostringstream os;
    os << std::fixed << std::setprecision(1) << val;
    return os.str();
}

std::string fmtUptime(uint64_t ms){
    uint64_t sec = ms / 1000;
    uint64_t min = sec / 60; sec %= 60;
    uint64_t hr  = min / 60; min %= 60;
    uint64_t day = hr  / 24; hr  %= 24;
    std::ostringstream os;
    if(day > 0) os << day << "d ";
    os << hr << "h " << min << "m " << sec << "s";
    return os.str();
}

ftxui::Color gaugeColor(float f){
    initColors();
    if(f >= 0.8f) return cRed;
    if(f >= 0.5f) return cYellow;
    return cGreen;
}

ftxui::Element cellGauge(float progress, ftxui::Color col, int cells){
    using namespace ftxui;
    initColors();
    int filled = static_cast<int>(progress * cells);
    if(filled > cells) filled = cells;
    if(filled < 0) filled = 0;
    std::string filledStr, emptyStr;
    for(int i = 0; i < filled; ++i) filledStr += "■";
    for(int i = filled; i < cells; ++i) emptyStr += "□";
    Elements elems;
    if(filled > 0) elems.push_back(text(filledStr) | color(col));
    if(filled < cells) elems.push_back(text(emptyStr) | color(cGaugeBg));
    return hbox(std::move(elems));
}

ftxui::Element renderHeader(bool connected, int actorCount, int clientCount, uint64_t uptimeMs, float cpuPct, float memPct){
    using namespace ftxui;
    initColors();
    return hbox({
        text(" ■ V2 Engine TUI ") | bold | color(cCyan),
        separator(),
        text(" v" V2_APP_VERSION " ") | color(cDim),
        separator(),
        text(" " + fmtUptime(uptimeMs) + " ") | color(cDim),
        filler(),
        text(std::to_string(actorCount) + " actors ") | color(cDim),
        separator(),
        text(std::to_string(clientCount) + " client ") | color(cDim),
        separator(),
        text(connected ? " ● Connected" : " ○ Disconnected") | color(connected ? cGreen : cRed),
    });
}

ftxui::Element renderActorList(const std::vector<ActorInfo>& actors){
    using namespace ftxui;
    initColors();
    int cells = gaugeCells();
    Elements actorRows;
    for(const auto& a : actors){
        float progress = a.mailboxCapacity > 0
            ? (float)a.mailboxCount / (float)a.mailboxCapacity : 0.0f;

        auto bar = cellGauge(progress, gaugeColor(progress), cells);
        float mbPct = progress * 100.0f;

        auto mbText = text(std::to_string(a.mailboxCount) + "/"
                          + std::to_string(a.mailboxCapacity)
                          + " (" + fmtPct(mbPct) + "%) ") | color(cDim);

        auto statusColor = (a.mailboxCount > 0) ? cGreen : cDim;
        auto statusIcon  = (a.mailboxCount > 0) ? " ●" : " ○";
        auto statusLabel = (a.mailboxCount > 0) ? "Active" : "Idle";

        actorRows.push_back(
            hbox({
                text("  " + std::to_string(a.id) + "  ") | color(cBlue),
                separator(),
                text(" " + a.name + " ") | bold | color(cWhite) | size(WIDTH, GREATER_THAN, 14),
                separator(),
                bar | flex,
                separator(),
                mbText,
                separator(),
                text(statusIcon) | color(statusColor),
                text(statusLabel) | color(statusColor) | size(WIDTH, GREATER_THAN, 6),
            })
        );
    }

    if(actorRows.empty()){
        actorRows.push_back(
            text(" No actors connected") | color(cDim) | hcenter
        );
    }

    return window(
        text(" Actors (" + std::to_string(actors.size()) + ") ") | bold | color(cCyan),
        vbox(std::move(actorRows)) | flex
    );
}

ftxui::Element renderSystemResources(const SystemResources& res){
    using namespace ftxui;
    initColors();
    int cells = gaugeCells();

    float cpuFrac = res.cpuPercent / 100.0f;
    float memFrac = (res.memoryTotalKb > 0)
        ? ((float)res.memoryRssKb / (float)res.memoryTotalKb) : 0.0f;
    float rssMb = (float)res.memoryRssKb / 1024.0f;
    float totalMb = (float)res.memoryTotalKb / 1024.0f;

    float loadFrac = res.loadAvg1 / 8.0f;
    if(loadFrac > 1.0f) loadFrac = 1.0f;

    float sysMemFrac = (res.sysMemTotalKb > 0)
        ? 1.0f - (float)res.sysMemAvailKb / (float)res.sysMemTotalKb : 0.0f;
    float sysTotalMb = (float)res.sysMemTotalKb / 1024.0f;
    float sysAvailMb = (float)res.sysMemAvailKb / 1024.0f;

    auto sysRow = [&](const std::string& label, ftxui::Element gauge, const std::string& value){
        return hbox({
            text(" " + label + " ") | bold | size(WIDTH, GREATER_THAN, 6) | color(cCyan),
            separator(),
            std::move(gauge),
            separator(),
            filler(),
            text(" " + value + " ") | color(cWhite),
        });
    };

    return window(
        text(" System Resources ") | bold | color(cCyan),
        vbox({
            sysRow("CPU", cellGauge(cpuFrac, gaugeColor(cpuFrac), cells), fmtPct(res.cpuPercent) + "%"),
            separator(),
            sysRow("MEM", cellGauge(memFrac, gaugeColor(memFrac), cells), fmtPct(rssMb) + " / " + fmtPct(totalMb) + " MB (" + fmtPct(memFrac * 100.0f) + "%)"),
            separator(),
            sysRow("SYS", cellGauge(sysMemFrac, cBlue, cells), fmtPct(sysTotalMb - sysAvailMb) + " / " + fmtPct(sysTotalMb) + " MB (" + fmtPct(sysMemFrac * 100.0f) + "%)"),
            separator(),
            sysRow("LOAD", cellGauge(loadFrac, cBlue, cells), fmtPct(res.loadAvg1) + " / " + fmtPct(res.loadAvg5) + " / " + fmtPct(res.loadAvg15)),
        })
    );
}

ftxui::Element renderProcessInfo(const SystemResources& res){
    using namespace ftxui;
    initColors();
    float rssMb = (float)res.memoryRssKb / 1024.0f;
    float peakMb = (float)res.vmPeakKb / 1024.0f;
    float hwmMb = (float)res.vmHwmKb / 1024.0f;
    float swapMb = (float)res.vmSwapKb / 1024.0f;

    auto row = [&](const std::string& key, const std::string& val){
        return hbox({
            text(" " + key + ":") | color(cDim) | size(WIDTH, GREATER_THAN, 10),
            filler(),
            text(" " + val + " ") | color(cWhite),
        });
    };

    auto content = vbox({
        row("PID", std::to_string(getpid())),
        row("Threads", std::to_string(res.threadCount)),
        row("RSS", fmtPct(rssMb) + " MB"),
        row("Peak", fmtPct(peakMb) + " MB"),
        row("HWM", fmtPct(hwmMb) + " MB"),
        row("Swap", fmtPct(swapMb) + " MB"),
    });

    return window(
        text(" Process Info ") | bold | color(cCyan),
        content | flex
    );
}

ftxui::Element renderFooter(){
    using namespace ftxui;
    initColors();
    return hbox({
        text(" " + Time::nowDateString() + " ") | color(cDim),
        filler(),
        separator(),
        text(" [q] quit  [h] help ") | color(cDim),
    });
}
