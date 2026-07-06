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
    handlers_["info"]  = [this](const auto& a) { return handleInfo(a); };
    handlers_["actor"] = [this](const auto& a) { return handleActor(a); };
    handlers_["pmu"] = [this](const auto& a) { return handlePmu(a); };
    handlers_["test"]  = [this](const auto& a) { return handleTest(a); };
    //
    state_ = Opened;
    return 0;
}

int CmdActor::close(){
    if(state_ == Closed) return 0;
    state_ = Closing;
    //
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

    std::ostringstream os;
    os << "name: " << V2_ENGINE_NAME << "\n"
       << "version: " << V2_ENGINE_VERSION << "\n"
       << "uptime: " << uptimeMs << "\n";
    return os.str();
}

namespace{

const char* stateString(ActorState s){
    switch(s){
        case Closed: return "Closed";
        case Closing: return "Closing";
        case Opening: return "Opening";
        case Opened: return "Opened";
        case Inherited: return "Inherited";
    }
    return "Unknown";
}

} // namespace

std::string CmdActor::handleActor(const std::vector<std::string>& args){
    auto* reg = actorContext()->actorRegistry();
    if(!reg) return "error: actor registry unavailable\n";
    std::string result;
    auto err = parseOptions(args, "d:e:l", [&](char opt, const std::string& val){
        switch(opt){
            case 'd':{
                auto* a = reg->findByName(val);
                if(!a){
                    result += "error: not found '" + val + "'\n";
                }else if(a->isEssential()){
                    result += "error: '" + val + "' is essential\n";
                }else{
                    if(a->close() == 0){
                        result += "ok: '" + val + "' disabled\n";
                    }else{
                        result += "error: disable failed\n";
                    }
                }
                break;
            }
            case 'e':{
                auto* a = reg->findByName(val);
                if(!a){
                    result += "error: not found '" + val + "'\n";
                }else{
                    if(a->open() == 0){
                        result += "ok: '" + val + "' enabled\n";
                    }else{
                        result += "error: enable failed\n";
                    }
                }
                break;
            }
            case 'l':{
                int n = 0;
                char buf[256];
                result += "actor_count: ";
                reg->forEachActor([&](Actor* a){ ++n; });
                result += std::to_string(n) + "\n\n";
                result += "  ID  NAME              STATE      ESSENTIAL\n";
                result += "  --- ----------------- ---------- ---------\n";
                reg->forEachActor([&](Actor* a){
                    std::snprintf(buf, sizeof(buf), "  %3lu  %-17s %-10s %s\n",
                        (unsigned long)a->id(), a->name().c_str(),
                        stateString(a->getState()),
                        a->isEssential() ? "yes" : "no");
                    result += buf;
                });
                break;
            }
        }
    });
    if(!err.empty()) return err;
    if(result.empty()) return "error: no action specified\n";
    return result;
}

std::string CmdActor::handlePmu(const std::vector<std::string>& args){
    bool json = false;
    parseOptions(args, "s", [&](char opt, const std::string& val){
        if(opt == 's') json = true;
    });
#if V2_PLATFORM_LINUX && defined(__aarch64__)
    PmuRsp5 pmu;
#else
    PmuMock pmu;
#endif
    pmu.open();
    PmuData d;
    pmu.readPmuData(d);
    pmu.close();

    if(json){
        return nlohmann::json(d).dump() + "\n";
    }
    std::ostringstream os;
    os << "ARM   : " << (d.clockArmHz / 1000000) << " MHz\n"
       << "Core  : " << (d.clockCoreHz / 1000000) << " MHz\n"
       << "V3D   : " << (d.clockV3dHz / 1000000) << " MHz\n"
       << "Temp  : " << d.tempCelsius << " °C\n"
       << "Vcore : " << d.voltCore << " V\n"
       << "Icore : " << d.currentVddCoreA << " A\n"
       << "Mem   : ARM=" << d.memArmMb << "M  GPU=" << d.memGpuMb << "M\n";
    if(d.throttled){
        os << "THROTTLED: 0x" << std::hex << d.throttled << std::dec << "\n";
    }else{
        os << "Throttle: 0x0 (OK)\n";
    }
    return os.str();
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
