#include "runtime_launch_plan.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>

namespace mvrvb {
namespace {

std::string jsonEscape(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (const char ch : value) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20u) {
                    std::ostringstream code;
                    code << "\\u" << std::hex << std::uppercase;
                    code.width(4);
                    code.fill('0');
                    code << static_cast<int>(static_cast<unsigned char>(ch));
                    out += code.str();
                } else {
                    out += ch;
                }
                break;
        }
    }
    return out;
}

void appendJsonString(std::ostringstream* out, std::string_view value) {
    if (!out) return;
    *out << '"' << jsonEscape(value) << '"';
}

template <typename T, typename Writer>
void appendJsonArray(std::ostringstream* out, const std::vector<T>& values, Writer writeValue) {
    if (!out) return;
    *out << '[';
    for (size_t i = 0; i < values.size(); ++i) {
        if (i != 0) *out << ',';
        writeValue(out, values[i]);
    }
    *out << ']';
}

void appendJsonStringMap(std::ostringstream* out,
                         const std::map<std::string, std::string>& values) {
    if (!out) return;
    *out << '{';
    size_t i = 0;
    for (const auto& [key, value] : values) {
        if (i++ != 0) *out << ',';
        appendJsonString(out, key);
        *out << ':';
        appendJsonString(out, value);
    }
    *out << '}';
}

bool writeTextFile(const std::filesystem::path& path,
                   std::string_view contents,
                   std::string* errorMessage) {
    std::error_code ec;
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            if (errorMessage) {
                *errorMessage = "Failed to create parent directory for launch-plan file: " +
                                ec.message();
            }
            return false;
        }
    }

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open()) {
        if (errorMessage) {
            *errorMessage = "Failed to open launch-plan output file: " + path.string();
        }
        return false;
    }

    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!stream.good()) {
        if (errorMessage) {
            *errorMessage = "Failed while writing launch-plan output file: " + path.string();
        }
        return false;
    }

    return true;
}

void appendUtf8CodePoint(std::string* out, uint32_t codePoint) {
    if (!out) return;
    if (codePoint <= 0x7Fu) {
        out->push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7FFu) {
        out->push_back(static_cast<char>(0xC0u | ((codePoint >> 6u) & 0x1Fu)));
        out->push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
    } else if (codePoint <= 0xFFFFu) {
        out->push_back(static_cast<char>(0xE0u | ((codePoint >> 12u) & 0x0Fu)));
        out->push_back(static_cast<char>(0x80u | ((codePoint >> 6u) & 0x3Fu)));
        out->push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
    } else {
        out->push_back(static_cast<char>(0xF0u | ((codePoint >> 18u) & 0x07u)));
        out->push_back(static_cast<char>(0x80u | ((codePoint >> 12u) & 0x3Fu)));
        out->push_back(static_cast<char>(0x80u | ((codePoint >> 6u) & 0x3Fu)));
        out->push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
    }
}

std::optional<RendererBackend> rendererBackendFromName(std::string_view value) {
    if (value == "auto") return RendererBackend::Auto;
    if (value == "dxvk") return RendererBackend::DXVK;
    if (value == "vkd3d-proton") return RendererBackend::VKD3DProton;
    if (value == "d3dmetal") return RendererBackend::D3DMetal;
    if (value == "dxmt") return RendererBackend::DXMT;
    if (value == "wined3d") return RendererBackend::WineD3D;
    if (value == "native-vulkan") return RendererBackend::NativeVulkan;
    return std::nullopt;
}

std::optional<SyncMode> syncModeFromName(std::string_view value) {
    if (value == "default") return SyncMode::Default;
    if (value == "msync") return SyncMode::MSync;
    if (value == "esync") return SyncMode::ESync;
    if (value == "disabled") return SyncMode::Disabled;
    return std::nullopt;
}

std::optional<AntiCheatRisk> antiCheatRiskFromName(std::string_view value) {
    if (value == "unknown") return AntiCheatRisk::Unknown;
    if (value == "low") return AntiCheatRisk::Low;
    if (value == "medium") return AntiCheatRisk::Medium;
    if (value == "high") return AntiCheatRisk::High;
    if (value == "blocking") return AntiCheatRisk::Blocking;
    return std::nullopt;
}

