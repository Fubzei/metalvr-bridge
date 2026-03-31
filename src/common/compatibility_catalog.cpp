#include "compatibility_catalog.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string_view>

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

void appendJsonIntMap(std::ostringstream* out, const std::map<std::string, int>& values) {
    if (!out) return;
    *out << '{';
    size_t i = 0;
    for (const auto& [key, value] : values) {
        if (i++ != 0) *out << ',';
        appendJsonString(out, key);
        *out << ':' << value;
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
                *errorMessage =
                    "Failed to create parent directory for compatibility catalog: " +
                    ec.message();
            }
            return false;
        }
    }

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open()) {
        if (errorMessage) {
            *errorMessage = "Failed to open compatibility catalog output file: " + path.string();
        }
        return false;
    }

    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!stream.good()) {
        if (errorMessage) {
            *errorMessage =
                "Failed while writing compatibility catalog output file: " + path.string();
        }
        return false;
    }

    return true;
}

CompatibilityCatalogEntry makeEntry(const CompatibilityProfile& profile) {
    CompatibilityCatalogEntry entry;
    entry.profileId = profile.profileId;
    entry.displayName = profile.displayName;
    entry.status = profile.status;
    entry.allowAutoMatch = profile.allowAutoMatch;
    entry.category = profile.category;
    entry.notes = profile.notes;
    entry.defaultRenderer = profile.defaultRenderer;
    entry.fallbackRenderers = profile.fallbackRenderers;
    entry.latencySensitive = profile.latencySensitive;
    entry.competitive = profile.competitive;
    entry.antiCheatRisk = profile.antiCheatRisk;
    entry.runtime = profile.runtime;
    entry.match = profile.match;
    entry.launchArgs = profile.launchArgs;
    entry.environmentCount = profile.environment.size();
    entry.dllOverrideCount = profile.dllOverrides.size();
    return entry;
}

}  // namespace

CompatibilityCatalogResult buildCompatibilityCatalog(
    const std::vector<CompatibilityProfile>& profiles) {
    CompatibilityCatalogResult result;
    if (profiles.empty()) {
        result.errorMessage = "No compatibility profiles are available";
        return result;
    }

    result.catalog.entries.reserve(profiles.size());
    for (const auto& profile : profiles) {
        result.catalog.entries.push_back(makeEntry(profile));
        ++result.catalog.summary.totalProfiles;
        if (profile.allowAutoMatch) {
            ++result.catalog.summary.autoMatchProfiles;
        } else {
            ++result.catalog.summary.templateProfiles;
        }
        if (profile.competitive) {
            ++result.catalog.summary.competitiveProfiles;
        }
        if (profile.latencySensitive) {
            ++result.catalog.summary.latencySensitiveProfiles;
        }
        ++result.catalog.summary.statusCounts[profileStatusName(profile.status)];
        ++result.catalog.summary.categoryCounts[profile.category.empty() ? "uncategorized"
                                                                        : profile.category];
        ++result.catalog.summary.backendCounts[rendererBackendName(profile.defaultRenderer)];
    }

    std::sort(result.catalog.entries.begin(),
              result.catalog.entries.end(),
              [](const CompatibilityCatalogEntry& lhs, const CompatibilityCatalogEntry& rhs) {
                  if (lhs.category != rhs.category) return lhs.category < rhs.category;
                  return lhs.profileId < rhs.profileId;
              });

    return result;
}

CompatibilityCatalogResult buildCompatibilityCatalogFromDirectory(
    const std::filesystem::path& root) {
    const auto batch = loadCompatibilityProfilesFromDirectory(root);
    CompatibilityCatalogResult result;
    if (!batch) {
        std::ostringstream errors;
        for (size_t i = 0; i < batch.errorMessages.size(); ++i) {
            if (i != 0) errors << "; ";
            errors << batch.errorMessages[i];
        }
        result.errorMessage = errors.str();
        return result;
    }
    return buildCompatibilityCatalog(batch.profiles);
}

