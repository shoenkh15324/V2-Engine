#include "cmd_actor.hpp"
#include "core/actor_system/actor/actor_context.hpp"
#include "core/actor_system/actor/i_actor_registry.hpp"
#include "core/actor_system/messages/cmd_messages.hpp"
#include "core/common/time/time.hpp"
#include "core/common/log/log.hpp"
#include "core/common/config/platform_config.h"
#include "infra/hal/pmu/pmu_rsp5.hpp"
#include "infra/hal/pmu/pmu_mock.hpp"
#include "service/monitor/monitor_data.hpp"
#include <sstream>
#include <iomanip>

CmdActor::CmdActor(const std::string& name, uint64_t id) : Actor(std::move(name), id){
    //
}

CmdActor::~CmdActor(){
    close();
}

int CmdActor::open(){
    if(state_ != Closed) close();
    state_ = Opening;
    //
    startTimeMs_ = Time::nowMs();
    handlers_["info"]  = [this](const auto& a){ return handleInfo(a); };
    handlers_["actor"] = [this](const auto& a){ return handleActor(a); };
    handlers_["pmu"] = [this](const auto& a){ return handlePmu(a); };
    handlers_["wifi"] = [this](const auto& a){ return handleWifi(a); };
    handlers_["test"]  = [this](const auto& a){ return handleTest(a); };
    //
    pmu_ = []() -> std::unique_ptr<IPmu> {
#if V2_PLATFORM_LINUX && defined(__aarch64__)
        return std::make_unique<PmuRsp5>();
#else
        return std::make_unique<PmuMock>();
#endif
    }();
    if(pmu_) pmu_->open();
    //
    state_ = Opened;
    return 0;
}

int CmdActor::close(){
    if(state_ == Closed) return 0;
    state_ = Closing;
    //
    if(pmu_){ pmu_->close(); pmu_.reset(); }
    //
    state_ = Closed;
    return 0;
}

void CmdActor::handle(const Message& msg){
    if(state_ < Opened){ V2_LOG_ERROR("Actor is not opened"); return; }
    std::visit(overloaded{
        [this](const CmdRequest& msg){
            auto response = dispatch(msg.cmd);
            sendMsg("ipc_server", CmdResponse{msg.conn, std::move(response)});
        },
        [this](const NetScanResult& msg){ lastScan_ = msg; },
        [this](const NetStatusResult& msg){ lastStatus_ = msg; },
        [](const auto&){}
    }, msg);
}

std::string CmdActor::dispatch(const std::string& cmd){
    auto s = cmd;
    auto space = s.find(' ');
    std::string name = (space == std::string::npos) ? std::move(s) : s.substr(0, space);

    auto it = handlers_.find(name);
    if(it == handlers_.end()) return "error: unknown command '" + name + "'\n";

    std::vector<std::string> args;
    if(space != std::string::npos){
        auto rest = std::string_view(cmd).substr(space + 1);
        while(!rest.empty()){
            auto start = rest.find_first_not_of(' ');
            if(start == std::string_view::npos) break;
            rest.remove_prefix(start);
            auto end = rest.find(' ');
            args.emplace_back(rest.substr(0, end));
            if(end == std::string_view::npos) break;
            rest.remove_prefix(end + 1);
        }
    }
    return it->second(args);
}

std::string CmdActor::handleInfo(const std::vector<std::string>&){
    auto nowMs = Time::nowMs();
    uint64_t uptimeMs = 0;
    if(nowMs >= startTimeMs_) uptimeMs = static_cast<uint64_t>(nowMs - startTimeMs_);

    int s = static_cast<int>(uptimeMs / 1000);
    int m = s / 60; s %= 60;
    int h = m / 60; m %= 60;
    int d = h / 24; h %= 24;

    std::ostringstream oss;
    oss << "name: " << V2_ENGINE_NAME << "\n"
       << "version: " << V2_ENGINE_VERSION << "\n"
       << "uptime: " << d << "d "
       << (h < 10 ? "0" : "") << h << "h "
       << (m < 10 ? "0" : "") << m << "m "
       << (s < 10 ? "0" : "") << s << "s\n";
    return oss.str();
}

