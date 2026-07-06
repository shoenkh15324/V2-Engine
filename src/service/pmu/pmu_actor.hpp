#pragma once
#include "core/actor_system/actor/actor.hpp"
#include "core/common/config/platform_config.h"
#include "infra/hal/pmu/i_pmu.hpp"
#include <memory>

class PmuActor : public Actor{
public:
    PmuActor(const std::string& name, uint64_t id, int pollIntervalMs);
    ~PmuActor() override;

    PmuActor(const PmuActor&) = delete;
    PmuActor& operator=(const PmuActor&) = delete;
    PmuActor(PmuActor&&) = delete;
    PmuActor& operator=(PmuActor&&) = delete;

    int open() override;
    int close() override;
    void handle(const Message& msg) override;

private:
    void collectAndBroadcast();
    bool hasVacgencmd();

    std::unique_ptr<IPmu> pmu_;
    PmuData latestData_;
    int pollIntervalMs_;

};
