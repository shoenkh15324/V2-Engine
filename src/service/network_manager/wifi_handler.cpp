#include "wifi_handler.hpp"
#include "core/common/log/log.hpp"
#include "core/common/util/return.hpp"

#if V2_PLATFORM_LINUX

WifiHandler::WifiHandler(){
    //
}

WifiHandler::~WifiHandler(){
    close();
}

int WifiHandler::open(sdbus::IConnection& connection, sdbus::IProxy& nmProxy){
    dbus_.connection = &connection;
    dbus_.nmProxy = &nmProxy;
    if(!findWirelessDevice()){ V2_LOG_WARN("No wireless device found");
        return Fail;
    }
    try{
        auto dev = dbus_.deviceProxy.get();
        if(dev){
            dev->uponSignal("AccessPointAdded")
                .onInterface("org.freedesktop.NetworkManager.Device.Wireless")
                .call([this](const sdbus::ObjectPath&){ V2_LOG_INFO("AP added, refreshing");
                    scan_.refreshPending.store(true);
                });
            dev->uponSignal("AccessPointRemoved")
                .onInterface("org.freedesktop.NetworkManager.Device.Wireless")
                .call([this](const sdbus::ObjectPath&){
                    scan_.refreshPending.store(true);
                });
            dev->uponSignal("PropertiesChanged")
                .onInterface("org.freedesktop.DBus.Properties")
                .call([this](const std::string& interfaceName, const std::map<std::string, sdbus::Variant>& changedProps, const std::vector<std::string>&){
                    if(interfaceName == "org.freedesktop.NetworkManager.Device.Wireless"){
                        scan_.refreshPending.store(true);
                    }
                });
        }
    }catch(const sdbus::Error& e){
        V2_LOG_ERROR("Failed to subscribe device signals: {}", e.what());
    }
    syncDeviceState();
    return Ok;
}
int WifiHandler::close(){
    scan_.refreshPending.store(false);
    conn_.activePath.clear();
    dbus_.devicePath.clear();
    dbus_.deviceProxy.reset();
    conn_.state = WifiState::Disconnected;
    V2_LOG_INFO("WifiHandler closed");
    return Ok;
}

// Wifi operations
void WifiHandler::requestScan(){
    if(dbus_.devicePath.empty()) return;
    if(conn_.state != WifiState::Disconnected && conn_.state != WifiState::Connected){ V2_LOG_WARN("Cannot scan in current state");
        return;
    }
    auto* dev = dbus_.deviceProxy.get();
    if(!dev) return;
    try{
        std::map<std::string, sdbus::Variant> options;
        dev->callMethod("RequestScan")
            .onInterface("org.freedesktop.NetworkManager.Device.Wireless")
            .withArguments(options);
        conn_.state = WifiState::Scanning;
        V2_LOG_INFO("Scan requested");
    }catch(const sdbus::Error& e){
        V2_LOG_ERROR("RequestScan failed: {}", e.what());
    }
}

void WifiHandler::refreshAps(){
    if(dbus_.devicePath.empty()) return;
    auto* dev = dbus_.deviceProxy.get();
    if(!dev) return;

    try{
        sdbus::Variant aps;
        dev->callMethod("Get")
            .onInterface("org.freedesktop.DBus.Properties")
            .withArguments("org.freedesktop.NetworkManager.Device.Wireless", "AccessPoints")
            .storeResultsTo(aps);
        auto apPaths = aps.get<std::vector<sdbus::ObjectPath>>();
        auto activeApPath = getActiveApPath();

        std::vector<WifiApInfo> results;
        for(const auto& apPath : apPaths){
            auto info = readApInfo(apPath);
            if(apPath == activeApPath) info.connected = true;
            results.push_back(std::move(info));
        }
        scan_.results = std::move(results);
    }catch(const sdbus::Error& e){
        V2_LOG_ERROR("refreshAps failed: {}", e.what());
    }
}

