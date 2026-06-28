#pragma once
#include <string>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>
#include "service/monitor/monitor_data.hpp"

std::string fmtPct(float val);
ftxui::Color gaugeColor(float f);
ftxui::Element cellGauge(float progress, ftxui::Color col, int cells);

ftxui::Element renderHeader(bool connected, int actorCount, int clientCount);
ftxui::Element renderActorList(const std::vector<ActorInfo>& actors);
ftxui::Element renderSystemResources(const SystemResources& resources);
ftxui::Element renderFooter();
