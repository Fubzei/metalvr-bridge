#include "compatibility_catalog.h"
#include "profile_lint.h"
#include "runtime_launch_command.h"
#include "runtime_launch_plan.h"
#include "runtime_setup_command.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

namespace {

constexpr const char* kLaunchPlanJsonName = "launch-plan.json";
constexpr const char* kLaunchPlanReportName = "launch-plan.txt";
constexpr const char* kSetupChecklistName = "launch-plan.md";
constexpr const char* kBashSetupScriptName = "launch-plan.setup.sh";
constexpr const char* kPowerShellSetupScriptName = "launch-plan.setup.ps1";
constexpr const char* kBashLaunchScriptName = "launch-plan.sh";
constexpr const char* kPowerShellLaunchScriptName = "launch-plan.ps1";
constexpr const char* kCatalogJsonName = "compatibility-catalog.json";
constexpr const char* kCatalogReportName = "compatibility-catalog.txt";
constexpr const char* kCatalogMarkdownName = "compatibility-catalog.md";
constexpr const char* kLintReportName = "profile-lint.txt";
constexpr const char* kManifestName = "bundle-manifest.json";

void printUsage(std::ostream& out) {
    out
        << "Usage: mvrvb_runtime_bundle_builder --exe <path> [options]\n"
        << "Options:\n"
        << "  --exe <path>             Windows game executable path or name\n"
        << "  --launcher <name>        Launcher name, for example Steam or Battle.net\n"
        << "  --store <name>           Store identifier, for example steam or battlenet\n"
        << "  --profiles-dir <path>    Profile directory (defaults to the checked-in profiles tree)\n"
        << "  --out-dir <path>         Bundle output directory (defaults to <exe-stem>-bundle)\n"
        << "  --managed-prefix-root <path> Managed prefix root override for generated bundles\n"
        << "  --prefix <path>          Prefix path to embed in generated setup/launch scripts\n"
        << "  --working-dir <path>     Working directory to embed in generated scripts\n"
        << "  --wine-binary <path>     Wine executable to use for launch scripts (default: wine)\n"
        << "  --wineboot-binary <path> Wineboot executable to use for setup scripts (default: wineboot)\n"
        << "  --winetricks-binary <path> Winetricks executable to use for setup scripts (default: winetricks)\n"
        << "  --help                   Show this message\n";
}

std::string executableStem(std::string_view executable) {
    const auto stem = std::filesystem::path(executable).stem().string();
    if (!stem.empty()) {
        return stem;
    }
    return "runtime-bundle";
}

std::string sanitizeStem(std::string value) {
    for (char& ch : value) {
        const bool allowed = (ch >= 'a' && ch <= 'z') ||
                             (ch >= 'A' && ch <= 'Z') ||
                             (ch >= '0' && ch <= '9') ||
                             ch == '-' ||
                             ch == '_';
        if (!allowed) {
            ch = '-';
        }
    }

    while (!value.empty() && value.back() == '-') {
        value.pop_back();
    }
    if (value.empty()) {
        return "runtime-bundle";
    }
    return value;
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
                *errorMessage = "Failed to create parent directory for " + path.string() +
                                ": " + ec.message();
            }
            return false;
        }
    }

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open()) {
        if (errorMessage) {
            *errorMessage = "Failed to open output file: " + path.string();
        }
        return false;
    }

    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!stream.good()) {
        if (errorMessage) {
            *errorMessage = "Failed while writing output file: " + path.string();
        }
        return false;
    }

    return true;
}

std::string jsonEscape(std::string_view value) {
    std::ostringstream out;
    for (const char ch : value) {
        switch (ch) {
            case '\\':
                out << "\\\\";
                break;
            case '"':
                out << "\\\"";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                out << "\\r";
                break;
            case '\t':
                out << "\\t";
                break;
            default:
                out << ch;
                break;
        }
    }
    return out.str();
}