class RuntimeLaunchPlanJsonParser {
public:
    explicit RuntimeLaunchPlanJsonParser(std::string_view input) : input_(input) {}

    RuntimeLaunchPlanResult parse() {
        RuntimeLaunchPlanResult result;
        bool sawSchemaVersion = false;
        skipWhitespace();
        if (!parseTopLevelObject(&result.plan, &sawSchemaVersion, &result.errorMessage)) {
            return result;
        }
        skipWhitespace();
        if (!atEnd()) {
            result.errorMessage = "Unexpected trailing JSON content at offset " +
                                  std::to_string(position_);
            return result;
        }
        if (result.plan.selectedProfileId.empty()) {
            result.errorMessage = "Runtime launch plan JSON is missing selectedProfileId";
        } else if (sawSchemaVersion &&
                   schemaVersion_ != kRuntimeLaunchPlanSchemaVersion) {
            result.errorMessage = "Unsupported runtime launch plan schemaVersion: " +
                                  schemaVersion_;
        }
        return result;
    }

private:
    bool atEnd() const { return position_ >= input_.size(); }

    char peek() const { return atEnd() ? '\0' : input_[position_]; }

    void skipWhitespace() {
        while (!atEnd() &&
               std::isspace(static_cast<unsigned char>(input_[position_])) != 0) {
            ++position_;
        }
    }

    bool consume(char expected) {
        skipWhitespace();
        if (peek() != expected) return false;
        ++position_;
        return true;
    }

    bool expect(char expected, std::string* errorMessage) {
        if (consume(expected)) return true;
        if (errorMessage) {
            *errorMessage = std::string("Expected '") + expected + "' at offset " +
                            std::to_string(position_);
        }
        return false;
    }

    bool parseString(std::string* out, std::string* errorMessage) {
        if (!out) return false;
        skipWhitespace();
        if (peek() != '"') {
            if (errorMessage) {
                *errorMessage = "Expected JSON string at offset " + std::to_string(position_);
            }
            return false;
        }
        ++position_;
        std::string value;
        while (!atEnd()) {
            const char ch = input_[position_++];
            if (ch == '"') {
                *out = std::move(value);
                return true;
            }
            if (ch != '\\') {
                value.push_back(ch);
                continue;
            }
            if (atEnd()) {
                if (errorMessage) {
                    *errorMessage = "Unterminated escape sequence in JSON string";
                }
                return false;
            }

            const char escape = input_[position_++];
            switch (escape) {
                case '"': value.push_back('"'); break;
                case '\\': value.push_back('\\'); break;
                case '/': value.push_back('/'); break;
                case 'b': value.push_back('\b'); break;
                case 'f': value.push_back('\f'); break;
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                case 'u': {
                    if (position_ + 4u > input_.size()) {
                        if (errorMessage) {
                            *errorMessage = "Incomplete \\u escape in JSON string";
                        }
                        return false;
                    }
                    uint32_t codePoint = 0u;
                    for (size_t i = 0; i < 4u; ++i) {
                        const char hex = input_[position_++];
                        codePoint <<= 4u;
                        if (hex >= '0' && hex <= '9') codePoint |= static_cast<uint32_t>(hex - '0');
                        else if (hex >= 'A' && hex <= 'F') codePoint |= static_cast<uint32_t>(10 + hex - 'A');
                        else if (hex >= 'a' && hex <= 'f') codePoint |= static_cast<uint32_t>(10 + hex - 'a');
                        else {
                            if (errorMessage) {
                                *errorMessage = "Invalid hex digit in \\u escape";
                            }
                            return false;
                        }
                    }
                    appendUtf8CodePoint(&value, codePoint);
                    break;
                }
                default:
                    if (errorMessage) {
                        *errorMessage = std::string("Unsupported JSON escape sequence: \\") +
                                        escape;
                    }
                    return false;
            }
        }

        if (errorMessage) {
            *errorMessage = "Unterminated JSON string";
        }
        return false;
    }

    bool parseBool(bool* out, std::string* errorMessage) {
        if (!out) return false;
        skipWhitespace();
        if (input_.substr(position_, 4u) == "true") {
            position_ += 4u;
            *out = true;
            return true;
        }
        if (input_.substr(position_, 5u) == "false") {
            position_ += 5u;
            *out = false;
            return true;
        }
        if (errorMessage) {
            *errorMessage = "Expected JSON boolean at offset " + std::to_string(position_);
        }
        return false;
    }

