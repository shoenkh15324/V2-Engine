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
    connection_ = &connection;
    nmProxy_ = &nmProxy;
    if(!findWirelessDevice()){ V2_LOG_WARN("No wireless device found");
        return Fail;
    }
    try{
        auto dev = deviceProxy_.get();
        if(dev){
            dev->uponSignal("AccessPointAdded")
                .onInterface("org.freedesktop.NetworkManager.Device.Wireless")
                .call([this](const sdbus::ObjectPath&){ V2_LOG_INFO("AP added, refreshing");
                    scanRefreshPending_.store(true);
                });
            dev->uponSignal("AccessPointRemoved")
                .onInterface("org.freedesktop.NetworkManager.Device.Wireless")
                .call([this](const sdbus::ObjectPath&){
                    scanRefreshPending_.store(true);
                });
            dev->uponSignal("PropertiesChanged")
                .onInterface("org.freedesktop.DBus.Properties")
                .call([this](const std::string& interfaceName, const std::map<std::string, sdbus::Variant>& changedProps, const std::vector<std::string>&){
                    if(interfaceName == "org.freedesktop.NetworkManager.Device.Wireless"){
                        scanRefreshPending_.store(true);
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
    scanRefreshPending_.store(false);
    activeConnectionPath_.clear();
    devicePath_.clear();
    deviceProxy_.reset();
    wifiState_ = WifiState::Disconnected;
    return Ok;
}

// Wifi operations
void WifiHandler::requestScan(){
    if(devicePath_.empty()) return;
    if(wifiState_ != WifiState::Disconnected && wifiState_ != WifiState::Connected){ V2_LOG_WARN("Cannot scan in current state");
        return;
    }
    auto* dev = deviceProxy_.get();
    if(!dev) return;
    try{
        std::map<std::string, sdbus::Variant> options;
        dev->callMethod("RequestScan")
            .onInterface("org.freedesktop.NetworkManager.Device.Wireless")
            .withArguments(options);
        wifiState_ = WifiState::Scanning;
        V2_LOG_INFO("Scan requested");
    }catch(const sdbus::Error& e){
        V2_LOG_ERROR("RequestScan failed: {}", e.what());
    }
}

void WifiHandler::refreshAps(){
    if(devicePath_.empty()) return;
    auto* dev = deviceProxy_.get();
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
        lastScanResults_ = std::move(results);
    }catch(const sdbus::Error& e){
        V2_LOG_ERROR("refreshAps failed: {}", e.what());
    }
}

bool WifiHandler::addAndActivateConnection(const std::string& ssid, const std::string& password){
    if(devicePath_.empty()) return false;
    if(wifiState_ != WifiState::Disconnected){ V2_LOG_WARN("Cannot connect in current state");
        return false;
    }
    wifiState_ = WifiState::Connecting;

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
        nmProxy_->callMethod("AddAndActivateConnection")
            .onInterface("org.freedesktop.NetworkManager")
            .withArguments(settings, sdbus::ObjectPath(devicePath_), sdbus::ObjectPath("/"))
            .storeResultsTo(connPath, activeConnPath);
        activeConnectionPath_ = activeConnPath;
        wifiState_ = WifiState::Connected;
        V2_LOG_INFO("Connected to '{}', active conn: {}", ssid, activeConnectionPath_);
        return true;
    }catch(const sdbus::Error& e){ V2_LOG_ERROR("AddAndActivateConnection failed: {}", e.what());
        wifiState_ = WifiState::Disconnected;
        return false;
    }
}

bool WifiHandler::disconnectDevice(){
    if(wifiState_ != WifiState::Connected && wifiState_ != WifiState::Connecting){ V2_LOG_WARN("Cannot disconnect in current state");
        return false;
    }
    wifiState_ = WifiState::Disconnecting;
    auto* dev = deviceProxy_.get();
    if(!dev) return false;
    try{
        dev->callMethod("Disconnect").onInterface("org.freedesktop.NetworkManager.Device");
        activeConnectionPath_.clear();
        wifiState_ = WifiState::Disconnected;
        V2_LOG_INFO("Disconnected");
        return true;
    }catch(const sdbus::Error& e){
        wifiState_ = WifiState::Connected;
        V2_LOG_ERROR("Disconnect failed: {}", e.what());
        return false;
    }
}

// State queries
uint32_t WifiHandler::getDeviceState(){
    auto* dev = deviceProxy_.get();
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
    auto* dev = deviceProxy_.get();
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
    auto apProxy = sdbus::createProxy(*connection_, sdbus::ServiceName("org.freedesktop.NetworkManager"), sdbus::ObjectPath(apPath));
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
    auto* dev = deviceProxy_.get();
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
    auto* dev = deviceProxy_.get();
    if(!dev) return {};
    try{
        sdbus::Variant ipVar;
        dev->callMethod("Get")
            .onInterface("org.freedesktop.DBus.Properties")
            .withArguments("org.freedesktop.NetworkManager.Device", "Ip4Config")
            .storeResultsTo(ipVar);
        std::string ip4Path = std::string(ipVar.get<sdbus::ObjectPath>());
        if(ip4Path.empty() || ip4Path == "/") return {};
        auto ipProxy = sdbus::createProxy(*connection_, sdbus::ServiceName("org.freedesktop.NetworkManager"), sdbus::ObjectPath(ip4Path));
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
        nmProxy_->callMethod("GetAllDevices")
            .onInterface("org.freedesktop.NetworkManager")
            .storeResultsTo(devices);
        for(const auto& devPath : devices){
            auto tempProxy = sdbus::createProxy(*connection_, sdbus::ServiceName("org.freedesktop.NetworkManager"), sdbus::ObjectPath(devPath));
            sdbus::Variant devTypeVar;
            tempProxy->callMethod("Get")
                .onInterface("org.freedesktop.DBus.Properties")
                .withArguments("org.freedesktop.NetworkManager.Device", "DeviceType")
                .storeResultsTo(devTypeVar);
            uint32_t devType = devTypeVar.get<uint32_t>();
            if(devType == 2){ // NM_DEVICE_TYPE_WIFI = 2
                devicePath_ = devPath;
                deviceProxy_ = std::move(tempProxy);
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

std::string WifiHandler::bssidBytesToString(const std::vector<uint8_t>& bssid){
    if(bssid.size() != 6) return {};
    char buf[18];
    std::snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    return buf;
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
    wifiState_ = mapDeviceState(nmState);
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
