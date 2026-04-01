#pragma once

#include "compatibility_profile.h"
#include "prefix_preset.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace mvrvb {

inline constexpr const char* kRuntimeLaunchPlanSchemaVersion = "1";

struct RuntimeLaunchPlan {
    std::vector<std::string> appliedProfileIds;
    std::string appliedPrefixPresetId;
    std::string appliedPrefixPresetDisplayName;
    std::string selectedProfileId;
    std::string selectedDisplayName;
    std::string managedPrefixSlug;
    std::string managedPrefixRoot;
    std::string managedPrefixPath;
    std::string resolvedPrefixSource{"managed"};
    std::string resolvedPrefixPath;
    int matchScore{0};
    RendererBackend backend{RendererBackend::Auto};
    std::vector<RendererBackend> fallbackBackends;
    std::string windowsVersion;
    std::string minimumWineVersion;
    std::string preferredWineVersion;
    bool requiresWineMono{false};
    RendererBackend dx11Backend{RendererBackend::Auto};
    RendererBackend dx12Backend{RendererBackend::Auto};
    RendererBackend vulkanBackend{RendererBackend::Auto};
    SyncMode syncMode{SyncMode::Default};
    bool highResolutionMode{false};
    bool metalFxUpscaling{false};
    bool latencySensitive{false};
    bool competitive{false};
    AntiCheatRisk antiCheatRisk{AntiCheatRisk::Unknown};
    CompatibilityInstallPolicy install;
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
RuntimeLaunchPlanResult buildRuntimeLaunchPlan(
    const std::vector<CompatibilityProfile>& profiles,
    const std::vector<PrefixPreset>& prefixPresets,
    const CompatibilityProfileQuery& query);
RuntimeLaunchPlanResult buildRuntimeLaunchPlanFromDirectory(
    const std::filesystem::path& root,
    const CompatibilityProfileQuery& query);
std::filesystem::path defaultManagedPrefixRootForCurrentPlatform();
RuntimeLaunchPlan resolveRuntimeLaunchPlanPrefix(
    const RuntimeLaunchPlan& plan,
    std::string_view explicitPrefixPath = {},
    std::string_view managedPrefixRoot = {});
RuntimeLaunchPlanResult parseRuntimeLaunchPlanJson(std::string_view text);
RuntimeLaunchPlanResult loadRuntimeLaunchPlanJson(const std::filesystem::path& path);
std::string summarizeRuntimeLaunchPlan(const RuntimeLaunchPlan& plan);
std::string describeRuntimeLaunchPlan(const RuntimeLaunchPlan& plan);
std::string runtimeLaunchPlanToJson(const RuntimeLaunchPlan& plan);
std::string runtimeLaunchPlanToMarkdownChecklist(const RuntimeLaunchPlan& plan);
bool writeRuntimeLaunchPlanReport(
    const RuntimeLaunchPlan& plan,
    const std::filesystem::path& path,
    std::string* errorMessage = nullptr);
bool writeRuntimeLaunchPlanJson(
    const RuntimeLaunchPlan& plan,
    const std::filesystem::path& path,
    std::string* errorMessage = nullptr);
bool writeRuntimeLaunchPlanMarkdownChecklist(
    const RuntimeLaunchPlan& plan,
    const std::filesystem::path& path,
    std::string* errorMessage = nullptr);

}  // namespace mvrvb
