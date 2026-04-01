#pragma once

#include "compatibility_profile.h"
#include "prefix_preset.h"

#include <filesystem>
#include <string>
#include <vector>

namespace mvrvb {

enum class ProfileLintSeverity {
    Warning,
    Error,
};

struct ProfileLintFinding {
    ProfileLintSeverity severity{ProfileLintSeverity::Warning};
    std::string code;
    std::string message;
};

struct ProfileLintResult {
    std::vector<ProfileLintFinding> findings;
    int warningCount{0};
    int errorCount{0};

    explicit operator bool() const noexcept { return errorCount == 0; }
};

const char* profileLintSeverityName(ProfileLintSeverity severity) noexcept;
ProfileLintResult lintCompatibilityData(const std::vector<CompatibilityProfile>& profiles,
                                        const std::vector<PrefixPreset>& prefixPresets);
ProfileLintResult lintCompatibilityDataFromDirectory(const std::filesystem::path& root);
std::string summarizeProfileLintResult(const ProfileLintResult& result);

}  // namespace mvrvb
