#pragma once
#include <string>
#include <cstdint>
#include <ftxui/component/component.hpp>

namespace tui{

class HeaderWidget : public ftxui::ComponentBase{
public:
    HeaderWidget();

    ftxui::Element OnRender() override;

    void setConnected(bool v);
    void setActorCount(int v);
    void setClientCount(int v);
    void setUptime(uint64_t ms);

private:
    bool connected_ = false;
    int actorCount_ = 0;
    int clientCount_ = 0;
    uint64_t uptimeMs_ = 0;

};

} // namespace tui
