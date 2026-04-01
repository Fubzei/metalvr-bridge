#include <gtest/gtest.h>

#include "runtime_launch_plan.h"

#include <fstream>
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
    EXPECT_EQ(result.plan.appliedPrefixPresetId, "general-game");
    EXPECT_EQ(result.plan.appliedPrefixPresetDisplayName, "General Game");
    EXPECT_EQ(result.plan.selectedProfileId, "global-defaults");
    ASSERT_EQ(result.plan.appliedProfileIds.size(), 1u);
    EXPECT_EQ(result.plan.appliedProfileIds[0], "global-defaults");
    EXPECT_EQ(result.plan.backend, RendererBackend::Auto);
    EXPECT_EQ(result.plan.windowsVersion, "win11");
    EXPECT_EQ(result.plan.minimumWineVersion, "11.0");
    EXPECT_EQ(result.plan.preferredWineVersion, "11.0");
    EXPECT_FALSE(result.plan.requiresWineMono);
    EXPECT_EQ(result.plan.dx11Backend, RendererBackend::DXVK);
    EXPECT_EQ(result.plan.dx12Backend, RendererBackend::VKD3DProton);
    EXPECT_EQ(result.plan.vulkanBackend, RendererBackend::NativeVulkan);
    EXPECT_EQ(result.plan.syncMode, SyncMode::Default);
    EXPECT_EQ(result.plan.environment.at("MVRVB_PROFILE"), "global-defaults");
    EXPECT_EQ(result.plan.environment.at("MVRVB_PREFIX_FAMILY"), "general-game");
    EXPECT_EQ(result.plan.install.prefixPreset, "general-game");
    ASSERT_EQ(result.plan.install.packages.size(), 2u);
    EXPECT_EQ(result.plan.install.packages[0], "dxvk");
    EXPECT_EQ(result.plan.install.packages[1], "vkd3d-proton");
    ASSERT_EQ(result.plan.install.winetricks.size(), 1u);
    EXPECT_EQ(result.plan.install.winetricks[0], "corefonts");
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
    EXPECT_EQ(result.plan.appliedPrefixPresetId, "battlenet-shooter");
    EXPECT_EQ(result.plan.appliedPrefixPresetDisplayName, "Battle.net Shooter");
    EXPECT_EQ(result.plan.selectedProfileId, "overwatch-2");
    ASSERT_EQ(result.plan.appliedProfileIds.size(), 2u);
    EXPECT_EQ(result.plan.appliedProfileIds[0], "global-defaults");
    EXPECT_EQ(result.plan.appliedProfileIds[1], "overwatch-2");
    EXPECT_EQ(result.plan.backend, RendererBackend::DXVK);
    EXPECT_EQ(result.plan.matchScore, 175);
    EXPECT_EQ(result.plan.windowsVersion, "win11");
    EXPECT_EQ(result.plan.minimumWineVersion, "11.0");
    EXPECT_EQ(result.plan.preferredWineVersion, "11.0");
    EXPECT_FALSE(result.plan.requiresWineMono);
    EXPECT_EQ(result.plan.dx11Backend, RendererBackend::DXVK);
    EXPECT_EQ(result.plan.dx12Backend, RendererBackend::VKD3DProton);
    EXPECT_EQ(result.plan.vulkanBackend, RendererBackend::NativeVulkan);
    EXPECT_EQ(result.plan.syncMode, SyncMode::MSync);
    EXPECT_TRUE(result.plan.highResolutionMode);
    EXPECT_FALSE(result.plan.metalFxUpscaling);
    EXPECT_TRUE(result.plan.latencySensitive);
    EXPECT_TRUE(result.plan.competitive);
    EXPECT_EQ(result.plan.antiCheatRisk, AntiCheatRisk::Blocking);
    EXPECT_EQ(result.plan.environment.at("MVRVB_PROFILE"), "overwatch-2");
    EXPECT_EQ(result.plan.environment.at("MVRVB_PREFIX_FAMILY"), "battlenet-shooter");
    EXPECT_EQ(result.plan.dllOverrides.at("d3d11"), "native,builtin");
    EXPECT_EQ(result.plan.install.prefixPreset, "battlenet-shooter");
    EXPECT_TRUE(result.plan.install.requiresLauncher);
    ASSERT_EQ(result.plan.install.packages.size(), 2u);
    EXPECT_EQ(result.plan.install.packages[0], "dxvk");
    EXPECT_EQ(result.plan.install.packages[1], "battle.net");
    ASSERT_EQ(result.plan.install.winetricks.size(), 2u);
    EXPECT_EQ(result.plan.install.winetricks[0], "corefonts");
    EXPECT_EQ(result.plan.install.winetricks[1], "vcrun2022");
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

    EXPECT_NE(summary.find("Applied prefix preset: battlenet-shooter"), std::string::npos);
    EXPECT_NE(summary.find("Selected profile: overwatch-2"), std::string::npos);
    EXPECT_NE(summary.find("Backend: dxvk"), std::string::npos);
    EXPECT_NE(summary.find("Wine minimum version: 11.0"), std::string::npos);
    EXPECT_NE(summary.find("DX12 backend route: vkd3d-proton"), std::string::npos);
    EXPECT_NE(summary.find("Sync mode: msync"), std::string::npos);
    EXPECT_NE(summary.find("Install prefix preset: battlenet-shooter"), std::string::npos);
    EXPECT_NE(summary.find("Match score: 175"), std::string::npos);
}

