#include "compatibility_profile.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace mvrvb {
namespace {

std::string trim(std::string_view value) {
    size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }

    size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }

    return std::string(value.substr(first, last - first));
}

std::string normalizeToken(std::string_view value) {
    std::string token = trim(value);
    std::transform(token.begin(), token.end(), token.begin(), [](unsigned char ch) {
        if (ch == '_' || ch == ' ') return static_cast<unsigned char>('-');
        return static_cast<unsigned char>(std::tolower(ch));
    });
    return token;
}

std::vector<std::string> splitCsv(std::string_view value) {
    std::vector<std::string> tokens;
    size_t start = 0;
    while (start <= value.size()) {
        const size_t comma = value.find(',', start);
        const size_t end = (comma == std::string_view::npos) ? value.size() : comma;
        std::string token = trim(value.substr(start, end - start));
        if (!token.empty()) {
            tokens.push_back(std::move(token));
        }
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    return tokens;
}

bool parseBoolValue(std::string_view value, bool* out) {
    const std::string token = normalizeToken(value);
    if (token == "1" || token == "true" || token == "yes" || token == "on") {
        *out = true;
        return true;
    }
    if (token == "0" || token == "false" || token == "no" || token == "off") {
        *out = false;
        return true;
    }
    return false;
}

bool parseRendererBackend(std::string_view value, RendererBackend* out) {
    const std::string token = normalizeToken(value);
    if (token == "auto") {
        *out = RendererBackend::Auto;
        return true;
    }
    if (token == "dxvk") {
        *out = RendererBackend::DXVK;
        return true;
    }
    if (token == "vkd3d-proton" || token == "vkd3dproton") {
        *out = RendererBackend::VKD3DProton;
        return true;
    }
    if (token == "d3dmetal") {
        *out = RendererBackend::D3DMetal;
        return true;
    }
    if (token == "dxmt") {
        *out = RendererBackend::DXMT;
        return true;
    }
    if (token == "wined3d") {
        *out = RendererBackend::WineD3D;
        return true;
    }
    if (token == "native-vulkan" || token == "vulkan") {
        *out = RendererBackend::NativeVulkan;
        return true;
    }
    return false;
}

bool parseProfileStatus(std::string_view value, ProfileStatus* out) {
    const std::string token = normalizeToken(value);
    if (token == "planning") {
        *out = ProfileStatus::Planning;
        return true;
    }
    if (token == "experimental") {
        *out = ProfileStatus::Experimental;
        return true;
    }
    if (token == "validated") {
        *out = ProfileStatus::Validated;
        return true;
    }
    return false;
}

bool parseAntiCheatRisk(std::string_view value, AntiCheatRisk* out) {
    const std::string token = normalizeToken(value);
    if (token == "unknown") {
        *out = AntiCheatRisk::Unknown;
        return true;
    }
    if (token == "low") {
        *out = AntiCheatRisk::Low;
        return true;
    }
    if (token == "medium") {
        *out = AntiCheatRisk::Medium;
        return true;
    }
    if (token == "high") {
        *out = AntiCheatRisk::High;
        return true;
    }
    if (token == "blocking") {
        *out = AntiCheatRisk::Blocking;
        return true;
    }
    return false;
}

std::string sectionKey(const std::string& section, const std::string& key) {
    return section.empty() ? key : (section + "." + key);
}

std::string normalizeIdentity(std::string_view value) {
    std::string token = trim(value);
    std::transform(token.begin(), token.end(), token.begin(), [](unsigned char ch) {
        return static_cast<unsigned char>(std::tolower(ch));
    });
    return token;
}

std::string executableBaseName(std::string_view value) {
    const std::string trimmed = trim(value);
    const size_t slash = trimmed.find_last_of("/\\");
    if (slash == std::string::npos) {
        return normalizeIdentity(trimmed);
    }
    return normalizeIdentity(trimmed.substr(slash + 1));
}

bool parseSyncMode(std::string_view value, SyncMode* out) {
    const std::string token = normalizeToken(value);
    if (token == "default" || token == "auto") {
        *out = SyncMode::Default;
        return true;
    }
    if (token == "msync") {
        *out = SyncMode::MSync;
        return true;
    }
    if (token == "esync") {
        *out = SyncMode::ESync;
        return true;
    }
    if (token == "disabled" || token == "off") {
        *out = SyncMode::Disabled;
        return true;
    }
    return false;
}

int profileStatusRank(ProfileStatus status) noexcept {
    switch (status) {
        case ProfileStatus::Planning: return 0;
        case ProfileStatus::Experimental: return 1;
        case ProfileStatus::Validated: return 2;
        default: return 0;
    }
}

}  // namespace

