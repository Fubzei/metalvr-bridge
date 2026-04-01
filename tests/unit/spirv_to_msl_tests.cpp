#include <gtest/gtest.h>

#include "msl_emitter/spirv_to_msl.h"

#include <array>
#include <sstream>
#include <string>

namespace mvrvb::msl {
namespace {

using spirv::BaseType;
using spirv::EntryPoint;
using spirv::SPIRVModule;
using spirv::ShaderStage;
using spirv::SpvBlock;
using spirv::SpvConstant;
using spirv::SpvType;
using spirv::SpvVariable;
using spirv::StorageClass;

SPIRVModule makeComputeModuleWithResources() {
    SPIRVModule module;
    module.versionMajor = 1u;
    module.versionMinor = 6u;

    EntryPoint entryPoint;
    entryPoint.functionId = 42u;
    entryPoint.name = "main";
    entryPoint.stage = ShaderStage::Compute;
    entryPoint.localSizeX = 8u;
    entryPoint.localSizeY = 4u;
    entryPoint.localSizeZ = 2u;
    module.entryPoints.push_back(entryPoint);

    SpvType floatType;
    floatType.id = 1u;
    floatType.base = BaseType::Float;
    floatType.bitWidth = 32u;
    module.types[floatType.id] = floatType;

    SpvType globalsType;
    globalsType.id = 2u;
    globalsType.base = BaseType::Struct;
    module.types[globalsType.id] = globalsType;

    SpvBlock globalsBlock;
    globalsBlock.typeId = globalsType.id;
    globalsBlock.name = "Globals";
    spirv::MemberInfo valueMember;
    valueMember.name = "value";
    valueMember.typeId = floatType.id;
    globalsBlock.members.push_back(valueMember);
    module.blocks[globalsBlock.typeId] = globalsBlock;

    SpvType uniformPtrType;
    uniformPtrType.id = 3u;
    uniformPtrType.base = BaseType::Pointer;
    uniformPtrType.isPointer = true;
    uniformPtrType.elementTypeId = globalsType.id;
    uniformPtrType.storageClass = static_cast<uint32_t>(StorageClass::Uniform);
    module.types[uniformPtrType.id] = uniformPtrType;

    SpvType pushConstPtrType = uniformPtrType;
    pushConstPtrType.id = 4u;
    pushConstPtrType.storageClass = static_cast<uint32_t>(StorageClass::PushConstant);
    module.types[pushConstPtrType.id] = pushConstPtrType;

    SpvType imageType;
    imageType.id = 5u;
    imageType.base = BaseType::Image;
    imageType.elementTypeId = floatType.id;
    imageType.imageDim = 1u;
    imageType.imageIsSampled = true;
    module.types[imageType.id] = imageType;

    SpvType sampledImageType;
    sampledImageType.id = 6u;
    sampledImageType.base = BaseType::SampledImage;
    sampledImageType.elementTypeId = floatType.id;
    sampledImageType.imageDim = 1u;
    sampledImageType.imageIsSampled = true;
    module.types[sampledImageType.id] = sampledImageType;

    SpvType texturePtrType;
    texturePtrType.id = 7u;
    texturePtrType.base = BaseType::Pointer;
    texturePtrType.isPointer = true;
    texturePtrType.elementTypeId = sampledImageType.id;
    texturePtrType.storageClass = static_cast<uint32_t>(StorageClass::UniformConstant);
    module.types[texturePtrType.id] = texturePtrType;

    SpvType samplerType;
    samplerType.id = 8u;
    samplerType.base = BaseType::Sampler;
    module.types[samplerType.id] = samplerType;

    SpvType samplerPtrType;
    samplerPtrType.id = 9u;
    samplerPtrType.base = BaseType::Pointer;
    samplerPtrType.isPointer = true;
    samplerPtrType.elementTypeId = samplerType.id;
    samplerPtrType.storageClass = static_cast<uint32_t>(StorageClass::UniformConstant);
    module.types[samplerPtrType.id] = samplerPtrType;

    SpvVariable globals;
    globals.id = 10u;
    globals.typeId = uniformPtrType.id;
    globals.storageClass = StorageClass::Uniform;
    globals.name = "globals";
    globals.set = 0u;
    globals.binding = 2u;
    module.variables[globals.id] = globals;

    SpvVariable pushConsts;
    pushConsts.id = 11u;
    pushConsts.typeId = pushConstPtrType.id;
    pushConsts.storageClass = StorageClass::PushConstant;
    pushConsts.name = "pushConsts";
    module.variables[pushConsts.id] = pushConsts;

    SpvVariable texture;
    texture.id = 12u;
    texture.typeId = texturePtrType.id;
    texture.storageClass = StorageClass::UniformConstant;
    texture.name = "albedo";
    texture.set = 1u;
    texture.binding = 0u;
    module.variables[texture.id] = texture;

    SpvVariable sampler;
    sampler.id = 13u;
    sampler.typeId = samplerPtrType.id;
    sampler.storageClass = StorageClass::UniformConstant;
    sampler.name = "samp0";
    sampler.set = 1u;
    sampler.binding = 0u;
    module.variables[sampler.id] = sampler;

    return module;
}

SPIRVModule makeComputeModuleWithIntegerComparisons() {
    SPIRVModule module;
    module.versionMajor = 1u;
    module.versionMinor = 6u;

    EntryPoint entryPoint;
    entryPoint.functionId = 42u;
    entryPoint.name = "main";
    entryPoint.stage = ShaderStage::Compute;
    module.entryPoints.push_back(entryPoint);

    SpvType voidType;
    voidType.id = 1u;
    voidType.base = BaseType::Void;
    module.types[voidType.id] = voidType;

    SpvType boolType;
    boolType.id = 2u;
    boolType.base = BaseType::Bool;
    module.types[boolType.id] = boolType;

    SpvType uintType;
    uintType.id = 3u;
    uintType.base = BaseType::UInt;
    uintType.bitWidth = 32u;
    uintType.isSigned = false;
    module.types[uintType.id] = uintType;

    SpvType intType;
    intType.id = 4u;
    intType.base = BaseType::Int;
    intType.bitWidth = 32u;
    intType.isSigned = true;
    module.types[intType.id] = intType;

    module.constants[5u] = SpvConstant{5u, uintType.id, 1u};
    module.constants[6u] = SpvConstant{6u, uintType.id, 2u};
    module.constants[7u] = SpvConstant{7u, intType.id, 1u};
    module.constants[8u] = SpvConstant{8u, intType.id, 2u};

    spirv::SpvFunction function;
    function.id = entryPoint.functionId;
    function.returnTypeId = voidType.id;

    spirv::BasicBlock block;
    block.labelId = 100u;

    const std::array<spirv::Instruction, 8> comparisons{{
        {172u, 10u, boolType.id, {6u, 5u}},
        {173u, 11u, boolType.id, {8u, 7u}},
        {174u, 12u, boolType.id, {6u, 5u}},
        {175u, 13u, boolType.id, {8u, 7u}},
        {176u, 14u, boolType.id, {5u, 6u}},
        {177u, 15u, boolType.id, {7u, 8u}},
        {178u, 16u, boolType.id, {5u, 6u}},
        {179u, 17u, boolType.id, {7u, 8u}},
    }};

    block.instructions.insert(block.instructions.end(), comparisons.begin(), comparisons.end());
    block.instructions.push_back(spirv::Instruction{253u});
    function.blocks.push_back(std::move(block));
    module.functions[function.id] = std::move(function);

    return module;
}

std::string findLineContaining(const std::string& source, const std::string& needle) {
    std::istringstream stream(source);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.find(needle) != std::string::npos) {
            return line;
        }
    }
    return {};
}