    bool parseInt(int* out, std::string* errorMessage) {
        if (!out) return false;
        skipWhitespace();
        const size_t start = position_;
        if (peek() == '-') {
            ++position_;
        }
        while (!atEnd() &&
               std::isdigit(static_cast<unsigned char>(input_[position_])) != 0) {
            ++position_;
        }
        if (position_ == start || (position_ == start + 1u && input_[start] == '-')) {
            if (errorMessage) {
                *errorMessage = "Expected JSON integer at offset " + std::to_string(start);
            }
            return false;
        }

        const auto numberView = input_.substr(start, position_ - start);
        const char* begin = numberView.data();
        const char* end = numberView.data() + numberView.size();
        auto [ptr, ec] = std::from_chars(begin, end, *out);
        if (ec != std::errc() || ptr != end) {
            if (errorMessage) {
                *errorMessage = "Invalid integer in runtime launch plan JSON";
            }
            return false;
        }
        return true;
    }

    bool parseStringArray(std::vector<std::string>* out, std::string* errorMessage) {
        if (!out) return false;
        out->clear();
        if (!expect('[', errorMessage)) return false;
        skipWhitespace();
        if (consume(']')) return true;

        while (true) {
            std::string value;
            if (!parseString(&value, errorMessage)) return false;
            out->push_back(std::move(value));
            skipWhitespace();
            if (consume(']')) return true;
            if (!expect(',', errorMessage)) return false;
        }
    }

    bool parseBackendArray(std::vector<RendererBackend>* out, std::string* errorMessage) {
        if (!out) return false;
        std::vector<std::string> names;
        if (!parseStringArray(&names, errorMessage)) return false;
        out->clear();
        for (const auto& name : names) {
            const auto backend = rendererBackendFromName(name);
            if (!backend.has_value()) {
                if (errorMessage) {
                    *errorMessage = "Unknown renderer backend in runtime launch plan JSON: " + name;
                }
                return false;
            }
            out->push_back(*backend);
        }
        return true;
    }

    bool parseStringMap(std::map<std::string, std::string>* out, std::string* errorMessage) {
        if (!out) return false;
        out->clear();
        if (!expect('{', errorMessage)) return false;
        skipWhitespace();
        if (consume('}')) return true;

        while (true) {
            std::string key;
            std::string value;
            if (!parseString(&key, errorMessage)) return false;
            if (!expect(':', errorMessage)) return false;
            if (!parseString(&value, errorMessage)) return false;
            (*out)[std::move(key)] = std::move(value);
            skipWhitespace();
            if (consume('}')) return true;
            if (!expect(',', errorMessage)) return false;
        }
    }

    bool skipLiteral(std::string_view literal) {
        if (input_.substr(position_, literal.size()) != literal) return false;
        position_ += literal.size();
        return true;
    }

