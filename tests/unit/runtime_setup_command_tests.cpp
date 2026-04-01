#include <gtest/gtest.h>

#include "runtime_launch_plan.h"
#include "runtime_setup_command.h"

#include <filesystem>

namespace mvrvb {
namespace {

std::filesystem::path repoRoot() {
    return std::filesystem::path(MVRVB_SOURCE_ROOT);
}

RuntimeLaunchPlan buildOverwatchPlan() {
    const auto result = buildRuntimeLaunchPlanFromDirectory(
        repoRoot() / "profiles",
        CompatibilityProfileQuery{
            .executable = R"(C:\Games\Overwatch\Overwatch.exe)",
            .launcher = "Battle.net",
            .store = "battlenet",
        });

    EXPECT_TRUE(result) << result.errorMessage;
    return result.plan;
}

TEST(RuntimeSetupCommand, BuildsAutomatedAndManualSetupSteps) {
    const RuntimeLaunchPlan plan = buildOverwatchPlan();
    const auto result = buildRuntimeSetupCommandPlan(
        plan,
        RuntimeSetupRequest{
            .prefixPath = R"(C:\Prefixes\Overwatch)",
            .winebootBinary = "wineboot64",
        });

    ASSERT_TRUE(result) << result.errorMessage;
    EXPECT_EQ(result.plan.workingDirectory, ".");
    EXPECT_EQ(result.plan.environment.at("WINEPREFIX"), R"(C:\Prefixes\Overwatch)");
    EXPECT_EQ(result.plan.environment.at("MVRVB_PREFIX_PRESET"), "battlenet-shooter");
    EXPECT_EQ(result.plan.environment.at("MVRVB_PREFIX_FAMILY"), "battlenet-shooter");
    EXPECT_EQ(result.plan.environment.at("MVRVB_INSTALL_PACKAGES"), "dxvk,battle.net");
    EXPECT_EQ(result.plan.environment.at("MVRVB_INSTALL_WINETRICKS"), "corefonts,vcrun2022");
    EXPECT_EQ(result.plan.environment.at("MVRVB_WINE_MIN_VERSION"), "11.0");
    EXPECT_EQ(result.plan.environment.at("MVRVB_WINE_PREFERRED_VERSION"), "11.0");
    EXPECT_EQ(result.plan.environment.at("MVRVB_REQUIRES_WINE_MONO"), "0");
    EXPECT_EQ(result.plan.environment.at("MVRVB_DX11_BACKEND"), "dxvk");
    EXPECT_EQ(result.plan.environment.at("MVRVB_DX12_BACKEND"), "vkd3d-proton");
    EXPECT_EQ(result.plan.environment.at("MVRVB_VULKAN_BACKEND"), "native-vulkan");
    ASSERT_EQ(result.plan.actions.size(), 2u);
    EXPECT_EQ(result.plan.actions[0].program, "wineboot64");
    ASSERT_EQ(result.plan.actions[0].arguments.size(), 1u);
    EXPECT_EQ(result.plan.actions[0].arguments[0], "-u");
    EXPECT_EQ(result.plan.actions[1].program, "winetricks");
    ASSERT_EQ(result.plan.actions[1].arguments.size(), 2u);
    EXPECT_EQ(result.plan.actions[1].arguments[0], "corefonts");
    EXPECT_EQ(result.plan.actions[1].arguments[1], "vcrun2022");
    ASSERT_EQ(result.plan.manualActions.size(), 6u);
    EXPECT_NE(result.plan.manualActions[0].find("dxvk"), std::string::npos);
    EXPECT_NE(result.plan.manualActions[1].find("battle.net"), std::string::npos);
    EXPECT_NE(result.plan.manualActions[3].find("Wine 11.0 or newer"), std::string::npos);
    EXPECT_NE(result.plan.manualActions[4].find("Prefer Wine 11.0"), std::string::npos);
    ASSERT_EQ(result.warnings.size(), 1u);
    EXPECT_NE(result.warnings.front().find("Anti-cheat risk is blocking"), std::string::npos);
}

TEST(RuntimeSetupCommand, BashScriptIncludesAutomatedAndManualSteps) {
    const RuntimeLaunchPlan plan = buildOverwatchPlan();
    const auto result = buildRuntimeSetupCommandPlan(
        plan,
        RuntimeSetupRequest{
            .prefixPath = R"(C:\Prefixes\Overwatch)",
        });

    ASSERT_TRUE(result) << result.errorMessage;
    const std::string script = renderRuntimeSetupCommandBash(result.plan);

    EXPECT_NE(script.find("export WINEPREFIX='C:\\Prefixes\\Overwatch'"), std::string::npos);
    EXPECT_NE(script.find("export MVRVB_DX12_BACKEND='vkd3d-proton'"), std::string::npos);
    EXPECT_NE(script.find("'wineboot' '-u'"), std::string::npos);
    EXPECT_NE(script.find("'winetricks' 'corefonts' 'vcrun2022'"), std::string::npos);
    EXPECT_NE(script.find("# - Prefer Wine 11.0 when preparing this prefix."), std::string::npos);
}

TEST(RuntimeSetupCommand, PowerShellScriptIncludesAutomatedAndManualSteps) {
    const RuntimeLaunchPlan plan = buildOverwatchPlan();
    const auto result = buildRuntimeSetupCommandPlan(
        plan,
        RuntimeSetupRequest{
            .prefixPath = R"(C:\Prefixes\Overwatch)",
        });

    ASSERT_TRUE(result) << result.errorMessage;
    const std::string script = renderRuntimeSetupCommandPowerShell(result.plan);

    EXPECT_NE(script.find("$env:WINEPREFIX = 'C:\\Prefixes\\Overwatch'"), std::string::npos);
    EXPECT_NE(script.find("$env:MVRVB_WINE_MIN_VERSION = '11.0'"), std::string::npos);
    EXPECT_NE(script.find("& 'wineboot' '-u'"), std::string::npos);
    EXPECT_NE(script.find("& 'winetricks' 'corefonts' 'vcrun2022'"), std::string::npos);
    EXPECT_NE(script.find("# - Bootstrap the required launcher inside the prefix"), std::string::npos);
}

TEST(RuntimeSetupCommand, RejectsMissingPrefixPath) {
    const auto result = buildRuntimeSetupCommandPlan(RuntimeLaunchPlan{}, RuntimeSetupRequest{});

    ASSERT_FALSE(result);
    EXPECT_NE(result.errorMessage.find("missing prefixPath"), std::string::npos);
}

}  // namespace
}  // namespace mvrvb
