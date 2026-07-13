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
    wifi_.open(*connection_);
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
            if(wifi_.consumeScanRefreshPending()){
                wifi_.refreshAps();
                sendMsg("cmd_actor", WifiScanResult{wifi_.lastScanResults()});
            }
            syncDeviceState();
        },
        [this](const WifiScanRequest&){
            wifi_.requestScan();
        },
        [this](const WifiConnectRequest& msg){
            wifi_.addAndActivateConnection(msg.ssid, msg.password);
        },
        [this](const WifiDisconnectRequest&){
            wifi_.disconnectDevice();
        },
        [this](const NmStatusRequest&){
            syncDeviceState();
        },
        [](const auto&){}
    }, msg);
}

void NetworkManagerActor::syncDeviceState(){
    WifiStatusResult r;
    auto state = wifi_.getDeviceState();
    r.connected = (state == 100);
    r.state = wifi_.deviceStateToString(state);
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
