#include "profile_lint.h"

#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

void printUsage(std::ostream& out) {
    out
        << "Usage: mvrvb_profile_lint [options]\n"
        << "Options:\n"
        << "  --profiles-dir <path>  Profile directory (defaults to the checked-in profiles tree)\n"
        << "  --strict               Treat warnings as a non-zero exit condition\n"
        << "  --help                 Show this message\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::filesystem::path profilesDir = MVRVB_TOOLS_PROFILE_DIR;
    bool strict = false;

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
        if (arg == "--strict") {
            strict = true;
            continue;
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

    const auto result = mvrvb::lintCompatibilityDataFromDirectory(profilesDir);
    std::cout << mvrvb::summarizeProfileLintResult(result) << "\n";

    if (!result) {
        return 1;
    }
    if (strict && result.warningCount > 0) {
        return 2;
    }
    return 0;
}
