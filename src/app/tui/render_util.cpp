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

// ─── 임시 유지: Step 2~5에서 각 widget으로 이관 후 제거 ───

ftxui::Element renderHeader(bool connected, int actorCount, int clientCount, uint64_t uptimeMs, float cpuPct, float memPct){

    using namespace ftxui;
    return hbox({
        text(" ■ V2 Engine TUI ") | bold | color(cCyan()),
        separator(),
        text(" v" V2_APP_VERSION " ") | color(cDim()),
        separator(),
        text(" " + fmtUptime(uptimeMs) + " ") | color(cDim()),
        filler(),
        text(std::to_string(actorCount) + " actors ") | color(cDim()),
        separator(),
        text(std::to_string(clientCount) + " client ") | color(cDim()),
        separator(),
        text(connected ? " ● Connected" : " ○ Disconnected") | color(connected ? cGreen() : cRed()),
    });
}

ftxui::Element renderActorList(const std::vector<ActorInfo>& actors, std::vector<ftxui::Box>& outCheckboxes, std::vector<std::string>& outActorNames, std::vector<bool>& outActorStates){

    using namespace ftxui;
    int cells = gaugeCells();
    Elements actorRows;
    size_t idx = 0;
    for(const auto& a : actors){
        float progress = (a.mailboxCapacity > 0) ? ((float)a.mailboxCount / (float)a.mailboxCapacity) : 0.0f;

        auto bar = cellGauge(progress, gaugeColor(progress), cells);
        float mailboxPercent = progress * 100.0f;

        auto mailboxText = hbox({
            text(" " + std::to_string(a.mailboxCount) + "/" + std::to_string(a.mailboxCapacity) + " "),
            text("(" + fmtPercent(mailboxPercent) + "%) ") | color(gaugeColor(progress)),
        });

        auto statusColor = (a.mailboxCount > 0) ? cGreen() : cDim();
        auto statusIcon = (a.mailboxCount > 0) ? " ● " : " ○ ";
        auto statusLabel = (a.mailboxCount > 0) ? "Active" : "Idle";

        Elements rowParts;

        if(a.essential){
            rowParts.push_back(text(" [ESS] ") | color(cDim()) | size(WIDTH, GREATER_THAN, 6));
        }else{
            bool isOn = (a.state >= 3);
            auto& cbBox = outCheckboxes[idx];
            rowParts.push_back(
                text(isOn ? " ■ " : " □ ")
                    | color(isOn ? cGreen() : cDim())
                    | size(WIDTH, GREATER_THAN, 5)
                    | reflect(cbBox)
            );

            outActorNames[idx] = a.name;
            outActorStates[idx] = isOn;
            ++idx;
        }

        rowParts.push_back(text(" " + std::to_string(a.id) + "  ") | color(cBlue()));
        rowParts.push_back(separator());
        rowParts.push_back(text(" " + a.name + " ") | color(cWhite()) | size(WIDTH, GREATER_THAN, 30));
        rowParts.push_back(separator());
        rowParts.push_back(bar | flex);
        rowParts.push_back(separator());
        rowParts.push_back(mailboxText);
        rowParts.push_back(separator());
        rowParts.push_back(text(statusIcon) | color(statusColor));
        rowParts.push_back(text(statusLabel) | color(statusColor) | size(WIDTH, GREATER_THAN, 6));

        actorRows.push_back(hbox(std::move(rowParts)));
    }

    if(actorRows.empty()){
        actorRows.push_back(text(" No actors connected") | color(cDim()) | hcenter);
    }

    return window(
        text(" Actors (" + std::to_string(actors.size()) + ") ") | bold | color(cCyan()),
        vbox(std::move(actorRows)) | flex
    );
}

