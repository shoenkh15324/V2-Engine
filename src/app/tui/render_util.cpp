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

} // namespace tui
