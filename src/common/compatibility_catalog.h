#pragma once

#include "compatibility_profile.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace mvrvb {

inline constexpr const char* kCompatibilityCatalogSchemaVersion = "1";

struct CompatibilityCatalogEntry {
    std::string profileId;
    std::string displayName;
    ProfileStatus status{ProfileStatus::Planning};
    bool allowAutoMatch{true};
    std::string category;
    std::string notes;
    RendererBackend defaultRenderer{RendererBackend::Auto};
    std::vector<RendererBackend> fallbackRenderers;
    bool latencySensitive{false};
    bool competitive{false};
    AntiCheatRisk antiCheatRisk{AntiCheatRisk::Unknown};
    CompatibilityRuntimePolicy runtime;
    CompatibilityMatchCriteria match;
    std::vector<std::string> launchArgs;
    size_t environmentCount{0};
    size_t dllOverrideCount{0};
};

struct CompatibilityCatalogSummary {
    int totalProfiles{0};
    int autoMatchProfiles{0};
    int templateProfiles{0};
    int competitiveProfiles{0};
    int latencySensitiveProfiles{0};
    std::map<std::string, int> statusCounts;
    std::map<std::string, int> categoryCounts;
    std::map<std::string, int> backendCounts;
};

struct CompatibilityCatalog {
    CompatibilityCatalogSummary summary;
    std::vector<CompatibilityCatalogEntry> entries;
};

struct CompatibilityCatalogResult {
    CompatibilityCatalog catalog;
    std::string errorMessage;

    explicit operator bool() const noexcept { return errorMessage.empty(); }
};

CompatibilityCatalogResult buildCompatibilityCatalog(
    const std::vector<CompatibilityProfile>& profiles);
CompatibilityCatalogResult buildCompatibilityCatalogFromDirectory(
    const std::filesystem::path& root);
std::string summarizeCompatibilityCatalog(const CompatibilityCatalog& catalog);
std::string describeCompatibilityCatalog(const CompatibilityCatalog& catalog);
std::string compatibilityCatalogToJson(const CompatibilityCatalog& catalog);
bool writeCompatibilityCatalogReport(
    const CompatibilityCatalog& catalog,
    const std::filesystem::path& path,
    std::string* errorMessage = nullptr);
bool writeCompatibilityCatalogJson(
    const CompatibilityCatalog& catalog,
    const std::filesystem::path& path,
    std::string* errorMessage = nullptr);

}  // namespace mvrvb
