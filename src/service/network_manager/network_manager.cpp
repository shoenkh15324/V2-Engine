#include "network_manager.hpp"
#include "core/common/log/log.hpp"
#include "core/common/util/return.hpp"
#include "core/actor_system/actor/actor_context.hpp"
#include "service/dbus/dbus_actor.hpp"

#if V2_PLATFORM_LINUX

NetmanagerActor::NetmanagerActor(const std::string& name, uint64_t id) : Actor(std::move(name), id){
    //
}

NetmanagerActor::~NetmanagerActor(){
    close();
}

int NetmanagerActor::open(){
    if(state_ != Closed) close();
    state_ = Opening;
    //
    auto* dbus = dynamic_cast<DbusActor*>(actorContext()->actorRegistry()->findByName("dbus_actor"));
    if(!dbus){ V2_LOG_ERROR("dbus_actor not found");
        state_ = Closed;
        return Fail;
    }
    connection_ = &dbus->connection();
    if(!findWirelessDevice()){
        V2_LOG_WARN("No wireless device found");
    }else{
        try{
            auto dev = deviceProxy();
            if(dev){
                dev->uponSignal("AccessPointAdded")
                    .onInterface("org.freedesktop.NetworkManager.Device.Wireless")
                    .call([this](const sdbus::ObjectPath&){ V2_LOG_INFO("AP added, refreshing");
                        refreshAps();
                    });
                dev->uponSignal("AccessPointRemoved")
                    .onInterface("org.freedesktop.NetworkManager.Device.Wireless")
                    .call([this](const sdbus::ObjectPath&){
                        refreshAps();
                    });
                dev->uponSignal("PropertiesChanged")
                    .onInterface("org.freedesktop.DBus.Properties")
                    .call([this](const std::string& interfaceName, const std::map<std::string, sdbus::Variant>& changedProps, const std::vector<std::string>&){
                        if(interfaceName == "org.freedesktop.NetworkManager.Device.Wireless"){
                            if(changedProps.count("LastScan")){ V2_LOG_INFO("Scan completed");
                                refreshAps();
                            }
                        }
                    });
            }
        }catch(const sdbus::Error& e){
            V2_LOG_ERROR("Failed to subscribe device signals: {}", e.what());
        }
    }
    //
    state_ = Opened;
    return Ok;
}

int NetmanagerActor::close(){
    if(state_ == Closed) return Ok;
    state_ = Closing;
    //
    proxies_.clear();
    deviceProxy_.reset();
    nmProxy_.reset();
    //
    state_ = Closed;
    return Ok;
}

void NetmanagerActor::handle(const Message& msg){
    if(state_ < Opened){ V2_LOG_ERROR("Actor is not opened"); return; }
    std::visit(overloaded{
        [this](const NetScanRequest&){
            requestScan();
        },
        [this](const NetConnectRequest& msg){
            addAndActivateConnection(msg.ssid, msg.password);
        },
        [this](const NetDisconnectRequest&){
            disconnectDevice();
        },
        [this](const NetStatusRequest&){
            handleStatus();
        },
        [](const auto&){}
    }, msg);
}

sdbus::IProxy* NetmanagerActor::nmProxy(){
    if(!nmProxy_){
        nmProxy_ = sdbus::createProxy(
            *connection_,
            sdbus::ServiceName("org.freedesktop.NetworkManager"),
            sdbus::ObjectPath("/org/freedesktop/NetworkManager")
        );
    }
    return nmProxy_.get();
}

sdbus::IProxy* NetmanagerActor::deviceProxy(){
    if(devicePath_.empty()) return nullptr;
    return proxyFor("org.freedesktop.NetworkManager", devicePath_);
}

sdbus::IProxy* NetmanagerActor::proxyFor(const std::string& destination, const std::string& objectPath){
    auto key = destination + ":" + objectPath;
    auto it = proxies_.find(key);
    if(it != proxies_.end()) return it->second.get();

    auto proxy = sdbus::createProxy(*connection_, sdbus::ServiceName(destination), sdbus::ObjectPath(objectPath));
    auto* ptr = proxy.get();
    proxies_[key] = std::move(proxy);
    return ptr;
}

