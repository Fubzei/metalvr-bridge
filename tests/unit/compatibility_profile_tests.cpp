#include <gtest/gtest.h>

#include "compatibility_profile.h"

#include <filesystem>

namespace mvrvb {
namespace {

std::filesystem::path repoRoot() {
    return std::filesystem::path(MVRVB_SOURCE_ROOT);
}

TEST(CompatibilityProfile, ParsesStructuredProfileFields) {
    const char* profileText = R"(
schema_version = 1
profile_id = arena-shooter
display_name = Arena Shooter
status = experimental
category = competitive-shooter
default_renderer = dxvk
fallback_renderers = d3dmetal, wined3d
latency_sensitive = true
competitive = true
anti_cheat_risk = medium
notes = Runtime intent only

[runtime]
windows_version = win11
sync_mode = msync
high_resolution_mode = true
metalfx_upscaling = true

[install]
prefix_preset = shooter-prefix
packages = dxvk, battle.net
winetricks = corefonts, vcrun2022
requires_launcher = true
notes = Install launcher first

[match]
executables = Arena.exe, Arena-Win64-Shipping.exe
launchers = Steam
stores = steam

[env]
DXVK_HUD = 0
MVRVB_PROFILE = arena-shooter

[dll_overrides]
d3d11 = native,builtin
dxgi = native,builtin

[launch]
args = --fullscreen, --novid
)";

    const auto result = parseCompatibilityProfile(profileText);

