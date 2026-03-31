#include "compatibility_catalog.h"

#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

void printUsage(std::ostream& out) {
    out
        << "Usage: mvrvb_profile_catalog [options]\n"
        << "Options:\n"
        << "  --profiles-dir <path>  Profile directory (defaults to the checked-in profiles tree)\n"
        << "  --out <path>           Write the catalog to a file instead of stdout\n"
        << "  --json                 Print the catalog as JSON\n"
        << "  --markdown             Print the catalog as Markdown\n"
        << "  --help                 Show this message\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::filesystem::path profilesDir = MVRVB_TOOLS_PROFILE_DIR;
    std::filesystem::path outputPath;
    enum class OutputMode {
        Report,
        Json,
        Markdown,
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
        if (arg == "--markdown") {
            outputMode = OutputMode::Markdown;
            continue;
        }
        if (arg == "--out") {
            if (const char* value = requireValue("--out")) {
                outputPath = value;
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

    const auto result = mvrvb::buildCompatibilityCatalogFromDirectory(profilesDir);
    if (!result) {
        std::cerr << "Failed to build compatibility catalog: " << result.errorMessage << "\n";
        return 1;
    }

    std::string errorMessage;
    if (!outputPath.empty()) {
        outputPath = std::filesystem::absolute(outputPath);
        bool ok = false;
        switch (outputMode) {
            case OutputMode::Json:
                ok = mvrvb::writeCompatibilityCatalogJson(result.catalog, outputPath, &errorMessage);
                break;
            case OutputMode::Markdown:
                ok = mvrvb::writeCompatibilityCatalogMarkdown(
                    result.catalog,
                    outputPath,
                    &errorMessage);
                break;
            case OutputMode::Report:
                ok = mvrvb::writeCompatibilityCatalogReport(
                    result.catalog,
                    outputPath,
                    &errorMessage);
                break;
        }
        if (!ok) {
            std::cerr << "Failed to write compatibility catalog: " << errorMessage << "\n";
            return 1;
        }
        std::cout << outputPath.string() << "\n";
        return 0;
    }

    switch (outputMode) {
        case OutputMode::Json:
            std::cout << mvrvb::compatibilityCatalogToJson(result.catalog) << "\n";
            break;
        case OutputMode::Markdown:
            std::cout << mvrvb::compatibilityCatalogToMarkdown(result.catalog);
            break;
        case OutputMode::Report:
            std::cout << mvrvb::describeCompatibilityCatalog(result.catalog);
            break;
    }
    return 0;
}
