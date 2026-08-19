#include "actor_system.hpp"
#include <cassert>
#include "core/common/util/return.hpp"
#include "core/actor_system/runtime/dispatcher/worker.hpp"
#include "core/actor_system/actor/actor_handle.hpp"
#include "core/actor_system/runtime/actor_runtime/actor_runtime.hpp"
#include "core/actor_system/runtime/scheduler/scheduler.hpp"
#include "core/actor_system/runtime/supervisor/supervisor.hpp"
#include "core/actor_system/runtime/supervisor/dead_letter_queue.hpp"
#include "core/actor_system/runtime/dispatcher/work_dispatcher.hpp"
#include "core/actor_system/actor/actor_registry.hpp"
#include "core/actor_system/messages/core_messages.hpp"
#include "core/actor_system/runtime/mailbox/mailbox.hpp"
#include "core/common/timer/i_timer.hpp"
#include "core/perf/metrics/metrics.hpp"
#include "core/common/log/log.hpp"

ActorSystem::ActorSystem(const ActorSystemConfig& config, ActorSystemDeps deps)
    : deadLetterQueue_(std::move(deps.deadLetterQueue)),
        supervisor_(std::move(deps.supervisor)),
        dispatcher_(std::move(deps.dispatcher)),
        scheduler_(std::move(deps.scheduler)),
        registry_(std::move(deps.registry)),
        eventLoop_(std::move(deps.eventLoop)),
        defaultMailboxSize_(config.defaultMailboxSize),
        maxBatch_(config.maxBatch)
{
    assert(dispatcher_ && scheduler_ && supervisor_ && deadLetterQueue_ && registry_);
    
    V2_METRICS()->init(config.numWorkers);
    workers_.reserve(config.numWorkers);
    for(int i = 0; i < config.numWorkers; i++){
        workers_.push_back(std::make_unique<Worker>(dispatcher_.get(), i, config.maxBatch));
    }
    supervisor_->setRestartAll([this]() -> int {
        int count = 0;
        registry_->forEachActor([&](ActorHandle h){
            Actor* a = h.get();
            if(!a || !a->runtime()) return;
            ActorRestartRequest req;
            req.reason = "one-for-all restart";
            a->runtime()->enqueue(Message::make(std::move(req)));
            ++count;
        });
        return count;
    });
}

ActorSystem::~ActorSystem(){
    stop();
    registry_->clear();
}

void ActorSystem::start(){
    dispatcher_->start();
    if(eventLoop_) eventLoop_->start();
    scheduler_->start();

    try{
        for(auto& ctx : actorRuntimes_){
            int ret = ctx->actor()->open();
            if(ret != Ok) V2_LOG_ERROR("Actor {} failed to open", ctx->actor()->name().c_str());
        }
    }catch(const std::exception& e){
        V2_LOG_ERROR("ActorSystem start failed: {}", e.what());
        stop();
        throw;
    }catch(...){
        V2_LOG_ERROR("ActorSystem start failed with unknown exception");
        stop();
        throw;
    }

    for(auto& w : workers_){
        w->start();
    }
}

void ActorSystem::stop(){
    scheduler_->stop();
    if(eventLoop_) eventLoop_->stop();
    dispatcher_->beginDrain();
    for(auto& w : workers_){
        w->stop();
    }
    dispatcher_->stop();
    for(auto& ctx : actorRuntimes_){
        ctx->actor()->close();
    }
}

void ActorSystem::run(){
    if(eventLoop_) eventLoop_->run();
}

void ActorSystem::requestStop(){
    if(eventLoop_) eventLoop_->stop();
}

void ActorSystem::attachActor(std::unique_ptr<Actor> actor, size_t mailboxSize, uint64_t id){
    auto mailbox = std::make_unique<Mailbox>(mailboxSize);
    auto rt = std::make_unique<ActorRuntime>(
        std::move(actor),
        std::move(mailbox),
        dispatcher_.get(),
        scheduler_.get(),
        registry_.get(),
        eventLoop_.get(),
        supervisor_.get()
    );
    registry_->add(rt->actor());
    actorRuntimes_.push_back(std::move(rt));
    V2_METRICS()->registerActor(id);
}

std::unique_ptr<ActorSystem> createDefaultActorSystem(const ActorSystemConfig& config, std::unique_ptr<IEventLoop> eventLoop, std::unique_ptr<ITimer> timer){
    auto dlqCapacity = static_cast<size_t>(config.deadLetterQueueCapacity);
    auto deadLetterQueue = std::make_unique<DeadLetterQueue>(dlqCapacity);
    auto strategy = static_cast<RestartStrategy>(config.supervisorDefaultStrategy);
    auto supervisor = std::make_unique<Supervisor>(*deadLetterQueue, config.supervisorMaxRestarts, strategy);
    auto registry = std::make_unique<ActorRegistry>();
    int highWatermark = (config.dispatcherHighWatermark > 0) ? config.dispatcherHighWatermark : (config.dispatcherQueueCapacity * 7 / 10);
    auto dispatcher = std::make_unique<WorkDispatcher>(
        config.numWorkers,
        config.dispatcherQueueCapacity,
        highWatermark,
        config.busyStealIntervalUs,
        config.idleStealIntervalUs
    );
    auto scheduler = std::make_unique<Scheduler>(std::move(timer));

    ActorSystemDeps deps;
    deps.deadLetterQueue = std::move(deadLetterQueue);
    deps.supervisor = std::move(supervisor);
    deps.registry = std::move(registry);
    deps.dispatcher = std::move(dispatcher);
    deps.scheduler = std::move(scheduler);
    deps.eventLoop = std::move(eventLoop);

    return std::make_unique<ActorSystem>(config, std::move(deps));
}
