#pragma once
#include <chrono>
#include <string>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>
#include "service/monitor/monitor_data.hpp"

namespace tui{

// Color palette — inline getter + static local (FTXUI Color::RGB is not constexpr)
inline const ftxui::Color& cCyan() { static const auto c = ftxui::Color::RGB(153, 229, 229); return c; }
inline const ftxui::Color& cGreen() { static const auto c = ftxui::Color::RGB(153, 255, 153); return c; }
inline const ftxui::Color& cYellow() { static const auto c = ftxui::Color::RGB(255, 255, 153); return c; }
inline const ftxui::Color& cRed() { static const auto c = ftxui::Color::RGB(255, 153, 153); return c; }
inline const ftxui::Color& cBlue() { static const auto c = ftxui::Color::RGB(153, 204, 255); return c; }
inline const ftxui::Color& cWhite() { static const auto c = ftxui::Color::RGB(249, 249, 249); return c; }
inline const ftxui::Color& cDim() { static const auto c = ftxui::Color::RGB(160, 160, 160); return c; }
inline const ftxui::Color& cGaugeBg() { static const auto c = ftxui::Color::RGB(94, 94, 102);  return c; }

std::string fmtPercent(float val);
std::string fmtUptime(uint64_t ms);
ftxui::Color gaugeColor(float f);
int gaugeCells();
ftxui::Element cellGauge(float progress, ftxui::Color col, int cells);

// TODO: 아래 render* 함수들은 Step 2~5에서 각 widget으로 이관 후 제거
ftxui::Element renderActorList(const std::vector<ActorInfo>& actors, std::vector<ftxui::Box>& outCheckboxes, std::vector<std::string>& outActorNames, std::vector<bool>& outActorStates);
ftxui::Element renderSystemResources(const SystemResources& resources);
ftxui::Element renderProcessInfo(const SystemResources& resources);

} // namespace tui
