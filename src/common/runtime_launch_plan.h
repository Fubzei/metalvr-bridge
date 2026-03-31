#pragma once

#include "compatibility_profile.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace mvrvb {

inline constexpr const char* kRuntimeLaunchPlanSchemaVersion = "1";

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
RuntimeLaunchPlanResult parseRuntimeLaunchPlanJson(std::string_view text);
RuntimeLaunchPlanResult loadRuntimeLaunchPlanJson(const std::filesystem::path& path);
std::string summarizeRuntimeLaunchPlan(const RuntimeLaunchPlan& plan);
std::string describeRuntimeLaunchPlan(const RuntimeLaunchPlan& plan);
std::string runtimeLaunchPlanToJson(const RuntimeLaunchPlan& plan);
bool writeRuntimeLaunchPlanReport(
    const RuntimeLaunchPlan& plan,
    const std::filesystem::path& path,
    std::string* errorMessage = nullptr);
bool writeRuntimeLaunchPlanJson(
    const RuntimeLaunchPlan& plan,
    const std::filesystem::path& path,
    std::string* errorMessage = nullptr);

}  // namespace mvrvb
