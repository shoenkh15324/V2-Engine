#include "render_util.hpp"
#include "core/common/time/time.hpp"
#include <sstream>
#include <iomanip>

std::string fmtPct(float val){
    std::ostringstream os;
    os << std::fixed << std::setprecision(1) << val;
    return os.str();
}

ftxui::Color gaugeColor(float f){
    if(f >= 0.8f) return ftxui::Color::Red;
    if(f >= 0.5f) return ftxui::Color::Yellow;
    return ftxui::Color::GreenLight;
}

ftxui::Element cellGauge(float progress, ftxui::Color col, int cells){
    using namespace ftxui;
    int filled = static_cast<int>(progress * cells);
    if(filled > cells) filled = cells;
    if(filled < 0) filled = 0;
    std::string filledStr, emptyStr;
    for(int i = 0; i < filled; ++i) filledStr += "■";
    for(int i = filled; i < cells; ++i) emptyStr += "□";
    Elements elems;
    if(filled > 0) elems.push_back(text(filledStr) | color(col));
    if(filled < cells) elems.push_back(text(emptyStr) | color(ftxui::Color::GrayDark));
    return hbox(std::move(elems));
}

ftxui::Element renderHeader(bool connected, int actorCount, int clientCount){
    using namespace ftxui;
    return hbox({
        text(" ■ V2 Engine TUI ") | bold | color(Color::Cyan),
        separator(),
        text(" v" V2_APP_VERSION " ") | dim,
        filler(),
        text(connected ? " ● Connected" : " ○ Disconnected")
            | color(connected ? Color::Green : Color::Red),
        separator(),
        text(" " + std::to_string(actorCount) + " actors ") | dim,
        separator(),
        text(" " + std::to_string(clientCount) + " client ") | dim,
    });
}

ftxui::Element renderActorList(const std::vector<ActorInfo>& actors){
    using namespace ftxui;
    Elements actorRows;
    for(const auto& a : actors){
        float progress = a.mailboxCapacity > 0
            ? (float)a.mailboxCount / (float)a.mailboxCapacity : 0.0f;

        auto bar = cellGauge(progress, gaugeColor(progress), 20);
        float mailboxPercent = progress * 100.0f;

        auto mailboxText = text(" " + std::to_string(a.mailboxCount) + "/"
                          + std::to_string(a.mailboxCapacity)
                          + " (" + fmtPct(mailboxPercent) + "%) ")
            | size(WIDTH, GREATER_THAN, 18) | dim;

        actorRows.push_back(
            hbox({
                text("  " + std::to_string(a.id) + "  ") | color(Color::Cyan),
                separator(),
                text(" " + a.name + " ") | bold | size(WIDTH, GREATER_THAN, 14),
                separator(),
                bar,
                separator(),
                mailboxText,
                filler(),
                text(" ● Active") | color(Color::Green),
            })
        );
    }

    if(actorRows.empty()){
        actorRows.push_back(
            text(" No actors connected") | dim | hcenter
        );
    }

    return window(
        text(" Active Actors (" + std::to_string(actors.size()) + ") ") | bold,
        vbox(std::move(actorRows))
    );
}

ftxui::Element renderSystemResources(const SystemResources& res){
    using namespace ftxui;
    float cpuFrac = res.cpuPercent / 100.0f;

    float memFrac = (res.memoryTotalKb > 0)
        ? ((float)res.memoryRssKb / (float)res.memoryTotalKb) : 0.0f;
    float rssMb = (float)res.memoryRssKb / 1024.0f;
    float totalMb = (float)res.memoryTotalKb / 1024.0f;

    return window(
        text(" System Resources ") | bold,
        vbox({
            hbox({
                text(" CPU  ") | bold | size(WIDTH, GREATER_THAN, 6),
                separator(),
                cellGauge(cpuFrac, gaugeColor(cpuFrac), 20),
                separator(),
                filler(),
                text(" " + fmtPct(res.cpuPercent) + "% "),
            }),
            separator(),
            hbox({
                text(" MEM  ") | bold | size(WIDTH, GREATER_THAN, 6),
                separator(),
                cellGauge(memFrac, gaugeColor(memFrac), 20),
                separator(),
                filler(),
                text(" " + fmtPct(rssMb) + " / " + fmtPct(totalMb) + " MB (" + fmtPct(memFrac * 100.0f) + "%) "),
            }),
        })
    );
}

ftxui::Element renderFooter(){
    using namespace ftxui;
    return hbox({
        text(" " + Time::nowDateString() + " ") | dim,
        filler(),
        separator(),
        text(" Press q to quit ") | dim,
    });
}
