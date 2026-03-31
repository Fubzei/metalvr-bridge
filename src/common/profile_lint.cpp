#include "profile_lint.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>

namespace mvrvb {
namespace {

std::string normalizeIdentity(std::string_view value) {
    std::string token;
    token.reserve(value.size());
    for (const unsigned char ch : value) {
        if (std::isspace(ch) != 0) continue;
        token.push_back(static_cast<char>(std::tolower(ch)));
    }
    return token;
}

std::string executableBaseName(std::string_view value) {
    const size_t slash = value.find_last_of("/\\");
    if (slash == std::string_view::npos) {
        return normalizeIdentity(value);
    }
    return normalizeIdentity(value.substr(slash + 1));
}

bool hasSpecificMatchCriteria(const CompatibilityProfile& profile) {
    return !profile.match.executables.empty() ||
           !profile.match.launchers.empty() ||
           !profile.match.stores.empty();
}

bool hasOverlap(std::vector<std::string> lhs, std::vector<std::string> rhs) {
    if (lhs.empty() || rhs.empty()) return true;
    std::sort(lhs.begin(), lhs.end());
    lhs.erase(std::unique(lhs.begin(), lhs.end()), lhs.end());
    std::sort(rhs.begin(), rhs.end());
    rhs.erase(std::unique(rhs.begin(), rhs.end()), rhs.end());
    size_t i = 0;
    size_t j = 0;
    while (i < lhs.size() && j < rhs.size()) {
        if (lhs[i] == rhs[j]) return true;
        if (lhs[i] < rhs[j]) {
            ++i;
        } else {
            ++j;
        }
    }
    return false;
}

std::vector<std::string> normalizedExecutables(const CompatibilityProfile& profile) {
    std::vector<std::string> values;
    values.reserve(profile.match.executables.size());
    for (const auto& executable : profile.match.executables) {
        values.push_back(executableBaseName(executable));
    }
    return values;
}

std::vector<std::string> normalizedLaunchers(const CompatibilityProfile& profile) {
    std::vector<std::string> values;
    values.reserve(profile.match.launchers.size());
    for (const auto& launcher : profile.match.launchers) {
        values.push_back(normalizeIdentity(launcher));
    }
    return values;
}

std::vector<std::string> normalizedStores(const CompatibilityProfile& profile) {
    std::vector<std::string> values;
    values.reserve(profile.match.stores.size());
    for (const auto& store : profile.match.stores) {
        values.push_back(normalizeIdentity(store));
    }
    return values;
}

bool couldAmbiguouslyAutoMatch(const CompatibilityProfile& lhs,
                               const CompatibilityProfile& rhs) {
    if (!lhs.allowAutoMatch || !rhs.allowAutoMatch) return false;
    if (lhs.profileId == "global-defaults" || rhs.profileId == "global-defaults") return false;
    if (lhs.profileId == rhs.profileId) return false;
    if (lhs.match.executables.empty() || rhs.match.executables.empty()) return false;
    return hasOverlap(normalizedExecutables(lhs), normalizedExecutables(rhs)) &&
           hasOverlap(normalizedLaunchers(lhs), normalizedLaunchers(rhs)) &&
           hasOverlap(normalizedStores(lhs), normalizedStores(rhs));
}

void addFinding(ProfileLintResult* result,
                ProfileLintSeverity severity,
                std::string code,
                std::string message) {
    if (!result) return;
    result->findings.push_back(ProfileLintFinding{
        .severity = severity,
        .code = std::move(code),
        .message = std::move(message),
    });
    if (severity == ProfileLintSeverity::Error) {
        ++result->errorCount;
    } else {
        ++result->warningCount;
    }
}

}  // namespace

const char* profileLintSeverityName(ProfileLintSeverity severity) noexcept {
    switch (severity) {
        case ProfileLintSeverity::Warning: return "warning";
        case ProfileLintSeverity::Error: return "error";
        default: return "warning";
    }
}

ProfileLintResult lintCompatibilityData(const std::vector<CompatibilityProfile>& profiles,
                                        const std::vector<PrefixPreset>& prefixPresets) {
    ProfileLintResult result;
    if (profiles.empty()) {
        addFinding(
            &result,
            ProfileLintSeverity::Error,
            "profiles.empty",
            "No compatibility profiles are available to lint.");
        return result;
    }

    std::unordered_map<std::string, int> profileIdCounts;
    bool hasGlobalDefaults = false;
    for (const auto& profile : profiles) {
        ++profileIdCounts[profile.profileId];
        if (profile.profileId == "global-defaults") {
            hasGlobalDefaults = true;
        }
    }
    if (!hasGlobalDefaults) {
        addFinding(
            &result,
            ProfileLintSeverity::Error,
            "profiles.missing-global-defaults",
            "No checked-in profile with profile_id 'global-defaults' was found.");
    }
    for (const auto& [profileId, count] : profileIdCounts) {
        if (count > 1) {
            addFinding(
                &result,
                ProfileLintSeverity::Error,
                "profiles.duplicate-id",
                "Duplicate compatibility profile id detected: " + profileId);
        }
    }

    std::unordered_map<std::string, int> presetIdCounts;
    for (const auto& preset : prefixPresets) {
        ++presetIdCounts[preset.presetId];
    }
    for (const auto& [presetId, count] : presetIdCounts) {
        if (count > 1) {
            addFinding(
                &result,
                ProfileLintSeverity::Error,
                "prefix-presets.duplicate-id",
                "Duplicate prefix preset id detected: " + presetId);
        }
    }

    for (const auto& profile : profiles) {
        if (!profile.install.prefixPreset.empty() &&
            findPrefixPresetById(prefixPresets, profile.install.prefixPreset) == nullptr) {
            addFinding(
                &result,
                ProfileLintSeverity::Error,
                "profiles.missing-prefix-preset",
                "Profile '" + profile.profileId + "' references missing prefix preset '" +
                    profile.install.prefixPreset + "'.");
        }

        if (profile.allowAutoMatch &&
            profile.profileId != "global-defaults" &&
            !hasSpecificMatchCriteria(profile)) {
            addFinding(
                &result,
                ProfileLintSeverity::Warning,
                "profiles.auto-match-without-criteria",
                "Profile '" + profile.profileId +
                    "' allows auto-match but has no executable, launcher, or store criteria.");
        }
    }

    for (size_t i = 0; i < profiles.size(); ++i) {
        for (size_t j = i + 1; j < profiles.size(); ++j) {
            if (couldAmbiguouslyAutoMatch(profiles[i], profiles[j])) {
                addFinding(
                    &result,
                    ProfileLintSeverity::Warning,
                    "profiles.ambiguous-auto-match",
                    "Profiles '" + profiles[i].profileId + "' and '" + profiles[j].profileId +
                        "' share overlapping auto-match identity rules.");
            }
        }
    }

    return result;
}

ProfileLintResult lintCompatibilityDataFromDirectory(const std::filesystem::path& root) {
    const auto profileBatch = loadCompatibilityProfilesFromDirectory(root);
    ProfileLintResult result;
    if (!profileBatch) {
        for (const auto& error : profileBatch.errorMessages) {
            addFinding(
                &result,
                ProfileLintSeverity::Error,
                "profiles.load-failure",
                error);
        }
        return result;
    }

    const auto presetBatch = loadPrefixPresetsFromDirectory(root / "prefix-presets");
    if (!presetBatch) {
        for (const auto& error : presetBatch.errorMessages) {
            addFinding(
                &result,
                ProfileLintSeverity::Error,
                "prefix-presets.load-failure",
                error);
        }
        return result;
    }

    return lintCompatibilityData(profileBatch.profiles, presetBatch.presets);
}

std::string summarizeProfileLintResult(const ProfileLintResult& result) {
    std::ostringstream out;
    out << "Profile lint summary: "
        << result.errorCount << " error(s), "
        << result.warningCount << " warning(s)\n";
    if (result.findings.empty()) {
        out << "No lint findings.";
        return out.str();
    }

    for (const auto& finding : result.findings) {
        out << "- [" << profileLintSeverityName(finding.severity) << "] "
            << finding.code << ": " << finding.message << "\n";
    }
    return out.str();
}

}  // namespace mvrvb
