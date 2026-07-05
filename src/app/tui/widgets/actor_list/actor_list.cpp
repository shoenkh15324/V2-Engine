#include "actor_list.hpp"
#include "app/tui/render_util.hpp"
#include <ftxui/dom/elements.hpp>
#include <ftxui/util/ref.hpp>

namespace tui{

ActorListWidget::ActorListWidget() = default;

void ActorListWidget::setOnToggle(std::function<void(const std::string&, bool)> fn){
    onToggle_ = std::move(fn);
}

static ftxui::CheckboxOption makeCheckboxOpt() {
    auto opt = ftxui::CheckboxOption::Simple();
    opt.transform = [](const ftxui::EntryState& s) -> ftxui::Element {
        using namespace ftxui;
        return text(s.state ? "[■]" : "[□]") | color(cDim());
    };
    return opt;
}

void ActorListWidget::rebuildCheckboxes(){
    checkboxes_.clear();
    for(const auto& a : actors_){
        if(!a.essential){
            auto opt = makeCheckboxOpt();
            auto state = std::make_shared<bool>(a.state >= 3);
            opt.checked = ftxui::Ref<bool>(state.get());
            auto cb = ftxui::Checkbox(std::move(opt));
            checkboxes_.push_back(CbEntry{std::move(cb), std::move(state)});
        }
    }
}

void ActorListWidget::setActors(const std::vector<ActorInfo>& actors){
    actors_ = actors;
    rebuildCheckboxes();
}

// ─── 렌더링 ───

ftxui::Element ActorListWidget::renderTitle(){
    using namespace ftxui;
    return text(" Actors (" + std::to_string(actors_.size()) + ") ") | bold | color(cCyan());
}

ftxui::Element ActorListWidget::renderEmpty(){
    using namespace ftxui;
    return text(" No actors connected") | color(cDim()) | hcenter;
}

ftxui::Element ActorListWidget::renderTableHeader(){
    using namespace ftxui;
    return hbox({
        text(" ID ") | size(WIDTH, GREATER_THAN, 7) | bold,
        separator(),
        text(" NAME ") | size(WIDTH, GREATER_THAN, 24) | bold,
        separator(),
        text(" STATUS ") | size(WIDTH, GREATER_THAN, 10) | bold,
        separator(),
        text(" MAILBOX ") | bold,
    }) | color(cDim());
}

ftxui::Element ActorListWidget::renderRow(const ActorInfo& a, int cbIdx, int cells){
    using namespace ftxui;
    float progress = (a.mailboxCapacity > 0) ? ((float)a.mailboxCount / (float)a.mailboxCapacity) : 0.0f;
    
    // PID
    auto idElem = text(std::to_string(a.id)) | color(cBlue()) | size(WIDTH, GREATER_THAN, 3);
    
    // State marker
    Element stateElem;
    if(a.essential){
        stateElem = text("[*]") | color(cDim()) | size(WIDTH, GREATER_THAN, 4);
    }else{
        auto& entry = checkboxes_[cbIdx];
        auto& box = boxByCbIdx_[cbIdx];
        stateElem = entry.component->Render() | reflect(box) | size(WIDTH, GREATER_THAN, 4);
    }

    // Name
    auto nameElem = text(" " + a.name + " ") | color(cWhite()) | size(WIDTH, GREATER_THAN, 24);

    // Status
    bool running = (a.mailboxCount > 0);
    auto statusColor_ = running ? cGreen() : cDim();
    auto statusElem = hbox({
        text(running ? " ● " : " ○ ") | color(statusColor_),
        text(running ? "Active " : "Idle ") | color(statusColor_) | size(WIDTH, GREATER_THAN, 7),
    });

    // Mailbox gauge + text
    auto bar = cellGauge(progress, gaugeColor(progress), cells) | flex;
    float mailboxPercent = progress * 100.0f;
    auto mailboxElem = hbox({
        text(" " + std::to_string(a.mailboxCount) + "/" + std::to_string(a.mailboxCapacity) + " "),
        text("(" + fmtPercent(mailboxPercent) + "%) ") | color(gaugeColor(progress)),
    });

    return hbox({
        stateElem,
        idElem,
        separator(),
        nameElem,
        separator(),
        statusElem,
        separator(),
        bar,
        filler(),
        mailboxElem,
    });
}

ftxui::Element ActorListWidget::OnRender(){
    using namespace ftxui;
    int cells = gaugeCells();
    boxByCbIdx_.resize(checkboxes_.size());

    Elements rows;
    rows.push_back(renderTableHeader());
    rows.push_back(separatorHeavy());

    int cbIdx = 0;
    for(const auto& a : actors_){
        if(!a.essential){
            rows.push_back(renderRow(a, cbIdx, cells));
            ++cbIdx;
        }else{
            rows.push_back(renderRow(a, -1, cells));
        }
    }

    if(actors_.empty()){
        return window(renderTitle(), renderEmpty() | flex);
    }

    return window(renderTitle(), vbox(std::move(rows)) | flex);
}

// ─── 이벤트 ───

bool ActorListWidget::OnEvent(ftxui::Event event){
    if(!event.is_mouse() || (event.mouse().button != ftxui::Mouse::Left) || (event.mouse().motion != ftxui::Mouse::Pressed)){
        return false;
    }

    int x = event.mouse().x;
    int y = event.mouse().y;
    size_t cbIdx = 0;
    for(size_t ai = 0; ai < actors_.size(); ++ai){
        if(actors_[ai].essential) continue;
        if(cbIdx < boxByCbIdx_.size() && boxByCbIdx_[cbIdx].Contain(x, y)){
            bool wasOn = *checkboxes_[cbIdx].state;
            *checkboxes_[cbIdx].state = !wasOn;
            if(onToggle_){
                onToggle_(actors_[ai].name, wasOn);
            }
            return true;
        }
        ++cbIdx;
    }
    return false;
}

} // namespace tui
