#include "network_manager_actor.hpp"
#include "service/tick/tick_messages.hpp"
#include "service/network_manager/network_manager_messages.hpp"
#include "service/network_manager/wifi_messages.hpp"
#include "core/common/log/log.hpp"
#include "core/common/util/return.hpp"
#include "core/actor_system/runtime/actor_runtime/i_actor_runtime.hpp"
#include "core/actor_system/actor/i_actor_registry.hpp"
#include "service/dbus/dbus_actor.hpp"

#if V2_PLATFORM_LINUX

NetworkManagerActor::NetworkManagerActor(std::string name, uint64_t id) : Actor(std::move(name), id){
    //
}

NetworkManagerActor::~NetworkManagerActor(){
    close();
}

int NetworkManagerActor::open(){
    if(state_ != Closed) close();
    state_ = Opening;
    //
    auto* dbus = dynamic_cast<DbusActor*>(runtime()->actorRegistry()->findActorByName("dbus"));
    if(!dbus || dbus->getState() != Opened){ V2_LOG_ERROR("D-Bus actor is not found");
        connection_ = nullptr;
        nmProxy_.reset();
        state_ = Closed;
        return Fail;
    }
    connection_ = &dbus->connection();
    nmProxy_ = sdbus::createProxy(*connection_, sdbus::ServiceName("org.freedesktop.NetworkManager"), sdbus::ObjectPath("/org/freedesktop/NetworkManager"));
    
    bool wifiOk = false;
    for(int i = 0; i < 4; ++i){
        if(wifi_.open(*connection_, *nmProxy_) == Ok){
            wifiOk = true;
            break;
        }
        V2_LOG_WARN("WiFi init retry {}/5...", i + 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    if(wifiOk) startTimer(Tick{}, wifiSyncIntervalMs_, true);
    //
    state_ = Opened;
    V2_LOG_INFO("Network Manager Actor opened");
    return Ok;
}

int NetworkManagerActor::close(){
    if(state_ == Closed) return Ok;
    state_ = Closing;
    //
    cancelAllTimers();
    wifi_.close();
    connection_ = nullptr;
    nmProxy_.reset();
    //
    state_ = Closed;
    V2_LOG_INFO("Network Manager Actor closed");
    return Ok;
}

void NetworkManagerActor::handle(const Message& msg){
    if(state_ < Opened){ V2_LOG_ERROR("Actor is not opened"); return; }
    switch(msg.id()){
    case MessageId::Tick:
        wifi_.syncDeviceState();
        if(wifi_.consumeScanRefreshPending()){
            wifi_.refreshAps();
            sendMsg("cmd", WifiScanResult{wifi_.lastScanResults()});
        }
        wifi_.autoReconnect();
        reportStatus();
        break;
    case MessageId::WifiScanRequest:
        wifi_.requestScan();
        break;
    case MessageId::WifiConnectRequest:{
        const auto& m = msg.as<WifiConnectRequest>();
        bool ok = wifi_.addAndActivateConnection(m.ssid, m.password);
        sendMsg("cmd", WifiConnectResult{ok, ok ? "" : "Invalid state or device"});
        break;
    }
    case MessageId::WifiDisconnectRequest:{
        bool ok = wifi_.disconnectDevice();
        sendMsg("cmd", WifiDisconnectResult{ok});
        break;
    }
    case MessageId::WifiAutoReconnectRequest:
        wifi_.setAutoReconnect(msg.as<WifiAutoReconnectRequest>().enable);
        break;
    case MessageId::NmStatusRequest:
        reportStatus();
        break;
    default:
        break;
    }
}

void NetworkManagerActor::reportStatus(){
    WifiStatusResult r;
    r.state = wifi_.state();
    r.autoReconnect = wifi_.getAutoReconnect();
    r.connected = (r.state == WifiState::Connected);
    if(wifi_.deviceFound()){
        r.interfaceName = wifi_.readInterfaceName();
        if(r.connected){
            auto apPath = wifi_.getActiveApPath();
            if(!apPath.empty()){
                auto apInfo = wifi_.readApInfo(apPath);
                r.ssid = apInfo.ssid;
                r.signalStrength = apInfo.signalStrength;
            }
            r.ipAddress = wifi_.readIp4Address();
        }
    }
    sendMsg("cmd", std::move(r));
}

#endif // V2_PLATFORM_LINUX
