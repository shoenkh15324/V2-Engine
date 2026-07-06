#pragma once
#include "core/actor_system/actor/actor.hpp"
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <string_view>
#include <unordered_map>

#include "infra/hal/pmu/i_pmu.hpp"

class CmdActor : public Actor{
public:
    CmdActor(const std::string& name, uint64_t id);
    ~CmdActor() override;

    CmdActor(const CmdActor&) = delete;
    CmdActor& operator=(const CmdActor&) = delete;
    CmdActor(CmdActor&&) = delete;
    CmdActor& operator=(CmdActor&&) = delete;

    int open() override;
    int close() override;
    void handle(const Message& msg) override;

private:
    using Handler = std::function<std::string(const std::vector<std::string>&)>;
    using OnOption = std::function<void(char opt, const std::string& val)>;

    std::string dispatch(const std::string& cmd);
    std::string handleInfo(const std::vector<std::string>&);
    std::string handleActor(const std::vector<std::string>& args);
    std::string handlePmu(const std::vector<std::string>& args);
    std::string handleTest(const std::vector<std::string>& args);
    std::string parseOptions(const std::vector<std::string>& args, std::string_view optstring, const OnOption& onOption);

    std::unordered_map<std::string, Handler> handlers_;
    std::unique_ptr<IPmu> pmu_;
    int64_t startTimeMs_ = 0;
};