std::string iso8601NowUtc() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm utcTime{};
#if defined(_WIN32)
    gmtime_s(&utcTime, &time);
#else
    gmtime_r(&time, &utcTime);
#endif
    std::ostringstream out;
    out << std::put_time(&utcTime, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

std::string describeLintReport(const mvrvb::ProfileLintResult& result) {
    std::ostringstream out;
    out << mvrvb::summarizeProfileLintResult(result) << "\n";
    if (result.findings.empty()) {
        return out.str();
    }

    out << "\nFindings:\n";
    for (const auto& finding : result.findings) {
        out << "- [" << mvrvb::profileLintSeverityName(finding.severity) << "] "
            << finding.code << ": " << finding.message << "\n";
    }
    return out.str();
}

bool writeManifest(const std::filesystem::path& manifestPath,
                   std::string_view executable,
                   std::string_view launcher,
                   std::string_view store,
                   const mvrvb::RuntimeLaunchPlan& plan,
                   const std::filesystem::path& profilesDir,
                   std::string* errorMessage) {
    std::ostringstream manifest;
    manifest << "{\n"
             << "  \"generatedAt\": \"" << jsonEscape(iso8601NowUtc()) << "\",\n"
             << "  \"executable\": \"" << jsonEscape(executable) << "\",\n"
             << "  \"launcher\": \"" << jsonEscape(launcher) << "\",\n"
             << "  \"store\": \"" << jsonEscape(store) << "\",\n"
             << "  \"prefixPath\": \"" << jsonEscape(plan.resolvedPrefixPath) << "\",\n"
             << "  \"prefixSource\": \"" << jsonEscape(plan.resolvedPrefixSource) << "\",\n"
             << "  \"managedPrefixRoot\": \"" << jsonEscape(plan.managedPrefixRoot) << "\",\n"
             << "  \"managedPrefixPath\": \"" << jsonEscape(plan.managedPrefixPath) << "\",\n"
             << "  \"profilesDir\": \"" << jsonEscape(profilesDir.string()) << "\",\n"
             << "  \"files\": {\n"
             << "    \"launchPlanJson\": \"" << kLaunchPlanJsonName << "\",\n"
             << "    \"launchPlanReport\": \"" << kLaunchPlanReportName << "\",\n"
             << "    \"setupChecklist\": \"" << kSetupChecklistName << "\",\n"
             << "    \"bashSetupScript\": \"" << kBashSetupScriptName << "\",\n"
             << "    \"powershellSetupScript\": \"" << kPowerShellSetupScriptName << "\",\n"
             << "    \"bashLaunchScript\": \"" << kBashLaunchScriptName << "\",\n"
             << "    \"powershellLaunchScript\": \"" << kPowerShellLaunchScriptName << "\",\n"
             << "    \"compatibilityCatalogJson\": \"" << kCatalogJsonName << "\",\n"
             << "    \"compatibilityCatalogReport\": \"" << kCatalogReportName << "\",\n"
             << "    \"compatibilityCatalogMarkdown\": \"" << kCatalogMarkdownName << "\",\n"
             << "    \"profileLintReport\": \"" << kLintReportName << "\"\n"
             << "  }\n"
             << "}\n";

    return writeTextFile(manifestPath, manifest.str(), errorMessage);
}

}  // namespace