const char* profileStatusName(ProfileStatus status) noexcept {
    switch (status) {
        case ProfileStatus::Planning: return "planning";
        case ProfileStatus::Experimental: return "experimental";
        case ProfileStatus::Validated: return "validated";
        default: return "planning";
    }
}

const char* rendererBackendName(RendererBackend backend) noexcept {
    switch (backend) {
        case RendererBackend::Auto: return "auto";
        case RendererBackend::DXVK: return "dxvk";
        case RendererBackend::VKD3DProton: return "vkd3d-proton";
        case RendererBackend::D3DMetal: return "d3dmetal";
        case RendererBackend::DXMT: return "dxmt";
        case RendererBackend::WineD3D: return "wined3d";
        case RendererBackend::NativeVulkan: return "native-vulkan";
        default: return "auto";
    }
}

const char* antiCheatRiskName(AntiCheatRisk risk) noexcept {
    switch (risk) {
        case AntiCheatRisk::Unknown: return "unknown";
        case AntiCheatRisk::Low: return "low";
        case AntiCheatRisk::Medium: return "medium";
        case AntiCheatRisk::High: return "high";
        case AntiCheatRisk::Blocking: return "blocking";
        default: return "unknown";
    }
}

const char* syncModeName(SyncMode mode) noexcept {
    switch (mode) {
        case SyncMode::Default: return "default";
        case SyncMode::MSync: return "msync";
        case SyncMode::ESync: return "esync";
        case SyncMode::Disabled: return "disabled";
        default: return "default";
    }
}

