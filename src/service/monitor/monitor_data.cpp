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
    data += "mem_rss_kb:" + std::to_string(snap.resources.memoryRssKb) + "\n";
    data += "mem_total_kb:" + std::to_string(snap.resources.memoryTotalKb) + "\n";
    data += "cpu_pct:" + std::to_string(snap.resources.cpuPercent) + "\n";
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
    }else if(line.compare(0, 11, "mem_rss_kb:") == 0){
        snap.resources.memoryRssKb = std::stoull(line.substr(11));
    }else if(line.compare(0, 13, "mem_total_kb:") == 0){
        snap.resources.memoryTotalKb = std::stoull(line.substr(13));
    }else if(line.compare(0, 8, "cpu_pct:") == 0){
        snap.resources.cpuPercent = std::stof(line.substr(8));
    }else{
        return false;
    }
    return true;
}
