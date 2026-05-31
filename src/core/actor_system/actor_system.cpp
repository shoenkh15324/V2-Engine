#include "actor_system.hpp"
#include "core/actor_system/actor/actor.hpp"
#include "core/actor_system/runtime/actor_context.hpp"
#include "core/actor_system/runtime/worker.hpp"

ActorSystem::ActorSystem(int numWorkers){
    workers_.reserve(numWorkers);
    for(int i = 0; i < numWorkers; i++){
        workers_.push_back(std::make_unique<Worker>(&dispatcher_));
    }
}

ActorSystem::~ActorSystem(){
    stop();
}

void ActorSystem::start(){
    for(auto& w : workers_){
        w->start();
    }    
}

void ActorSystem::stop(){
    for(auto& w : workers_){
        w->stop();
    }
}
