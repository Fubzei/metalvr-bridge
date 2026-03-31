#pragma once

#include "runtime_launch_plan.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace mvrvb {

struct RuntimeLaunchRequest {
    std::string executablePath;
    std::string wineBinary{"wine"};
    std::string prefixPath;
    std::string workingDirectory;
};

struct RuntimeLaunchCommand {
    std::string program;
    std::vector<std::string> arguments;
    std::string workingDirectory;
    std::map<std::string, std::string> environment;
};

struct RuntimeLaunchCommandResult {
    RuntimeLaunchCommand command;
    std::vector<std::string> warnings;
    std::string errorMessage;

    explicit operator bool() const noexcept { return errorMessage.empty(); }
};

RuntimeLaunchCommandResult materializeRuntimeLaunchCommand(
    const RuntimeLaunchPlan& plan,
    const RuntimeLaunchRequest& request);
std::string summarizeRuntimeLaunchCommand(const RuntimeLaunchCommand& command);
std::string renderRuntimeLaunchCommandBash(const RuntimeLaunchCommand& command);
std::string renderRuntimeLaunchCommandPowerShell(const RuntimeLaunchCommand& command);
bool writeRuntimeLaunchCommandBash(
    const RuntimeLaunchCommand& command,
    const std::filesystem::path& path,
    std::string* errorMessage = nullptr);
bool writeRuntimeLaunchCommandPowerShell(
    const RuntimeLaunchCommand& command,
    const std::filesystem::path& path,
    std::string* errorMessage = nullptr);

}  // namespace mvrvb