std::string CmdActor::handleActor(const std::vector<std::string>& args){
    std::string result;
    auto err = parseOptions(args, "d:e:l", [&](char opt, const std::string& val){
        switch(opt){
            case 'd': case 'e':{
                auto* reg = actorContext()->actorRegistry();
                if(!reg){ result += "error: actor registry unavailable\n"; return; }
                int ret = (opt == 'e') ? reg->enableActor(val) : reg->disableActor(val);
                if(ret == 0){
                    result += "ok: '" + val + "' " + (opt == 'e' ? "enabled" : "disabled") + "\n";
                }else if(ret == -1){
                    result += "error: not found '" + val + "'\n";
                }else if(ret == -2){
                    result += "error: '" + val + "' is essential\n";
                }else{
                    result += "error: " + std::string(opt == 'e' ? "enable" : "disable") + " failed\n";
                }
                return;
            }
            case 'l':{
                auto* reg = actorContext()->actorRegistry();
                if(!reg){ result += "error: actor registry unavailable\n"; return; }
                int n = 0;
                reg->forEachActor([&](Actor*){ ++n; });
                char buf[256];
                result += "actor_count: " + std::to_string(n) + "\n\n"
                          "  ID  NAME              STATE      ESSENTIAL\n"
                          "  --- ----------------- ---------- ---------\n";
                reg->forEachActor([&](Actor* a){
                    const char* st = "Unknown";
                    switch(a->getState()){
                        case Closed: st = "Closed"; break;
                        case Closing: st = "Closing"; break;
                        case Opening: st = "Opening"; break;
                        case Opened: st = "Opened"; break;
                        case Inherited: st = "Inherited"; break;
                    }
                    std::snprintf(buf, sizeof(buf), "  %3lu  %-17s %-10s %s\n",
                        (unsigned long)a->id(), a->name().c_str(),
                        st,
                        a->isEssential() ? "yes" : "no");
                    result += buf;
                });
                return;
            }
        }
    });
    if(!err.empty()) return err;
    if(result.empty()) return "error: no action specified\n";
    return result;
}

std::string CmdActor::handlePmu(const std::vector<std::string>& args){
    if(!pmu_) return "error: pmu unavailable\n";
    PmuData d;
    pmu_->readPmuData(d);

    std::ostringstream oss;
    oss << "ARM   : " << (d.clockArmHz / 1000000) << " MHz\n"
       << "Core  : " << (d.clockCoreHz / 1000000) << " MHz\n"
       << "V3D   : " << (d.clockV3dHz / 1000000) << " MHz\n"
       << "Temp  : " << d.tempCelsius << " °C\n"
       << "Vcore : " << d.voltCore << " V\n"
       << "Icore : " << d.currentVddCoreA << " A\n"
       << "Mem   : ARM=" << d.memArmMb << "M  GPU=" << d.memGpuMb << "M\n"
       << (d.throttled ? "THROTTLED" : "Throttle")
       << ": 0x" << std::hex << d.throttled << std::dec
       << (d.throttled ? " (THROTTLED!)\n" : " (OK)\n");
    return oss.str();
}