TEST(SpirvToMsl, RejectsModulesWithoutEntryPoints) {
    const TranslateResult result = translateToMSL({});

    EXPECT_EQ(result.error, TranslateError::NoEntryPoint);
    EXPECT_EQ(result.errorMessage, "No entry points found in SPIR-V module");
}

TEST(SpirvToMsl, RejectsGeometryEntryPointsWithoutEmulation) {
    SPIRVModule module;
    EntryPoint entryPoint;
    entryPoint.functionId = 7u;
    entryPoint.name = "geom_main";
    entryPoint.stage = ShaderStage::Geometry;
    module.entryPoints.push_back(entryPoint);

    const TranslateResult result = translateToMSL(module);

    EXPECT_EQ(result.error, TranslateError::UnsupportedStage);
    EXPECT_EQ(result.errorMessage, "Geometry shaders must go through the geometry emulator");
}

TEST(SpirvToMsl, EmitsComputeStubAndReflectsResources) {
    const SPIRVModule module = makeComputeModuleWithResources();

    MSLOptions options;
    options.entryPointName = "unit_main";
    options.emitComments = false;

    const TranslateResult result = translateToMSL(module, options);

    ASSERT_TRUE(result) << result.errorMessage;
    EXPECT_EQ(result.stage, ShaderStage::Compute);
    EXPECT_EQ(result.reflection.numThreadgroupsX, 8u);
    EXPECT_EQ(result.reflection.numThreadgroupsY, 4u);
    EXPECT_EQ(result.reflection.numThreadgroupsZ, 2u);

    ASSERT_EQ(result.reflection.buffers.size(), 2u);
    const BufferBinding* pushBinding = nullptr;
    const BufferBinding* uniformBinding = nullptr;
    for (const auto& binding : result.reflection.buffers) {
        if (binding.isPushConst) {
            pushBinding = &binding;
        } else if (binding.name == "globals") {
            uniformBinding = &binding;
        }
    }

    ASSERT_NE(pushBinding, nullptr);
    ASSERT_NE(uniformBinding, nullptr);
    EXPECT_EQ(pushBinding->metalSlot, kPushConstantBufferSlot);
    EXPECT_EQ(pushBinding->name, "pushConsts");
    EXPECT_EQ(pushBinding->typeName, "Globals");
    EXPECT_EQ(uniformBinding->metalSlot, kFirstUBOSlot);
    EXPECT_TRUE(uniformBinding->isUniform);
    EXPECT_EQ(uniformBinding->set, 0u);
    EXPECT_EQ(uniformBinding->binding, 2u);

    ASSERT_EQ(result.reflection.textures.size(), 1u);
    EXPECT_EQ(result.reflection.textures.front().name, "albedo");
    EXPECT_EQ(result.reflection.textures.front().metalTextureSlot, 0u);
    EXPECT_TRUE(result.reflection.textures.front().isSampledImage);
    EXPECT_EQ(result.reflection.textures.front().metalSamplerSlot, 0u);

    ASSERT_EQ(result.reflection.samplers.size(), 1u);
    EXPECT_EQ(result.reflection.samplers.front().name, "samp0");
    EXPECT_EQ(result.reflection.samplers.front().metalSamplerSlot, 0u);

    EXPECT_NE(result.mslSource.find("kernel void unit_main"), std::string::npos);
    EXPECT_NE(result.mslSource.find("constant Globals& pushConsts [[buffer(0)]]"), std::string::npos);
    EXPECT_NE(result.mslSource.find("constant Globals& globals [[buffer(1)]]"), std::string::npos);
    EXPECT_NE(result.mslSource.find("albedo [[texture(0)]]"), std::string::npos);
    EXPECT_NE(result.mslSource.find("samp0 [[sampler(0)]]"), std::string::npos);
    EXPECT_NE(result.mslSource.find("return;"), std::string::npos);
}

