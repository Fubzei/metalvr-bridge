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
        << "  --help                 Show this message\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::filesystem::path profilesDir = MVRVB_TOOLS_PROFILE_DIR;
    std::filesystem::path outputPath;
    bool jsonOutput = false;

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
            jsonOutput = true;
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
        const bool ok = jsonOutput
            ? mvrvb::writeCompatibilityCatalogJson(result.catalog, outputPath, &errorMessage)
            : mvrvb::writeCompatibilityCatalogReport(result.catalog, outputPath, &errorMessage);
        if (!ok) {
            std::cerr << "Failed to write compatibility catalog: " << errorMessage << "\n";
            return 1;
        }
        std::cout << outputPath.string() << "\n";
        return 0;
    }

    if (jsonOutput) {
        std::cout << mvrvb::compatibilityCatalogToJson(result.catalog) << "\n";
    } else {
        std::cout << mvrvb::describeCompatibilityCatalog(result.catalog);
    }
    return 0;
}