CompatibilityProfileParseResult parseCompatibilityProfile(std::string_view text) {
    CompatibilityProfileParseResult result;

    std::map<std::string, std::string> globals;
    std::map<std::string, std::map<std::string, std::string>> sections;

    std::istringstream stream{std::string(text)};
    std::string line;
    std::string currentSection;
    size_t lineNumber = 0;

    while (std::getline(stream, line)) {
        ++lineNumber;
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
            continue;
        }

        if (trimmed.front() == '[' && trimmed.back() == ']') {
            currentSection = normalizeToken(trimmed.substr(1, trimmed.size() - 2));
            if (currentSection.empty()) {
                result.errorMessage = "Empty section header on line " + std::to_string(lineNumber);
                return result;
            }
            continue;
        }

        const size_t equals = trimmed.find('=');
        if (equals == std::string::npos) {
            result.errorMessage = "Expected key=value on line " + std::to_string(lineNumber);
            return result;
        }

        const std::string rawKey = trim(trimmed.substr(0, equals));
        const std::string normalizedKey = normalizeToken(rawKey);
        const std::string value = trim(trimmed.substr(equals + 1));
        if (rawKey.empty()) {
            result.errorMessage = "Empty key on line " + std::to_string(lineNumber);
            return result;
        }

        if (currentSection.empty()) {
            globals[normalizedKey] = value;
        } else {
            const bool preserveKeySpelling =
                currentSection == "env" || currentSection == "dll-overrides";
            sections[currentSection][preserveKeySpelling ? rawKey : normalizedKey] = value;
        }
    }

    auto readRequiredGlobal = [&](const char* key, std::string* out) -> bool {
        const auto it = globals.find(key);
        if (it == globals.end() || trim(it->second).empty()) {
            result.errorMessage = "Missing required profile field '" + std::string(key) + "'";
            return false;
        }
        *out = trim(it->second);
        return true;
    };

    if (auto it = globals.find("schema-version"); it != globals.end()) {
        result.profile.schemaVersion = trim(it->second);
    }

    if (!readRequiredGlobal("profile-id", &result.profile.profileId)) {
        return result;
    }
    if (!readRequiredGlobal("display-name", &result.profile.displayName)) {
        return result;
    }

    if (const auto it = globals.find("status"); it != globals.end()) {
        if (!parseProfileStatus(it->second, &result.profile.status)) {
            result.errorMessage = "Invalid profile status '" + it->second + "'";
            return result;
        }
    }
    if (const auto it = globals.find("allow-auto-match"); it != globals.end()) {
        if (!parseBoolValue(it->second, &result.profile.allowAutoMatch)) {
            result.errorMessage = "Invalid boolean for " + sectionKey("", it->first);
            return result;
        }
    }

    if (const auto it = globals.find("category"); it != globals.end()) {
        result.profile.category = trim(it->second);
    }
    if (const auto it = globals.find("notes"); it != globals.end()) {
        result.profile.notes = trim(it->second);
    }
    if (const auto it = globals.find("default-renderer"); it != globals.end()) {
        if (!parseRendererBackend(it->second, &result.profile.defaultRenderer)) {
            result.errorMessage = "Invalid renderer backend '" + it->second + "'";
            return result;
        }
    }

    if (const auto it = globals.find("fallback-renderers"); it != globals.end()) {
        std::unordered_set<int> seen;
        for (const auto& token : splitCsv(it->second)) {
            RendererBackend backend{};
            if (!parseRendererBackend(token, &backend)) {
                result.errorMessage = "Invalid fallback renderer '" + token + "'";
                return result;
            }
            if (seen.insert(static_cast<int>(backend)).second) {
                result.profile.fallbackRenderers.push_back(backend);
            }
        }
    }

    if (const auto it = globals.find("latency-sensitive"); it != globals.end()) {
        if (!parseBoolValue(it->second, &result.profile.latencySensitive)) {
            result.errorMessage = "Invalid boolean for " + sectionKey("", it->first);
            return result;
        }
    }

    if (const auto it = globals.find("competitive"); it != globals.end()) {
        if (!parseBoolValue(it->second, &result.profile.competitive)) {
            result.errorMessage = "Invalid boolean for " + sectionKey("", it->first);
            return result;
        }
    }

    if (const auto it = globals.find("anti-cheat-risk"); it != globals.end()) {
        if (!parseAntiCheatRisk(it->second, &result.profile.antiCheatRisk)) {
            result.errorMessage = "Invalid anti-cheat risk '" + it->second + "'";
            return result;
        }
    }

    if (const auto sectionIt = sections.find("match"); sectionIt != sections.end()) {
        if (const auto it = sectionIt->second.find("executables"); it != sectionIt->second.end()) {
            result.profile.match.executables = splitCsv(it->second);
        }
        if (const auto it = sectionIt->second.find("launchers"); it != sectionIt->second.end()) {
            result.profile.match.launchers = splitCsv(it->second);
        }
        if (const auto it = sectionIt->second.find("stores"); it != sectionIt->second.end()) {
            result.profile.match.stores = splitCsv(it->second);
        }
    }

    if (const auto sectionIt = sections.find("launch"); sectionIt != sections.end()) {
        if (const auto it = sectionIt->second.find("args"); it != sectionIt->second.end()) {
            result.profile.launchArgs = splitCsv(it->second);
        }
    }

    if (const auto sectionIt = sections.find("runtime"); sectionIt != sections.end()) {
        if (const auto it = sectionIt->second.find("windows-version");
            it != sectionIt->second.end()) {
            result.profile.runtime.windowsVersion = trim(it->second);
        }
        if (const auto it = sectionIt->second.find("sync-mode"); it != sectionIt->second.end()) {
            if (!parseSyncMode(it->second, &result.profile.runtime.syncMode)) {
                result.errorMessage = "Invalid sync mode '" + it->second + "'";
                return result;
            }
        }
        if (const auto it = sectionIt->second.find("high-resolution-mode");
            it != sectionIt->second.end()) {
            if (!parseBoolValue(it->second, &result.profile.runtime.highResolutionMode)) {
                result.errorMessage = "Invalid boolean for runtime.high-resolution-mode";
                return result;
            }
        }
        if (const auto it = sectionIt->second.find("metalfx-upscaling");
            it != sectionIt->second.end()) {
            if (!parseBoolValue(it->second, &result.profile.runtime.metalFxUpscaling)) {
                result.errorMessage = "Invalid boolean for runtime.metalfx-upscaling";
                return result;
            }
        }
    }

    if (const auto sectionIt = sections.find("install"); sectionIt != sections.end()) {
        if (const auto it = sectionIt->second.find("prefix-preset"); it != sectionIt->second.end()) {
            result.profile.install.prefixPreset = trim(it->second);
        }
        if (const auto it = sectionIt->second.find("packages"); it != sectionIt->second.end()) {
            result.profile.install.packages = splitCsv(it->second);
        }
        if (const auto it = sectionIt->second.find("winetricks"); it != sectionIt->second.end()) {
            result.profile.install.winetricks = splitCsv(it->second);
        }
        if (const auto it = sectionIt->second.find("requires-launcher");
            it != sectionIt->second.end()) {
            if (!parseBoolValue(it->second, &result.profile.install.requiresLauncher)) {
                result.errorMessage = "Invalid boolean for install.requires-launcher";
                return result;
            }
        }
        if (const auto it = sectionIt->second.find("notes"); it != sectionIt->second.end()) {
            result.profile.install.notes = trim(it->second);
        }
    }

    if (const auto sectionIt = sections.find("env"); sectionIt != sections.end()) {
        result.profile.environment = sectionIt->second;
    }

    if (const auto sectionIt = sections.find("dll-overrides"); sectionIt != sections.end()) {
        result.profile.dllOverrides = sectionIt->second;
    }

    return result;
}

