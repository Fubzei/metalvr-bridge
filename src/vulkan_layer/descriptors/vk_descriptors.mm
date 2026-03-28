/**
 * @file vk_descriptors.mm
 * @brief Milestone 6 - descriptor layouts, pools, and descriptor writes.
 */

#include "vk_descriptors.h"
#include "../resources/vk_resources.h"
#include "../../common/logging.h"

#include <algorithm>

using namespace mvrvb;

namespace {

const DescriptorSetLayoutBinding* findLayoutBinding(const MvDescriptorSet* ds,
                                                    uint32_t binding) {
    if (!ds || !ds->layout) return nullptr;

    for (const auto& layoutBinding : ds->layout->bindings) {
        if (layoutBinding.binding == binding) return &layoutBinding;
    }

    return nullptr;
}

MvSampler* resolveSamplerResource(const MvDescriptorSet* ds,
                                  uint32_t binding,
                                  uint32_t arrayIndex,
                                  VkSampler fallbackSampler) {
    const auto* layoutBinding = findLayoutBinding(ds, binding);
    if (layoutBinding && arrayIndex < layoutBinding->immutableSamplers.size()) {
        return toMv(layoutBinding->immutableSamplers[arrayIndex]);
    }

    return fallbackSampler ? toMv(fallbackSampler) : nullptr;
}

void removeLiveSet(MvDescriptorPool* pool, MvDescriptorSet* set) {
    if (!pool || !set) return;

    auto it = std::remove(pool->liveSets.begin(), pool->liveSets.end(), set);
    pool->liveSets.erase(it, pool->liveSets.end());
}

DescriptorBinding& findOrInsert(MvDescriptorSet* ds,
                                uint32_t binding,
                                uint32_t arrayIndex) {
    for (auto& descriptor : ds->bindings) {
        if (descriptor.binding == binding && descriptor.arrayIndex == arrayIndex) {
            return descriptor;
        }
    }

    DescriptorBinding descriptor{};
    descriptor.binding = binding;
    descriptor.arrayIndex = arrayIndex;
    ds->bindings.push_back(descriptor);
    return ds->bindings.back();
}

} // namespace