void NetmanagerActor::requestScan(){
    if(devicePath_.empty()) return;
    auto* dev = deviceProxy();
    if(!dev) return;

    try{
        std::map<std::string, sdbus::Variant> options;
        dev->callMethod("RequestScan")
            .onInterface("org.freedesktop.NetworkManager.Device.Wireless")
            .withArguments(options);
        V2_LOG_INFO("Scan requested");
    }catch(const sdbus::Error& e){
        V2_LOG_ERROR("RequestScan failed: {}", e.what());
    }

}

std::string NetmanagerActor::addAndActivateConnection(const std::string& ssid, const std::string& password){
    if(devicePath_.empty()) return {};
    // Build a{sa{sv}} structure
    // connection section
    std::map<std::string, sdbus::Variant> connSection;
    connSection["type"] = sdbus::Variant(std::string("802-11-wireless"));
    connSection["id"] = sdbus::Variant(ssid);

    // 802-11-wireless section
    std::map<std::string, sdbus::Variant> wifiSection;
    {
        std::vector<uint8_t> ssidBytes(ssid.begin(), ssid.end());
        wifiSection["ssid"] = sdbus::Variant(ssidBytes);
        wifiSection["mode"] = sdbus::Variant(std::string("infrastructure"));
    }

    // 802-11-wireless-security section (if password provided)
    std::map<std::string, sdbus::Variant> secSection;
    if(!password.empty()){
        secSection["key-mgmt"] = sdbus::Variant(std::string("wpa-psk"));
        secSection["psk"] = sdbus::Variant(password);
    }

    std::map<std::string, std::map<std::string, sdbus::Variant>> settings;
    settings["connection"] = std::move(connSection);
    settings["802-11-wireless"] = std::move(wifiSection);
    if(!password.empty()){
        settings["802-11-wireless-security"] = std::move(secSection);
    }

    try{
        sdbus::ObjectPath connPath;
        sdbus::ObjectPath activeConnPath;
        nmProxy()->callMethod("AddAndActivateConnection")
            .onInterface("org.freedesktop.NetworkManager")
            .withArguments(settings, sdbus::ObjectPath(devicePath_), sdbus::ObjectPath("/"))
            .storeResultsTo(connPath, activeConnPath);
        activeConnectionPath_ = activeConnPath;
        V2_LOG_INFO("Connected to '{}', active conn: {}", ssid, activeConnectionPath_);
        return activeConnectionPath_;
    }catch(const sdbus::Error& e){ V2_LOG_ERROR("AddAndActivateConnection failed: {}", e.what());
        return {};
    }
}

void NetmanagerActor::disconnectDevice(){
    auto* dev = deviceProxy();
    if(!dev) return;
    try{
        dev->callMethod("Disconnect").onInterface("org.freedesktop.NetworkManager.Device");
        activeConnectionPath_.clear();
        V2_LOG_INFO("Disconnected");
    }catch(const sdbus::Error& e){
        V2_LOG_ERROR("Disconnect failed: {}", e.what());
    }
}

uint32_t NetmanagerActor::getDeviceState(){
    auto* dev = deviceProxy();
    if(!dev) return 0;
    try{
        sdbus::Variant v;
        dev->callMethod("Get")
            .onInterface("org.freedesktop.DBus.Properties")
            .withArguments("org.freedesktop.NetworkManager.Device", "State")
            .storeResultsTo(v);
        return v.get<uint32_t>();
    }catch(...){
        return 0;
    }
}

std::string NetmanagerActor::getActiveApPath(){
    auto* dev = deviceProxy();
    if(!dev) return {};
    try{
        sdbus::Variant v;
        dev->callMethod("Get")
            .onInterface("org.freedesktop.DBus.Properties")
            .withArguments("org.freedesktop.NetworkManager.Device.Wireless", "ActiveAccessPoint")
            .storeResultsTo(v);
        return v.get<std::string>();
    }catch(...){
        return {};
    }
}

