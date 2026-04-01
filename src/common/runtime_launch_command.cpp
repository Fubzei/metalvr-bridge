#include "runtime_launch_command.h"

#include <cctype>
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

std::string joinFallbackBackends(const std::vector<RendererBackend>& values) {
    std::ostringstream out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i != 0) out << ',';
        out << rendererBackendName(values[i]);
    }
    return out.str();
}

std::string joinDllOverrides(const std::map<std::string, std::string>& values) {
    std::ostringstream out;
    size_t i = 0;
    for (const auto& [key, value] : values) {
        if (i++ != 0) out << ';';
        out << key << '=' << value;
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

bool looksLikeWindowsAbsolutePath(std::string_view value) {
    if (value.size() >= 3 &&
        std::isalpha(static_cast<unsigned char>(value[0])) &&
        value[1] == ':' &&
        (value[2] == '\\' || value[2] == '/')) {
        return true;
    }

    return value.size() >= 2 && value[0] == '\\' && value[1] == '\\';
}

std::string defaultWorkingDirectoryForExecutable(std::string_view executablePath) {
    if (executablePath.empty()) return ".";
    if (looksLikeWindowsAbsolutePath(executablePath)) {
        return ".";
    }
    const auto parent = std::filesystem::path(std::string(executablePath)).parent_path();
    if (parent.empty()) return ".";
    return parent.string();
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
                    "Failed to create parent directory for runtime launch script: " +
                    ec.message();
            }
            return false;
        }
    }

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open()) {
        if (errorMessage) {
            *errorMessage = "Failed to open runtime launch script output file: " + path.string();
        }
        return false;
    }

    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!stream.good()) {
        if (errorMessage) {
            *errorMessage =
                "Failed while writing runtime launch script output file: " + path.string();
        }
        return false;
    }

    return true;
}

}  // namespace

RuntimeLaunchCommandResult materializeRuntimeLaunchCommand(
    const RuntimeLaunchPlan& plan,
    const RuntimeLaunchRequest& request) {
    RuntimeLaunchCommandResult result;
    if (request.executablePath.empty()) {
        result.errorMessage = "Runtime launch request is missing executablePath";
        return result;
    }
    if (request.wineBinary.empty()) {
        result.errorMessage = "Runtime launch request is missing wineBinary";
        return result;
    }

    const RuntimeLaunchPlan resolvedPlan = resolveRuntimeLaunchPlanPrefix(
        plan,
        request.prefixPath,
        request.managedPrefixRoot);
    if (resolvedPlan.resolvedPrefixPath.empty()) {
        result.errorMessage = "Runtime launch plan did not resolve a usable prefix path";
        return result;
    }

    result.command.program = request.wineBinary;
    result.command.arguments.push_back(request.executablePath);
    for (const auto& argument : resolvedPlan.launchArgs) {
        result.command.arguments.push_back(argument);
    }
    result.command.workingDirectory = request.workingDirectory.empty()
        ? defaultWorkingDirectoryForExecutable(request.executablePath)
        : request.workingDirectory;
    result.command.environment = resolvedPlan.environment;

    result.command.environment["WINEPREFIX"] = resolvedPlan.resolvedPrefixPath;
    result.command.environment["MVRVB_MANAGED_PREFIX_ROOT"] = resolvedPlan.managedPrefixRoot;
    result.command.environment["MVRVB_MANAGED_PREFIX_PATH"] = resolvedPlan.managedPrefixPath;
    result.command.environment["MVRVB_RESOLVED_PREFIX_SOURCE"] =
        resolvedPlan.resolvedPrefixSource;
    result.command.environment["MVRVB_RESOLVED_PREFIX_PATH"] =
        resolvedPlan.resolvedPrefixPath;
    if (!resolvedPlan.dllOverrides.empty()) {
        result.command.environment["WINEDLLOVERRIDES"] = joinDllOverrides(resolvedPlan.dllOverrides);
    }

    result.command.environment["MVRVB_SELECTED_PROFILE"] = resolvedPlan.selectedProfileId;
    result.command.environment["MVRVB_RENDERER_BACKEND"] = rendererBackendName(resolvedPlan.backend);
    result.command.environment["MVRVB_RENDERER_FALLBACKS"] =
        joinFallbackBackends(resolvedPlan.fallbackBackends);
    result.command.environment["MVRVB_WINDOWS_VERSION"] = resolvedPlan.windowsVersion;
    result.command.environment["MVRVB_WINE_MIN_VERSION"] = resolvedPlan.minimumWineVersion;
    result.command.environment["MVRVB_WINE_PREFERRED_VERSION"] = resolvedPlan.preferredWineVersion;
    result.command.environment["MVRVB_REQUIRES_WINE_MONO"] =
        boolEnvValue(resolvedPlan.requiresWineMono);
    result.command.environment["MVRVB_DX11_BACKEND"] = rendererBackendName(resolvedPlan.dx11Backend);
    result.command.environment["MVRVB_DX12_BACKEND"] = rendererBackendName(resolvedPlan.dx12Backend);
    result.command.environment["MVRVB_VULKAN_BACKEND"] = rendererBackendName(resolvedPlan.vulkanBackend);
    result.command.environment["MVRVB_SYNC_MODE"] = syncModeName(resolvedPlan.syncMode);
    result.command.environment["MVRVB_HIGH_RESOLUTION_MODE"] =
        boolEnvValue(resolvedPlan.highResolutionMode);
    result.command.environment["MVRVB_METALFX_UPSCALING"] =
        boolEnvValue(resolvedPlan.metalFxUpscaling);
    result.command.environment["MVRVB_ANTI_CHEAT_RISK"] =
        antiCheatRiskName(resolvedPlan.antiCheatRisk);
    result.command.environment["MVRVB_PREFIX_PRESET"] = resolvedPlan.install.prefixPreset;
    result.command.environment["MVRVB_INSTALL_PACKAGES"] =
        joinStrings(resolvedPlan.install.packages, ",");
    result.command.environment["MVRVB_INSTALL_WINETRICKS"] =
        joinStrings(resolvedPlan.install.winetricks, ",");
    result.command.environment["MVRVB_REQUIRES_LAUNCHER"] =
        boolEnvValue(resolvedPlan.install.requiresLauncher);

    switch (resolvedPlan.antiCheatRisk) {
        case AntiCheatRisk::Blocking:
            result.warnings.push_back(
                "Anti-cheat risk is blocking; this launch plan is not expected to be service-safe.");
            break;
        case AntiCheatRisk::High:
            result.warnings.push_back(
                "Anti-cheat risk is high; validate service compatibility before real gameplay.");
            break;
        default:
            break;
    }
    if (resolvedPlan.backend == RendererBackend::Auto) {
        result.warnings.push_back(
            "Renderer backend is set to auto; runtime wrapper should be ready to apply fallback logic.");
    }

    return result;
}