TEST(SpirvToMsl, FallsBackToFirstSamplerWhenNoBindingMatchExists) {
    SPIRVModule module = makeComputeModuleWithResources();
    module.variables[13u].binding = 5u;

    const TranslateResult result = translateToMSL(module);

    ASSERT_TRUE(result) << result.errorMessage;
    ASSERT_EQ(result.reflection.textures.size(), 1u);
    ASSERT_EQ(result.reflection.samplers.size(), 1u);

    EXPECT_EQ(result.reflection.samplers.front().binding, 5u);
    EXPECT_EQ(result.reflection.samplers.front().metalSamplerSlot, 0u);
    EXPECT_EQ(result.reflection.textures.front().binding, 0u);
    EXPECT_EQ(result.reflection.textures.front().metalSamplerSlot, 0u);
}

TEST(SpirvToMsl, EmitsCorrectIntegerComparisonOperators) {
    const TranslateResult result = translateToMSL(makeComputeModuleWithIntegerComparisons());

    ASSERT_TRUE(result) << result.errorMessage;

    const std::string uGreater = findLineContaining(result.mslSource, "bool v10 =");
    const std::string sGreater = findLineContaining(result.mslSource, "bool v11 =");
    const std::string uGreaterEqual = findLineContaining(result.mslSource, "bool v12 =");
    const std::string sGreaterEqual = findLineContaining(result.mslSource, "bool v13 =");
    const std::string uLess = findLineContaining(result.mslSource, "bool v14 =");
    const std::string sLess = findLineContaining(result.mslSource, "bool v15 =");
    const std::string uLessEqual = findLineContaining(result.mslSource, "bool v16 =");
    const std::string sLessEqual = findLineContaining(result.mslSource, "bool v17 =");

    ASSERT_FALSE(uGreater.empty());
    ASSERT_FALSE(sGreater.empty());
    ASSERT_FALSE(uGreaterEqual.empty());
    ASSERT_FALSE(sGreaterEqual.empty());
    ASSERT_FALSE(uLess.empty());
    ASSERT_FALSE(sLess.empty());
    ASSERT_FALSE(uLessEqual.empty());
    ASSERT_FALSE(sLessEqual.empty());

    EXPECT_NE(uGreater.find(" > "), std::string::npos);
    EXPECT_NE(sGreater.find(" > "), std::string::npos);
    EXPECT_NE(uGreaterEqual.find(" >= "), std::string::npos);
    EXPECT_NE(sGreaterEqual.find(" >= "), std::string::npos);
    EXPECT_NE(uLess.find(" < "), std::string::npos);
    EXPECT_NE(sLess.find(" < "), std::string::npos);
    EXPECT_NE(uLessEqual.find(" <= "), std::string::npos);
    EXPECT_NE(sLessEqual.find(" <= "), std::string::npos);
}

}  // namespace
}  // namespace mvrvb::msl
