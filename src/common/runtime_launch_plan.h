#pragma once

#include "compatibility_profile.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace mvrvb {

struct RuntimeLaunchPlan {
    std::vector<std::string> appliedProfileIds;
    std::string selectedProfileId;
    std::string selectedDisplayName;
    int matchScore{0};
    RendererBackend backend{RendererBackend::Auto};
    std::vector<RendererBackend> fallbackBackends;
    std::string windowsVersion;
    SyncMode syncMode{SyncMode::Default};
    bool highResolutionMode{false};
    bool metalFxUpscaling{false};
    bool latencySensitive{false};
    bool competitive{false};
    AntiCheatRisk antiCheatRisk{AntiCheatRisk::Unknown};
    std::vector<std::string> launchArgs;
    std::map<std::string, std::string> environment;
    std::map<std::string, std::string> dllOverrides;
};

struct RuntimeLaunchPlanResult {
    RuntimeLaunchPlan plan;
    std::string errorMessage;

    explicit operator bool() const noexcept { return errorMessage.empty(); }
};

RuntimeLaunchPlanResult buildRuntimeLaunchPlan(
    const std::vector<CompatibilityProfile>& profiles,
    const CompatibilityProfileQuery& query);
RuntimeLaunchPlanResult buildRuntimeLaunchPlanFromDirectory(
    const std::filesystem::path& root,
    const CompatibilityProfileQuery& query);
std::string summarizeRuntimeLaunchPlan(const RuntimeLaunchPlan& plan);

}  // namespace mvrvb