std::string summarizeCompatibilityCatalog(const CompatibilityCatalog& catalog) {
    std::ostringstream out;
    out << "Total profiles: " << catalog.summary.totalProfiles << "\n";
    out << "Auto-match profiles: " << catalog.summary.autoMatchProfiles << "\n";
    out << "Template/manual profiles: " << catalog.summary.templateProfiles << "\n";
    out << "Competitive profiles: " << catalog.summary.competitiveProfiles << "\n";
    out << "Latency-sensitive profiles: " << catalog.summary.latencySensitiveProfiles;
    return out.str();
}

std::string describeCompatibilityCatalog(const CompatibilityCatalog& catalog) {
    std::ostringstream out;
    out << summarizeCompatibilityCatalog(catalog) << "\n";
    out << "Status counts:\n";
    for (const auto& [name, count] : catalog.summary.statusCounts) {
        out << "  " << name << ": " << count << "\n";
    }
    out << "Category counts:\n";
    for (const auto& [name, count] : catalog.summary.categoryCounts) {
        out << "  " << name << ": " << count << "\n";
    }
    out << "Default backend counts:\n";
    for (const auto& [name, count] : catalog.summary.backendCounts) {
        out << "  " << name << ": " << count << "\n";
    }
    out << "Profiles:\n";
    for (const auto& entry : catalog.entries) {
        out << "- " << entry.profileId << " (" << entry.displayName << ")\n";
        out << "  status: " << profileStatusName(entry.status) << "\n";
        out << "  category: " << entry.category << "\n";
        out << "  auto match: " << (entry.allowAutoMatch ? "true" : "false") << "\n";
        out << "  renderer: " << rendererBackendName(entry.defaultRenderer) << "\n";
        out << "  fallbacks: ";
        for (size_t i = 0; i < entry.fallbackRenderers.size(); ++i) {
            if (i != 0) out << ", ";
            out << rendererBackendName(entry.fallbackRenderers[i]);
        }
        if (entry.fallbackRenderers.empty()) {
            out << "(none)";
        }
        out << "\n";
        out << "  launchers: ";
        for (size_t i = 0; i < entry.match.launchers.size(); ++i) {
            if (i != 0) out << ", ";
            out << entry.match.launchers[i];
        }
        if (entry.match.launchers.empty()) {
            out << "(none)";
        }
        out << "\n";
        out << "  stores: ";
        for (size_t i = 0; i < entry.match.stores.size(); ++i) {
            if (i != 0) out << ", ";
            out << entry.match.stores[i];
        }
        if (entry.match.stores.empty()) {
            out << "(none)";
        }
        out << "\n";
        out << "  executables: ";
        for (size_t i = 0; i < entry.match.executables.size(); ++i) {
            if (i != 0) out << ", ";
            out << entry.match.executables[i];
        }
        if (entry.match.executables.empty()) {
            out << "(none)";
        }
        out << "\n";
        out << "  runtime: windows=" << entry.runtime.windowsVersion
            << ", sync=" << syncModeName(entry.runtime.syncMode)
            << ", high-res=" << (entry.runtime.highResolutionMode ? "true" : "false")
            << ", MetalFX=" << (entry.runtime.metalFxUpscaling ? "true" : "false") << "\n";
        out << "  flags: competitive=" << (entry.competitive ? "true" : "false")
            << ", latency-sensitive=" << (entry.latencySensitive ? "true" : "false")
            << ", anti-cheat-risk=" << antiCheatRiskName(entry.antiCheatRisk) << "\n";
        out << "  counts: env=" << entry.environmentCount
            << ", dll-overrides=" << entry.dllOverrideCount
            << ", launch-args=" << entry.launchArgs.size() << "\n";
    }
    return out.str();
}

