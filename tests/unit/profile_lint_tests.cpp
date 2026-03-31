#include <gtest/gtest.h>

#include "profile_lint.h"

#include <filesystem>

namespace mvrvb {
namespace {

std::filesystem::path repoRoot() {
    return std::filesystem::path(MVRVB_SOURCE_ROOT);
}

CompatibilityProfile makeProfile(std::string profileId) {
    CompatibilityProfile profile;
    profile.profileId = std::move(profileId);
    profile.displayName = profile.profileId;
    return profile;
}

PrefixPreset makePreset(std::string presetId) {
    PrefixPreset preset;
    preset.presetId = std::move(presetId);
    preset.displayName = preset.presetId;
    return preset;
}

TEST(ProfileLint, AcceptsCheckedInProfilesAndPresets) {
    const auto result = lintCompatibilityDataFromDirectory(repoRoot() / "profiles");

    EXPECT_TRUE(result) << summarizeProfileLintResult(result);
    EXPECT_EQ(result.errorCount, 0);
    EXPECT_EQ(result.warningCount, 0);
}

TEST(ProfileLint, DetectsMissingGlobalDefaultsAndPresetReferences) {
    auto profile = makeProfile("arena-shooter");
    profile.allowAutoMatch = true;
    profile.install.prefixPreset = "missing-preset";
    profile.match.executables = {"Arena.exe"};

    const auto result = lintCompatibilityData({profile}, {});

    EXPECT_FALSE(result);
    EXPECT_EQ(result.errorCount, 2);
    EXPECT_NE(summarizeProfileLintResult(result).find("global-defaults"), std::string::npos);
    EXPECT_NE(summarizeProfileLintResult(result).find("missing prefix preset"),
              std::string::npos);
}

TEST(ProfileLint, DetectsDuplicateIdsAndAmbiguousAutoMatch) {
    auto global = makeProfile("global-defaults");
    global.allowAutoMatch = true;

    auto left = makeProfile("arena-left");
    left.allowAutoMatch = true;
    left.install.prefixPreset = "general-game";
    left.match.executables = {"Arena.exe"};
    left.match.launchers = {"Steam"};

    auto right = makeProfile("arena-right");
    right.allowAutoMatch = true;
    right.install.prefixPreset = "general-game";
    right.match.executables = {"C:/Games/Arena.exe"};
    right.match.launchers = {"Steam"};

    auto presetLeft = makePreset("general-game");
    auto presetRight = makePreset("general-game");

    const auto result = lintCompatibilityData({global, left, right}, {presetLeft, presetRight});

    EXPECT_FALSE(result);
    EXPECT_EQ(result.errorCount, 1);
    EXPECT_EQ(result.warningCount, 1);
    const std::string summary = summarizeProfileLintResult(result);
    EXPECT_NE(summary.find("duplicate-id"), std::string::npos);
    EXPECT_NE(summary.find("ambiguous-auto-match"), std::string::npos);
}

TEST(ProfileLint, WarnsOnBroadAutoMatchProfilesOutsideGlobalDefaults) {
    auto global = makeProfile("global-defaults");
    global.allowAutoMatch = true;

    auto broad = makeProfile("broad-template");
    broad.allowAutoMatch = true;
    broad.install.prefixPreset = "general-game";

    auto preset = makePreset("general-game");

    const auto result = lintCompatibilityData({global, broad}, {preset});

    EXPECT_TRUE(result);
    EXPECT_EQ(result.errorCount, 0);
    EXPECT_EQ(result.warningCount, 1);
    EXPECT_NE(summarizeProfileLintResult(result).find("auto-match but has no executable"),
              std::string::npos);
}

}  // namespace
}  // namespace mvrvb
