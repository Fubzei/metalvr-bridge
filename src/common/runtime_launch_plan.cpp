#include "runtime_launch_plan.h"

#include <algorithm>
#include <sstream>

namespace mvrvb {
namespace {

void appendUniqueString(std::vector<std::string>* items, const std::string& value) {
    if (!items || value.empty()) return;
    if (std::find(items->begin(), items->end(), value) == items->end()) {
        items->push_back(value);
    }
}

void appendUniqueBackend(std::vector<RendererBackend>* items, RendererBackend backend) {
    if (!items) return;
    if (std::find(items->begin(), items->end(), backend) == items->end()) {
        items->push_back(backend);
    }
}

const CompatibilityProfile* findGlobalDefaults(const std::vector<CompatibilityProfile>& profiles) {
    for (const auto& profile : profiles) {
        if (profile.profileId == "global-defaults") {
            return &profile;
        }
    }
    return nullptr;
}

void mergeMaps(std::map<std::string, std::string>* dst,
               const std::map<std::string, std::string>& src) {
    if (!dst) return;
    for (const auto& [key, value] : src) {
        (*dst)[key] = value;
    }
}

void mergeArgs(std::vector<std::string>* dst, const std::vector<std::string>& src) {
    if (!dst) return;
    for (const auto& arg : src) {
        appendUniqueString(dst, arg);
    }
}

void appendFallbacks(std::vector<RendererBackend>* dst,
                     const std::vector<RendererBackend>& src) {
    if (!dst) return;
    for (RendererBackend backend : src) {
        appendUniqueBackend(dst, backend);
    }
}

}  // namespace

RuntimeLaunchPlanResult buildRuntimeLaunchPlan(
    const std::vector<CompatibilityProfile>& profiles,
    const CompatibilityProfileQuery& query) {
    RuntimeLaunchPlanResult result;
    if (profiles.empty()) {
        result.errorMessage = "No compatibility profiles are available";
        return result;
    }

    const CompatibilityProfile* globalDefaults = findGlobalDefaults(profiles);
    const auto selectedIndex = selectBestCompatibilityProfileIndex(profiles, query);
    const CompatibilityProfile* selectedProfile = nullptr;
    if (selectedIndex.has_value()) {
        selectedProfile = &profiles[*selectedIndex];
    } else {
        selectedProfile = globalDefaults;
    }

    if (!selectedProfile) {
        result.errorMessage = "No matching compatibility profile and no global defaults found";
        return result;
    }

    if (globalDefaults) {
        appendUniqueString(&result.plan.appliedProfileIds, globalDefaults->profileId);
        mergeMaps(&result.plan.environment, globalDefaults->environment);
        mergeMaps(&result.plan.dllOverrides, globalDefaults->dllOverrides);
        mergeArgs(&result.plan.launchArgs, globalDefaults->launchArgs);
    }

    if (selectedProfile != globalDefaults) {
        appendUniqueString(&result.plan.appliedProfileIds, selectedProfile->profileId);
    }

    result.plan.selectedProfileId = selectedProfile->profileId;
    result.plan.selectedDisplayName = selectedProfile->displayName;
    result.plan.matchScore = compatibilityProfileMatchScore(*selectedProfile, query);
    result.plan.backend = selectedProfile->defaultRenderer;
    result.plan.windowsVersion = selectedProfile->runtime.windowsVersion;
    if (result.plan.windowsVersion.empty() && globalDefaults) {
        result.plan.windowsVersion = globalDefaults->runtime.windowsVersion;
    }
    result.plan.syncMode = selectedProfile->runtime.syncMode;
    result.plan.highResolutionMode = selectedProfile->runtime.highResolutionMode;
    result.plan.metalFxUpscaling = selectedProfile->runtime.metalFxUpscaling;
    result.plan.latencySensitive = selectedProfile->latencySensitive;
    result.plan.competitive = selectedProfile->competitive;
    result.plan.antiCheatRisk = selectedProfile->antiCheatRisk;

    appendFallbacks(&result.plan.fallbackBackends, selectedProfile->fallbackRenderers);
    if (globalDefaults && globalDefaults != selectedProfile) {
        appendFallbacks(&result.plan.fallbackBackends, globalDefaults->fallbackRenderers);
    }

    mergeMaps(&result.plan.environment, selectedProfile->environment);
    mergeMaps(&result.plan.dllOverrides, selectedProfile->dllOverrides);
    mergeArgs(&result.plan.launchArgs, selectedProfile->launchArgs);

    return result;
}

RuntimeLaunchPlanResult buildRuntimeLaunchPlanFromDirectory(
    const std::filesystem::path& root,
    const CompatibilityProfileQuery& query) {
    const auto batch = loadCompatibilityProfilesFromDirectory(root);
    RuntimeLaunchPlanResult result;
    if (!batch) {
        std::ostringstream errors;
        for (size_t i = 0; i < batch.errorMessages.size(); ++i) {
            if (i != 0) errors << "; ";
            errors << batch.errorMessages[i];
        }
        result.errorMessage = errors.str();
        return result;
    }
    return buildRuntimeLaunchPlan(batch.profiles, query);
}

std::string summarizeRuntimeLaunchPlan(const RuntimeLaunchPlan& plan) {
    std::ostringstream out;
    out << "Selected profile: " << plan.selectedProfileId;
    if (!plan.selectedDisplayName.empty()) {
        out << " (" << plan.selectedDisplayName << ")";
    }
    out << "\n";
    out << "Applied profiles: ";
    for (size_t i = 0; i < plan.appliedProfileIds.size(); ++i) {
        if (i != 0) out << ", ";
        out << plan.appliedProfileIds[i];
    }
    out << "\n";
    out << "Backend: " << rendererBackendName(plan.backend) << "\n";
    out << "Fallbacks: ";
    for (size_t i = 0; i < plan.fallbackBackends.size(); ++i) {
        if (i != 0) out << ", ";
        out << rendererBackendName(plan.fallbackBackends[i]);
    }
    out << "\n";
    out << "Windows version: " << plan.windowsVersion << "\n";
    out << "Sync mode: " << syncModeName(plan.syncMode) << "\n";
    out << "High resolution mode: " << (plan.highResolutionMode ? "true" : "false") << "\n";
    out << "MetalFX upscaling: " << (plan.metalFxUpscaling ? "true" : "false") << "\n";
    out << "Latency sensitive: " << (plan.latencySensitive ? "true" : "false") << "\n";
    out << "Competitive: " << (plan.competitive ? "true" : "false") << "\n";
    out << "Anti-cheat risk: " << antiCheatRiskName(plan.antiCheatRisk) << "\n";
    out << "Match score: " << plan.matchScore << "\n";
    out << "Launch args: ";
    for (size_t i = 0; i < plan.launchArgs.size(); ++i) {
        if (i != 0) out << ", ";
        out << plan.launchArgs[i];
    }
    out << "\n";
    out << "Environment entries: " << plan.environment.size() << "\n";
    out << "DLL override entries: " << plan.dllOverrides.size();
    return out.str();
}

}  // namespace mvrvb
