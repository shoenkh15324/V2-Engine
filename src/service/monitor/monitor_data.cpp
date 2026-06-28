#include "monitor_data.hpp"
#include <sstream>

static const char* kBeginMarker = "SNAPSHOT_BEGIN v1";
static const char* kEndMarker   = "SNAPSHOT_END";

std::string serializeSnapshot(const MonitorSnapshot& snap){
    std::string data;
    data += kBeginMarker;
    data += '\n';
    data += "timestamp:" + std::to_string(snap.timestampMs) + "\n";
    data += "clients:" + std::to_string(snap.clientCount) + "\n";
    for(const auto& actor : snap.actors){
        data += "actor id:" + std::to_string(actor.id) +
                " name:" + actor.name +
                " mb_used:" + std::to_string(actor.mailboxCount) +
                " mb_cap:" + std::to_string(actor.mailboxCapacity) + "\n";
    }
    auto& r = snap.resources;
    data += "mem_rss_kb:" + std::to_string(r.memoryRssKb) + "\n";
    data += "mem_total_kb:" + std::to_string(r.memoryTotalKb) + "\n";
    data += "cpu_pct:" + std::to_string(r.cpuPercent) + "\n";
    data += "uptime_ms:" + std::to_string(r.uptimeMs) + "\n";
    data += "threads:" + std::to_string(r.threadCount) + "\n";
    data += "vm_peak_kb:" + std::to_string(r.vmPeakKb) + "\n";
    data += "vm_hwm_kb:" + std::to_string(r.vmHwmKb) + "\n";
    data += "vm_swap_kb:" + std::to_string(r.vmSwapKb) + "\n";
    data += "load_avg1:" + std::to_string(r.loadAvg1) + "\n";
    data += "load_avg5:" + std::to_string(r.loadAvg5) + "\n";
    data += "load_avg15:" + std::to_string(r.loadAvg15) + "\n";
    data += "sys_mem_total_kb:" + std::to_string(r.sysMemTotalKb) + "\n";
    data += "sys_mem_avail_kb:" + std::to_string(r.sysMemAvailKb) + "\n";
    data += kEndMarker;
    data += '\n';
    return data;
}

bool isSnapshotBegin(const std::string& line){
    return line.compare(0, 15, "SNAPSHOT_BEGIN ") == 0;
}

bool isSnapshotEnd(const std::string& line){
    return line == kEndMarker;
}

bool parseSnapshotLine(const std::string& line, MonitorSnapshot& snap){
    static_cast<void>(snap);
    if(line.compare(0, 10, "timestamp:") == 0){
        snap.timestampMs = std::stoull(line.substr(10));
    }else if(line.compare(0, 8, "clients:") == 0){
        snap.clientCount = std::stoi(line.substr(8));
    }else if(line.compare(0, 6, "actor ") == 0){
        auto rest = line.substr(6);
        std::istringstream ss(rest);
        std::string token;
        ActorInfo info;
        while(ss >> token){
            auto colon = token.find(':');
            if(colon == std::string::npos) continue;
            auto key = token.substr(0, colon);
            auto val = token.substr(colon + 1);
            if(key == "id") info.id = std::stoull(val);
            else if(key == "name") info.name = val;
            else if(key == "mb_used") info.mailboxCount = std::stoul(val);
            else if(key == "mb_cap") info.mailboxCapacity = std::stoul(val);
        }
        snap.actors.push_back(std::move(info));
    }else{
        auto& r = snap.resources;
        if(line.compare(0, 11, "mem_rss_kb:") == 0){
            r.memoryRssKb = std::stoull(line.substr(11));
        }else if(line.compare(0, 13, "mem_total_kb:") == 0){
            r.memoryTotalKb = std::stoull(line.substr(13));
        }else if(line.compare(0, 8, "cpu_pct:") == 0){
            r.cpuPercent = std::stof(line.substr(8));
        }else if(line.compare(0, 10, "uptime_ms:") == 0){
            r.uptimeMs = std::stoull(line.substr(10));
        }else if(line.compare(0, 8, "threads:") == 0){
            r.threadCount = std::stoull(line.substr(8));
        }else if(line.compare(0, 11, "vm_peak_kb:") == 0){
            r.vmPeakKb = std::stoull(line.substr(11));
        }else if(line.compare(0, 10, "vm_hwm_kb:") == 0){
            r.vmHwmKb = std::stoull(line.substr(10));
        }else if(line.compare(0, 11, "vm_swap_kb:") == 0){
            r.vmSwapKb = std::stoull(line.substr(11));
        }else if(line.compare(0, 10, "load_avg1:") == 0){
            r.loadAvg1 = std::stof(line.substr(10));
        }else if(line.compare(0, 10, "load_avg5:") == 0){
            r.loadAvg5 = std::stof(line.substr(10));
        }else if(line.compare(0, 11, "load_avg15:") == 0){
            r.loadAvg15 = std::stof(line.substr(11));
        }else if(line.compare(0, 17, "sys_mem_total_kb:") == 0){
            r.sysMemTotalKb = std::stoull(line.substr(17));
        }else if(line.compare(0, 17, "sys_mem_avail_kb:") == 0){
            r.sysMemAvailKb = std::stoull(line.substr(17));
        }else{
            return false;
        }
    }
    return true;
}