    ASSERT_TRUE(result) << result.errorMessage;
    EXPECT_EQ(result.profile.schemaVersion, "1");
    EXPECT_EQ(result.profile.profileId, "arena-shooter");
    EXPECT_EQ(result.profile.displayName, "Arena Shooter");
    EXPECT_EQ(result.profile.status, ProfileStatus::Experimental);
    EXPECT_TRUE(result.profile.allowAutoMatch);
    EXPECT_EQ(result.profile.category, "competitive-shooter");
    EXPECT_EQ(result.profile.defaultRenderer, RendererBackend::DXVK);
    ASSERT_EQ(result.profile.fallbackRenderers.size(), 2u);
    EXPECT_EQ(result.profile.fallbackRenderers[0], RendererBackend::D3DMetal);
    EXPECT_EQ(result.profile.fallbackRenderers[1], RendererBackend::WineD3D);
    EXPECT_TRUE(result.profile.latencySensitive);
    EXPECT_TRUE(result.profile.competitive);
    EXPECT_EQ(result.profile.antiCheatRisk, AntiCheatRisk::Medium);
    EXPECT_EQ(result.profile.runtime.windowsVersion, "win11");
    EXPECT_EQ(result.profile.runtime.syncMode, SyncMode::MSync);
    EXPECT_TRUE(result.profile.runtime.highResolutionMode);
    EXPECT_TRUE(result.profile.runtime.metalFxUpscaling);
    EXPECT_EQ(result.profile.install.prefixPreset, "shooter-prefix");
    ASSERT_EQ(result.profile.install.packages.size(), 2u);
    EXPECT_EQ(result.profile.install.packages[0], "dxvk");
    EXPECT_EQ(result.profile.install.packages[1], "battle.net");
    ASSERT_EQ(result.profile.install.winetricks.size(), 2u);
    EXPECT_EQ(result.profile.install.winetricks[0], "corefonts");
    EXPECT_EQ(result.profile.install.winetricks[1], "vcrun2022");
    EXPECT_TRUE(result.profile.install.requiresLauncher);
    EXPECT_EQ(result.profile.install.notes, "Install launcher first");
    ASSERT_EQ(result.profile.match.executables.size(), 2u);
    EXPECT_EQ(result.profile.match.executables[0], "Arena.exe");
    EXPECT_EQ(result.profile.match.executables[1], "Arena-Win64-Shipping.exe");
    ASSERT_EQ(result.profile.match.launchers.size(), 1u);
    EXPECT_EQ(result.profile.match.launchers[0], "Steam");
    ASSERT_EQ(result.profile.launchArgs.size(), 2u);
    EXPECT_EQ(result.profile.launchArgs[0], "--fullscreen");
    EXPECT_EQ(result.profile.launchArgs[1], "--novid");
    EXPECT_EQ(result.profile.environment.at("DXVK_HUD"), "0");
    EXPECT_EQ(result.profile.environment.at("MVRVB_PROFILE"), "arena-shooter");
    EXPECT_EQ(result.profile.dllOverrides.at("d3d11"), "native,builtin");
}

TEST(CompatibilityProfile, RejectsMissingRequiredFields) {
    const auto result = parseCompatibilityProfile("display_name = Missing Id\n");

    EXPECT_FALSE(result);
    EXPECT_EQ(result.errorMessage, "Missing required profile field 'profile-id'");
}

TEST(CompatibilityProfile, RejectsInvalidEnumAndBooleanValues) {
    auto rendererResult = parseCompatibilityProfile(R"(
profile_id = bad-renderer
display_name = Bad Renderer
default_renderer = impossible
)");
    EXPECT_FALSE(rendererResult);
    EXPECT_EQ(rendererResult.errorMessage, "Invalid renderer backend 'impossible'");

    auto boolResult = parseCompatibilityProfile(R"(
profile_id = bad-bool
display_name = Bad Bool
latency_sensitive = maybe
)");
    EXPECT_FALSE(boolResult);
    EXPECT_EQ(boolResult.errorMessage, "Invalid boolean for latency-sensitive");

    auto syncResult = parseCompatibilityProfile(R"(
profile_id = bad-sync
display_name = Bad Sync

[runtime]
sync_mode = impossible
)");
    EXPECT_FALSE(syncResult);
    EXPECT_EQ(syncResult.errorMessage, "Invalid sync mode 'impossible'");
}

TEST(CompatibilityProfile, LoadsCheckedInProfiles) {
    const auto batch = loadCompatibilityProfilesFromDirectory(repoRoot() / "profiles");

    ASSERT_TRUE(batch) << ::testing::PrintToString(batch.errorMessages);
    EXPECT_GE(batch.profiles.size(), 3u);
    for (const auto& profile : batch.profiles) {
        EXPECT_FALSE(profile.profileId.empty());
        EXPECT_FALSE(profile.displayName.empty());
    }
}

TEST(CompatibilityProfile, CheckedInOverwatchProfileIsPlanningOnly) {
    const auto result = loadCompatibilityProfile(
        repoRoot() / "profiles" / "games" / "overwatch-2.mvrvb-profile");

    ASSERT_TRUE(result) << result.errorMessage;
    EXPECT_EQ(result.profile.status, ProfileStatus::Planning);
    EXPECT_EQ(result.profile.defaultRenderer, RendererBackend::DXVK);
    EXPECT_TRUE(result.profile.latencySensitive);
    EXPECT_TRUE(result.profile.competitive);
    EXPECT_EQ(result.profile.antiCheatRisk, AntiCheatRisk::Blocking);
    EXPECT_EQ(result.profile.runtime.syncMode, SyncMode::MSync);
    EXPECT_TRUE(result.profile.runtime.highResolutionMode);
    EXPECT_EQ(result.profile.install.prefixPreset, "battlenet-shooter");
    EXPECT_TRUE(result.profile.install.requiresLauncher);
    ASSERT_EQ(result.profile.install.packages.size(), 2u);
    EXPECT_EQ(result.profile.install.packages[0], "battle.net");
    EXPECT_EQ(result.profile.install.packages[1], "dxvk");
    ASSERT_EQ(result.profile.match.launchers.size(), 1u);
    EXPECT_EQ(result.profile.match.launchers[0], "Battle.net");
}

TEST(CompatibilityProfile, AutoSelectsBestMatchingCheckedInProfile) {
    const auto batch = loadCompatibilityProfilesFromDirectory(repoRoot() / "profiles");
    ASSERT_TRUE(batch) << ::testing::PrintToString(batch.errorMessages);

    const CompatibilityProfileQuery overwatchQuery{
        .executable = R"(C:\Games\Overwatch\Overwatch.exe)",
        .launcher = "Battle.net",
        .store = "battlenet",
    };
    const auto overwatchIndex = selectBestCompatibilityProfileIndex(batch.profiles, overwatchQuery);
    ASSERT_TRUE(overwatchIndex.has_value());
    EXPECT_EQ(batch.profiles[*overwatchIndex].profileId, "overwatch-2");

    const CompatibilityProfileQuery unknownGameQuery{
        .executable = "UnknownGame.exe",
        .launcher = "Steam",
        .store = "steam",
    };
    const auto unknownIndex = selectBestCompatibilityProfileIndex(batch.profiles, unknownGameQuery);
    ASSERT_TRUE(unknownIndex.has_value());
    EXPECT_EQ(batch.profiles[*unknownIndex].profileId, "global-defaults");
}

}  // namespace
}  // namespace mvrvb