std::string summarizeRuntimeLaunchCommand(const RuntimeLaunchCommand& command) {
    std::ostringstream out;
    out << "Program: " << command.program << "\n";
    out << "Working directory: " << command.workingDirectory << "\n";
    out << "Arguments: " << joinStrings(command.arguments, ", ") << "\n";
    out << "Environment entries: " << command.environment.size();
    return out.str();
}

std::string renderRuntimeLaunchCommandBash(const RuntimeLaunchCommand& command) {
    std::ostringstream out;
    out << "#!/usr/bin/env bash\n";
    out << "set -euo pipefail\n";
    for (const auto& [key, value] : command.environment) {
        out << "export " << key << "=" << shellEscapeBash(value) << "\n";
    }
    out << "cd " << shellEscapeBash(command.workingDirectory) << "\n";
    out << "exec " << shellEscapeBash(command.program);
    for (const auto& arg : command.arguments) {
        out << " " << shellEscapeBash(arg);
    }
    out << "\n";
    return out.str();
}

std::string renderRuntimeLaunchCommandPowerShell(const RuntimeLaunchCommand& command) {
    std::ostringstream out;
    out << "$ErrorActionPreference = 'Stop'\n";
    for (const auto& [key, value] : command.environment) {
        out << "$env:" << key << " = " << shellEscapePowerShell(value) << "\n";
    }
    out << "Set-Location -LiteralPath " << shellEscapePowerShell(command.workingDirectory) << "\n";
    out << "& " << shellEscapePowerShell(command.program);
    for (const auto& arg : command.arguments) {
        out << " " << shellEscapePowerShell(arg);
    }
    out << "\n";
    return out.str();
}

bool writeRuntimeLaunchCommandBash(const RuntimeLaunchCommand& command,
                                   const std::filesystem::path& path,
                                   std::string* errorMessage) {
    return writeTextFile(path, renderRuntimeLaunchCommandBash(command), errorMessage);
}

bool writeRuntimeLaunchCommandPowerShell(const RuntimeLaunchCommand& command,
                                         const std::filesystem::path& path,
                                         std::string* errorMessage) {
    return writeTextFile(path, renderRuntimeLaunchCommandPowerShell(command), errorMessage);
}

}  // namespace mvrvb