TEST(RuntimeLaunchPlan, DetailedReportIncludesEnvironmentAndArguments) {
    const auto result = buildRuntimeLaunchPlanFromDirectory(
        repoRoot() / "profiles",
        CompatibilityProfileQuery{
            .executable = R"(C:\Games\Overwatch\Overwatch.exe)",
            .launcher = "Battle.net",
            .store = "battlenet",
        });

    ASSERT_TRUE(result) << result.errorMessage;
    const std::string report = describeRuntimeLaunchPlan(result.plan);

    EXPECT_NE(report.find("Applied prefix preset: battlenet-shooter"), std::string::npos);
    EXPECT_NE(report.find("Environment:"), std::string::npos);
    EXPECT_NE(report.find("MVRVB_PROFILE=overwatch-2"), std::string::npos);
    EXPECT_NE(report.find("MVRVB_PREFIX_FAMILY=battlenet-shooter"), std::string::npos);
    EXPECT_NE(report.find("DLL overrides:"), std::string::npos);
    EXPECT_NE(report.find("d3d11=native,builtin"), std::string::npos);
    EXPECT_NE(report.find("Install policy:"), std::string::npos);
    EXPECT_NE(report.find("wine_minimum_version=11.0"), std::string::npos);
    EXPECT_NE(report.find("requires_wine_mono=false"), std::string::npos);
    EXPECT_NE(report.find("prefix_preset=battlenet-shooter"), std::string::npos);
    EXPECT_NE(report.find("battle.net"), std::string::npos);
    EXPECT_NE(report.find("Launch arguments:"), std::string::npos);
    EXPECT_NE(report.find("--fullscreen"), std::string::npos);
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

    EXPECT_NE(json.find("\"schemaVersion\":\"1\""), std::string::npos);
    EXPECT_NE(json.find("\"appliedPrefixPresetId\":\"battlenet-shooter\""), std::string::npos);
    EXPECT_NE(json.find("\"selectedProfileId\":\"overwatch-2\""), std::string::npos);
    EXPECT_NE(json.find("\"backend\":\"dxvk\""), std::string::npos);
    EXPECT_NE(json.find("\"minimumWineVersion\":\"11.0\""), std::string::npos);
    EXPECT_NE(json.find("\"preferredWineVersion\":\"11.0\""), std::string::npos);
    EXPECT_NE(json.find("\"requiresWineMono\":false"), std::string::npos);
    EXPECT_NE(json.find("\"dx11Backend\":\"dxvk\""), std::string::npos);
    EXPECT_NE(json.find("\"dx12Backend\":\"vkd3d-proton\""), std::string::npos);
    EXPECT_NE(json.find("\"vulkanBackend\":\"native-vulkan\""), std::string::npos);
    EXPECT_NE(json.find("\"syncMode\":\"msync\""), std::string::npos);
    EXPECT_NE(json.find("\"antiCheatRisk\":\"blocking\""), std::string::npos);
    EXPECT_NE(json.find("\"prefixPreset\":\"battlenet-shooter\""), std::string::npos);
    EXPECT_NE(json.find("\"requiresLauncher\":true"), std::string::npos);
    EXPECT_NE(json.find("\"d3d11\":\"native,builtin\""), std::string::npos);
    EXPECT_NE(json.find("\"launchArgs\":[\"--fullscreen\"]"), std::string::npos);
}

