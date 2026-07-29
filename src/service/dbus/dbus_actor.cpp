#include "dbus_actor.hpp"
#include "dbus_server_handler.hpp"
#include "dbus_client_handler.hpp"
#include "core/actor_system/runtime/i_actor_runtime.hpp"
#include "core/common/log/log.hpp"

#if V2_PLATFORM_LINUX

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
    switch(msg.id()){
    case MessageId::DbusRegisterMethod:
        serverHandler_->handleRegisterMethod(msg.as<DbusRegisterMethod>());
        break;
    case MessageId::DbusUnregisterMethod:
        serverHandler_->handleUnregisterMethod(msg.as<DbusUnregisterMethod>());
        break;
    case MessageId::DbusMethodCallResult:
        serverHandler_->handleMethodCallResult(msg.as<DbusMethodCallResult>());
        break;
    case MessageId::DbusProxyCallRequest:
        clientHandler_->handleProxyCallRequest(msg.as<DbusProxyCallRequest>());
        break;
    case MessageId::DbusSubscribeSignal:
        clientHandler_->handleSubscribeSignal(msg.as<DbusSubscribeSignal>());
        break;
    default:
        break;
    }
}

#endif // V2_PLATFORM_LINUX