    bool skipNumber(std::string* errorMessage) {
        const size_t start = position_;
        if (peek() == '-') ++position_;
        while (!atEnd() &&
               std::isdigit(static_cast<unsigned char>(input_[position_])) != 0) {
            ++position_;
        }
        if (!atEnd() && input_[position_] == '.') {
            ++position_;
            while (!atEnd() &&
                   std::isdigit(static_cast<unsigned char>(input_[position_])) != 0) {
                ++position_;
            }
        }
        if (!atEnd() && (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (!atEnd() && (input_[position_] == '+' || input_[position_] == '-')) ++position_;
            while (!atEnd() &&
                   std::isdigit(static_cast<unsigned char>(input_[position_])) != 0) {
                ++position_;
            }
        }
        if (position_ == start) {
            if (errorMessage) {
                *errorMessage = "Expected JSON number at offset " + std::to_string(start);
            }
            return false;
        }
        return true;
    }

    bool skipArray(std::string* errorMessage) {
        if (!expect('[', errorMessage)) return false;
        skipWhitespace();
        if (consume(']')) return true;
        while (true) {
            if (!skipValue(errorMessage)) return false;
            skipWhitespace();
            if (consume(']')) return true;
            if (!expect(',', errorMessage)) return false;
        }
    }

    bool skipObject(std::string* errorMessage) {
        if (!expect('{', errorMessage)) return false;
        skipWhitespace();
        if (consume('}')) return true;
        while (true) {
            std::string ignoredKey;
            if (!parseString(&ignoredKey, errorMessage)) return false;
            if (!expect(':', errorMessage)) return false;
            if (!skipValue(errorMessage)) return false;
            skipWhitespace();
            if (consume('}')) return true;
            if (!expect(',', errorMessage)) return false;
        }
    }

    bool skipValue(std::string* errorMessage) {
        skipWhitespace();
        switch (peek()) {
            case '{': return skipObject(errorMessage);
            case '[': return skipArray(errorMessage);
            case '"': {
                std::string ignored;
                return parseString(&ignored, errorMessage);
            }
            case 't': return skipLiteral("true");
            case 'f': return skipLiteral("false");
            case 'n': return skipLiteral("null");
            default:
                if (peek() == '-' ||
                    std::isdigit(static_cast<unsigned char>(peek())) != 0) {
                    return skipNumber(errorMessage);
                }
                if (errorMessage) {
                    *errorMessage = "Unsupported JSON value at offset " +
                                    std::to_string(position_);
                }
                return false;
        }
    }

    bool parseRuntimeObject(RuntimeLaunchPlan* plan, std::string* errorMessage) {
        if (!plan) return false;
        if (!expect('{', errorMessage)) return false;
        skipWhitespace();
        if (consume('}')) return true;

        while (true) {
            std::string key;
            if (!parseString(&key, errorMessage)) return false;
            if (!expect(':', errorMessage)) return false;

            if (key == "windowsVersion") {
                if (!parseString(&plan->windowsVersion, errorMessage)) return false;
            } else if (key == "syncMode") {
                std::string value;
                if (!parseString(&value, errorMessage)) return false;
                const auto mode = syncModeFromName(value);
                if (!mode.has_value()) {
                    if (errorMessage) {
                        *errorMessage = "Unknown sync mode in runtime launch plan JSON: " + value;
                    }
                    return false;
                }
                plan->syncMode = *mode;
            } else if (key == "highResolutionMode") {
                if (!parseBool(&plan->highResolutionMode, errorMessage)) return false;
            } else if (key == "metalFxUpscaling") {
                if (!parseBool(&plan->metalFxUpscaling, errorMessage)) return false;
            } else {
                if (!skipValue(errorMessage)) return false;
            }

            skipWhitespace();
            if (consume('}')) return true;
            if (!expect(',', errorMessage)) return false;
        }
    }

    bool parseInstallObject(RuntimeLaunchPlan* plan, std::string* errorMessage) {
        if (!plan) return false;
        if (!expect('{', errorMessage)) return false;
        skipWhitespace();
        if (consume('}')) return true;

        while (true) {
            std::string key;
            if (!parseString(&key, errorMessage)) return false;
            if (!expect(':', errorMessage)) return false;

            if (key == "prefixPreset") {
                if (!parseString(&plan->install.prefixPreset, errorMessage)) return false;
            } else if (key == "packages") {
                if (!parseStringArray(&plan->install.packages, errorMessage)) return false;
            } else if (key == "winetricks") {
                if (!parseStringArray(&plan->install.winetricks, errorMessage)) return false;
            } else if (key == "requiresLauncher") {
                if (!parseBool(&plan->install.requiresLauncher, errorMessage)) return false;
            } else if (key == "notes") {
                if (!parseString(&plan->install.notes, errorMessage)) return false;
            } else {
                if (!skipValue(errorMessage)) return false;
            }

            skipWhitespace();
            if (consume('}')) return true;
            if (!expect(',', errorMessage)) return false;
        }
    }

    bool parseTopLevelObject(RuntimeLaunchPlan* plan,
                            bool* sawSchemaVersion,
                            std::string* errorMessage) {
        if (!plan) return false;
        if (!expect('{', errorMessage)) return false;
        skipWhitespace();
        if (consume('}')) return true;

        while (true) {
            std::string key;
            if (!parseString(&key, errorMessage)) return false;
            if (!expect(':', errorMessage)) return false;

            if (key == "schemaVersion") {
                if (!parseString(&schemaVersion_, errorMessage)) return false;
                if (sawSchemaVersion) *sawSchemaVersion = true;
            } else if (key == "selectedProfileId") {
                if (!parseString(&plan->selectedProfileId, errorMessage)) return false;
            } else if (key == "selectedDisplayName") {
                if (!parseString(&plan->selectedDisplayName, errorMessage)) return false;
            } else if (key == "appliedProfileIds") {
                if (!parseStringArray(&plan->appliedProfileIds, errorMessage)) return false;
            } else if (key == "matchScore") {
                if (!parseInt(&plan->matchScore, errorMessage)) return false;
            } else if (key == "backend") {
                std::string value;
                if (!parseString(&value, errorMessage)) return false;
                const auto backend = rendererBackendFromName(value);
                if (!backend.has_value()) {
                    if (errorMessage) {
                        *errorMessage =
                            "Unknown renderer backend in runtime launch plan JSON: " + value;
                    }
                    return false;
                }
                plan->backend = *backend;
            } else if (key == "fallbackBackends") {
                if (!parseBackendArray(&plan->fallbackBackends, errorMessage)) return false;
            } else if (key == "runtime") {
                if (!parseRuntimeObject(plan, errorMessage)) return false;
            } else if (key == "install") {
                if (!parseInstallObject(plan, errorMessage)) return false;
            } else if (key == "latencySensitive") {
                if (!parseBool(&plan->latencySensitive, errorMessage)) return false;
            } else if (key == "competitive") {
                if (!parseBool(&plan->competitive, errorMessage)) return false;
            } else if (key == "antiCheatRisk") {
                std::string value;
                if (!parseString(&value, errorMessage)) return false;
                const auto risk = antiCheatRiskFromName(value);
                if (!risk.has_value()) {
                    if (errorMessage) {
                        *errorMessage =
                            "Unknown anti-cheat risk in runtime launch plan JSON: " + value;
                    }
                    return false;
                }
                plan->antiCheatRisk = *risk;
            } else if (key == "launchArgs") {
                if (!parseStringArray(&plan->launchArgs, errorMessage)) return false;
            } else if (key == "environment") {
                if (!parseStringMap(&plan->environment, errorMessage)) return false;
            } else if (key == "dllOverrides") {
                if (!parseStringMap(&plan->dllOverrides, errorMessage)) return false;
            } else {
                if (!skipValue(errorMessage)) return false;
            }

            skipWhitespace();
            if (consume('}')) return true;
            if (!expect(',', errorMessage)) return false;
        }
    }

    std::string_view input_;
    std::string schemaVersion_;
    size_t position_{0};
};

void appendUniqueString(std::vector<std::string>* items, const std::string& value) {
    if (!items || value.empty()) return;
    if (std::find(items->begin(), items->end(), value) == items->end()) {
        items->push_back(value);
    }
}

void appendUniqueBackend(std::vector<RendererBackend>* items, RendererBackend backend) {
    if (!items) return;
    if (std::find(items->begin(), items->end(), backend) == items->end()) {
        items->push_back(backend);
    }
}

const CompatibilityProfile* findGlobalDefaults(const std::vector<CompatibilityProfile>& profiles) {
    for (const auto& profile : profiles) {
        if (profile.profileId == "global-defaults") {
            return &profile;
        }
    }
    return nullptr;
}

void mergeMaps(std::map<std::string, std::string>* dst,
               const std::map<std::string, std::string>& src) {
    if (!dst) return;
    for (const auto& [key, value] : src) {
        (*dst)[key] = value;
    }
}

void mergeArgs(std::vector<std::string>* dst, const std::vector<std::string>& src) {
    if (!dst) return;
    for (const auto& arg : src) {
        appendUniqueString(dst, arg);
    }
}

void appendFallbacks(std::vector<RendererBackend>* dst,
                     const std::vector<RendererBackend>& src) {
    if (!dst) return;
    for (RendererBackend backend : src) {
        appendUniqueBackend(dst, backend);
    }
}

void mergeInstallPolicy(CompatibilityInstallPolicy* dst,
                        const CompatibilityInstallPolicy& src,
                        bool preferScalarFields) {
    if (!dst) return;
    if ((preferScalarFields || dst->prefixPreset.empty()) && !src.prefixPreset.empty()) {
        dst->prefixPreset = src.prefixPreset;
    }
    for (const auto& package : src.packages) {
        appendUniqueString(&dst->packages, package);
    }
    for (const auto& verb : src.winetricks) {
        appendUniqueString(&dst->winetricks, verb);
    }
    dst->requiresLauncher = dst->requiresLauncher || src.requiresLauncher;
    if ((preferScalarFields || dst->notes.empty()) && !src.notes.empty()) {
        dst->notes = src.notes;
    }
}

}  // namespace