TEST(RuntimeLaunchPlan, MarkdownChecklistIncludesSetupGuidance) {
    const auto result = buildRuntimeLaunchPlanFromDirectory(
        repoRoot() / "profiles",
        CompatibilityProfileQuery{
            .executable = R"(C:\Games\Overwatch\Overwatch.exe)",
            .launcher = "Battle.net",
            .store = "battlenet",
        });

    ASSERT_TRUE(result) << result.errorMessage;
    const std::string checklist = runtimeLaunchPlanToMarkdownChecklist(result.plan);

    EXPECT_NE(checklist.find("# Runtime Setup Checklist"), std::string::npos);
    EXPECT_NE(checklist.find("- Applied prefix preset: `battlenet-shooter`"), std::string::npos);
    EXPECT_NE(checklist.find("- Prefix preset: `battlenet-shooter`"), std::string::npos);
    EXPECT_NE(checklist.find("- Wine minimum version: `11.0`"), std::string::npos);
    EXPECT_NE(checklist.find("- DX12 backend route: `vkd3d-proton`"), std::string::npos);
    EXPECT_NE(checklist.find("- Requires launcher bootstrap: `true`"), std::string::npos);
    EXPECT_NE(checklist.find("`battle.net`"), std::string::npos);
    EXPECT_NE(checklist.find("Install Battle.net in the target prefix first"), std::string::npos);
}

