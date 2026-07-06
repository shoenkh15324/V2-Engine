#pragma once
#include <ftxui/component/component.hpp>
#include "service/monitor/monitor_data.hpp"

namespace tui{

class PmuPanelWidget : public ftxui::ComponentBase{
public:
    PmuPanelWidget() = default;

    ftxui::Element OnRender() override;
    void setPmuData(const PmuData& data);

private:
    ftxui::Element renderInfo(); // clock → GHz 변환, throttled bitmask 해석 등의 render 헬퍼

    PmuData data_;
};

} // namespace tui
