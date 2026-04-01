#pragma once
/**
 * @file prefix_preset.h
 * @brief Checked-in prefix preset format for bottle-style setup defaults.
 *
 * Prefix presets let game profiles point at named setup templates instead of
 * duplicating common package, Winetricks, launch-argument, and environment
 * intent in every profile.
 */

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mvrvb {

struct PrefixPresetInstallPolicy {
    std::vector<std::string> packages;
    std::vector<std::string> winetricks;
    bool requiresLauncher{false};
    std::string notes;
};

struct PrefixPreset {
    std::string schemaVersion{"1"};
    std::string presetId;
    std::string displayName;
    std::string category;
    std::string notes;
    PrefixPresetInstallPolicy install;
    std::vector<std::string> launchArgs;
    std::map<std::string, std::string> environment;
    std::map<std::string, std::string> dllOverrides;
};

struct PrefixPresetParseResult {
    PrefixPreset preset;
    std::string errorMessage;

    explicit operator bool() const noexcept { return errorMessage.empty(); }
};

struct PrefixPresetBatchLoadResult {
    std::vector<PrefixPreset> presets;
    std::vector<std::string> errorMessages;

    explicit operator bool() const noexcept { return errorMessages.empty(); }
};

PrefixPresetParseResult parsePrefixPreset(std::string_view text);
PrefixPresetParseResult loadPrefixPreset(const std::filesystem::path& path);
PrefixPresetBatchLoadResult loadPrefixPresetsFromDirectory(const std::filesystem::path& root);
const PrefixPreset* findPrefixPresetById(const std::vector<PrefixPreset>& presets,
                                         std::string_view presetId) noexcept;

}  // namespace mvrvb
