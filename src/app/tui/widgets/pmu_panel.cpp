#include "pmu_panel.hpp"
#include "app/tui/render_util.hpp"
#include <ftxui/dom/elements.hpp>
#include <sstream>
#include <iomanip>

namespace tui{

static std::string fmtHz(uint64_t hz){
    if(hz >= 1000000000){
        return fmtPercent(static_cast<float>(hz) / 1000000000.0f) + " GHz";
    }else if(hz >= 1000000){
        return std::to_string(hz / 1000000) + " MHz";
    }else if(hz >= 1000){
        return std::to_string(hz / 1000) + " KHz";
    }
    return std::to_string(hz) + " Hz";
}

static std::string fmtThrottled(uint64_t v){
    if(v == 0) return "OK";
    std::string s;
    if(v & 0x1) s += " UNDER-VOLTAGE";
    if(v & 0x2) s += " FREQ-CAPPED";
    if(v & 0x4) s += " THROTTLING";
    if(v & 0x8) s += " SOFT-TEMP";
    // past events (bits 16-19)
    if(v & 0x10000) s += " [HIST:UNDER-VOLTAGE]";
    if(v & 0x20000) s += " [HIST:FREQ-CAPPED]";
    if(v & 0x40000) s += " [HIST:THROTTLING]";
    if(v & 0x80000) s += " [HIST:SOFT-TEMP]";
    if(s.empty()) s = "0x" + std::to_string(v);
    return s;
}

ftxui::Element PmuPanelWidget::OnRender(){
    return renderInfo();
}

void PmuPanelWidget::setPmuData(const PmuData& data){
    data_ = data;
}

ftxui::Element PmuPanelWidget::renderInfo(){
    using namespace ftxui;
    auto row = [&](const std::string& key, const std::string& val, const Color& col){
        return hbox({
            text(" " + key + ":") | color(cDim()) | size(WIDTH, GREATER_THAN, 12),
            filler(),
            text(" " + val + " ") | color(col),
        });
    };

    // Temp Color
    Color tempCol = cGreen();
    if(data_.tempCelsius >= 70.0f) tempCol = cYellow();
    if(data_.tempCelsius >= 80.0f) tempCol = cRed();

    // Throttled Color
    Color throtCol = (data_.throttled == 0) ? cGreen() : cRed();

    // Voltage / Current Color (normal range check)
    Color voltCol = (data_.voltCore >= 0.8f && data_.voltCore <= 1.4f) ? cGreen() : cYellow();

    std::ostringstream tempSs;
    tempSs << std::fixed << std::setprecision(1) << data_.tempCelsius << " °C";

    std::ostringstream voltSs;
    voltSs << std::fixed << std::setprecision(2) << data_.voltCore << " V";

    std::ostringstream currSs;
    currSs << std::fixed << std::setprecision(3) << data_.currentVddCoreA << " A";

    return window(
        text(" PMU Status ") | bold | color(cCyan()),
        vbox({
            row("ARM Clock", fmtHz(data_.clockArmHz), cWhite()),
            row("Core Clock", fmtHz(data_.clockCoreHz), cWhite()),
            row("V3D Clock", fmtHz(data_.clockV3dHz), cWhite()),
            row("Temperature", tempSs.str(), tempCol),
            row("Core Voltage", voltSs.str(), voltCol),
            row("VDD_CORE", currSs.str(), cWhite()),
            row("ARM/GPU Mem", std::to_string(data_.memArmMb) + " / " + std::to_string(data_.memGpuMb) + " MB", cWhite()),
            row("Throttled", fmtThrottled(data_.throttled), throtCol),
        })
    );
}

} // namespace tui