int main(int argc, char** argv) {
    std::filesystem::path profilesDir = MVRVB_TOOLS_PROFILE_DIR;
    std::filesystem::path outputDir;
    std::string executable;
    std::string launcher;
    std::string store;
    std::string prefixPath;
    std::string managedPrefixRoot;
    std::string workingDirectory;
    std::string wineBinary = "wine";
    std::string winebootBinary = "wineboot";
    std::string winetricksBinary = "winetricks";

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
        if (arg == "--exe") {
            if (const char* value = requireValue("--exe")) {
                executable = value;
                continue;
            }
            return 1;
        }
        if (arg == "--launcher") {
            if (const char* value = requireValue("--launcher")) {
                launcher = value;
                continue;
            }
            return 1;
        }
        if (arg == "--store") {
            if (const char* value = requireValue("--store")) {
                store = value;
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
        if (arg == "--out-dir") {
            if (const char* value = requireValue("--out-dir")) {
                outputDir = value;
                continue;
            }
            return 1;
        }
        if (arg == "--prefix") {
            if (const char* value = requireValue("--prefix")) {
                prefixPath = value;
                continue;
            }
            return 1;
        }
        if (arg == "--managed-prefix-root") {
            if (const char* value = requireValue("--managed-prefix-root")) {
                managedPrefixRoot = value;
                continue;
            }
            return 1;
        }
        if (arg == "--working-dir") {
            if (const char* value = requireValue("--working-dir")) {
                workingDirectory = value;
                continue;
            }
            return 1;
        }
        if (arg == "--wine-binary") {
            if (const char* value = requireValue("--wine-binary")) {
                wineBinary = value;
                continue;
            }
            return 1;
        }
        if (arg == "--wineboot-binary") {
            if (const char* value = requireValue("--wineboot-binary")) {
                winebootBinary = value;
                continue;
            }
            return 1;
        }
        if (arg == "--winetricks-binary") {
            if (const char* value = requireValue("--winetricks-binary")) {
                winetricksBinary = value;
                continue;
            }
            return 1;
        }

        std::cerr << "Unknown option: " << arg << "\n";
        printUsage(std::cerr);
        return 1;
    }

    if (executable.empty()) {
        std::cerr << "--exe is required\n";
        printUsage(std::cerr);
        return 1;
    }

    if (outputDir.empty()) {
        outputDir = std::filesystem::current_path() /
                    (sanitizeStem(executableStem(executable)) + "-bundle");
    }
    outputDir = std::filesystem::absolute(outputDir);

    std::error_code ec;
    std::filesystem::create_directories(outputDir, ec);
    if (ec) {
        std::cerr << "Failed to create output directory " << outputDir << ": "
                  << ec.message() << "\n";
        return 1;
    }

    const mvrvb::CompatibilityProfileQuery query{
        .executable = executable,
        .launcher = launcher,
        .store = store,
    };
    auto planResult = mvrvb::buildRuntimeLaunchPlanFromDirectory(profilesDir, query);
    if (!planResult) {
        std::cerr << "Failed to build runtime launch plan: " << planResult.errorMessage << "\n";
        return 1;
    }
    planResult.plan = mvrvb::resolveRuntimeLaunchPlanPrefix(
        planResult.plan,
        prefixPath,
        managedPrefixRoot);

    const mvrvb::RuntimeLaunchRequest launchRequest{
        .executablePath = executable,
        .wineBinary = wineBinary,
        .prefixPath = prefixPath,
        .managedPrefixRoot = managedPrefixRoot,
        .workingDirectory = workingDirectory,
    };
    const auto launchCommandResult =
        mvrvb::materializeRuntimeLaunchCommand(planResult.plan, launchRequest);
    if (!launchCommandResult) {
        std::cerr << "Failed to materialize runtime launch command: "
                  << launchCommandResult.errorMessage << "\n";
        return 1;
    }

    const mvrvb::RuntimeSetupRequest setupRequest{
        .prefixPath = prefixPath,
        .managedPrefixRoot = managedPrefixRoot,
        .winebootBinary = winebootBinary,
        .winetricksBinary = winetricksBinary,
        .workingDirectory = workingDirectory.empty() ? "." : workingDirectory,
    };
    const auto setupCommandResult =
        mvrvb::buildRuntimeSetupCommandPlan(planResult.plan, setupRequest);
    if (!setupCommandResult) {
        std::cerr << "Failed to materialize runtime setup command plan: "
                  << setupCommandResult.errorMessage << "\n";
        return 1;
    }

    const auto catalogResult = mvrvb::buildCompatibilityCatalogFromDirectory(profilesDir);
    if (!catalogResult) {
        std::cerr << "Failed to build compatibility catalog: "
                  << catalogResult.errorMessage << "\n";
        return 1;
    }

    const auto lintResult = mvrvb::lintCompatibilityDataFromDirectory(profilesDir);
    const std::string lintReport = describeLintReport(lintResult);

    std::string errorMessage;
    const auto writeOrFail = [&](bool success, const char* label) {
        if (!success) {
            std::cerr << "Failed to write " << label << ": " << errorMessage << "\n";
            return false;
        }
        return true;
    };

    if (!writeOrFail(
            mvrvb::writeRuntimeLaunchPlanJson(
                planResult.plan,
                outputDir / kLaunchPlanJsonName,
                &errorMessage),
            kLaunchPlanJsonName)) {
        return 1;
    }
    if (!writeOrFail(
            mvrvb::writeRuntimeLaunchPlanReport(
                planResult.plan,
                outputDir / kLaunchPlanReportName,
                &errorMessage),
            kLaunchPlanReportName)) {
        return 1;
    }
    if (!writeOrFail(
            mvrvb::writeRuntimeLaunchPlanMarkdownChecklist(
                planResult.plan,
                outputDir / kSetupChecklistName,
                &errorMessage),
            kSetupChecklistName)) {
        return 1;
    }
    if (!writeOrFail(
            mvrvb::writeRuntimeSetupCommandBash(
                setupCommandResult.plan,
                outputDir / kBashSetupScriptName,
                &errorMessage),
            kBashSetupScriptName)) {
        return 1;
    }
    if (!writeOrFail(
            mvrvb::writeRuntimeSetupCommandPowerShell(
                setupCommandResult.plan,
                outputDir / kPowerShellSetupScriptName,
                &errorMessage),
            kPowerShellSetupScriptName)) {
        return 1;
    }
    if (!writeOrFail(
            mvrvb::writeRuntimeLaunchCommandBash(
                launchCommandResult.command,
                outputDir / kBashLaunchScriptName,
                &errorMessage),
            kBashLaunchScriptName)) {
        return 1;
    }
    if (!writeOrFail(
            mvrvb::writeRuntimeLaunchCommandPowerShell(
                launchCommandResult.command,
                outputDir / kPowerShellLaunchScriptName,
                &errorMessage),
            kPowerShellLaunchScriptName)) {
        return 1;
    }
    if (!writeOrFail(
            mvrvb::writeCompatibilityCatalogJson(
                catalogResult.catalog,
                outputDir / kCatalogJsonName,
                &errorMessage),
            kCatalogJsonName)) {
        return 1;
    }
    if (!writeOrFail(
            mvrvb::writeCompatibilityCatalogReport(
                catalogResult.catalog,
                outputDir / kCatalogReportName,
                &errorMessage),
            kCatalogReportName)) {
        return 1;
    }
    if (!writeOrFail(
            mvrvb::writeCompatibilityCatalogMarkdown(
                catalogResult.catalog,
                outputDir / kCatalogMarkdownName,
                &errorMessage),
            kCatalogMarkdownName)) {
        return 1;
    }
    if (!writeOrFail(
            writeTextFile(outputDir / kLintReportName, lintReport, &errorMessage),
            kLintReportName)) {
        return 1;
    }
    if (!writeOrFail(
            writeManifest(
                outputDir / kManifestName,
                executable,
                launcher,
                store,
                planResult.plan,
                profilesDir,
                &errorMessage),
            kManifestName)) {
        return 1;
    }

    std::cout << "Runtime bundle directory: " << outputDir.string() << "\n";
    std::cout << "Bundle manifest:         " << (outputDir / kManifestName).string() << "\n";
    std::cout << "Prefix path:             " << planResult.plan.resolvedPrefixPath << "\n";
    return 0;
}