TEST(RuntimeLaunchPlan, WritesJsonAndReportFiles) {
    const auto result = buildRuntimeLaunchPlanFromDirectory(
        repoRoot() / "profiles",
        CompatibilityProfileQuery{
            .executable = R"(C:\Games\Overwatch\Overwatch.exe)",
            .launcher = "Battle.net",
            .store = "battlenet",
        });

    ASSERT_TRUE(result) << result.errorMessage;

    const auto tempRoot = std::filesystem::temp_directory_path() / "mvrvb-runtime-launch-plan-tests";
    std::error_code ec;
    std::filesystem::create_directories(tempRoot, ec);
    ASSERT_FALSE(static_cast<bool>(ec)) << ec.message();

    const auto jsonPath = tempRoot / "overwatch.launch-plan.json";
    const auto reportPath = tempRoot / "overwatch.launch-plan.txt";
    const auto checklistPath = tempRoot / "overwatch.launch-plan.md";
    std::filesystem::remove(jsonPath, ec);
    std::filesystem::remove(reportPath, ec);
    std::filesystem::remove(checklistPath, ec);

    std::string errorMessage;
    ASSERT_TRUE(writeRuntimeLaunchPlanJson(result.plan, jsonPath, &errorMessage)) << errorMessage;
    ASSERT_TRUE(writeRuntimeLaunchPlanReport(result.plan, reportPath, &errorMessage)) << errorMessage;
    ASSERT_TRUE(writeRuntimeLaunchPlanMarkdownChecklist(result.plan, checklistPath, &errorMessage))
        << errorMessage;

    std::ifstream jsonStream(jsonPath);
    ASSERT_TRUE(jsonStream.is_open());
    const std::string json((std::istreambuf_iterator<char>(jsonStream)),
                           std::istreambuf_iterator<char>());
    EXPECT_NE(json.find("\"selectedProfileId\":\"overwatch-2\""), std::string::npos);
    const auto loaded = loadRuntimeLaunchPlanJson(jsonPath);
    ASSERT_TRUE(loaded) << loaded.errorMessage;
    EXPECT_EQ(loaded.plan.appliedPrefixPresetId, "battlenet-shooter");
    EXPECT_EQ(loaded.plan.selectedProfileId, "overwatch-2");
    EXPECT_EQ(loaded.plan.backend, RendererBackend::DXVK);
    EXPECT_EQ(loaded.plan.minimumWineVersion, "11.0");
    EXPECT_EQ(loaded.plan.preferredWineVersion, "11.0");
    EXPECT_FALSE(loaded.plan.requiresWineMono);
    EXPECT_EQ(loaded.plan.dx11Backend, RendererBackend::DXVK);
    EXPECT_EQ(loaded.plan.dx12Backend, RendererBackend::VKD3DProton);
    EXPECT_EQ(loaded.plan.vulkanBackend, RendererBackend::NativeVulkan);
    EXPECT_EQ(loaded.plan.syncMode, SyncMode::MSync);
    EXPECT_EQ(loaded.plan.install.prefixPreset, "battlenet-shooter");
    EXPECT_TRUE(loaded.plan.install.requiresLauncher);

    std::ifstream reportStream(reportPath);
    ASSERT_TRUE(reportStream.is_open());
    const std::string report((std::istreambuf_iterator<char>(reportStream)),
                             std::istreambuf_iterator<char>());
    EXPECT_NE(report.find("Selected profile: overwatch-2"), std::string::npos);
    EXPECT_NE(report.find("Launch arguments:"), std::string::npos);

    std::ifstream checklistStream(checklistPath);
    ASSERT_TRUE(checklistStream.is_open());
    const std::string checklist((std::istreambuf_iterator<char>(checklistStream)),
                                std::istreambuf_iterator<char>());
    EXPECT_NE(checklist.find("# Runtime Setup Checklist"), std::string::npos);
    EXPECT_NE(checklist.find("battlenet-shooter"), std::string::npos);

    std::filesystem::remove(jsonPath, ec);
    std::filesystem::remove(reportPath, ec);
    std::filesystem::remove(checklistPath, ec);
}

TEST(RuntimeLaunchPlan, RejectsUnknownBackendInPersistedJson) {
    const auto result = parseRuntimeLaunchPlanJson(
        R"({"schemaVersion":"1","selectedProfileId":"bad","backend":"totally-unknown","runtime":{"windowsVersion":"win11","syncMode":"default","highResolutionMode":false,"metalFxUpscaling":false},"latencySensitive":false,"competitive":false,"antiCheatRisk":"unknown","launchArgs":[],"environment":{},"dllOverrides":{}})");

    ASSERT_FALSE(result);
    EXPECT_NE(result.errorMessage.find("Unknown renderer backend"), std::string::npos);
}

TEST(RuntimeLaunchPlan, RejectsUnsupportedSchemaVersionInPersistedJson) {
    const auto result = parseRuntimeLaunchPlanJson(
        R"({"schemaVersion":"999","selectedProfileId":"bad","backend":"auto","runtime":{"windowsVersion":"win11","syncMode":"default","highResolutionMode":false,"metalFxUpscaling":false},"latencySensitive":false,"competitive":false,"antiCheatRisk":"unknown","launchArgs":[],"environment":{},"dllOverrides":{}})");

    ASSERT_FALSE(result);
    EXPECT_NE(result.errorMessage.find("Unsupported runtime launch plan schemaVersion"),
              std::string::npos);
}

}  // namespace
}  // namespace mvrvb
