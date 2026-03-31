#include <gtest/gtest.h>

#include "runtime_launch_command.h"
#include "runtime_launch_plan.h"

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

TEST(RuntimeLaunchCommand, MaterializesWineCommandFromLaunchPlan) {
    const RuntimeLaunchPlan plan = buildOverwatchPlan();
    const auto result = materializeRuntimeLaunchCommand(
        plan,
        RuntimeLaunchRequest{
            .executablePath = R"(C:\Games\Overwatch\Overwatch.exe)",
            .wineBinary = "wine64",
            .prefixPath = R"(C:\Prefixes\Overwatch)",
        });

    ASSERT_TRUE(result) << result.errorMessage;
    EXPECT_EQ(result.command.program, "wine64");
    ASSERT_EQ(result.command.arguments.size(), 2u);
    EXPECT_EQ(result.command.arguments[0], R"(C:\Games\Overwatch\Overwatch.exe)");
    EXPECT_EQ(result.command.arguments[1], "--fullscreen");
    EXPECT_EQ(result.command.workingDirectory, R"(C:\Games\Overwatch)");
    EXPECT_EQ(result.command.environment.at("WINEPREFIX"), R"(C:\Prefixes\Overwatch)");
    EXPECT_EQ(result.command.environment.at("WINEDLLOVERRIDES"),
              "d3d11=native,builtin;dxgi=native,builtin");
    EXPECT_EQ(result.command.environment.at("MVRVB_RENDERER_BACKEND"), "dxvk");
    EXPECT_EQ(result.command.environment.at("MVRVB_SYNC_MODE"), "msync");
    EXPECT_EQ(result.command.environment.at("MVRVB_HIGH_RESOLUTION_MODE"), "1");
    EXPECT_EQ(result.command.environment.at("MVRVB_SELECTED_PROFILE"), "overwatch-2");
    EXPECT_EQ(result.command.environment.at("MVRVB_PREFIX_PRESET"), "battlenet-shooter");
    EXPECT_EQ(result.command.environment.at("MVRVB_PREFIX_FAMILY"), "battlenet-shooter");
    EXPECT_EQ(result.command.environment.at("MVRVB_INSTALL_PACKAGES"), "dxvk,battle.net");
    EXPECT_EQ(result.command.environment.at("MVRVB_INSTALL_WINETRICKS"), "corefonts,vcrun2022");
    EXPECT_EQ(result.command.environment.at("MVRVB_REQUIRES_LAUNCHER"), "1");
    ASSERT_EQ(result.warnings.size(), 1u);
    EXPECT_NE(result.warnings.front().find("Anti-cheat risk is blocking"), std::string::npos);
}

TEST(RuntimeLaunchCommand, BashScriptIncludesExportsAndExecInvocation) {
    const RuntimeLaunchPlan plan = buildOverwatchPlan();
    const auto result = materializeRuntimeLaunchCommand(
        plan,
        RuntimeLaunchRequest{
            .executablePath = R"(C:\Games\Overwatch\Overwatch.exe)",
            .prefixPath = R"(C:\Prefixes\Overwatch)",
        });

    ASSERT_TRUE(result) << result.errorMessage;
    const std::string script = renderRuntimeLaunchCommandBash(result.command);

    EXPECT_NE(script.find("export WINEPREFIX='C:\\Prefixes\\Overwatch'"), std::string::npos);
    EXPECT_NE(script.find("export MVRVB_PREFIX_PRESET='battlenet-shooter'"), std::string::npos);
    EXPECT_NE(script.find("export WINEDLLOVERRIDES='d3d11=native,builtin;dxgi=native,builtin'"),
              std::string::npos);
    EXPECT_NE(script.find("cd 'C:\\Games\\Overwatch'"), std::string::npos);
    EXPECT_NE(script.find("exec 'wine' 'C:\\Games\\Overwatch\\Overwatch.exe' '--fullscreen'"),
              std::string::npos);
}

TEST(RuntimeLaunchCommand, PowerShellScriptIncludesEnvironmentAndInvocation) {
    const RuntimeLaunchPlan plan = buildOverwatchPlan();
    const auto result = materializeRuntimeLaunchCommand(
        plan,
        RuntimeLaunchRequest{
            .executablePath = R"(C:\Games\Overwatch\Overwatch.exe)",
            .workingDirectory = R"(C:\Games\Overwatch)",
        });

    ASSERT_TRUE(result) << result.errorMessage;
    const std::string script = renderRuntimeLaunchCommandPowerShell(result.command);

    EXPECT_NE(script.find("$env:MVRVB_RENDERER_BACKEND = 'dxvk'"), std::string::npos);
    EXPECT_NE(script.find("$env:MVRVB_INSTALL_PACKAGES = 'dxvk,battle.net'"), std::string::npos);
    EXPECT_NE(script.find("$env:WINEDLLOVERRIDES = 'd3d11=native,builtin;dxgi=native,builtin'"),
              std::string::npos);
    EXPECT_NE(script.find("Set-Location -LiteralPath 'C:\\Games\\Overwatch'"), std::string::npos);
    EXPECT_NE(script.find("& 'wine' 'C:\\Games\\Overwatch\\Overwatch.exe' '--fullscreen'"),
              std::string::npos);
}

TEST(RuntimeLaunchCommand, RejectsMissingExecutablePath) {
    const auto result = materializeRuntimeLaunchCommand(
        RuntimeLaunchPlan{},
        RuntimeLaunchRequest{});

    ASSERT_FALSE(result);
    EXPECT_NE(result.errorMessage.find("missing executablePath"), std::string::npos);
}

}  // namespace
}  // namespace mvrvb
