#include "dbus_actor.hpp"
#include "core/actor_system/actor/actor_context.hpp"
#include "core/actor_system/runtime/dispatcher.hpp"

#if V2_PLATFORM_LINUX

DbusActor::DbusActor(const std::string& name, uint64_t id, const std::string& dbusName, const std::string& dbusObjectPath, const std::string& dbusInterfaceName)
    : Actor(std::move(name), id), busName_(dbusName), objectPath_(dbusObjectPath), interfaceName_(dbusInterfaceName){
}

DbusActor::~DbusActor(){
    close();
}

int DbusActor::open(){
    if(state_ != Closed) close();
    state_ = Opening;
    //
    //
    state_ = Opened;
    return Ok;
}

int DbusActor::close(){
    if(state_ == Closed) return Ok;
    state_ = Closing;
    //

    //
    state_ = Closed;
    return Ok;
}

void DbusActor::handle(const Message& msg){
    if(state_ < Opened){ V2_LOG_ERROR("Actor is not opened"); return; }
    std::visit(overloaded{
        [](const auto&){},
    }, msg);
}

#endif