extern "C" {

VkResult mvb_CreateDescriptorSetLayout(VkDevice,
                                       const VkDescriptorSetLayoutCreateInfo* pCI,
                                       const VkAllocationCallbacks*,
                                       VkDescriptorSetLayout* pSetLayout) {
    if (!pCI || !pSetLayout) return VK_ERROR_INITIALIZATION_FAILED;

    auto* layout = new MvDescriptorSetLayout();
    layout->flags = pCI->flags;
    layout->bindings.resize(pCI->bindingCount);

    for (uint32_t i = 0; i < pCI->bindingCount; ++i) {
        const VkDescriptorSetLayoutBinding& src = pCI->pBindings[i];
        DescriptorSetLayoutBinding& dst = layout->bindings[i];
        dst.binding = src.binding;
        dst.descriptorType = src.descriptorType;
        dst.descriptorCount = src.descriptorCount;
        dst.stageFlags = src.stageFlags;
        if (src.pImmutableSamplers && src.descriptorCount > 0) {
            dst.immutableSamplers.assign(src.pImmutableSamplers,
                                         src.pImmutableSamplers + src.descriptorCount);
        }
    }

    std::sort(layout->bindings.begin(), layout->bindings.end(),
              [](const DescriptorSetLayoutBinding& a, const DescriptorSetLayoutBinding& b) {
                  return a.binding < b.binding;
              });

    *pSetLayout = toVk(layout);
    MVRVB_LOG_DEBUG("DescriptorSetLayout created (%u bindings)", pCI->bindingCount);
    return VK_SUCCESS;
}

void mvb_DestroyDescriptorSetLayout(VkDevice,
                                    VkDescriptorSetLayout layout,
                                    const VkAllocationCallbacks*) {
    delete toMv(layout);
}

VkResult mvb_CreateDescriptorPool(VkDevice,
                                  const VkDescriptorPoolCreateInfo* pCI,
                                  const VkAllocationCallbacks*,
                                  VkDescriptorPool* pPool) {
    if (!pCI || !pPool) return VK_ERROR_INITIALIZATION_FAILED;

    auto* pool = new MvDescriptorPool();
    pool->maxSets = pCI->maxSets;
    pool->flags = pCI->flags;
    *pPool = toVk(pool);

    MVRVB_LOG_DEBUG("DescriptorPool created (maxSets=%u)", pCI->maxSets);
    return VK_SUCCESS;
}

void mvb_DestroyDescriptorPool(VkDevice, VkDescriptorPool pool, const VkAllocationCallbacks*) {
    auto* descriptorPool = toMv(pool);
    if (!descriptorPool) return;

    for (auto* set : descriptorPool->liveSets) {
        delete set;
    }
    delete descriptorPool;
}

VkResult mvb_ResetDescriptorPool(VkDevice,
                                 VkDescriptorPool pool,
                                 VkDescriptorPoolResetFlags) {
    auto* descriptorPool = toMv(pool);
    if (!descriptorPool) return VK_ERROR_INITIALIZATION_FAILED;

    for (auto* set : descriptorPool->liveSets) {
        delete set;
    }
    descriptorPool->liveSets.clear();
    descriptorPool->allocatedSets = 0;

    MVRVB_LOG_DEBUG("DescriptorPool reset");
    return VK_SUCCESS;
}

VkResult mvb_AllocateDescriptorSets(VkDevice,
                                    const VkDescriptorSetAllocateInfo* pAI,
                                    VkDescriptorSet* pSets) {
    if (!pAI || !pSets) return VK_ERROR_INITIALIZATION_FAILED;

    auto* pool = toMv(pAI->descriptorPool);
    if (!pool) return VK_ERROR_INITIALIZATION_FAILED;

    for (uint32_t i = 0; i < pAI->descriptorSetCount; ++i) {
        if (pool->allocatedSets >= pool->maxSets) {
            MVRVB_LOG_ERROR("DescriptorPool exhausted (maxSets=%u)", pool->maxSets);
            for (uint32_t j = 0; j < i; ++j) {
                auto* liveSet = toMv(pSets[j]);
                removeLiveSet(pool, liveSet);
                delete liveSet;
            }
            pool->allocatedSets -= i;
            return VK_ERROR_OUT_OF_POOL_MEMORY;
        }

        auto* ds = new MvDescriptorSet();
        ds->pool = pool;
        ds->layout = toMv(pAI->pSetLayouts[i]);

        if (ds->layout) {
            for (const auto& layoutBinding : ds->layout->bindings) {
                for (uint32_t arrayIndex = 0; arrayIndex < layoutBinding.descriptorCount;
                     ++arrayIndex) {
                    DescriptorBinding descriptor{};
                    descriptor.binding = layoutBinding.binding;
                    descriptor.arrayIndex = arrayIndex;
                    descriptor.descriptorType = layoutBinding.descriptorType;
                    ds->bindings.push_back(descriptor);
                }
            }
        }

        ++pool->allocatedSets;
        pool->liveSets.push_back(ds);
        pSets[i] = toVk(ds);
    }

    return VK_SUCCESS;
}

VkResult mvb_FreeDescriptorSets(VkDevice,
                                VkDescriptorPool pool,
                                uint32_t count,
                                const VkDescriptorSet* pSets) {
    auto* descriptorPool = toMv(pool);
    if (!descriptorPool) return VK_ERROR_INITIALIZATION_FAILED;

    for (uint32_t i = 0; i < count; ++i) {
        auto* descriptorSet = toMv(pSets[i]);
        if (!descriptorSet) continue;

        removeLiveSet(descriptorPool, descriptorSet);
        delete descriptorSet;
        if (descriptorPool->allocatedSets > 0) {
            --descriptorPool->allocatedSets;
        }
    }

    return VK_SUCCESS;
}

void mvb_UpdateDescriptorSets(VkDevice,
                              uint32_t writeCount,
                              const VkWriteDescriptorSet* pWrites,
                              uint32_t copyCount,
                              const VkCopyDescriptorSet* pCopies) {
    for (uint32_t i = 0; i < writeCount; ++i) {
        auto* ds = toMv(pWrites[i].dstSet);
        if (!ds) continue;

        for (uint32_t d = 0; d < pWrites[i].descriptorCount; ++d) {
            const uint32_t binding = pWrites[i].dstBinding;
            const uint32_t arrayIndex = pWrites[i].dstArrayElement + d;
            auto& descriptor = findOrInsert(ds, binding, arrayIndex);
            descriptor.descriptorType = pWrites[i].descriptorType;
            descriptor.resource = nullptr;
            descriptor.samplerResource = nullptr;
            descriptor.offset = 0;
            descriptor.range = 0;

            switch (pWrites[i].descriptorType) {
                case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
                case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
                case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
                    if (pWrites[i].pBufferInfo) {
                        descriptor.resource = toMv(pWrites[i].pBufferInfo[d].buffer);
                        descriptor.offset = pWrites[i].pBufferInfo[d].offset;
                        descriptor.range = pWrites[i].pBufferInfo[d].range;
                    }
                    break;

                case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
                    if (pWrites[i].pImageInfo) {
                        descriptor.resource = toMv(pWrites[i].pImageInfo[d].imageView);
                        descriptor.samplerResource =
                            resolveSamplerResource(ds, binding, arrayIndex,
                                                   pWrites[i].pImageInfo[d].sampler);
                    }
                    break;

                case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
                case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
                    if (pWrites[i].pImageInfo) {
                        descriptor.resource = toMv(pWrites[i].pImageInfo[d].imageView);
                    }
                    break;

                case VK_DESCRIPTOR_TYPE_SAMPLER:
                    descriptor.samplerResource =
                        resolveSamplerResource(ds, binding, arrayIndex,
                                               pWrites[i].pImageInfo
                                                   ? pWrites[i].pImageInfo[d].sampler
                                                   : VK_NULL_HANDLE);
                    break;

                case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
                case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
                    if (pWrites[i].pTexelBufferView) {
                        descriptor.resource = toMv(pWrites[i].pTexelBufferView[d]);
                    }
                    break;

                default:
                    break;
            }
        }
    }

    for (uint32_t i = 0; i < copyCount && pCopies; ++i) {
        auto* srcSet = toMv(pCopies[i].srcSet);
        auto* dstSet = toMv(pCopies[i].dstSet);
        if (!srcSet || !dstSet) continue;

        for (uint32_t d = 0; d < pCopies[i].descriptorCount; ++d) {
            const uint32_t srcBinding = pCopies[i].srcBinding;
            const uint32_t srcArrayIndex = pCopies[i].srcArrayElement + d;
            const uint32_t dstBinding = pCopies[i].dstBinding;
            const uint32_t dstArrayIndex = pCopies[i].dstArrayElement + d;

            for (const auto& src : srcSet->bindings) {
                if (src.binding != srcBinding || src.arrayIndex != srcArrayIndex) continue;

                auto& dst = findOrInsert(dstSet, dstBinding, dstArrayIndex);
                dst.descriptorType = src.descriptorType;
                dst.resource = src.resource;
                dst.samplerResource = src.samplerResource;
                dst.offset = src.offset;
                dst.range = src.range;
                break;
            }
        }
    }
}

} // extern "C"
