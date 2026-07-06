#include "sys_linux.hpp"
#include "core/common/time/time.hpp"
#include <fstream>
#include <ctime>

int SysLinux::open(){
    return 0;
}

int SysLinux::close(){
    cpuHistory_.clear();
    return 0;
}

int SysLinux::collect(SystemResources& data){
    data = {};

    std::ifstream status("/proc/self/status");
    std::string line;
    while(std::getline(status, line)){
        if(line.compare(0, 6, "VmRSS:") == 0){
            data.memoryRssKb = std::stoul(line.substr(6));
        }else if(line.compare(0, 7, "VmSize:") == 0){
            data.memoryTotalKb = std::stoul(line.substr(7));
        }else if(line.compare(0, 8, "Threads:") == 0){
            data.threadCount = std::stoul(line.substr(8));
        }else if(line.compare(0, 7, "VmPeak:") == 0){
            data.vmPeakKb = std::stoul(line.substr(7));
        }else if(line.compare(0, 6, "VmHWM:") == 0){
            data.vmHwmKb = std::stoul(line.substr(6));
        }else if(line.compare(0, 7, "VmSwap:") == 0){
            data.vmSwapKb = std::stoul(line.substr(7));
        }
    }

    std::ifstream loadavg("/proc/loadavg");
    if(loadavg){
        loadavg >> data.loadAvg1 >> data.loadAvg5 >> data.loadAvg15;
    }

    std::ifstream meminfo("/proc/meminfo");
    while(std::getline(meminfo, line)){
        if(line.compare(0, 9, "MemTotal:") == 0){
            data.sysMemTotalKb = std::stoull(line.substr(9));
        }else if(line.compare(0, 13, "MemAvailable:") == 0){
            data.sysMemAvailKb = std::stoull(line.substr(13));
        }
    }

    struct timespec ts;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    uint64_t cpuNs = (uint64_t)ts.tv_sec * 1000000000 + (uint64_t)ts.tv_nsec;
    uint64_t nowWall = Time::nowMs();

    cpuHistory_.push_back({cpuNs, nowWall});
    while(cpuHistory_.size() > 1 && nowWall - cpuHistory_.front().wallMs > kCpuWindowMs)
        cpuHistory_.pop_front();

    if(cpuHistory_.size() >= 2){
        auto& first = cpuHistory_.front();
        auto& last = cpuHistory_.back();
        uint64_t deltaCpuNs = last.cpuNs - first.cpuNs;
        uint64_t deltaWall = last.wallMs - first.wallMs;
        if(deltaWall > 0){
            data.cpuPercent = (double)deltaCpuNs * 100.0 / (deltaWall * 1000000.0);
            if(data.cpuPercent > 100.0f) data.cpuPercent = 100.0f;
        }
    }

    return 0;
}
