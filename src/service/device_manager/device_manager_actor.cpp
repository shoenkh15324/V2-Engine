#include "device_manager_actor.hpp"
#include "core/common/log/log.hpp"
#include "core/common/util/return.hpp"

DeviceManagerActor::DeviceManagerActor(std::string name, uint64_t id) : Actor(std::move(name), id){}

DeviceManagerActor::~DeviceManagerActor(){
    close();
}

int DeviceManagerActor::open(){
    if(state_ != Closed) close();
    state_ = Opening;
    //
    //
    state_ = Opened;
    V2_LOG_INFO("Device Manager Actor opened");
    return Ok;
}

int DeviceManagerActor::close(){
    if(state_ == Closed) return Ok;
    state_ = Closing;
    //
    devices_.clear();
    //
    state_ = Closed;
    V2_LOG_INFO("Device Manager closed");
    return Ok;
}

void DeviceManagerActor::handle(const Message& msg){
    if(state_ < Opened){ V2_LOG_ERROR("Device Manager is not opened"); return; }
    switch(msg.id()){
    case MessageId::DeviceRegister:{
        const auto& m = msg.as<DeviceRegister>();
        for(const auto& d : devices_){
            if(d.name == m.name){ V2_LOG_WARN("Device '{}' already registered", m.name.c_str());
                return;
            }
        }
        devices_.push_back({m.name, m.type, m.bus});
        V2_LOG_INFO("Device registered: {} (type={}, bus={})", m.name.c_str(), static_cast<int>(m.type), m.bus);
        break;
    }
    case MessageId::DeviceUnregister:{
        const auto& m = msg.as<DeviceUnregister>();
        for(auto it = devices_.begin(); it != devices_.end(); ++it){
            if(it->name == m.name){
                devices_.erase(it);
                V2_LOG_INFO("Device unregistered: {}", m.name.c_str());
                return;
            }
        }
        V2_LOG_WARN("Device '{}' not found", m.name.c_str());
        break;
    }
    case MessageId::DeviceEnumerate:{
        const auto& m = msg.as<DeviceEnumerate>();
        DeviceList rsp;
        for(const auto& d : devices_){
            rsp.names.push_back(d.name);
            rsp.types.push_back(static_cast<uint8_t>(d.type));
            rsp.buses.push_back(d.bus);
        }
        sendMsg(m.requesterName, std::move(rsp));
        break;
    }
    default:
        break;
    }
}