ftxui::Element renderSystemResources(const SystemResources& res){

    using namespace ftxui;
    int cells = gaugeCells();

    float cpuFrac = res.cpuPercent / 100.0f;
    float memFrac = (res.memoryTotalKb > 0)
        ? ((float)res.memoryRssKb / (float)res.memoryTotalKb) : 0.0f;
    float rssMb = (float)res.memoryRssKb / 1024.0f;
    float totalMb = (float)res.memoryTotalKb / 1024.0f;

    unsigned int nproc = std::thread::hardware_concurrency();
    if(nproc == 0) nproc = 1;
    float loadFrac = res.loadAvg1 / nproc;
    if(loadFrac > 1.0f) loadFrac = 1.0f;

    float sysMemFrac = (res.sysMemTotalKb > 0)
        ? 1.0f - (float)res.sysMemAvailKb / (float)res.sysMemTotalKb : 0.0f;
    float sysTotalMb = (float)res.sysMemTotalKb / 1024.0f;
    float sysAvailMb = (float)res.sysMemAvailKb / 1024.0f;

    auto sysRow = [&](const std::string& label, ftxui::Element gauge, ftxui::Element value){
        return hbox({
            text(" " + label + " ") | bold | size(WIDTH, GREATER_THAN, 6) | color(cCyan()),
            separator(),
            std::move(gauge),
            separator(),
            filler(),
            std::move(value),
        });
    };

    return window(
        text(" System Resources ") | bold | color(cCyan()),
        vbox({
            sysRow("CPU", cellGauge(cpuFrac, gaugeColor(cpuFrac), cells), text(" " + fmtPercent(res.cpuPercent) + "% ") | color(gaugeColor(cpuFrac))),
            separator(),
            sysRow("MEM", cellGauge(memFrac, gaugeColor(memFrac), cells), hbox({
                text(" " + fmtPercent(rssMb) + " / " + fmtPercent(totalMb) + " MB ") | color(cWhite()),
                text("(" + fmtPercent(memFrac * 100.0f) + "%) ") | color(gaugeColor(memFrac)),
            })),
            separator(),
            sysRow("SYS", cellGauge(sysMemFrac, gaugeColor(sysMemFrac), cells), hbox({
                text(" " + fmtPercent(sysTotalMb - sysAvailMb) + " / " + fmtPercent(sysTotalMb) + " MB ") | color(cWhite()),
                text("(" + fmtPercent(sysMemFrac * 100.0f) + "%) ") | color(gaugeColor(sysMemFrac)),
            })),
            separator(),
            sysRow("LOAD", cellGauge(loadFrac, gaugeColor(loadFrac), cells), hbox({
                text(" 1m:" + fmtPercent(res.loadAvg1 / nproc * 100.0f) + "% ") | color(gaugeColor(res.loadAvg1 / nproc)),
                separator(),
                text(" 5m:" + fmtPercent(res.loadAvg5 / nproc * 100.0f) + "% ") | color(gaugeColor(res.loadAvg5 / nproc)),
                separator(),
                text(" 15m:" + fmtPercent(res.loadAvg15 / nproc * 100.0f) + "% ") | color(gaugeColor(res.loadAvg15 / nproc)),
                separator(),
                text(" " + std::to_string(nproc) + " cores ") | color(cWhite()),
            })),
        })
    );
}

ftxui::Element renderProcessInfo(const SystemResources& res){

    using namespace ftxui;
    float rssMb = (float)res.memoryRssKb / 1024.0f;
    float peakMb = (float)res.vmPeakKb / 1024.0f;
    float hwmMb = (float)res.vmHwmKb / 1024.0f;
    float swapMb = (float)res.vmSwapKb / 1024.0f;

    auto row = [&](const std::string& key, const std::string& val){
        return hbox({
            text(" " + key + ":") | color(cDim()) | size(WIDTH, GREATER_THAN, 10),
            filler(),
            text(" " + val + " ") | color(cWhite()),
        });
    };

    auto content = vbox({
        row("PID", std::to_string(getpid())),
        row("Threads", std::to_string(res.threadCount)),
        row("RSS", fmtPercent(rssMb) + " MB"),
        row("Peak", fmtPercent(peakMb) + " MB"),
        row("HWM", fmtPercent(hwmMb) + " MB"),
        row("Swap", fmtPercent(swapMb) + " MB"),
    });

    return window(
        text(" Process Info ") | bold | color(cCyan()),
        content | flex
    );
}

} // namespace tui
