#pragma once
/**
 * @file compatibility_profile.h
 * @brief Checked-in compatibility profile format for product-style game tuning.
 *
 * These profiles are the first step toward a CrossOver-class runtime layer:
 * per-title defaults, backend selection, launch arguments, environment
 * overrides, and launcher matching data that can be validated in CI before
 * dedicated Mac hardware is available.
 */

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mvrvb {

enum class ProfileStatus : uint8_t {
    Planning,
    Experimental,
    Validated,
};

enum class RendererBackend : uint8_t {
    Auto,
    DXVK,
    VKD3DProton,
    D3DMetal,
    DXMT,
    WineD3D,
    NativeVulkan,
};

enum class AntiCheatRisk : uint8_t {
    Unknown,
    Low,
    Medium,
    High,
    Blocking,
};

enum class SyncMode : uint8_t {
    Default,
    MSync,
    ESync,
    Disabled,
};

struct CompatibilityMatchCriteria {
    std::vector<std::string> executables;
    std::vector<std::string> launchers;
    std::vector<std::string> stores;
};

struct CompatibilityRuntimePolicy {
    std::string windowsVersion;
    SyncMode syncMode{SyncMode::Default};
    bool highResolutionMode{false};
    bool metalFxUpscaling{false};
};

struct CompatibilityInstallPolicy {
    std::string prefixPreset;
    std::vector<std::string> packages;
    std::vector<std::string> winetricks;
    bool requiresLauncher{false};
    std::string notes;
};

struct CompatibilityProfile {
    std::string schemaVersion{"1"};
    std::string profileId;
    std::string displayName;
    ProfileStatus status{ProfileStatus::Planning};
    bool allowAutoMatch{true};
    std::string category;
    std::string notes;
    RendererBackend defaultRenderer{RendererBackend::Auto};
    std::vector<RendererBackend> fallbackRenderers;
    bool latencySensitive{false};
    bool competitive{false};
    AntiCheatRisk antiCheatRisk{AntiCheatRisk::Unknown};
    CompatibilityRuntimePolicy runtime;
    CompatibilityInstallPolicy install;
    CompatibilityMatchCriteria match;
    std::vector<std::string> launchArgs;
    std::map<std::string, std::string> environment;
    std::map<std::string, std::string> dllOverrides;
};

struct CompatibilityProfileParseResult {
    CompatibilityProfile profile;
    std::string errorMessage;

    explicit operator bool() const noexcept { return errorMessage.empty(); }
};

struct CompatibilityProfileQuery {
    std::string executable;
    std::string launcher;
    std::string store;
};

struct CompatibilityProfileBatchLoadResult {
    std::vector<CompatibilityProfile> profiles;
    std::vector<std::string> errorMessages;

    explicit operator bool() const noexcept { return errorMessages.empty(); }
};

const char* profileStatusName(ProfileStatus status) noexcept;
const char* rendererBackendName(RendererBackend backend) noexcept;
const char* antiCheatRiskName(AntiCheatRisk risk) noexcept;
const char* syncModeName(SyncMode mode) noexcept;

CompatibilityProfileParseResult parseCompatibilityProfile(std::string_view text);
CompatibilityProfileParseResult loadCompatibilityProfile(const std::filesystem::path& path);
CompatibilityProfileBatchLoadResult loadCompatibilityProfilesFromDirectory(
    const std::filesystem::path& root);
int compatibilityProfileMatchScore(
    const CompatibilityProfile& profile,
    const CompatibilityProfileQuery& query) noexcept;
std::optional<size_t> selectBestCompatibilityProfileIndex(
    const std::vector<CompatibilityProfile>& profiles,
    const CompatibilityProfileQuery& query) noexcept;

}  // namespace mvrvb
