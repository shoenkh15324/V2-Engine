#pragma once
#include <string>
#include <ftxui/component/component.hpp>
#include "core/common/time/time.hpp"

namespace tui{

class FooterWidget : public ftxui::ComponentBase{
public:
    FooterWidget();

    ftxui::Element OnRender() override;
    void setToast(const std::string& msg, Time::TimeStamp expiry);

private:
    std::string toastMsg_;
    Time::TimeStamp toastExpiry_;
};

} // namespace tui
