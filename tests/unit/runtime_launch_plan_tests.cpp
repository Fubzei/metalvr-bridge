#include <gtest/gtest.h>

#include "runtime_launch_plan.h"

#include <filesystem>

namespace mvrvb {
namespace {

std::filesystem::path repoRoot() {
    return std::filesystem::path(MVRVB_SOURCE_ROOT);
}

TEST(RuntimeLaunchPlan, FallsBackToGlobalDefaultsForUnknownGames) {
    const auto result = buildRuntimeLaunchPlanFromDirectory(
        repoRoot() / "profiles",
        CompatibilityProfileQuery{
            .executable = "UnknownGame.exe",
            .launcher = "Steam",
            .store = "steam",
        });

    ASSERT_TRUE(result) << result.errorMessage;
    EXPECT_EQ(result.plan.selectedProfileId, "global-defaults");
    ASSERT_EQ(result.plan.appliedProfileIds.size(), 1u);
    EXPECT_EQ(result.plan.appliedProfileIds[0], "global-defaults");
    EXPECT_EQ(result.plan.backend, RendererBackend::Auto);
    EXPECT_EQ(result.plan.windowsVersion, "win11");
    EXPECT_EQ(result.plan.syncMode, SyncMode::Default);
    EXPECT_EQ(result.plan.environment.at("MVRVB_PROFILE"), "global-defaults");
    ASSERT_EQ(result.plan.fallbackBackends.size(), 3u);
    EXPECT_EQ(result.plan.fallbackBackends[0], RendererBackend::DXVK);
    EXPECT_EQ(result.plan.fallbackBackends[1], RendererBackend::D3DMetal);
    EXPECT_EQ(result.plan.fallbackBackends[2], RendererBackend::WineD3D);
}

TEST(RuntimeLaunchPlan, MergesMatchedGameProfileOverGlobalDefaults) {
    const auto result = buildRuntimeLaunchPlanFromDirectory(
        repoRoot() / "profiles",
        CompatibilityProfileQuery{
            .executable = R"(C:\Games\Overwatch\Overwatch.exe)",
            .launcher = "Battle.net",
            .store = "battlenet",
        });

    ASSERT_TRUE(result) << result.errorMessage;
    EXPECT_EQ(result.plan.selectedProfileId, "overwatch-2");
    ASSERT_EQ(result.plan.appliedProfileIds.size(), 2u);
    EXPECT_EQ(result.plan.appliedProfileIds[0], "global-defaults");
    EXPECT_EQ(result.plan.appliedProfileIds[1], "overwatch-2");
    EXPECT_EQ(result.plan.backend, RendererBackend::DXVK);
    EXPECT_EQ(result.plan.matchScore, 175);
    EXPECT_EQ(result.plan.windowsVersion, "win11");
    EXPECT_EQ(result.plan.syncMode, SyncMode::MSync);
    EXPECT_TRUE(result.plan.highResolutionMode);
    EXPECT_FALSE(result.plan.metalFxUpscaling);
    EXPECT_TRUE(result.plan.latencySensitive);
    EXPECT_TRUE(result.plan.competitive);
    EXPECT_EQ(result.plan.antiCheatRisk, AntiCheatRisk::Blocking);
    EXPECT_EQ(result.plan.environment.at("MVRVB_PROFILE"), "overwatch-2");
    EXPECT_EQ(result.plan.dllOverrides.at("d3d11"), "native,builtin");
    ASSERT_EQ(result.plan.launchArgs.size(), 1u);
    EXPECT_EQ(result.plan.launchArgs[0], "--fullscreen");
}

TEST(RuntimeLaunchPlan, SummaryIncludesCoreDecisionFields) {
    const auto result = buildRuntimeLaunchPlanFromDirectory(
        repoRoot() / "profiles",
        CompatibilityProfileQuery{
            .executable = R"(C:\Games\Overwatch\Overwatch.exe)",
            .launcher = "Battle.net",
            .store = "battlenet",
        });

    ASSERT_TRUE(result) << result.errorMessage;
    const std::string summary = summarizeRuntimeLaunchPlan(result.plan);

    EXPECT_NE(summary.find("Selected profile: overwatch-2"), std::string::npos);
    EXPECT_NE(summary.find("Backend: dxvk"), std::string::npos);
    EXPECT_NE(summary.find("Sync mode: msync"), std::string::npos);
    EXPECT_NE(summary.find("Match score: 175"), std::string::npos);
}

TEST(RuntimeLaunchPlan, JsonIncludesMachineReadableFields) {
    const auto result = buildRuntimeLaunchPlanFromDirectory(
        repoRoot() / "profiles",
        CompatibilityProfileQuery{
            .executable = R"(C:\Games\Overwatch\Overwatch.exe)",
            .launcher = "Battle.net",
            .store = "battlenet",
        });

    ASSERT_TRUE(result) << result.errorMessage;
    const std::string json = runtimeLaunchPlanToJson(result.plan);

    EXPECT_NE(json.find("\"selectedProfileId\":\"overwatch-2\""), std::string::npos);
    EXPECT_NE(json.find("\"backend\":\"dxvk\""), std::string::npos);
    EXPECT_NE(json.find("\"syncMode\":\"msync\""), std::string::npos);
    EXPECT_NE(json.find("\"antiCheatRisk\":\"blocking\""), std::string::npos);
    EXPECT_NE(json.find("\"d3d11\":\"native,builtin\""), std::string::npos);
    EXPECT_NE(json.find("\"launchArgs\":[\"--fullscreen\"]"), std::string::npos);
}

}  // namespace
}  // namespace mvrvb
