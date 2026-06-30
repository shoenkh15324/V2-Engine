#include "dbus_server_handler.hpp"

#if V2_PLATFORM_LINUX
#include "core/common/log/log.hpp"

DbusServerHandler::DbusServerHandler(sdbus::IConnection& connection, Actor& actor) : connection_(connection), actor_(actor){

}

DbusServerHandler::~DbusServerHandler(){
    close();
}

void DbusServerHandler::close(){
    methods_.clear();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingMethodCalls_.clear();
    }
}

void DbusServerHandler::handleRegisterMethod(const DbusRegisterMethod& msg){
    auto key = msg.interfaceName + "." + msg.methodName;
    if(methods_.count(key)){ V2_LOG_WARN("D-Bus method already registered: {}", key);
        return;
    }
    try{
        auto obj = sdbus::createObject(connection_, msg.objectPath);
        std::string owner = msg.ownerActorName;
        std::string objPath = msg.objectPath;
        std::string iface = msg.interfaceName;
        std::string method = msg.methodName;

        obj->registerMethod(iface, method, "s", "s", [this, owner, objPath, iface, method](sdbus::MethodCall call){
            std::string args;
            try{
                call >> args;
            }catch(const sdbus::Error& e){ V2_LOG_ERROR("Failed to deserialize D-Bus call args: {}", e.what());
                auto reply = call.createErrorReply(sdbus::Error(e.getName(), e.getMessage()));
                reply.send();
                return;
            }

            auto callId = nextCallId_.fetch_add(1);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                pendingMethodCalls_[callId] = std::move(call);
            }
            DbusIncomingMethodCall msg = {
                .callId = callId,
                .objectPath = objPath,
                .interfaceName = iface,
                .methodName = method,
                .args = std::move(args),
                .senderActorName = actor_.name(),
            };
            actor_.sendMsg(owner, std::move(msg));
        });
        obj->finishRegistration();
        methods_[key] = RegisteredMethod{msg.ownerActorName, std::move(obj)};
        V2_LOG_INFO("Registered D-Bus method: {}", key);
    }catch(const sdbus::Error& e){
        V2_LOG_ERROR("Failed to register D-Bus method {}: {}", key, e.what());
    }
}

void DbusServerHandler::handleUnregisterMethod(const DbusUnregisterMethod& msg){
    auto key = msg.interfaceName + "." + msg.methodName;
    if(methods_.erase(key) > 0){ 
        V2_LOG_INFO("Unregistered D-Bus method: {}", key);
    }else{
        V2_LOG_WARN("D-Bus method not found for unregister: {}", key);
    }
}

void DbusServerHandler::handleMethodCallResult(const DbusMethodCallResult& msg){
    sdbus::MethodCall call;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = pendingMethodCalls_.find(msg.callId);
        if(it == pendingMethodCalls_.end()){ V2_LOG_WARN("Pending call not found: {}", msg.callId);
            return;
        }
        call = std::move(it->second);
        pendingMethodCalls_.erase(it);
    }
    try{
        if(msg.isError){
            auto reply = call.createErrorReply(sdbus::Error("", msg.result));
            reply.send();
        }else{
            auto reply = call.createReply();
            reply << msg.result;
            reply.send();
        }
    }catch(const sdbus::Error& e){
        V2_LOG_ERROR("Failed to send D-Bus reply: {}", e.what());
    }
}

#endif // V2_PLATFORM_LINUX
