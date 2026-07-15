#include "network_manager.hpp"
#include "core/common/log/log.hpp"
#include "core/common/util/return.hpp"
#include "core/actor_system/actor/actor_context.hpp"
#include "service/dbus/dbus_actor.hpp"

#if V2_PLATFORM_LINUX

NetworkManagerActor::NetworkManagerActor(const std::string& name, uint64_t id) : Actor(std::move(name), id){
    //
}

NetworkManagerActor::~NetworkManagerActor(){
    close();
}

int NetworkManagerActor::open(){
    if(state_ != Closed) close();
    state_ = Opening;
    //
    auto* dbus = dynamic_cast<DbusActor*>(actorContext()->actorRegistry()->findByName("dbus_actor"));
    if(!dbus){ V2_LOG_ERROR("D-Bus actor is not found");
        state_ = Closed;
        return Fail;
    }
    connection_ = &dbus->connection();
    nmProxy_ = sdbus::createProxy(*connection_, sdbus::ServiceName("org.freedesktop.NetworkManager"), sdbus::ObjectPath("/org/freedesktop/NetworkManager"));
    for(int i = 0; i < 4; ++i){
        if(wifi_.open(*connection_, *nmProxy_) == Ok) break;
        V2_LOG_WARN("WiFi init retry %d/5...", i + 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    startTimer(Tick{}, wifiSyncIntervalMs_, true);
    //
    state_ = Opened;
    return Ok;
}

int NetworkManagerActor::close(){
    if(state_ == Closed) return Ok;
    state_ = Closing;
    //
    auto ids = timerIds_;
    for(int id : ids) cancelTimer(id);
    wifi_.close();
    connection_ = nullptr;
    //
    state_ = Closed;
    return Ok;
}

void NetworkManagerActor::handle(const Message& msg){
    if(state_ < Opened){ V2_LOG_ERROR("Actor is not opened"); return; }
    std::visit(overloaded{
        [this](const Tick&){
            wifi_.syncDeviceState();
            if(wifi_.consumeScanRefreshPending()){
                wifi_.refreshAps();
                sendMsg("cmd_actor", WifiScanResult{wifi_.lastScanResults()});
            }
            // Auto Reconnection
            wifi_.autoReconnect();
            reportStatus();
        },
        [this](const WifiScanRequest&){
            wifi_.requestScan();
        },
        [this](const WifiConnectRequest& msg){
            bool ok = wifi_.addAndActivateConnection(msg.ssid, msg.password);
            sendMsg("cmd_actor", WifiConnectResult{ok, ok ? "" : "Invalid state or device"});
        },
        [this](const WifiDisconnectRequest&){
            bool ok = wifi_.disconnectDevice();
            sendMsg("cmd_actor", WifiDisconnectResult{ok});
        },
        [this](const WifiAutoReconnectRequest& msg){
            wifi_.setAutoReconnect(msg.enable);
        },
        [this](const NmStatusRequest&){
            reportStatus();
        },
        [](const auto&){}
    }, msg);
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
    sendMsg("cmd_actor", std::move(r));
}

#endif // V2_PLATFORM_LINUX
