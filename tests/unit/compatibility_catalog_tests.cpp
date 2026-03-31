#include <gtest/gtest.h>

#include "compatibility_catalog.h"

#include <algorithm>
#include <filesystem>

namespace mvrvb {
namespace {

std::filesystem::path repoRoot() {
    return std::filesystem::path(MVRVB_SOURCE_ROOT);
}

TEST(CompatibilityCatalog, BuildsSummaryFromCheckedInProfiles) {
    const auto result = buildCompatibilityCatalogFromDirectory(repoRoot() / "profiles");

    ASSERT_TRUE(result) << result.errorMessage;
    EXPECT_EQ(result.catalog.summary.totalProfiles, 3);
    EXPECT_EQ(result.catalog.summary.autoMatchProfiles, 2);
    EXPECT_EQ(result.catalog.summary.templateProfiles, 1);
    EXPECT_EQ(result.catalog.summary.competitiveProfiles, 2);
    EXPECT_EQ(result.catalog.summary.latencySensitiveProfiles, 2);
    EXPECT_EQ(result.catalog.summary.statusCounts.at("planning"), 3);
    EXPECT_EQ(result.catalog.summary.categoryCounts.at("baseline"), 1);
    EXPECT_EQ(result.catalog.summary.categoryCounts.at("competitive-shooter"), 2);
    EXPECT_EQ(result.catalog.summary.backendCounts.at("auto"), 1);
    EXPECT_EQ(result.catalog.summary.backendCounts.at("dxvk"), 2);
}

TEST(CompatibilityCatalog, CapturesCheckedInProfileDetails) {
    const auto result = buildCompatibilityCatalogFromDirectory(repoRoot() / "profiles");

    ASSERT_TRUE(result) << result.errorMessage;
    const auto it = std::find_if(result.catalog.entries.begin(),
                                 result.catalog.entries.end(),
                                 [](const CompatibilityCatalogEntry& entry) {
                                     return entry.profileId == "overwatch-2";
                                 });
    ASSERT_NE(it, result.catalog.entries.end());
    EXPECT_EQ(it->displayName, "Overwatch 2");
    EXPECT_TRUE(it->allowAutoMatch);
    EXPECT_EQ(it->status, ProfileStatus::Planning);
    EXPECT_EQ(it->defaultRenderer, RendererBackend::DXVK);
    EXPECT_EQ(it->antiCheatRisk, AntiCheatRisk::Blocking);
    EXPECT_TRUE(it->competitive);
    EXPECT_TRUE(it->latencySensitive);
    EXPECT_EQ(it->runtime.syncMode, SyncMode::MSync);
    EXPECT_TRUE(it->runtime.highResolutionMode);
    EXPECT_EQ(it->install.prefixPreset, "battlenet-shooter");
    EXPECT_TRUE(it->install.requiresLauncher);
    ASSERT_EQ(it->install.packages.size(), 2u);
    EXPECT_EQ(it->install.packages[0], "battle.net");
    EXPECT_EQ(it->install.packages[1], "dxvk");
    ASSERT_EQ(it->install.winetricks.size(), 2u);
    EXPECT_EQ(it->install.winetricks[0], "corefonts");
    EXPECT_EQ(it->install.winetricks[1], "vcrun2022");
    ASSERT_EQ(it->match.launchers.size(), 1u);
    EXPECT_EQ(it->match.launchers[0], "Battle.net");
    ASSERT_EQ(it->match.executables.size(), 2u);
    EXPECT_EQ(it->environmentCount, 2u);
    EXPECT_EQ(it->dllOverrideCount, 2u);
    ASSERT_EQ(it->launchArgs.size(), 1u);
    EXPECT_EQ(it->launchArgs[0], "--fullscreen");
}

TEST(CompatibilityCatalog, JsonIncludesSummaryAndEntryFields) {
    const auto result = buildCompatibilityCatalogFromDirectory(repoRoot() / "profiles");

    ASSERT_TRUE(result) << result.errorMessage;
    const std::string json = compatibilityCatalogToJson(result.catalog);

    EXPECT_NE(json.find("\"schemaVersion\":\"1\""), std::string::npos);
    EXPECT_NE(json.find("\"totalProfiles\":3"), std::string::npos);
    EXPECT_NE(json.find("\"profileId\":\"overwatch-2\""), std::string::npos);
    EXPECT_NE(json.find("\"defaultRenderer\":\"dxvk\""), std::string::npos);
    EXPECT_NE(json.find("\"antiCheatRisk\":\"blocking\""), std::string::npos);
    EXPECT_NE(json.find("\"prefixPreset\":\"battlenet-shooter\""), std::string::npos);
    EXPECT_NE(json.find("\"requiresLauncher\":true"), std::string::npos);
}

TEST(CompatibilityCatalog, MarkdownIncludesMatrixAndHighlights) {
    const auto result = buildCompatibilityCatalogFromDirectory(repoRoot() / "profiles");

    ASSERT_TRUE(result) << result.errorMessage;
    const std::string markdown = compatibilityCatalogToMarkdown(result.catalog);

    EXPECT_NE(markdown.find("# Compatibility Catalog"), std::string::npos);
    EXPECT_NE(markdown.find("| Overwatch 2 (`overwatch-2`)"), std::string::npos);
    EXPECT_NE(markdown.find("### Competitive Shooter (DXVK Template)"), std::string::npos);
    EXPECT_NE(markdown.find("- Sync mode: `msync`"), std::string::npos);
    EXPECT_NE(markdown.find("- Prefix preset: `battlenet-shooter`"), std::string::npos);
}

TEST(CompatibilityCatalog, RejectsEmptyCatalogInput) {
    const auto result = buildCompatibilityCatalog({});

    ASSERT_FALSE(result);
    EXPECT_NE(result.errorMessage.find("No compatibility profiles"), std::string::npos);
}

}  // namespace
}  // namespace mvrvb