std::string compatibilityCatalogToJson(const CompatibilityCatalog& catalog) {
    std::ostringstream out;
    out << '{';
    out << "\"schemaVersion\":";
    appendJsonString(&out, kCompatibilityCatalogSchemaVersion);
    out << ",\"summary\":{";
    out << "\"totalProfiles\":" << catalog.summary.totalProfiles;
    out << ",\"autoMatchProfiles\":" << catalog.summary.autoMatchProfiles;
    out << ",\"templateProfiles\":" << catalog.summary.templateProfiles;
    out << ",\"competitiveProfiles\":" << catalog.summary.competitiveProfiles;
    out << ",\"latencySensitiveProfiles\":" << catalog.summary.latencySensitiveProfiles;
    out << ",\"statusCounts\":";
    appendJsonIntMap(&out, catalog.summary.statusCounts);
    out << ",\"categoryCounts\":";
    appendJsonIntMap(&out, catalog.summary.categoryCounts);
    out << ",\"backendCounts\":";
    appendJsonIntMap(&out, catalog.summary.backendCounts);
    out << '}';
    out << ",\"entries\":";
    appendJsonArray(
        &out,
        catalog.entries,
        [](std::ostringstream* stream, const CompatibilityCatalogEntry& entry) {
            *stream << '{';
            *stream << "\"profileId\":";
            appendJsonString(stream, entry.profileId);
            *stream << ",\"displayName\":";
            appendJsonString(stream, entry.displayName);
            *stream << ",\"status\":";
            appendJsonString(stream, profileStatusName(entry.status));
            *stream << ",\"allowAutoMatch\":" << (entry.allowAutoMatch ? "true" : "false");
            *stream << ",\"category\":";
            appendJsonString(stream, entry.category);
            *stream << ",\"notes\":";
            appendJsonString(stream, entry.notes);
            *stream << ",\"defaultRenderer\":";
            appendJsonString(stream, rendererBackendName(entry.defaultRenderer));
            *stream << ",\"fallbackRenderers\":";
            appendJsonArray(
                stream,
                entry.fallbackRenderers,
                [](std::ostringstream* out, RendererBackend backend) {
                    appendJsonString(out, rendererBackendName(backend));
                });
            *stream << ",\"latencySensitive\":" << (entry.latencySensitive ? "true" : "false");
            *stream << ",\"competitive\":" << (entry.competitive ? "true" : "false");
            *stream << ",\"antiCheatRisk\":";
            appendJsonString(stream, antiCheatRiskName(entry.antiCheatRisk));
            *stream << ",\"runtime\":{";
            *stream << "\"windowsVersion\":";
            appendJsonString(stream, entry.runtime.windowsVersion);
            *stream << ",\"syncMode\":";
            appendJsonString(stream, syncModeName(entry.runtime.syncMode));
            *stream << ",\"highResolutionMode\":"
                    << (entry.runtime.highResolutionMode ? "true" : "false");
            *stream << ",\"metalFxUpscaling\":"
                    << (entry.runtime.metalFxUpscaling ? "true" : "false");
            *stream << '}';
            *stream << ",\"match\":{";
            *stream << "\"executables\":";
            appendJsonArray(
                stream,
                entry.match.executables,
                [](std::ostringstream* out, const std::string& value) {
                    appendJsonString(out, value);
                });
            *stream << ",\"launchers\":";
            appendJsonArray(
                stream,
                entry.match.launchers,
                [](std::ostringstream* out, const std::string& value) {
                    appendJsonString(out, value);
                });
            *stream << ",\"stores\":";
            appendJsonArray(
                stream,
                entry.match.stores,
                [](std::ostringstream* out, const std::string& value) {
                    appendJsonString(out, value);
                });
            *stream << '}';
            *stream << ",\"launchArgs\":";
            appendJsonArray(
                stream,
                entry.launchArgs,
                [](std::ostringstream* out, const std::string& value) {
                    appendJsonString(out, value);
                });
            *stream << ",\"environmentCount\":" << entry.environmentCount;
            *stream << ",\"dllOverrideCount\":" << entry.dllOverrideCount;
            *stream << '}';
        });
    out << '}';
    return out.str();
}