RuntimeLaunchPlanResult buildRuntimeLaunchPlan(
    const std::vector<CompatibilityProfile>& profiles,
    const CompatibilityProfileQuery& query) {
    RuntimeLaunchPlanResult result;
    if (profiles.empty()) {
        result.errorMessage = "No compatibility profiles are available";
        return result;
    }

    const CompatibilityProfile* globalDefaults = findGlobalDefaults(profiles);
    const auto selectedIndex = selectBestCompatibilityProfileIndex(profiles, query);
    const CompatibilityProfile* selectedProfile = nullptr;
    if (selectedIndex.has_value()) {
        selectedProfile = &profiles[*selectedIndex];
    } else {
        selectedProfile = globalDefaults;
    }

    if (!selectedProfile) {
        result.errorMessage = "No matching compatibility profile and no global defaults found";
        return result;
    }

    if (globalDefaults) {
        appendUniqueString(&result.plan.appliedProfileIds, globalDefaults->profileId);
        mergeMaps(&result.plan.environment, globalDefaults->environment);
        mergeMaps(&result.plan.dllOverrides, globalDefaults->dllOverrides);
        mergeArgs(&result.plan.launchArgs, globalDefaults->launchArgs);
        mergeInstallPolicy(&result.plan.install, globalDefaults->install, true);
    }

    if (selectedProfile != globalDefaults) {
        appendUniqueString(&result.plan.appliedProfileIds, selectedProfile->profileId);
    }

    result.plan.selectedProfileId = selectedProfile->profileId;
    result.plan.selectedDisplayName = selectedProfile->displayName;
    result.plan.matchScore = compatibilityProfileMatchScore(*selectedProfile, query);
    result.plan.backend = selectedProfile->defaultRenderer;
    result.plan.windowsVersion = selectedProfile->runtime.windowsVersion;
    if (result.plan.windowsVersion.empty() && globalDefaults) {
        result.plan.windowsVersion = globalDefaults->runtime.windowsVersion;
    }
    result.plan.syncMode = selectedProfile->runtime.syncMode;
    result.plan.highResolutionMode = selectedProfile->runtime.highResolutionMode;
    result.plan.metalFxUpscaling = selectedProfile->runtime.metalFxUpscaling;
    result.plan.latencySensitive = selectedProfile->latencySensitive;
    result.plan.competitive = selectedProfile->competitive;
    result.plan.antiCheatRisk = selectedProfile->antiCheatRisk;

    appendFallbacks(&result.plan.fallbackBackends, selectedProfile->fallbackRenderers);
    if (globalDefaults && globalDefaults != selectedProfile) {
        appendFallbacks(&result.plan.fallbackBackends, globalDefaults->fallbackRenderers);
    }

    mergeMaps(&result.plan.environment, selectedProfile->environment);
    mergeMaps(&result.plan.dllOverrides, selectedProfile->dllOverrides);
    mergeArgs(&result.plan.launchArgs, selectedProfile->launchArgs);
    mergeInstallPolicy(&result.plan.install, selectedProfile->install, true);

    return result;
}