CompatibilityProfileParseResult loadCompatibilityProfile(const std::filesystem::path& path) {
    CompatibilityProfileParseResult result;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        result.errorMessage = "Failed to open compatibility profile: " + path.string();
        return result;
    }

    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return parseCompatibilityProfile(buffer.str());
}

CompatibilityProfileBatchLoadResult loadCompatibilityProfilesFromDirectory(
    const std::filesystem::path& root) {
    CompatibilityProfileBatchLoadResult result;
    if (!std::filesystem::exists(root)) {
        result.errorMessages.push_back("Compatibility profile directory does not exist: " +
                                       root.string());
        return result;
    }

    std::vector<std::filesystem::path> profilePaths;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".mvrvb-profile") {
            continue;
        }
        profilePaths.push_back(entry.path());
    }

    std::sort(profilePaths.begin(), profilePaths.end());
    for (const auto& path : profilePaths) {
        const auto loadResult = loadCompatibilityProfile(path);
        if (!loadResult) {
            result.errorMessages.push_back(path.string() + ": " + loadResult.errorMessage);
            continue;
        }
        result.profiles.push_back(loadResult.profile);
    }

    return result;
}

int compatibilityProfileMatchScore(
    const CompatibilityProfile& profile,
    const CompatibilityProfileQuery& query) noexcept {
    if (!profile.allowAutoMatch) {
        return 0;
    }

    int score = 0;
    const std::string queryExecutable = executableBaseName(query.executable);
    const std::string queryLauncher = normalizeIdentity(query.launcher);
    const std::string queryStore = normalizeIdentity(query.store);

    for (const auto& executable : profile.match.executables) {
        if (!queryExecutable.empty() &&
            executableBaseName(executable) == queryExecutable) {
            score += 100;
            break;
        }
    }
    for (const auto& launcher : profile.match.launchers) {
        if (!queryLauncher.empty() && normalizeIdentity(launcher) == queryLauncher) {
            score += 50;
            break;
        }
    }
    for (const auto& store : profile.match.stores) {
        if (!queryStore.empty() && normalizeIdentity(store) == queryStore) {
            score += 25;
            break;
        }
    }

    const bool hasSpecificCriteria =
        !profile.match.executables.empty() ||
        !profile.match.launchers.empty() ||
        !profile.match.stores.empty();

    if (!hasSpecificCriteria) {
        score += 1;
    }

    return score;
}

std::optional<size_t> selectBestCompatibilityProfileIndex(
    const std::vector<CompatibilityProfile>& profiles,
    const CompatibilityProfileQuery& query) noexcept {
    std::optional<size_t> bestIndex;
    int bestScore = 0;
    int bestStatusRank = -1;

    for (size_t i = 0; i < profiles.size(); ++i) {
        const int score = compatibilityProfileMatchScore(profiles[i], query);
        const int statusRank = profileStatusRank(profiles[i].status);
        if (score > bestScore || (score == bestScore && score > 0 && statusRank > bestStatusRank)) {
            bestIndex = i;
            bestScore = score;
            bestStatusRank = statusRank;
        }
    }

    return bestIndex;
}

}  // namespace mvrvb
