#include "runtime_launch_plan.h"

#include <filesystem>
#include <fstream>
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
        << "  --out <path>           Write the resolved launch plan to a file instead of stdout\n"
        << "  --json                 Print the resolved launch plan as JSON\n"
        << "  --help                 Show this message\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::filesystem::path profilesDir = MVRVB_TOOLS_PROFILE_DIR;
    std::filesystem::path inputPath;
    mvrvb::CompatibilityProfileQuery query;
    std::filesystem::path outputPath;
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

    std::string errorMessage;
    if (!outputPath.empty()) {
        outputPath = std::filesystem::absolute(outputPath);
        const bool ok = outputJson
            ? mvrvb::writeRuntimeLaunchPlanJson(result.plan, outputPath, &errorMessage)
            : mvrvb::writeRuntimeLaunchPlanReport(result.plan, outputPath, &errorMessage);
        if (!ok) {
            std::cerr << "Failed to write runtime launch plan: " << errorMessage << "\n";
            return 1;
        }
        std::cout << outputPath.string() << "\n";
        return 0;
    }

    if (outputJson) {
        std::cout << mvrvb::runtimeLaunchPlanToJson(result.plan) << "\n";
        return 0;
    }

    std::cout << mvrvb::describeRuntimeLaunchPlan(result.plan);
    return 0;
}