RuntimeLaunchPlanResult buildRuntimeLaunchPlanFromDirectory(
    const std::filesystem::path& root,
    const CompatibilityProfileQuery& query) {
    const auto batch = loadCompatibilityProfilesFromDirectory(root);
    RuntimeLaunchPlanResult result;
    if (!batch) {
        std::ostringstream errors;
        for (size_t i = 0; i < batch.errorMessages.size(); ++i) {
            if (i != 0) errors << "; ";
            errors << batch.errorMessages[i];
        }
        result.errorMessage = errors.str();
        return result;
    }
    return buildRuntimeLaunchPlan(batch.profiles, query);
}

RuntimeLaunchPlanResult parseRuntimeLaunchPlanJson(std::string_view text) {
    return RuntimeLaunchPlanJsonParser(text).parse();
}

RuntimeLaunchPlanResult loadRuntimeLaunchPlanJson(const std::filesystem::path& path) {
    RuntimeLaunchPlanResult result;
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
        result.errorMessage = "Failed to open runtime launch plan JSON: " + path.string();
        return result;
    }

    const std::string text((std::istreambuf_iterator<char>(stream)),
                           std::istreambuf_iterator<char>());
    return parseRuntimeLaunchPlanJson(text);
}

std::string summarizeRuntimeLaunchPlan(const RuntimeLaunchPlan& plan) {
    std::ostringstream out;
    out << "Selected profile: " << plan.selectedProfileId;
    if (!plan.selectedDisplayName.empty()) {
        out << " (" << plan.selectedDisplayName << ")";
    }
    out << "\n";
    out << "Applied profiles: ";
    for (size_t i = 0; i < plan.appliedProfileIds.size(); ++i) {
        if (i != 0) out << ", ";
        out << plan.appliedProfileIds[i];
    }
    out << "\n";
    out << "Backend: " << rendererBackendName(plan.backend) << "\n";
    out << "Fallbacks: ";
    for (size_t i = 0; i < plan.fallbackBackends.size(); ++i) {
        if (i != 0) out << ", ";
        out << rendererBackendName(plan.fallbackBackends[i]);
    }
    out << "\n";
    out << "Windows version: " << plan.windowsVersion << "\n";
    out << "Sync mode: " << syncModeName(plan.syncMode) << "\n";
    out << "High resolution mode: " << (plan.highResolutionMode ? "true" : "false") << "\n";
    out << "MetalFX upscaling: " << (plan.metalFxUpscaling ? "true" : "false") << "\n";
    out << "Latency sensitive: " << (plan.latencySensitive ? "true" : "false") << "\n";
    out << "Competitive: " << (plan.competitive ? "true" : "false") << "\n";
    out << "Anti-cheat risk: " << antiCheatRiskName(plan.antiCheatRisk) << "\n";
    out << "Install prefix preset: " << plan.install.prefixPreset << "\n";
    out << "Requires launcher: " << (plan.install.requiresLauncher ? "true" : "false") << "\n";
    out << "Install packages: ";
    for (size_t i = 0; i < plan.install.packages.size(); ++i) {
        if (i != 0) out << ", ";
        out << plan.install.packages[i];
    }
    out << "\n";
    out << "Winetricks verbs: ";
    for (size_t i = 0; i < plan.install.winetricks.size(); ++i) {
        if (i != 0) out << ", ";
        out << plan.install.winetricks[i];
    }
    out << "\n";
    out << "Match score: " << plan.matchScore << "\n";
    out << "Launch args: ";
    for (size_t i = 0; i < plan.launchArgs.size(); ++i) {
        if (i != 0) out << ", ";
        out << plan.launchArgs[i];
    }
    out << "\n";
    out << "Environment entries: " << plan.environment.size() << "\n";
    out << "DLL override entries: " << plan.dllOverrides.size();
    return out.str();
}

