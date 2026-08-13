#include "dbus_actor.hpp"
#include "dbus_server_handler.hpp"
#include "dbus_client_handler.hpp"
#include "core/actor_system/runtime/actor_runtime/i_actor_runtime.hpp"
#include "core/common/log/log.hpp"

#if V2_PLATFORM_LINUX

using DbusActorMessages = std::tuple<DbusRegisterMethod, DbusUnregisterMethod, DbusMethodCallResult, DbusProxyCallRequest, DbusSubscribeSignal>;

DbusActor::DbusActor(std::string name, uint64_t id, std::string dbusName, std::string dbusObjectPath, std::string dbusInterfaceName)
    : Actor(std::move(name), id), busName_(std::move(dbusName)), objectPath_(std::move(dbusObjectPath)), interfaceName_(std::move(dbusInterfaceName)){
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
    }catch(const sdbus::Error& e){ V2_LOG_ERROR("Failed to open D-Bus connection: {}", e.what());
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
    V2_LOG_INFO("Dbus Actor opened");
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
    V2_LOG_INFO("Dbus Actor closed");
    return Ok;
}

void DbusActor::handle(const Message& msg){
    if(state_ < Opened){ V2_LOG_ERROR("Actor is not opened"); return; }
    dispatch(*this, msg, DbusActorMessages{});
}

void DbusActor::handle(const DbusRegisterMethod& m){
    serverHandler_->handleRegisterMethod(m);
}

void DbusActor::handle(const DbusUnregisterMethod& m){
    serverHandler_->handleUnregisterMethod(m);
}

void DbusActor::handle(const DbusMethodCallResult& m){
    serverHandler_->handleMethodCallResult(m);
}

void DbusActor::handle(const DbusProxyCallRequest& m){
    clientHandler_->handleProxyCallRequest(m);
}

void DbusActor::handle(const DbusSubscribeSignal& m){
    clientHandler_->handleSubscribeSignal(m);
}

#endif // V2_PLATFORM_LINUX
