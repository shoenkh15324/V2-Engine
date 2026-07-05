#include "footer.hpp"
#include "app/tui/render_util.hpp"
#include <ftxui/dom/elements.hpp>

namespace tui{

FooterWidget::FooterWidget(){
    toastExpiry_ = std::chrono::steady_clock::time_point{};
}

ftxui::Element FooterWidget::OnRender(){
    using namespace ftxui;
    Elements elems;
    elems.push_back(text(" " + Time::nowDateString() + " ") | color(cDim()));
    elems.push_back(filler());

    if(!toastMsg_.empty()){
        auto now = Time::now();
        if(now < toastExpiry_){
            elems.push_back(text(" " + toastMsg_ + " ") | color(cYellow()));
            elems.push_back(separator());
        }
    }

    elems.push_back(text(" [q] quit   [h] help ") | color(cDim()));
    return hbox(std::move(elems));
}

void FooterWidget::setToast(const std::string& msg, Time::TimeStamp expiry){
    toastMsg_ = msg;
    toastExpiry_ = expiry;
}

} // namespace tui