std::string compatibilityCatalogToMarkdown(const CompatibilityCatalog& catalog) {
    std::ostringstream out;
    out << "# Compatibility Catalog\n\n";
    out << "Generated from checked-in `.mvrvb-profile` files.\n\n";
    out << "## Summary\n\n";
    out << "- Total profiles: " << catalog.summary.totalProfiles << "\n";
    out << "- Auto-match profiles: " << catalog.summary.autoMatchProfiles << "\n";
    out << "- Template/manual profiles: " << catalog.summary.templateProfiles << "\n";
    out << "- Competitive profiles: " << catalog.summary.competitiveProfiles << "\n";
    out << "- Latency-sensitive profiles: " << catalog.summary.latencySensitiveProfiles << "\n\n";

    out << "## Matrix\n\n";
    out << "| Profile | Status | Category | Auto Match | Renderer | Stores/Launchers | Anti-Cheat | Notes |\n";
    out << "|---|---|---|---|---|---|---|---|\n";
    for (const auto& entry : catalog.entries) {
        std::ostringstream identity;
        bool first = true;
        for (const auto& store : entry.match.stores) {
            if (!first) identity << ", ";
            identity << store;
            first = false;
        }
        for (const auto& launcher : entry.match.launchers) {
            if (!first) identity << ", ";
            identity << launcher;
            first = false;
        }
        if (first) {
            identity << "(none)";
        }

        out << "| " << entry.displayName << " (`" << entry.profileId << "`)"
            << " | " << profileStatusName(entry.status)
            << " | " << (entry.category.empty() ? "uncategorized" : entry.category)
            << " | " << (entry.allowAutoMatch ? "yes" : "no")
            << " | " << rendererBackendName(entry.defaultRenderer)
            << " | " << identity.str()
            << " | " << antiCheatRiskName(entry.antiCheatRisk)
            << " | " << entry.notes << " |\n";
    }
    out << "\n## Runtime Highlights\n\n";
    for (const auto& entry : catalog.entries) {
        out << "### " << entry.displayName << "\n\n";
        out << "- Profile ID: `" << entry.profileId << "`\n";
        out << "- Sync mode: `" << syncModeName(entry.runtime.syncMode) << "`\n";
        out << "- High resolution mode: `" << (entry.runtime.highResolutionMode ? "true" : "false")
            << "`\n";
        out << "- MetalFX upscaling: `" << (entry.runtime.metalFxUpscaling ? "true" : "false")
            << "`\n";
        out << "- Launch args: ";
        if (entry.launchArgs.empty()) {
            out << "`(none)`\n";
        } else {
            for (size_t i = 0; i < entry.launchArgs.size(); ++i) {
                if (i != 0) out << ", ";
                out << "`" << entry.launchArgs[i] << "`";
            }
            out << "\n";
        }
        out << "- Environment entries: `" << entry.environmentCount << "`\n";
        out << "- DLL override entries: `" << entry.dllOverrideCount << "`\n\n";
    }
    return out.str();
}

bool writeCompatibilityCatalogReport(const CompatibilityCatalog& catalog,
                                     const std::filesystem::path& path,
                                     std::string* errorMessage) {
    return writeTextFile(path, describeCompatibilityCatalog(catalog), errorMessage);
}

bool writeCompatibilityCatalogJson(const CompatibilityCatalog& catalog,
                                   const std::filesystem::path& path,
                                   std::string* errorMessage) {
    return writeTextFile(path, compatibilityCatalogToJson(catalog), errorMessage);
}

bool writeCompatibilityCatalogMarkdown(const CompatibilityCatalog& catalog,
                                       const std::filesystem::path& path,
                                       std::string* errorMessage) {
    return writeTextFile(path, compatibilityCatalogToMarkdown(catalog), errorMessage);
}

}  // namespace mvrvb
