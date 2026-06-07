#include "actor_system.hpp"
#include "core/actor_system/runtime/worker.hpp"

ActorSystem::ActorSystem(int numWorkers) : dispatcher_(numWorkers){
    workers_.reserve(numWorkers);
    for(int i = 0; i < numWorkers; i++){
        workers_.push_back(std::make_unique<Worker>(&dispatcher_, i));
    }
}

ActorSystem::~ActorSystem(){
    actorRegistry_.clear();
    stop();
}

void ActorSystem::start(){
    dispatcher_.start();
    scheduler_.start(&dispatcher_);
    for(auto& w : workers_){
        w->start();
    }
}

void ActorSystem::stop(){
    scheduler_.stop();
    dispatcher_.stop();
    for(auto& w : workers_){
        w->stop();
    }
}

void ActorSystem::run(){
#ifdef __linux__
    dispatcher_.run();
#endif
}

void ActorSystem::requestStop(){
    scheduler_.stop();
    dispatcher_.stop();
}
