#include "actor_system.hpp"
#include "core/actor_system/runtime/worker.hpp"

ActorSystem::ActorSystem(int numWorkers){
    workers_.reserve(numWorkers);
    for(int i = 0; i < numWorkers; i++){
        workers_.push_back(std::make_unique<Worker>(&dispatcher_));
    }
}

ActorSystem::~ActorSystem(){
    actorRegistry_.clear();
    stop();
}

void ActorSystem::start(){
    scheduler_.start();
    for(auto& w : workers_){
        w->start();
    }
}

void ActorSystem::stop(){
    scheduler_.stop();
    for(auto& w : workers_){
        w->stop();
    }
}
