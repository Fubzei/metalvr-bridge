#include "runtime_launch_command.h"
#include "runtime_launch_plan.h"
#include "runtime_setup_command.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void printUsage(std::ostream& out) {
    out
        << "Usage: mvrvb_runtime_plan_preview (--exe <path> | --input <path>) [options]\n"
        << "Options:\n"
        << "  --input <path>         Previously exported runtime launch-plan JSON\n"
        << "  --exe <path>           Windows game executable path or name\n"
        << "  --launcher <name>      Launcher name, for example Steam or Battle.net\n"
        << "  --store <name>         Store identifier, for example steam or battlenet\n"
        << "  --profiles-dir <path>  Profile directory (defaults to the checked-in profiles tree)\n"
        << "  --wine-binary <path>   Wine executable to use for script materialization (default: wine)\n"
        << "  --wineboot-binary <path> Wineboot executable to use for setup script materialization (default: wineboot)\n"
        << "  --winetricks-binary <path> Winetricks executable to use for setup script materialization (default: winetricks)\n"
        << "  --prefix <path>        Prefix path for generated launch scripts\n"
        << "  --working-dir <path>   Working directory for generated launch scripts\n"
        << "  --out <path>           Write the resolved launch plan to a file instead of stdout\n"
        << "  --json                 Print the resolved launch plan as JSON\n"
        << "  --checklist            Print the resolved launch plan as a Markdown setup checklist\n"
        << "  --setup-bash           Print a bash setup/bootstrap script\n"
        << "  --setup-powershell     Print a PowerShell setup/bootstrap script\n"
        << "  --bash                 Print a bash launch script instead of a plan summary\n"
        << "  --powershell           Print a PowerShell launch script instead of a plan summary\n"
        << "  --help                 Show this message\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::filesystem::path profilesDir = MVRVB_TOOLS_PROFILE_DIR;
    std::filesystem::path inputPath;
    mvrvb::CompatibilityProfileQuery query;
    mvrvb::RuntimeLaunchRequest launchRequest;
    mvrvb::RuntimeSetupRequest setupRequest;
    std::filesystem::path outputPath;
    enum class OutputMode {
        Report,
        Json,
        Checklist,
        SetupBash,
        SetupPowerShell,
        Bash,
        PowerShell,
    };
    OutputMode outputMode = OutputMode::Report;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];

        auto requireValue = [&](const char* option) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << option << "\n";
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "--help" || arg == "-h") {
            printUsage(std::cout);
            return 0;
        }
        if (arg == "--json") {
            outputMode = OutputMode::Json;
            continue;
        }
        if (arg == "--checklist") {
            outputMode = OutputMode::Checklist;
            continue;
        }
        if (arg == "--setup-bash") {
            outputMode = OutputMode::SetupBash;
            continue;
        }
        if (arg == "--setup-powershell") {
            outputMode = OutputMode::SetupPowerShell;
            continue;
        }
        if (arg == "--bash") {
            outputMode = OutputMode::Bash;
            continue;
        }
        if (arg == "--powershell") {
            outputMode = OutputMode::PowerShell;
            continue;
        }
        if (arg == "--input") {
            if (const char* value = requireValue("--input")) {
                inputPath = value;
                continue;
            }
            return 1;
        }
        if (arg == "--out") {
            if (const char* value = requireValue("--out")) {
                outputPath = value;
                continue;
            }
            return 1;
        }
        if (arg == "--wine-binary") {
            if (const char* value = requireValue("--wine-binary")) {
                launchRequest.wineBinary = value;
                continue;
            }
            return 1;
        }
        if (arg == "--wineboot-binary") {
            if (const char* value = requireValue("--wineboot-binary")) {
                setupRequest.winebootBinary = value;
                continue;
            }
            return 1;
        }
        if (arg == "--winetricks-binary") {
            if (const char* value = requireValue("--winetricks-binary")) {
                setupRequest.winetricksBinary = value;
                continue;
            }
            return 1;
        }
        if (arg == "--prefix") {
            if (const char* value = requireValue("--prefix")) {
                launchRequest.prefixPath = value;
                setupRequest.prefixPath = value;
                continue;
            }
            return 1;
        }
        if (arg == "--working-dir") {
            if (const char* value = requireValue("--working-dir")) {
                launchRequest.workingDirectory = value;
                setupRequest.workingDirectory = value;
                continue;
            }
            return 1;
        }
        if (arg == "--exe") {
            if (const char* value = requireValue("--exe")) {
                query.executable = value;
                launchRequest.executablePath = value;
                continue;
            }
            return 1;
        }
        if (arg == "--launcher") {
            if (const char* value = requireValue("--launcher")) {
                query.launcher = value;
                continue;
            }
            return 1;
        }
        if (arg == "--store") {
            if (const char* value = requireValue("--store")) {
                query.store = value;
                continue;
            }
            return 1;
        }
        if (arg == "--profiles-dir") {
            if (const char* value = requireValue("--profiles-dir")) {
                profilesDir = value;
                continue;
            }
            return 1;
        }

        std::cerr << "Unknown option: " << arg << "\n";
        printUsage(std::cerr);
        return 1;
    }

    mvrvb::RuntimeLaunchPlanResult result;
    if (!inputPath.empty()) {
        result = mvrvb::loadRuntimeLaunchPlanJson(inputPath);
    } else if (!query.executable.empty()) {
        result = mvrvb::buildRuntimeLaunchPlanFromDirectory(profilesDir, query);
    } else {
        std::cerr << "Either --exe or --input is required\n";
        printUsage(std::cerr);
        return 1;
    }

    if (!result) {
        std::cerr << "Failed to build runtime launch plan: " << result.errorMessage << "\n";
        return 1;
    }

    if ((outputMode == OutputMode::Bash || outputMode == OutputMode::PowerShell) &&
        launchRequest.executablePath.empty()) {
        std::cerr << "--exe is required when rendering launch scripts\n";
        return 1;
    }
    if ((outputMode == OutputMode::SetupBash || outputMode == OutputMode::SetupPowerShell) &&
        setupRequest.prefixPath.empty()) {
        std::cerr << "--prefix is required when rendering setup scripts\n";
        return 1;
    }

    mvrvb::RuntimeLaunchCommandResult launchCommand;
    if (outputMode == OutputMode::Bash || outputMode == OutputMode::PowerShell) {
        launchCommand = mvrvb::materializeRuntimeLaunchCommand(result.plan, launchRequest);
        if (!launchCommand) {
            std::cerr << "Failed to materialize runtime launch command: "
                      << launchCommand.errorMessage << "\n";
            return 1;
        }
    }
    mvrvb::RuntimeSetupCommandResult setupCommand;
    if (outputMode == OutputMode::SetupBash || outputMode == OutputMode::SetupPowerShell) {
        setupCommand = mvrvb::buildRuntimeSetupCommandPlan(result.plan, setupRequest);
        if (!setupCommand) {
            std::cerr << "Failed to materialize runtime setup command plan: "
                      << setupCommand.errorMessage << "\n";
            return 1;
        }
    }

    std::string errorMessage;
    if (!outputPath.empty()) {
        outputPath = std::filesystem::absolute(outputPath);
        bool ok = false;
        switch (outputMode) {
            case OutputMode::Json:
                ok = mvrvb::writeRuntimeLaunchPlanJson(result.plan, outputPath, &errorMessage);
                break;
            case OutputMode::Checklist:
                ok = mvrvb::writeRuntimeLaunchPlanMarkdownChecklist(
                    result.plan,
                    outputPath,
                    &errorMessage);
                break;
            case OutputMode::SetupBash:
                ok = mvrvb::writeRuntimeSetupCommandBash(
                    setupCommand.plan,
                    outputPath,
                    &errorMessage);
                break;
            case OutputMode::SetupPowerShell:
                ok = mvrvb::writeRuntimeSetupCommandPowerShell(
                    setupCommand.plan,
                    outputPath,
                    &errorMessage);
                break;
            case OutputMode::Bash:
                ok = mvrvb::writeRuntimeLaunchCommandBash(
                    launchCommand.command,
                    outputPath,
                    &errorMessage);
                break;
            case OutputMode::PowerShell:
                ok = mvrvb::writeRuntimeLaunchCommandPowerShell(
                    launchCommand.command,
                    outputPath,
                    &errorMessage);
                break;
            case OutputMode::Report:
                ok = mvrvb::writeRuntimeLaunchPlanReport(result.plan, outputPath, &errorMessage);
                break;
        }
        if (!ok) {
            std::cerr << "Failed to write runtime launch plan: " << errorMessage << "\n";
            return 1;
        }
        std::cout << outputPath.string() << "\n";
        return 0;
    }

    switch (outputMode) {
        case OutputMode::Json:
            std::cout << mvrvb::runtimeLaunchPlanToJson(result.plan) << "\n";
            break;
        case OutputMode::Checklist:
            std::cout << mvrvb::runtimeLaunchPlanToMarkdownChecklist(result.plan);
            break;
        case OutputMode::SetupBash:
            std::cout << mvrvb::renderRuntimeSetupCommandBash(setupCommand.plan);
            break;
        case OutputMode::SetupPowerShell:
            std::cout << mvrvb::renderRuntimeSetupCommandPowerShell(setupCommand.plan);
            break;
        case OutputMode::Bash:
            std::cout << mvrvb::renderRuntimeLaunchCommandBash(launchCommand.command);
            break;
        case OutputMode::PowerShell:
            std::cout << mvrvb::renderRuntimeLaunchCommandPowerShell(launchCommand.command);
            break;
        case OutputMode::Report:
            std::cout << mvrvb::describeRuntimeLaunchPlan(result.plan);
            break;
    }
    return 0;
}
