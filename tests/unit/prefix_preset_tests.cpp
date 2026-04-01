#include <gtest/gtest.h>

#include "prefix_preset.h"

#include <filesystem>

namespace mvrvb {
namespace {

std::filesystem::path repoRoot() {
    return std::filesystem::path(MVRVB_SOURCE_ROOT);
}

TEST(PrefixPreset, ParsesStructuredPresetFields) {
    const char* presetText = R"(
schema_version = 1
preset_id = battlenet-shooter
display_name = Battle.net Shooter
category = launcher
notes = Shared launcher-oriented shooter prefix

[install]
packages = dxvk, battle.net
winetricks = corefonts, vcrun2022
requires_launcher = true
notes = Install Battle.net before launching titles that depend on it

[env]
MVRVB_PREFIX_FAMILY = battlenet-shooter

[dll_overrides]
d3d11 = native,builtin
dxgi = native,builtin

[launch]
args = --fullscreen
)";

    const auto result = parsePrefixPreset(presetText);

    ASSERT_TRUE(result) << result.errorMessage;
    EXPECT_EQ(result.preset.schemaVersion, "1");
    EXPECT_EQ(result.preset.presetId, "battlenet-shooter");
    EXPECT_EQ(result.preset.displayName, "Battle.net Shooter");
    EXPECT_EQ(result.preset.category, "launcher");
    EXPECT_EQ(result.preset.notes, "Shared launcher-oriented shooter prefix");
    ASSERT_EQ(result.preset.install.packages.size(), 2u);
    EXPECT_EQ(result.preset.install.packages[0], "dxvk");
    EXPECT_EQ(result.preset.install.packages[1], "battle.net");
    ASSERT_EQ(result.preset.install.winetricks.size(), 2u);
    EXPECT_EQ(result.preset.install.winetricks[0], "corefonts");
    EXPECT_EQ(result.preset.install.winetricks[1], "vcrun2022");
    EXPECT_TRUE(result.preset.install.requiresLauncher);
    EXPECT_EQ(result.preset.environment.at("MVRVB_PREFIX_FAMILY"), "battlenet-shooter");
    EXPECT_EQ(result.preset.dllOverrides.at("d3d11"), "native,builtin");
    ASSERT_EQ(result.preset.launchArgs.size(), 1u);
    EXPECT_EQ(result.preset.launchArgs[0], "--fullscreen");
}

TEST(PrefixPreset, RejectsMissingRequiredFields) {
    const auto result = parsePrefixPreset("display_name = Missing Preset Id\n");

    EXPECT_FALSE(result);
    EXPECT_EQ(result.errorMessage, "Missing required prefix preset field 'preset-id'");
}

TEST(PrefixPreset, LoadsCheckedInPrefixPresets) {
    const auto batch = loadPrefixPresetsFromDirectory(repoRoot() / "profiles" / "prefix-presets");

    ASSERT_TRUE(batch) << ::testing::PrintToString(batch.errorMessages);
    EXPECT_GE(batch.presets.size(), 3u);
    EXPECT_NE(findPrefixPresetById(batch.presets, "general-game"), nullptr);
    EXPECT_NE(findPrefixPresetById(batch.presets, "competitive-shooter"), nullptr);
    EXPECT_NE(findPrefixPresetById(batch.presets, "battlenet-shooter"), nullptr);
}

TEST(PrefixPreset, CheckedInBattlenetPresetCarriesLauncherSetupIntent) {
    const auto result = loadPrefixPreset(
        repoRoot() / "profiles" / "prefix-presets" / "battlenet-shooter.mvrvb-prefix-preset");

    ASSERT_TRUE(result) << result.errorMessage;
    EXPECT_EQ(result.preset.displayName, "Battle.net Shooter");
    EXPECT_TRUE(result.preset.install.requiresLauncher);
    ASSERT_EQ(result.preset.install.packages.size(), 2u);
    EXPECT_EQ(result.preset.install.packages[0], "dxvk");
    EXPECT_EQ(result.preset.install.packages[1], "battle.net");
    EXPECT_EQ(result.preset.environment.at("MVRVB_PREFIX_FAMILY"), "battlenet-shooter");
    EXPECT_EQ(result.preset.dllOverrides.at("dxgi"), "native,builtin");
}

}  // namespace
}  // namespace mvrvb
