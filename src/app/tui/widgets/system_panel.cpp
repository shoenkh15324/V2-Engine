#include "system_panel.hpp"
#include "app/tui/render_util.hpp"
#include <ftxui/dom/elements.hpp>
#include <thread>
#include <unistd.h>

namespace tui{

SystemPanelWidget::SystemPanelWidget(){
    splitSize_ = ftxui::Terminal::Size().dimy / 2;
    processInfo_ = ftxui::Renderer([this](){ return renderProcessInfo(); });
    systemResourceInfo_ = ftxui::Renderer([this](){ return renderSystemResources(); });
    resizableContent_ = ftxui::ResizableSplitTop(processInfo_, systemResourceInfo_, &splitSize_);
    Add(resizableContent_);
}

ftxui::Element SystemPanelWidget::OnRender(){
    return resizableContent_->Render();
}

void SystemPanelWidget::setResources(const SystemResources& res){
    res_ = res;
}

ftxui::Element SystemPanelWidget::renderProcessInfo(){
    using namespace ftxui;
    float rssMb = static_cast<float>(res_.memoryRssKb) / 1024.0f;
    float peakMb = static_cast<float>(res_.vmPeakKb) / 1024.0f;
    float hwmMb = static_cast<float>(res_.vmHwmKb) / 1024.0f;
    float swapMb = static_cast<float>(res_.vmSwapKb) / 1024.0f;
    auto row = [&](const std::string& key, const std::string& val){
        return hbox({
            text(" " + key + ":") | color(cDim()) | size(WIDTH, GREATER_THAN, 10),
            filler(),
            text(" " + val + " ") | color(cWhite()),
        });
    };
    return window(
        text(" Process Info ") | bold | color(cCyan()),
        vbox({
            row("PID", std::to_string(getpid())),
            row("Threads", std::to_string(res_.threadCount)),
            row("RSS", fmtPercent(rssMb) + " MB"),
            row("Peak", fmtPercent(peakMb) + " MB"),
            row("HWM", fmtPercent(hwmMb) + " MB"),
            row("Swap", fmtPercent(swapMb) + " MB"),
        })
    );
}

ftxui::Element SystemPanelWidget::renderSystemResources(){
    using namespace ftxui;
    auto& r = res_;
    int cells = gaugeCells();
    float cpuFrac = r.cpuPercent / 100.0f;
    float memFrac = (r.memoryTotalKb > 0) ? (static_cast<float>(r.memoryRssKb) / static_cast<float>(r.memoryTotalKb)) : 0.0f;
    float rssMb = static_cast<float>(r.memoryRssKb) / 1024.0f;
    float totalMb = static_cast<float>(r.memoryTotalKb) / 1024.0f;
    unsigned int nproc = (std::thread::hardware_concurrency() > 0) ? std::thread::hardware_concurrency() : 1;
    float loadFrac = (r.loadAvg1 / nproc) <= 1.0f ? (r.loadAvg1 / nproc) : 1.0f;
    float sysMemFrac = (r.sysMemTotalKb > 0) ? 1.0f - static_cast<float>(r.sysMemAvailKb) / static_cast<float>(r.sysMemTotalKb) : 0.0f;
    float sysTotalMb = static_cast<float>(r.sysMemTotalKb) / 1024.0f;
    float sysAvailMb = static_cast<float>(r.sysMemAvailKb) / 1024.0f;

    auto sysRow = [&](const std::string& label, Element gauge, Element value){
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
            sysRow("CPU", cellGauge(cpuFrac, gaugeColor(cpuFrac), cells), text(" " + fmtPercent(r.cpuPercent) + "% ") | color(gaugeColor(cpuFrac))),
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
                text(" 1m:" + fmtPercent(r.loadAvg1 / nproc * 100.0f) + "% ") | color(gaugeColor(r.loadAvg1 / nproc)),
                separator(),
                text(" 5m:" + fmtPercent(r.loadAvg5 / nproc * 100.0f) + "% ") | color(gaugeColor(r.loadAvg5 / nproc)),
                separator(),
                text(" 15m:" + fmtPercent(r.loadAvg15 / nproc * 100.0f) + "% ") | color(gaugeColor(r.loadAvg15 / nproc)),
                separator(),
                text(" " + std::to_string(nproc) + " cores ") | color(cWhite()),
            })),
        })
    );
}

} // namespace tui
