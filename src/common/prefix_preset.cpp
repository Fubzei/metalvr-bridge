#include "prefix_preset.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace mvrvb {
namespace {

std::string trim(std::string_view value) {
    size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])) != 0) {
        ++first;
    }

    size_t last = value.size();
    while (last > first &&
           std::isspace(static_cast<unsigned char>(value[last - 1])) != 0) {
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

}  // namespace

PrefixPresetParseResult parsePrefixPreset(std::string_view text) {
    PrefixPresetParseResult result;

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
                result.errorMessage =
                    "Empty prefix preset section header on line " + std::to_string(lineNumber);
                return result;
            }
            continue;
        }

        const size_t equals = trimmed.find('=');
        if (equals == std::string::npos) {
            result.errorMessage =
                "Expected key=value in prefix preset on line " + std::to_string(lineNumber);
            return result;
        }

        const std::string rawKey = trim(trimmed.substr(0, equals));
        const std::string normalizedKey = normalizeToken(rawKey);
        const std::string value = trim(trimmed.substr(equals + 1));
        if (rawKey.empty()) {
            result.errorMessage =
                "Empty key in prefix preset on line " + std::to_string(lineNumber);
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
            result.errorMessage = "Missing required prefix preset field '" + std::string(key) + "'";
            return false;
        }
        *out = trim(it->second);
        return true;
    };

    if (auto it = globals.find("schema-version"); it != globals.end()) {
        result.preset.schemaVersion = trim(it->second);
    }
    if (!readRequiredGlobal("preset-id", &result.preset.presetId)) {
        return result;
    }
    if (!readRequiredGlobal("display-name", &result.preset.displayName)) {
        return result;
    }

    if (auto it = globals.find("category"); it != globals.end()) {
        result.preset.category = trim(it->second);
    }
    if (auto it = globals.find("notes"); it != globals.end()) {
        result.preset.notes = trim(it->second);
    }

    if (const auto sectionIt = sections.find("install"); sectionIt != sections.end()) {
        if (const auto it = sectionIt->second.find("packages"); it != sectionIt->second.end()) {
            result.preset.install.packages = splitCsv(it->second);
        }
        if (const auto it = sectionIt->second.find("winetricks"); it != sectionIt->second.end()) {
            result.preset.install.winetricks = splitCsv(it->second);
        }
        if (const auto it = sectionIt->second.find("requires-launcher");
            it != sectionIt->second.end()) {
            if (!parseBoolValue(it->second, &result.preset.install.requiresLauncher)) {
                result.errorMessage =
                    "Invalid boolean for prefix preset install.requires-launcher";
                return result;
            }
        }
        if (const auto it = sectionIt->second.find("notes"); it != sectionIt->second.end()) {
            result.preset.install.notes = trim(it->second);
        }
    }

    if (const auto sectionIt = sections.find("launch"); sectionIt != sections.end()) {
        if (const auto it = sectionIt->second.find("args"); it != sectionIt->second.end()) {
            result.preset.launchArgs = splitCsv(it->second);
        }
    }

    if (const auto sectionIt = sections.find("env"); sectionIt != sections.end()) {
        result.preset.environment = sectionIt->second;
    }
    if (const auto sectionIt = sections.find("dll-overrides"); sectionIt != sections.end()) {
        result.preset.dllOverrides = sectionIt->second;
    }

    return result;
}

PrefixPresetParseResult loadPrefixPreset(const std::filesystem::path& path) {
    PrefixPresetParseResult result;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        result.errorMessage = "Failed to open prefix preset: " + path.string();
        return result;
    }

    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return parsePrefixPreset(buffer.str());
}

PrefixPresetBatchLoadResult loadPrefixPresetsFromDirectory(const std::filesystem::path& root) {
    PrefixPresetBatchLoadResult result;
    if (!std::filesystem::exists(root)) {
        result.errorMessages.push_back("Prefix preset directory does not exist: " + root.string());
        return result;
    }

    std::vector<std::filesystem::path> presetPaths;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file() ||
            entry.path().extension() != ".mvrvb-prefix-preset") {
            continue;
        }
        presetPaths.push_back(entry.path());
    }

    std::sort(presetPaths.begin(), presetPaths.end());
    for (const auto& path : presetPaths) {
        const auto loadResult = loadPrefixPreset(path);
        if (!loadResult) {
            result.errorMessages.push_back(path.string() + ": " + loadResult.errorMessage);
            continue;
        }
        result.presets.push_back(loadResult.preset);
    }

    return result;
}

const PrefixPreset* findPrefixPresetById(const std::vector<PrefixPreset>& presets,
                                         std::string_view presetId) noexcept {
    for (const auto& preset : presets) {
        if (preset.presetId == presetId) {
            return &preset;
        }
    }
    return nullptr;
}

}  // namespace mvrvb