std::string describeRuntimeLaunchPlan(const RuntimeLaunchPlan& plan) {
    std::ostringstream out;
    out << summarizeRuntimeLaunchPlan(plan) << "\n";
    out << "Environment:\n";
    if (plan.environment.empty()) {
        out << "  (none)\n";
    } else {
        for (const auto& [key, value] : plan.environment) {
            out << "  " << key << "=" << value << "\n";
        }
    }
    out << "DLL overrides:\n";
    if (plan.dllOverrides.empty()) {
        out << "  (none)\n";
    } else {
        for (const auto& [key, value] : plan.dllOverrides) {
            out << "  " << key << "=" << value << "\n";
        }
    }
    out << "Install policy:\n";
    out << "  prefix_preset=" << plan.install.prefixPreset << "\n";
    out << "  requires_launcher=" << (plan.install.requiresLauncher ? "true" : "false") << "\n";
    out << "  packages:\n";
    if (plan.install.packages.empty()) {
        out << "    (none)\n";
    } else {
        for (const auto& package : plan.install.packages) {
            out << "    " << package << "\n";
        }
    }
    out << "  winetricks:\n";
    if (plan.install.winetricks.empty()) {
        out << "    (none)\n";
    } else {
        for (const auto& verb : plan.install.winetricks) {
            out << "    " << verb << "\n";
        }
    }
    out << "  notes: " << plan.install.notes << "\n";
    out << "Launch arguments:\n";
    if (plan.launchArgs.empty()) {
        out << "  (none)\n";
    } else {
        for (const auto& arg : plan.launchArgs) {
            out << "  " << arg << "\n";
        }
    }
    return out.str();
}

