#include "device_manager_actor.hpp"
#include "core/common/log/log.hpp"
#include "core/common/util/return.hpp"

DeviceManagerActor::DeviceManagerActor(const std::string& name, uint64_t id) : Actor(name, id){
    //
}

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
    std::visit(overloaded{
        [this](const DeviceRegister& msg){
            for(const auto& d : devices_){
                if(d.name == msg.name){ V2_LOG_WARN("Device '%s' already registered", msg.name.c_str());
                    return;
                }
            }
            devices_.push_back({msg.name, msg.type, msg.bus});
            V2_LOG_INFO("Device registered: %s (type=%d, bus=%d)", msg.name.c_str(), static_cast<int>(msg.type), msg.bus);
        },
        [this](const DeviceUnregister& msg){
            for(auto it = devices_.begin(); it != devices_.end(); ++it){
                if(it->name == msg.name){
                    devices_.erase(it);
                    V2_LOG_INFO("Device unregistered: %s", msg.name.c_str());
                    return;
                }
            }
            V2_LOG_WARN("Device '%s' not found", msg.name.c_str());
        },
        [this](const DeviceEnumerate& msg){
            DeviceList rsp;
            for(const auto& d : devices_){
                rsp.names.push_back(d.name);
                rsp.types.push_back(static_cast<uint8_t>(d.type));
                rsp.buses.push_back(d.bus);
            }
            sendMsg(msg.requesterName, std::move(rsp));
        },
        [](const auto&){}
    }, msg);
}