bool WifiHandler::addAndActivateConnection(const std::string& ssid, const std::string& password){
    if(dbus_.devicePath.empty()) return false;
    if(conn_.state != WifiState::Disconnected && conn_.state != WifiState::Connected){ V2_LOG_WARN("Cannot connect in current state: {}", static_cast<int>(conn_.state));
        return false;
    }
    conn_.state = WifiState::Connecting;

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
        dbus_.nmProxy->callMethod("AddAndActivateConnection")
            .onInterface("org.freedesktop.NetworkManager")
            .withArguments(settings, sdbus::ObjectPath(dbus_.devicePath), sdbus::ObjectPath("/"))
            .storeResultsTo(connPath, activeConnPath);
        conn_.activePath = activeConnPath;
        reconnect_.lastSsid = ssid;
        reconnect_.lastPassword = password;
        conn_.state = WifiState::Connected;
        V2_LOG_INFO("Connected to '{}', active conn: {}", ssid, conn_.activePath);
        return true;
    }catch(const sdbus::Error& e){ V2_LOG_ERROR("AddAndActivateConnection failed: {}", e.what());
        conn_.state = WifiState::Disconnected;
        return false;
    }
}

bool WifiHandler::disconnectDevice(){
    if(conn_.state != WifiState::Connected && conn_.state != WifiState::Connecting){ V2_LOG_WARN("Cannot disconnect in current state");
        return false;
    }
    conn_.state = WifiState::Disconnecting;
    auto* dev = dbus_.deviceProxy.get();
    if(!dev) return false;
    try{
        dev->callMethod("Disconnect").onInterface("org.freedesktop.NetworkManager.Device");
        conn_.activePath.clear();
        conn_.state = WifiState::Disconnected;
        V2_LOG_INFO("Disconnected");
        return true;
    }catch(const sdbus::Error& e){
        conn_.state = WifiState::Connected;
        V2_LOG_ERROR("Disconnect failed: {}", e.what());
        return false;
    }
}

void WifiHandler::autoReconnect(){
    if(reconnect_.enabled && conn_.state == WifiState::Disconnected && !reconnect_.lastSsid.empty()){ V2_LOG_INFO("Auto-Reconnecting to '{}'", reconnect_.lastSsid.c_str());
        addAndActivateConnection(reconnect_.lastSsid, reconnect_.lastPassword);
    }
}