std::string runtimeLaunchPlanToJson(const RuntimeLaunchPlan& plan) {
    std::ostringstream out;
    out << '{';

    out << "\"schemaVersion\":";
    appendJsonString(&out, kRuntimeLaunchPlanSchemaVersion);
    out << ",\"selectedProfileId\":";
    appendJsonString(&out, plan.selectedProfileId);
    out << ",\"selectedDisplayName\":";
    appendJsonString(&out, plan.selectedDisplayName);
    out << ",\"appliedProfileIds\":";
    appendJsonArray(&out, plan.appliedProfileIds, [](std::ostringstream* stream, const std::string& value) {
        appendJsonString(stream, value);
    });
    out << ",\"matchScore\":" << plan.matchScore;
    out << ",\"backend\":";
    appendJsonString(&out, rendererBackendName(plan.backend));
    out << ",\"fallbackBackends\":";
    appendJsonArray(
        &out,
        plan.fallbackBackends,
        [](std::ostringstream* stream, RendererBackend backend) {
            appendJsonString(stream, rendererBackendName(backend));
        });
    out << ",\"runtime\":{";
    out << "\"windowsVersion\":";
    appendJsonString(&out, plan.windowsVersion);
    out << ",\"syncMode\":";
    appendJsonString(&out, syncModeName(plan.syncMode));
    out << ",\"highResolutionMode\":" << (plan.highResolutionMode ? "true" : "false");
    out << ",\"metalFxUpscaling\":" << (plan.metalFxUpscaling ? "true" : "false");
    out << '}';
    out << ",\"install\":{";
    out << "\"prefixPreset\":";
    appendJsonString(&out, plan.install.prefixPreset);
    out << ",\"packages\":";
    appendJsonArray(&out, plan.install.packages, [](std::ostringstream* stream, const std::string& value) {
        appendJsonString(stream, value);
    });
    out << ",\"winetricks\":";
    appendJsonArray(&out, plan.install.winetricks, [](std::ostringstream* stream, const std::string& value) {
        appendJsonString(stream, value);
    });
    out << ",\"requiresLauncher\":" << (plan.install.requiresLauncher ? "true" : "false");
    out << ",\"notes\":";
    appendJsonString(&out, plan.install.notes);
    out << '}';
    out << ",\"latencySensitive\":" << (plan.latencySensitive ? "true" : "false");
    out << ",\"competitive\":" << (plan.competitive ? "true" : "false");
    out << ",\"antiCheatRisk\":";
    appendJsonString(&out, antiCheatRiskName(plan.antiCheatRisk));
    out << ",\"launchArgs\":";
    appendJsonArray(&out, plan.launchArgs, [](std::ostringstream* stream, const std::string& value) {
        appendJsonString(stream, value);
    });
    out << ",\"environment\":";
    appendJsonStringMap(&out, plan.environment);
    out << ",\"dllOverrides\":";
    appendJsonStringMap(&out, plan.dllOverrides);
    out << '}';

    return out.str();
}

bool writeRuntimeLaunchPlanReport(const RuntimeLaunchPlan& plan,
                                  const std::filesystem::path& path,
                                  std::string* errorMessage) {
    return writeTextFile(path, describeRuntimeLaunchPlan(plan), errorMessage);
}

bool writeRuntimeLaunchPlanJson(const RuntimeLaunchPlan& plan,
                                const std::filesystem::path& path,
                                std::string* errorMessage) {
    return writeTextFile(path, runtimeLaunchPlanToJson(plan), errorMessage);
}

}  // namespace mvrvb
