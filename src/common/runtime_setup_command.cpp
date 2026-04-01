#include "runtime_setup_command.h"

#include <fstream>
#include <sstream>
#include <string_view>

namespace mvrvb {
namespace {

std::string joinStrings(const std::vector<std::string>& values, std::string_view separator) {
    std::ostringstream out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i != 0) out << separator;
        out << values[i];
    }
    return out.str();
}

std::string shellEscapeBash(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 8);
    out.push_back('\'');
    for (const char ch : value) {
        if (ch == '\'') {
            out += "'\\''";
        } else {
            out.push_back(ch);
        }
    }
    out.push_back('\'');
    return out;
}

std::string shellEscapePowerShell(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 8);
    out.push_back('\'');
    for (const char ch : value) {
        if (ch == '\'') {
            out += "''";
        } else {
            out.push_back(ch);
        }
    }
    out.push_back('\'');
    return out;
}

std::string boolEnvValue(bool value) {
    return value ? "1" : "0";
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
                    "Failed to create parent directory for runtime setup script: " +
                    ec.message();
            }
            return false;
        }
    }

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open()) {
        if (errorMessage) {
            *errorMessage = "Failed to open runtime setup script output file: " + path.string();
        }
        return false;
    }

    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!stream.good()) {
        if (errorMessage) {
            *errorMessage =
                "Failed while writing runtime setup script output file: " + path.string();
        }
        return false;
    }

    return true;
}

std::string defaultWorkingDirectory(const RuntimeSetupRequest& request) {
    if (!request.workingDirectory.empty()) return request.workingDirectory;
    return ".";
}

}  // namespace

RuntimeSetupCommandResult buildRuntimeSetupCommandPlan(
    const RuntimeLaunchPlan& plan,
    const RuntimeSetupRequest& request) {
    RuntimeSetupCommandResult result;
    if (request.prefixPath.empty()) {
        result.errorMessage = "Runtime setup request is missing prefixPath";
        return result;
    }
    if (request.winebootBinary.empty()) {
        result.errorMessage = "Runtime setup request is missing winebootBinary";
        return result;
    }
    if (!plan.install.winetricks.empty() && request.winetricksBinary.empty()) {
        result.errorMessage =
            "Runtime setup request is missing winetricksBinary for requested install verbs";
        return result;
    }

    result.plan.workingDirectory = defaultWorkingDirectory(request);
    result.plan.environment = plan.environment;
    result.plan.environment["WINEPREFIX"] = request.prefixPath;
    result.plan.environment["MVRVB_SELECTED_PROFILE"] = plan.selectedProfileId;
    result.plan.environment["MVRVB_PREFIX_PRESET"] = plan.install.prefixPreset;
    result.plan.environment["MVRVB_INSTALL_PACKAGES"] = joinStrings(plan.install.packages, ",");
    result.plan.environment["MVRVB_INSTALL_WINETRICKS"] =
        joinStrings(plan.install.winetricks, ",");
    result.plan.environment["MVRVB_REQUIRES_LAUNCHER"] =
        boolEnvValue(plan.install.requiresLauncher);
    result.plan.environment["MVRVB_WINE_MIN_VERSION"] = plan.minimumWineVersion;
    result.plan.environment["MVRVB_WINE_PREFERRED_VERSION"] = plan.preferredWineVersion;
    result.plan.environment["MVRVB_REQUIRES_WINE_MONO"] =
        boolEnvValue(plan.requiresWineMono);
    result.plan.environment["MVRVB_DX11_BACKEND"] = rendererBackendName(plan.dx11Backend);
    result.plan.environment["MVRVB_DX12_BACKEND"] = rendererBackendName(plan.dx12Backend);
    result.plan.environment["MVRVB_VULKAN_BACKEND"] = rendererBackendName(plan.vulkanBackend);

    result.plan.actions.push_back(RuntimeSetupAction{
        .description = "Initialize or update the target Wine prefix",
        .program = request.winebootBinary,
        .arguments = {"-u"},
    });

    if (!plan.install.winetricks.empty()) {
        result.plan.actions.push_back(RuntimeSetupAction{
            .description = "Apply requested Winetricks verbs",
            .program = request.winetricksBinary,
            .arguments = plan.install.winetricks,
        });
    }

    for (const auto& package : plan.install.packages) {
        result.plan.manualActions.push_back("Install package or component: " + package);
    }
    if (plan.install.requiresLauncher) {
        result.plan.manualActions.push_back(
            "Bootstrap the required launcher inside the prefix before game launch attempts.");
    }
    if (!plan.minimumWineVersion.empty()) {
        result.plan.manualActions.push_back(
            "Use Wine " + plan.minimumWineVersion +
            " or newer for this runtime plan.");
    }
    if (!plan.preferredWineVersion.empty()) {
        result.plan.manualActions.push_back(
            "Prefer Wine " + plan.preferredWineVersion +
            " when preparing this prefix.");
    }
    if (plan.requiresWineMono) {
        result.plan.manualActions.push_back(
            "Install Wine Mono in the target prefix before launching .NET-dependent content.");
    }
    if (!plan.install.notes.empty()) {
        result.plan.manualActions.push_back("Install notes: " + plan.install.notes);
    }
    if (plan.antiCheatRisk == AntiCheatRisk::Blocking) {
        result.warnings.push_back(
            "Anti-cheat risk is blocking; treat this setup plan as technical experimentation only.");
    }

    return result;
}

