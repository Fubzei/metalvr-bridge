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
    EXPECT_EQ(result.profile.category, "competitive-shooter");
    EXPECT_EQ(result.profile.defaultRenderer, RendererBackend::DXVK);
    ASSERT_EQ(result.profile.fallbackRenderers.size(), 2u);
    EXPECT_EQ(result.profile.fallbackRenderers[0], RendererBackend::D3DMetal);
    EXPECT_EQ(result.profile.fallbackRenderers[1], RendererBackend::WineD3D);
    EXPECT_TRUE(result.profile.latencySensitive);
    EXPECT_TRUE(result.profile.competitive);
    EXPECT_EQ(result.profile.antiCheatRisk, AntiCheatRisk::Medium);
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
}

TEST(CompatibilityProfile, LoadsCheckedInProfiles) {
    const auto profilesRoot = repoRoot() / "profiles";
    size_t loadedProfiles = 0;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(profilesRoot)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".mvrvb-profile") {
            continue;
        }

        const auto result = loadCompatibilityProfile(entry.path());
        ASSERT_TRUE(result) << entry.path().string() << ": " << result.errorMessage;
        EXPECT_FALSE(result.profile.profileId.empty()) << entry.path().string();
        EXPECT_FALSE(result.profile.displayName.empty()) << entry.path().string();
        ++loadedProfiles;
    }

    EXPECT_GE(loadedProfiles, 3u);
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
    ASSERT_EQ(result.profile.match.launchers.size(), 1u);
    EXPECT_EQ(result.profile.match.launchers[0], "Battle.net");
}

}  // namespace
}  // namespace mvrvb
