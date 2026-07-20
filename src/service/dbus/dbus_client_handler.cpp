#include "dbus_client_handler.hpp"

#if V2_PLATFORM_LINUX
#include "core/common/log/log.hpp"

DbusClientHandler::DbusClientHandler(sdbus::IConnection& connection, Actor& actor) : connection_(connection), actor_(actor){
    //
}

DbusClientHandler::~DbusClientHandler(){
    close();
}

void DbusClientHandler::close(){
    signalSubscriptions_.clear();
    proxies_.clear();
}

void DbusClientHandler::handleProxyCallRequest(const DbusProxyCallRequest& msg){
    try{
        auto* proxy = createProxy(msg.destination, msg.objectPath);
        auto invoker = std::move(proxy->callMethod(msg.methodName).onInterface(msg.interfaceName));

        if(!msg.args.empty()) invoker.withArguments(msg.args);

        std::string result;
        invoker.storeResultsTo(result);
        actor_.sendMsg(msg.requesterActorName, DbusProxyCallResult{msg.callId, result, false});
    }catch(const sdbus::Error& e){ V2_LOG_ERROR("D-Bus proxy call failed: {}", e.what());
        actor_.sendMsg(msg.requesterActorName, DbusProxyCallResult{msg.callId, e.what(), true});
    }
}

void DbusClientHandler::handleSubscribeSignal(const DbusSubscribeSignal& msg){
    auto key = msg.destination + ":" + msg.objectPath + ":" + msg.interfaceName + "." + msg.signalName;
    if(signalSubscriptions_.count(key)) return;
    
    try{
        auto proxy = sdbus::createProxy(connection_, sdbus::ServiceName(msg.destination), sdbus::ObjectPath(msg.objectPath));
        std::string subscriber = msg.subscriberActorName;
        std::string dest = msg.destination;
        std::string objPath = msg.objectPath;
        std::string iface = msg.interfaceName;
        std::string signalName = msg.signalName;

        proxy->uponSignal(msg.signalName).onInterface(msg.interfaceName)
        .call([this, subscriber, dest, objPath, iface, signalName](const std::string& args){
            DbusSignalEvent event = {
                .destination = dest,
                .objectPath = objPath,
                .interfaceName = iface,
                .signalName = signalName,
                .args = args,
            };
            actor_.sendMsg(subscriber, std::move(event));
        });
        signalSubscriptions_[key] = {msg.subscriberActorName, std::move(proxy)};
        V2_LOG_INFO("Subscribed to D-Bus signal: %s", key);
    }catch(const sdbus::Error& e){
        V2_LOG_ERROR("Failed to subscribe D-Bus signal %s: %s", key, e.what());
    }
}

void DbusClientHandler::handleUnsubscribeSignal(const DbusSubscribeSignal& msg){
    auto key = msg.destination + ":" + msg.objectPath + ":" + msg.interfaceName + "." + msg.signalName;
    if(signalSubscriptions_.erase(key) > 0){
        V2_LOG_INFO("Unsubscribed from D-Bus signal: %s", key);
    }else{
        V2_LOG_WARN("Signal subscription not found: %s", key);
    }
}

sdbus::IProxy* DbusClientHandler::createProxy(const std::string& destination, const std::string& objectPath){
    auto key = destination + ":" + objectPath;
    auto it = proxies_.find(key);
    if(it != proxies_.end()) return it->second.get();

    auto proxy = sdbus::createProxy(connection_, sdbus::ServiceName(destination), sdbus::ObjectPath(objectPath));
    auto* ptr = proxy.get();
    proxies_[key] = std::move(proxy);
    return ptr;
}


#endif // V2_PLATFORM_LINUX