std::string summarizeRuntimeSetupCommandPlan(const RuntimeSetupCommandPlan& plan) {
    std::ostringstream out;
    out << "Working directory: " << plan.workingDirectory << "\n";
    out << "Environment entries: " << plan.environment.size() << "\n";
    out << "Automated setup actions: " << plan.actions.size() << "\n";
    out << "Manual setup actions: " << plan.manualActions.size();
    return out.str();
}

std::string renderRuntimeSetupCommandBash(const RuntimeSetupCommandPlan& plan) {
    std::ostringstream out;
    out << "#!/usr/bin/env bash\n";
    out << "set -euo pipefail\n";
    out << "\n";
    out << "# Generated setup script from MetalVR Bridge runtime policy.\n";
    for (const auto& [key, value] : plan.environment) {
        out << "export " << key << "=" << shellEscapeBash(value) << "\n";
    }
    out << "cd " << shellEscapeBash(plan.workingDirectory) << "\n";
    out << "\n";
    for (const auto& action : plan.actions) {
        out << "# " << action.description << "\n";
        out << shellEscapeBash(action.program);
        for (const auto& argument : action.arguments) {
            out << " " << shellEscapeBash(argument);
        }
        out << "\n\n";
    }
    if (!plan.manualActions.empty()) {
        out << "# Manual follow-up actions\n";
        for (const auto& manualAction : plan.manualActions) {
            out << "# - " << manualAction << "\n";
        }
    }
    return out.str();
}

std::string renderRuntimeSetupCommandPowerShell(const RuntimeSetupCommandPlan& plan) {
    std::ostringstream out;
    out << "$ErrorActionPreference = 'Stop'\n";
    out << "\n";
    out << "# Generated setup script from MetalVR Bridge runtime policy.\n";
    for (const auto& [key, value] : plan.environment) {
        out << "$env:" << key << " = " << shellEscapePowerShell(value) << "\n";
    }
    out << "Set-Location -LiteralPath " << shellEscapePowerShell(plan.workingDirectory) << "\n";
    out << "\n";
    for (const auto& action : plan.actions) {
        out << "# " << action.description << "\n";
        out << "& " << shellEscapePowerShell(action.program);
        for (const auto& argument : action.arguments) {
            out << " " << shellEscapePowerShell(argument);
        }
        out << "\n\n";
    }
    if (!plan.manualActions.empty()) {
        out << "# Manual follow-up actions\n";
        for (const auto& manualAction : plan.manualActions) {
            out << "# - " << manualAction << "\n";
        }
    }
    return out.str();
}

bool writeRuntimeSetupCommandBash(const RuntimeSetupCommandPlan& plan,
                                  const std::filesystem::path& path,
                                  std::string* errorMessage) {
    return writeTextFile(path, renderRuntimeSetupCommandBash(plan), errorMessage);
}

bool writeRuntimeSetupCommandPowerShell(const RuntimeSetupCommandPlan& plan,
                                        const std::filesystem::path& path,
                                        std::string* errorMessage) {
    return writeTextFile(path, renderRuntimeSetupCommandPowerShell(plan), errorMessage);
}

}  // namespace mvrvb
