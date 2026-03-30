#include <gtest/gtest.h>

#include "spirv/spirv_parser.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

namespace mvrvb::spirv {
namespace {

constexpr uint32_t kSpirvMagic = 0x07230203u;
constexpr uint32_t kSpirvVersion16 = 0x00010600u;

constexpr uint16_t kOpCapability = 17u;
constexpr uint16_t kOpMemoryModel = 14u;
constexpr uint16_t kOpEntryPoint = 15u;
constexpr uint16_t kOpExecutionMode = 16u;
constexpr uint16_t kOpTypeVoid = 19u;
constexpr uint16_t kOpTypeFunction = 33u;
constexpr uint16_t kOpFunction = 54u;
constexpr uint16_t kOpFunctionEnd = 56u;
constexpr uint16_t kOpLabel = 248u;
constexpr uint16_t kOpReturn = 253u;

constexpr uint32_t kExecutionModelCompute = 5u;
constexpr uint32_t kExecutionModeLocalSize = 17u;

uint32_t makeInstructionWord(uint16_t wordCount, uint16_t opcode) {
    return (static_cast<uint32_t>(wordCount) << 16) | opcode;
}

std::vector<uint32_t> packStringWords(const std::string& text) {
    std::vector<uint32_t> words((text.size() + 1u + 3u) / 4u, 0u);
    for (size_t i = 0; i < text.size(); ++i) {
        words[i / 4u] |= static_cast<uint32_t>(static_cast<unsigned char>(text[i]))
                         << ((i % 4u) * 8u);
    }
    return words;
}

void appendInstruction(std::vector<uint32_t>& words,
                       uint16_t opcode,
                       std::initializer_list<uint32_t> operands) {
    words.push_back(makeInstructionWord(
        static_cast<uint16_t>(1u + operands.size()), opcode));
    words.insert(words.end(), operands.begin(), operands.end());
}

void appendStringInstruction(std::vector<uint32_t>& words,
                             uint16_t opcode,
                             std::initializer_list<uint32_t> prefixOperands,
                             const std::string& text,
                             std::initializer_list<uint32_t> suffixOperands = {}) {
    const auto stringWords = packStringWords(text);
    const auto wordCount = static_cast<uint16_t>(
        1u + prefixOperands.size() + stringWords.size() + suffixOperands.size());

    words.push_back(makeInstructionWord(wordCount, opcode));
    words.insert(words.end(), prefixOperands.begin(), prefixOperands.end());
    words.insert(words.end(), stringWords.begin(), stringWords.end());
    words.insert(words.end(), suffixOperands.begin(), suffixOperands.end());
}

std::vector<uint32_t> makeMinimalComputeModule() {
    std::vector<uint32_t> words{
        kSpirvMagic,
        kSpirvVersion16,
        0u,
        5u,
        0u,
    };

    appendInstruction(words, kOpCapability, {1u});
    appendInstruction(words, kOpMemoryModel, {0u, 1u});
    appendStringInstruction(words, kOpEntryPoint, {kExecutionModelCompute, 1u}, "main");
    appendInstruction(words, kOpExecutionMode, {1u, kExecutionModeLocalSize, 8u, 4u, 2u});
    appendInstruction(words, kOpTypeVoid, {2u});
    appendInstruction(words, kOpTypeFunction, {3u, 2u});
    appendInstruction(words, kOpFunction, {2u, 1u, 0u, 3u});
    appendInstruction(words, kOpLabel, {4u});
    appendInstruction(words, kOpReturn, {});
    appendInstruction(words, kOpFunctionEnd, {});

    return words;
}

std::vector<uint8_t> wordsToBytes(const std::vector<uint32_t>& words) {
    std::vector<uint8_t> bytes(words.size() * sizeof(uint32_t));
    std::memcpy(bytes.data(), words.data(), bytes.size());
    return bytes;
}

TEST(SpirvParser, RejectsTruncatedHeaders) {
    const std::array<uint32_t, 4> words{0u, 0u, 0u, 0u};

    const ParseResult result = parseSPIRV(words.data(), words.size());

    EXPECT_EQ(result.error, ParseError::TruncatedHeader);
    EXPECT_EQ(result.errorMessage, "SPIR-V header requires at least 5 words");
}

TEST(SpirvParser, RejectsInvalidMagicNumbers) {
    const std::array<uint32_t, 5> words{0u, kSpirvVersion16, 0u, 0u, 0u};

    const ParseResult result = parseSPIRV(words.data(), words.size());

    EXPECT_EQ(result.error, ParseError::InvalidMagic);
    EXPECT_EQ(result.errorMessage, "Not a SPIR-V module (bad magic number)");
}

TEST(SpirvParser, ParsesMinimalComputeModule) {
    const auto words = makeMinimalComputeModule();

    const ParseResult result = parseSPIRV(words.data(), words.size());

    ASSERT_TRUE(result) << result.errorMessage;
    EXPECT_EQ(result.module.versionMajor, 1u);
    EXPECT_EQ(result.module.versionMinor, 6u);
    ASSERT_EQ(result.module.entryPoints.size(), 1u);

    const EntryPoint& entryPoint = result.module.entryPoints.front();
    EXPECT_EQ(entryPoint.name, "main");
    EXPECT_EQ(entryPoint.stage, ShaderStage::Compute);
    EXPECT_EQ(entryPoint.localSizeX, 8u);
    EXPECT_EQ(entryPoint.localSizeY, 4u);
    EXPECT_EQ(entryPoint.localSizeZ, 2u);
    EXPECT_TRUE(entryPoint.interfaceVars.empty());
    EXPECT_STREQ(shaderStageName(entryPoint.stage), "Compute");

    const auto functionIt = result.module.functions.find(1u);
    ASSERT_NE(functionIt, result.module.functions.end());
    ASSERT_EQ(functionIt->second.blocks.size(), 1u);
    EXPECT_EQ(functionIt->second.blocks.front().labelId, 4u);
}

TEST(SpirvParser, ParsesAlignedByteInputAndRejectsUnalignedBytes) {
    const auto words = makeMinimalComputeModule();
    const auto bytes = wordsToBytes(words);

    const ParseResult validResult = parseSPIRVBytes(bytes.data(), bytes.size());
    ASSERT_TRUE(validResult) << validResult.errorMessage;
    ASSERT_EQ(validResult.module.entryPoints.size(), 1u);
    EXPECT_EQ(validResult.module.entryPoints.front().name, "main");

    const std::array<uint8_t, 5> invalidBytes{0u, 1u, 2u, 3u, 4u};
    const ParseResult invalidResult = parseSPIRVBytes(invalidBytes.data(), invalidBytes.size());

    EXPECT_EQ(invalidResult.error, ParseError::MalformedInstruction);
    EXPECT_EQ(invalidResult.errorMessage, "SPIR-V byte count not aligned to 4 bytes");
}

}  // namespace
}  // namespace mvrvb::spirv
