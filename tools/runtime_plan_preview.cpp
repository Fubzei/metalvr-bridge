#include "runtime_launch_plan.h"

#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace {

void printUsage(std::ostream& out) {
    out
        << "Usage: mvrvb_runtime_plan_preview --exe <path> [options]\n"
        << "Options:\n"
        << "  --exe <path>           Windows game executable path or name\n"
        << "  --launcher <name>      Launcher name, for example Steam or Battle.net\n"
        << "  --store <name>         Store identifier, for example steam or battlenet\n"
        << "  --profiles-dir <path>  Profile directory (defaults to the checked-in profiles tree)\n"
        << "  --json                 Print the resolved launch plan as JSON\n"
        << "  --help                 Show this message\n";
}

void printKeyValueSection(const char* title, const std::map<std::string, std::string>& values) {
    std::cout << title << ":\n";
    if (values.empty()) {
        std::cout << "  (none)\n";
        return;
    }

    for (const auto& [key, value] : values) {
        std::cout << "  " << key << "=" << value << "\n";
    }
}

void printListSection(const char* title, const std::vector<std::string>& values) {
    std::cout << title << ":\n";
    if (values.empty()) {
        std::cout << "  (none)\n";
        return;
    }

    for (const auto& value : values) {
        std::cout << "  " << value << "\n";
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::filesystem::path profilesDir = MVRVB_TOOLS_PROFILE_DIR;
    mvrvb::CompatibilityProfileQuery query;
    bool outputJson = false;

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
            outputJson = true;
            continue;
        }
        if (arg == "--exe") {
            if (const char* value = requireValue("--exe")) {
                query.executable = value;
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

    if (query.executable.empty()) {
        std::cerr << "--exe is required\n";
        printUsage(std::cerr);
        return 1;
    }

    const auto result = mvrvb::buildRuntimeLaunchPlanFromDirectory(profilesDir, query);
    if (!result) {
        std::cerr << "Failed to build runtime launch plan: " << result.errorMessage << "\n";
        return 1;
    }

    if (outputJson) {
        std::cout << mvrvb::runtimeLaunchPlanToJson(result.plan) << "\n";
        return 0;
    }

    std::cout << mvrvb::summarizeRuntimeLaunchPlan(result.plan) << "\n";
    printKeyValueSection("Environment", result.plan.environment);
    printKeyValueSection("DLL overrides", result.plan.dllOverrides);
    printListSection("Launch arguments", result.plan.launchArgs);
    return 0;
}