ApInfo NetmanagerActor::readApInfo(const std::string& apPath){
    ApInfo info;
    info.objectPath = apPath;
    auto* proxy = proxyFor("org.freedesktop.NetworkManager", apPath);
    if(!proxy) return info;

    auto readProp = [&](const std::string& prop)->sdbus::Variant{
        try{
            sdbus::Variant v;
            proxy->callMethod("Get")
                .onInterface("org.freedesktop.DBus.Properties")
                .withArguments("org.freedesktop.NetworkManager.AccessPoint", prop)
                .storeResultsTo(v);
            return v;
        }catch(...){
            return {};
        }
    };
    try{
        auto ssidBytes = readProp("Ssid").get<std::vector<uint8_t>>();
        info.ssid = ssidBytesToString(ssidBytes);
    }catch(...){}
    try{ 
        auto bssidBytes = readProp("Bssid").get<std::vector<uint8_t>>();
        info.bssid = bssidBytesToString(bssidBytes);
    }catch(...){}
    try{ info.frequency = static_cast<uint16_t>(readProp("Frequency").get<uint32_t>()); }catch(...){}
    try{ info.maxBitrate = readProp("MaxBitrate").get<uint32_t>(); }catch(...){}
    try{ info.signalStrength = readProp("Strength").get<uint8_t>() * 100 / 255 - 100; }catch(...){}
    // NM Strength 0-100 → approximate dBm
    try{ info.mode = readProp("Mode").get<uint32_t>() == 0 ? "infrastructure" : "adhoc"; }catch(...){}
    // Security from WpaFlags / RsnFlags
    try{
        auto wpaFlags = readProp("WpaFlags").get<uint32_t>();
        auto rsnFlags = readProp("RsnFlags").get<uint32_t>();
        info.security = flagsToSecurity(wpaFlags, rsnFlags);
    }catch(...){}
    return info;
}

void NetmanagerActor::handleStatus(){
    NetStatusResult r;
    auto state = getDeviceState();
    r.connected = (state == 100); // NM_DEVICE_STATE_ACTIVATED
    r.state = deviceStateToString(state);
    if(devicePath_.empty()){
        sendMsg("cmd_actor", r);
        return;
    }
    r.interfaceName = readInterfaceName();
    if(!r.connected){
        sendMsg("cmd_actor", r);
        return;
    }
    auto apPath = getActiveApPath();
    if(!apPath.empty()){
        auto apInfo = readApInfo(apPath);
        r.ssid = apInfo.ssid;
        r.signalStrength = apInfo.signalStrength;
    }
    r.ipAddress = readIp4Address();
    sendMsg("cmd_actor", r);
}

std::string NetmanagerActor::readInterfaceName(){
    auto* dev = deviceProxy();
    if(!dev) return {};
    try{
        sdbus::Variant v;
        dev->callMethod("Get")
            .onInterface("org.freedesktop.DBus.Properties")
            .withArguments("org.freedesktop.NetworkManager.Device", "Interface")
            .storeResultsTo(v);
        return v.get<std::string>();
    }catch(...){
        return {};
    }
}

std::string NetmanagerActor::readIp4Address(){
    auto* dev = deviceProxy();
    if(!dev) return {};
    try{
        sdbus::Variant ipVar;
        dev->callMethod("Get")
            .onInterface("org.freedesktop.DBus.Properties")
            .withArguments("org.freedesktop.NetworkManager.Device", "Ip4Config")
            .storeResultsTo(ipVar);
        std::string ip4Path = ipVar.get<std::string>();
        if(ip4Path.empty() || ip4Path == "/") return {};
        auto* ipProxy = proxyFor("org.freedesktop.NetworkManager", ip4Path);
        sdbus::Variant addrVar;
        ipProxy->callMethod("Get")
            .onInterface("org.freedesktop.DBus.Properties")
            .withArguments("org.freedesktop.NetworkManager.IP4Config", "AddressData")
            .storeResultsTo(addrVar);
        auto addrs = addrVar.get<std::vector<std::map<std::string, sdbus::Variant>>>();
        if(addrs.empty()) return {};
        return addrs[0].at("address").get<std::string>();
    }catch(...){
        return {};
    }
}

