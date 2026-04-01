#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

std::filesystem::path repoRoot() {
    return std::filesystem::path(MVRVB_SOURCE_ROOT);
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Failed to open " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1u);
}

std::vector<std::string> readNonEmptyLines(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error("Failed to open " + path.string());
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(stream, line)) {
        line = trim(line);
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    return lines;
}

std::string extractJsonString(const std::string& json, const std::string& key) {
    const std::regex pattern("\"" + key + "\"\\s*:\\s*\"([^\"]+)\"");
    std::smatch match;
    if (!std::regex_search(json, match, pattern)) {
        return {};
    }
    return match[1].str();
}

bool extractJsonBool(const std::string& json, const std::string& key, bool* value) {
    const std::regex pattern("\"" + key + "\"\\s*:\\s*(true|false)");
    std::smatch match;
    if (!std::regex_search(json, match, pattern)) {
        return false;
    }
    *value = (match[1].str() == "true");
    return true;
}

std::vector<std::string> extractDispatchNames(const std::string& source) {
    const std::regex entryPattern(
        R"mvrvb(\{\s*"([^"]+)"\s*,\s*\(PFN_vkVoidFunction\)\s*[A-Za-z0-9_]+\s*\})mvrvb");
    std::vector<std::string> names;
    for (auto it = std::sregex_iterator(source.begin(), source.end(), entryPattern);
         it != std::sregex_iterator();
         ++it) {
        names.push_back((*it)[1].str());
    }
    return names;
}

std::unordered_map<std::string, std::string> extractDispatchMap(const std::string& source) {
    const std::regex entryPattern(
        R"mvrvb(\{\s*"([^"]+)"\s*,\s*\(PFN_vkVoidFunction\)\s*([A-Za-z0-9_]+)\s*\})mvrvb");
    std::unordered_map<std::string, std::string> map;
    for (auto it = std::sregex_iterator(source.begin(), source.end(), entryPattern);
         it != std::sregex_iterator();
         ++it) {
        map.emplace((*it)[1].str(), (*it)[2].str());
    }
    return map;
}

bool fileHasExportedSignature(const std::string& text, const std::string& functionName) {
    const std::regex pattern("MVVK_EXPORT[^\\n;{]*\\b" + functionName + R"(\b)");
    return std::regex_search(text, pattern);
}

TEST(IcdContracts, ExportListMatchesLoaderSurface) {
    const auto exports = readNonEmptyLines(
        repoRoot() / "src" / "vulkan_layer" / "icd" / "exported_symbols.txt");

    const std::vector<std::string> expected{
        "_vk_icdGetInstanceProcAddr",
        "_vk_icdGetPhysicalDeviceProcAddr",
        "_vk_icdNegotiateLoaderICDInterfaceVersion",
        "_vkGetDeviceProcAddr",
    };

    EXPECT_EQ(exports, expected);
    EXPECT_EQ(std::set<std::string>(exports.begin(), exports.end()).size(), exports.size());
}

TEST(IcdContracts, ManifestDeclaresPortableLoaderContract) {
    const std::string manifest = readFile(repoRoot() / "vulkan_icd.json");
    bool portabilityDriver = false;

    EXPECT_EQ(extractJsonString(manifest, "file_format_version"), "1.0.0");
    EXPECT_EQ(extractJsonString(manifest, "library_path"), "@rpath/libMetalVRBridge.dylib");
    EXPECT_EQ(extractJsonString(manifest, "api_version"), "1.2.0");
    ASSERT_TRUE(extractJsonBool(manifest, "is_portability_driver", &portabilityDriver));
    EXPECT_TRUE(portabilityDriver);
}

TEST(IcdContracts, DispatchTableIsSortedUniqueAndCoversCriticalEntryPoints) {
    const std::string source = readFile(
        repoRoot() / "src" / "vulkan_layer" / "icd" / "vulkan_icd.cpp");
    const auto names = extractDispatchNames(source);
    const auto bindings = extractDispatchMap(source);

    ASSERT_GE(names.size(), 200u);
    EXPECT_TRUE(std::is_sorted(names.begin(), names.end()));
    EXPECT_EQ(std::adjacent_find(names.begin(), names.end()), names.end());

    const std::vector<std::string> requiredNames{
        "vkCreateInstance",
        "vkEnumeratePhysicalDevices",
        "vkCreateDevice",
        "vkGetDeviceProcAddr",
        "vkCreateShaderModule",
        "vkCreateSwapchainKHR",
        "vkGetSwapchainImagesKHR",
        "vkQueueSubmit",
        "vkQueuePresentKHR",
        "vkCreateMetalSurfaceEXT",
        "vkCreateWin32SurfaceKHR",
        "vkCreateMacOSSurfaceMVK",
        "vkCmdDraw",
        "vkCmdDispatch",
    };

    for (const auto& name : requiredNames) {
        EXPECT_TRUE(bindings.contains(name)) << name;
    }

    EXPECT_EQ(bindings.at("vkCreateSwapchainKHR"), "mvb_CreateSwapchainKHR");
    EXPECT_EQ(bindings.at("vkQueueSubmit"), "mvb_QueueSubmit");
    EXPECT_EQ(bindings.at("vkQueuePresentKHR"), "mvb_QueuePresentKHR");
    EXPECT_EQ(bindings.at("vkCreatePipelineLayout"), "mvb_CreatePipelineLayout");
    EXPECT_EQ(bindings.at("vkAllocateDescriptorSets"), "mvb_AllocateDescriptorSets");
    EXPECT_EQ(bindings.at("vkCreateSemaphore"), "mvb_CreateSemaphore");
    EXPECT_EQ(bindings.at("vkCreateMetalSurfaceEXT"), "mvb_CreateMetalSurfaceEXT");
    EXPECT_EQ(bindings.at("vkCreateWin32SurfaceKHR"), "mvb_CreateWin32SurfaceKHR");
    EXPECT_EQ(bindings.at("vkCreateMacOSSurfaceMVK"), "mvb_CreateMacOSSurfaceMVK");
}

TEST(IcdContracts, ExportedLoaderFunctionsAreDeclaredAndDefined) {
    const auto exports = readNonEmptyLines(
        repoRoot() / "src" / "vulkan_layer" / "icd" / "exported_symbols.txt");
    const std::string header = readFile(
        repoRoot() / "src" / "vulkan_layer" / "icd" / "vulkan_icd.h");
    const std::string source = readFile(
        repoRoot() / "src" / "vulkan_layer" / "icd" / "vulkan_icd.cpp");

    for (const auto& exported : exports) {
        const std::string functionName = exported.substr(1u);
        EXPECT_TRUE(fileHasExportedSignature(header, functionName)) << functionName;
        EXPECT_TRUE(fileHasExportedSignature(source, functionName)) << functionName;
    }
}

}  // namespace
