#pragma once
#include <string>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>
#include "service/monitor/monitor_data.hpp"

std::string fmtPct(float val);
std::string fmtUptime(uint64_t ms);
ftxui::Color gaugeColor(float f);
ftxui::Element cellGauge(float progress, ftxui::Color col, int cells);

ftxui::Element renderHeader(bool connected, int actorCount, int clientCount, uint64_t uptimeMs, float cpuPct, float memPct);
ftxui::Element renderActorList(const std::vector<ActorInfo>& actors);
ftxui::Element renderSystemResources(const SystemResources& resources);
ftxui::Element renderProcessInfo(const SystemResources& resources);
ftxui::Element renderFooter();
