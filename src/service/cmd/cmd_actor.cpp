#include "cmd_actor.hpp"
#include "core/actor_system/actor/actor_context.hpp"
#include "core/actor_system/actor/i_actor_registry.hpp"
#include "core/actor_system/messages/cmd_messages.hpp"
#include "core/common/time/time.hpp"
#include "core/common/log/log.hpp"
#include "core/common/config/platform_config.h"
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

std::string CmdActor::handleActor(const std::vector<std::string>& args){
#if 1 // tmp disable
    return {};
#else
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
                    result += (a->close() == 0) ? "ok: disabled\n" : "error: disable failed\n";
                }
                break;
            }
            case 'e':{
                auto* a = reg->findByName(val);
                if(!a){
                    result += "error: not found\n";
                }else{
                    result += (a->open() == 0) ? "ok: enabled\n" : "error: enable failed\n";
                }
                break;
            }
            case 'l':{
                int n = 0;
                std::string listStr;
                reg->forEachActor([&](Actor* a){
                    ++n;
                    listStr += "  id:" + std::to_string(a->id())
                            + " name:" + a->name()
                            + " state:" + std::to_string(static_cast<int>(a->getState()))
                            + " essential:" + (a->isEssential() ? "yes" : "no") + "\n";
                });
                result = "actor_count: " + std::to_string(n) + "\n" + listStr + result;
                break;
            }
        }
    });
    if(!err.empty()) return err;
    if(result.empty()) return "error: no action specified\n";
    return result;
#endif
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
