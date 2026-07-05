#pragma once
#include <ftxui/component/component.hpp>
#include "service/monitor/monitor_data.hpp"

namespace tui{

class SystemPanelWidget : public ftxui::ComponentBase{
public:
    SystemPanelWidget();

    ftxui::Element OnRender() override;
    void setResources(const SystemResources& res);

private:
    ftxui::Element renderProcessInfo();
    ftxui::Element renderSystemResources();

    SystemResources res_;

};

} // namespace tui
