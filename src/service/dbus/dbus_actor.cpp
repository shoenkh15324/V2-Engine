#include "dbus_actor.hpp"
#include "dbus_server_handler.hpp"
#include "dbus_client_handler.hpp"
#include "core/actor_system/actor/actor_context.hpp"

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
    try{
        connection_ = sdbus::createSystemBusConnection(sdbus::ServiceName(busName_));
        connection_->enterEventLoopAsync();
        serverHandler_ = std::make_unique<DbusServerHandler>(*connection_, *this);
        clientHandler_ = std::make_unique<DbusClientHandler>(*connection_, *this);
    }catch(const sdbus::Error& e){ V2_LOG_ERROR("Failed to open D-Bus connection: %s", e.what());
        clientHandler_.reset();
        serverHandler_.reset();
        if(connection_){
            connection_->leaveEventLoop();
            connection_.reset();
        }
        state_ = Closed;
        return Fail;
    }
    //
    state_ = Opened;
    return Ok;
}

int DbusActor::close(){
    if(state_ == Closed) return Ok;
    state_ = Closing;
    //
    serverHandler_.reset();
    clientHandler_.reset();
    if(connection_){
        connection_->leaveEventLoop();
        connection_.reset();
    }
    //
    state_ = Closed;
    return Ok;
}

void DbusActor::handle(const Message& msg){
    if(state_ < Opened){ V2_LOG_ERROR("Actor is not opened"); return; }
    std::visit(overloaded{
        [this](const DbusRegisterMethod& msg){ serverHandler_->handleRegisterMethod(msg); },
        [this](const DbusUnregisterMethod& msg){ serverHandler_->handleUnregisterMethod(msg); },
        [this](const DbusMethodCallResult& msg){ serverHandler_->handleMethodCallResult(msg); },
        [this](const DbusProxyCallRequest& msg){ clientHandler_->handleProxyCallRequest(msg); },
        [this](const DbusSubscribeSignal& msg){ clientHandler_->handleSubscribeSignal(msg); },
        [](const auto&){},
    }, msg);

}

#endif // V2_PLATFORM_LINUX