// State queries
uint32_t WifiHandler::getDeviceState(){
    auto* dev = dbus_.deviceProxy.get();
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

std::string WifiHandler::getActiveApPath(){
    auto* dev = dbus_.deviceProxy.get();
    if(!dev) return {};
    try{
        sdbus::Variant v;
        dev->callMethod("Get")
            .onInterface("org.freedesktop.DBus.Properties")
            .withArguments("org.freedesktop.NetworkManager.Device.Wireless", "ActiveAccessPoint")
            .storeResultsTo(v);
        return std::string(v.get<sdbus::ObjectPath>());
    }catch(...){
        return {};
    }
}

WifiApInfo WifiHandler::readApInfo(const std::string& apPath){
    WifiApInfo info;
    info.objectPath = apPath;
    auto apProxy = sdbus::createProxy(*dbus_.connection, sdbus::ServiceName("org.freedesktop.NetworkManager"), sdbus::ObjectPath(apPath));
    auto readProp = [&](const std::string& prop)->sdbus::Variant{
        try{
            sdbus::Variant v;
            apProxy->callMethod("Get")
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
        info.bssid = readProp("HwAddress").get<std::string>();
    }catch(...){}
    try{ info.frequency = static_cast<uint16_t>(readProp("Frequency").get<uint32_t>()); }catch(...){}
    try{ info.maxBitrate = readProp("MaxBitrate").get<uint32_t>(); }catch(...){}
    try{ info.signalStrength = static_cast<int32_t>(readProp("Strength").get<uint8_t>()) - 100; }catch(...){}
    try{ info.mode = readProp("Mode").get<uint32_t>() == 0 ? "infrastructure" : "adhoc"; }catch(...){}
    try{
        auto wpaFlags = readProp("WpaFlags").get<uint32_t>();
        auto rsnFlags = readProp("RsnFlags").get<uint32_t>();
        info.security = flagsToSecurity(wpaFlags, rsnFlags);
    }catch(...){}
    return info;
}

std::string WifiHandler::readInterfaceName(){
    auto* dev = dbus_.deviceProxy.get();
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

std::string WifiHandler::readIp4Address(){
    auto* dev = dbus_.deviceProxy.get();
    if(!dev) return {};
    try{
        sdbus::Variant ipVar;
        dev->callMethod("Get")
            .onInterface("org.freedesktop.DBus.Properties")
            .withArguments("org.freedesktop.NetworkManager.Device", "Ip4Config")
            .storeResultsTo(ipVar);
        std::string ip4Path = std::string(ipVar.get<sdbus::ObjectPath>());
        if(ip4Path.empty() || ip4Path == "/") return {};
        auto ipProxy = sdbus::createProxy(*dbus_.connection, sdbus::ServiceName("org.freedesktop.NetworkManager"), sdbus::ObjectPath(ip4Path));
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

// Device discovery
bool WifiHandler::findWirelessDevice(){
    try{
        std::vector<sdbus::ObjectPath> devices;
        dbus_.nmProxy->callMethod("GetAllDevices")
            .onInterface("org.freedesktop.NetworkManager")
            .storeResultsTo(devices);
        for(const auto& devPath : devices){
            auto tempProxy = sdbus::createProxy(*dbus_.connection, sdbus::ServiceName("org.freedesktop.NetworkManager"), sdbus::ObjectPath(devPath));
            sdbus::Variant devTypeVar;
            tempProxy->callMethod("Get")
                .onInterface("org.freedesktop.DBus.Properties")
                .withArguments("org.freedesktop.NetworkManager.Device", "DeviceType")
                .storeResultsTo(devTypeVar);
            uint32_t devType = devTypeVar.get<uint32_t>();
            if(devType == 2){ // NM_DEVICE_TYPE_WIFI = 2
                dbus_.devicePath = devPath;
                dbus_.deviceProxy = std::move(tempProxy);
                return true;
            }
        }
    }catch(const sdbus::Error& e){
        V2_LOG_ERROR("GetAllDevices failed: {}", e.what());
    }
    return false;
}

// Utilities
std::string WifiHandler::ssidBytesToString(const std::vector<uint8_t>& ssid){
    return std::string(ssid.begin(), ssid.end());
}

std::string WifiHandler::flagsToSecurity(uint32_t wpaFlags, uint32_t rsnFlags){
    bool hasWpa = (wpaFlags & 0x10) != 0; // NM_WIFI_DEVICE_CAP_WPA
    bool hasRsn = (rsnFlags & 0x20) != 0; // NM_WIFI_DEVICE_CAP_RSN
    bool hasSae = (rsnFlags & 0x20000000) != 0; // NM_802_11_AP_SEC_FLAGS_KEY_MGMT_SAE
    if(hasSae) return "WPA3";
    if(hasWpa && hasRsn) return "WPA2/WPA3";
    if(hasRsn) return "WPA2";
    if(hasWpa) return "WPA";
    return "OPEN";
}

void WifiHandler::syncDeviceState(){
    uint32_t nmState = getDeviceState();
    conn_.state = mapDeviceState(nmState);
}

WifiState WifiHandler::mapDeviceState(uint32_t nmState){
    switch(nmState){
        case 10: // NM_DEVICE_STATE_UNMANAGED
        case 20: // NM_DEVICE_STATE_UNAVAILABLE
            return WifiState::Error;
        case 30: // NM_DEVICE_STATE_DISCONNECTED
            return WifiState::Disconnected;
        case 40: // NM_DEVICE_STATE_PREPARE
        case 50: // NM_DEVICE_STATE_CONFIG
        case 60: // NM_DEVICE_STATE_NEED_AUTH
        case 70: // NM_DEVICE_STATE_IP_CONFIG
        case 80: // NM_DEVICE_STATE_IP_CHECK
        case 90: // NM_DEVICE_STATE_SECONDARIES
            return WifiState::Connecting;
        case 100: // NM_DEVICE_STATE_ACTIVATED
            return WifiState::Connected;
        case 110: // NM_DEVICE_STATE_DEACTIVATING
            return WifiState::Disconnecting;
        case 120: // NM_DEVICE_STATE_FAILED
            return WifiState::Error;
        default:
            return WifiState::Error;
    }
}

#endif // V2_PLATFORM_LINUX