std::string NetmanagerActor::ssidBytesToString(const std::vector<uint8_t>& ssid){
    return std::string(ssid.begin(), ssid.end());
}

std::string NetmanagerActor::bssidBytesToString(const std::vector<uint8_t>& bssid){
    if(bssid.size() != 6) return {};
    char buf[18];
    std::snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    return buf;
}

std::string NetmanagerActor::flagsToSecurity(uint32_t wpaFlags, uint32_t rsnFlags){
    bool hasWpa = (wpaFlags & 0x10) != 0; // NM_WIFI_DEVICE_CAP_WPA
    bool hasRsn = (rsnFlags & 0x20) != 0; // NM_WIFI_DEVICE_CAP_RSN
    bool hasSae = (rsnFlags & 0x20000000) != 0; // NM_802_11_AP_SEC_FLAGS_KEY_MGMT_SAE
    
    if(hasSae) return "WPA3";
    if(hasWpa && hasRsn) return "WPA2/WPA3";
    if(hasRsn) return "WPA2";
    if(hasWpa) return "WPA";
    return "OPEN";
}

bool NetmanagerActor::findWirelessDevice(){
    try{
        std::vector<sdbus::ObjectPath> devices;
        nmProxy()->callMethod("GetAllDevices")
            .onInterface("org.freedesktop.NetworkManager")
            .storeResultsTo(devices);
        for(const auto& devPath : devices){
            auto* proxy = proxyFor("org.freedesktop.NetworkManager", devPath);
            sdbus::Variant devTypeVar;
            proxy->callMethod("Get")
                .onInterface("org.freedesktop.DBus.Properties")
                .withArguments("org.freedesktop.NetworkManager.Device", "DeviceType")
                .storeResultsTo(devTypeVar);
            uint32_t devType = devTypeVar.get<uint32_t>();
            // NM_DEVICE_TYPE_WIFI = 2
            if(devType == 2){
                devicePath_ = devPath;
                return true;
            }
        }
    }catch(const sdbus::Error& e){
        V2_LOG_ERROR("GetAllDevices failed: {}", e.what());
    }
    return false;
}

void NetmanagerActor::refreshAps(){
    if(devicePath_.empty()) return;
    auto* dev = deviceProxy();
    if(!dev) return;

    try{
        sdbus::Variant aps;
        dev->callMethod("Get")
            .onInterface("org.freedesktop.DBus.Properties")
            .withArguments("org.freedesktop.NetworkManager.Device.Wireless", "AccessPoints")
            .storeResultsTo(aps);

        auto apPaths = aps.get<std::vector<sdbus::ObjectPath>>();
        auto activeApPath = getActiveApPath();
        std::vector<ApInfo> results;

        for(const auto& apPath : apPaths){
            auto info = readApInfo(apPath);
            if(apPath == activeApPath){
                info.connected = true;
            }
            results.push_back(std::move(info));
        }
        lastScanResults_ = std::move(results);
        sendMsg("cmd_actor", NetScanResult{lastScanResults_});
    }catch(const sdbus::Error& e){
        V2_LOG_ERROR("refreshAps failed: {}", e.what());
    }
}

const char* NetmanagerActor::deviceStateToString(uint32_t s){
    switch(s){
        case 10: return "unmanaged";
        case 20: return "unavailable";
        case 30: return "disconnected";
        case 40: return "prepare";
        case 50: return "config";
        case 60: return "need-auth";
        case 70: return "ip-config";
        case 80: return "ip-check";
        case 90: return "secondaries";
        case 100: return "connected";
        case 110: return "deactivating";
        case 120: return "failed";
        default: return "unknown";
    }
}

#endif // V2_PLATFORM_LINUX
