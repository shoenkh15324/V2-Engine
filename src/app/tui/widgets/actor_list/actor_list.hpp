#pragma once
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/screen/box.hpp>
#include "service/monitor/monitor_data.hpp"

namespace tui{

class ActorListWidget : public ftxui::ComponentBase {
public:
    ActorListWidget();

    ftxui::Element OnRender() override;
    bool OnEvent(ftxui::Event event) override;

    void setActors(const std::vector<ActorInfo>& actors);
    void setOnToggle(std::function<void(const std::string& name, bool oldState)> fn);

private:
    struct CbEntry {
        ftxui::Component component;
        std::shared_ptr<bool> state;
    };

    void rebuildCheckboxes();

    std::vector<ActorInfo> actors_;
    std::vector<CbEntry> checkboxes_;
    std::vector<ftxui::Box> boxByCbIdx_;
    std::function<void(const std::string&, bool)> onToggle_;
    ftxui::Element renderTableHeader();
    ftxui::Element renderRow(const ActorInfo& actor, int cbIdx, int cells);
    ftxui::Element renderEmpty();
    ftxui::Element renderTitle();
};

} // namespace tui