std::string CmdActor::handleWifi(const std::vector<std::string>& args){
    if(args.empty()) return "error: missing subcommand\n";

    // flag-based subcommand
    if(args[0].size() == 2 && args[0][0] == '-'){
        char opt = args[0][1];
        switch(opt){
            case 'c':
                if(args.size() < 2) return "error: wifi -c <ssid> [password]\n";
                sendMsg("network_manager", NetConnectRequest{args[1], args.size() >= 3 ? args[2] : ""});
                return "Connecting to '" + args[1] + "'...\n";
            case 'l':
                if(lastScan_.accessPoints.empty()) return "No scan results. Try 'wifi scan' first.\n";
                return formatApList();
            case 's':
                sendMsg("network_manager", NetStatusRequest{});
                if(!lastStatus_.connected) return "Disconnected\n";
                return formatStatus();
            case 'd':
                sendMsg("network_manager", NetDisconnectRequest{});
                return "Disconnecting...\n";
            default:
                return "error: unknown wifi option '-" + std::string(1, opt) + "'\n";
        }
    }

    // subcommand-based
    auto& cmd = args[0];
    if(cmd == "scan"){
        sendMsg("network_manager", NetScanRequest{});
        return "Scanning...\n";
    }
    if(cmd == "list"){
        if(lastScan_.accessPoints.empty()) return "No scan results. Try 'wifi scan' first.\n";
        return formatApList();
    }
    if(cmd == "connect"){
        if(args.size() < 2) return "error: wifi connect <ssid> [password]\n";
        sendMsg("network_manager", NetConnectRequest{args[1], args.size() >= 3 ? args[2] : ""});
        return "Connecting to '" + args[1] + "'...\n";
    }
    if(cmd == "disconnect"){
        sendMsg("network_manager", NetDisconnectRequest{});
        return "Disconnecting...\n";
    }
    if(cmd == "status"){
        sendMsg("network_manager", NetStatusRequest{});
        if(!lastStatus_.connected) return "Disconnected\n";
        return formatStatus();
    }
    return "error: unknown wifi subcommand '" + cmd + "'\n";
}

std::string CmdActor::formatApList(){
    std::ostringstream oss;
    oss << std::left << std::setw(25) << "SSID"
       << std::setw(18) << "BSSID"
       << std::setw(6) << "SEC"
       << std::setw(5) << "SIG"
       << std::setw(8) << "FREQ"
       << "\n" << std::string(62, '-') << "\n";
    for(auto& ap : lastScan_.accessPoints){
        oss << std::setw(25) << ap.ssid
           << std::setw(18) << ap.bssid
           << std::setw(6) << ap.security
           << std::setw(5) << ap.signalStrength
           << std::setw(8) << ap.frequency
           << "\n";
    }
    return oss.str();
}

std::string CmdActor::formatStatus(){
    std::ostringstream oss;
    oss << "SSID: " << lastStatus_.ssid << "\n"
        << "IP: " << lastStatus_.ipAddress << "\n"
        << "Signal: " << lastStatus_.signalStrength << "\n"
        << "State: " << lastStatus_.state << "\n"
        << "Interface: " << lastStatus_.interfaceName << "\n";
    return oss.str();
}

std::string CmdActor::handleTest(const std::vector<std::string>& args){
    std::string result;
    result += "args: [";
    for(size_t i = 0; i < args.size(); ++i){
        if(i > 0) result += ", ";
        result += "'" + args[i] + "'";
    }
    result += "]\n";

    auto parsed = parseOptions(args, "d:e:l", [&](char opt, const std::string& val){
        result += "  option: '" + std::string(1, opt) + "'";
        if(!val.empty()) result += " -> value: '" + val + "'";
        result += "\n";
    });

    if(!parsed.empty()){
        result += "  error: " + parsed;
    }
    return result;
}

std::string CmdActor::parseOptions(const std::vector<std::string>& args, std::string_view optstring, const OnOption& onOption){
    for(size_t i = 0; i < args.size(); ++i){
        const auto& arg = args[i];
        if(arg.size() < 2 || arg[0] != '-'){
            return "error: unexpected argument '" + arg + "'\n";
        }
        char opt = arg[1];
        auto pos = optstring.find(opt);
        if(pos == std::string_view::npos) return "error: invalid argument '" + arg + "'\n";
        if(pos + 1 < optstring.size() && optstring[pos + 1] == ':'){
            if(i + 1 >= args.size()) return "error: option '-" + std::string(1, opt) + "' requires an argument\n";
            onOption(opt, args[++i]);
        }else{
            onOption(opt, "");
        }
    }
    return {};
}
