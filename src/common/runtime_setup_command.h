#pragma once

#include "runtime_launch_plan.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace mvrvb {

struct RuntimeSetupRequest {
    std::string prefixPath;
    std::string managedPrefixRoot;
    std::string winebootBinary{"wineboot"};
    std::string winetricksBinary{"winetricks"};
    std::string workingDirectory;
};

struct RuntimeSetupAction {
    std::string description;
    std::string program;
    std::vector<std::string> arguments;
};

struct RuntimeSetupCommandPlan {
    std::string workingDirectory;
    std::map<std::string, std::string> environment;
    std::vector<RuntimeSetupAction> actions;
    std::vector<std::string> manualActions;
};

struct RuntimeSetupCommandResult {
    RuntimeSetupCommandPlan plan;
    std::vector<std::string> warnings;
    std::string errorMessage;

    explicit operator bool() const noexcept { return errorMessage.empty(); }
};

RuntimeSetupCommandResult buildRuntimeSetupCommandPlan(
    const RuntimeLaunchPlan& plan,
    const RuntimeSetupRequest& request);
std::string summarizeRuntimeSetupCommandPlan(const RuntimeSetupCommandPlan& plan);
std::string renderRuntimeSetupCommandBash(const RuntimeSetupCommandPlan& plan);
std::string renderRuntimeSetupCommandPowerShell(const RuntimeSetupCommandPlan& plan);
bool writeRuntimeSetupCommandBash(
    const RuntimeSetupCommandPlan& plan,
    const std::filesystem::path& path,
    std::string* errorMessage = nullptr);
bool writeRuntimeSetupCommandPowerShell(
    const RuntimeSetupCommandPlan& plan,
    const std::filesystem::path& path,
    std::string* errorMessage = nullptr);

}  // namespace mvrvb
