#pragma once
/**
 * @file vk_descriptors.h
 * @brief Milestone 6 - descriptor set lifecycle.
 *
 * Descriptor replay itself lives in vk_commands.mm. This module owns the
 * descriptor layouts, pools, descriptor sets, and write/copy updates that the
 * replay engine consumes at draw and dispatch time.
 */

#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

VkResult mvb_CreateDescriptorSetLayout(VkDevice device,
                                       const VkDescriptorSetLayoutCreateInfo* pCreateInfo,
                                       const VkAllocationCallbacks* pAllocator,
                                       VkDescriptorSetLayout* pSetLayout);

void mvb_DestroyDescriptorSetLayout(VkDevice device,
                                    VkDescriptorSetLayout descriptorSetLayout,
                                    const VkAllocationCallbacks* pAllocator);

VkResult mvb_CreateDescriptorPool(VkDevice device,
                                  const VkDescriptorPoolCreateInfo* pCreateInfo,
                                  const VkAllocationCallbacks* pAllocator,
                                  VkDescriptorPool* pDescriptorPool);

void mvb_DestroyDescriptorPool(VkDevice device,
                               VkDescriptorPool descriptorPool,
                               const VkAllocationCallbacks* pAllocator);

VkResult mvb_ResetDescriptorPool(VkDevice device,
                                 VkDescriptorPool descriptorPool,
                                 VkDescriptorPoolResetFlags flags);

VkResult mvb_AllocateDescriptorSets(VkDevice device,
                                    const VkDescriptorSetAllocateInfo* pAllocateInfo,
                                    VkDescriptorSet* pDescriptorSets);

VkResult mvb_FreeDescriptorSets(VkDevice device,
                                VkDescriptorPool descriptorPool,
                                uint32_t descriptorSetCount,
                                const VkDescriptorSet* pDescriptorSets);

void mvb_UpdateDescriptorSets(VkDevice device,
                              uint32_t descriptorWriteCount,
                              const VkWriteDescriptorSet* pDescriptorWrites,
                              uint32_t descriptorCopyCount,
                              const VkCopyDescriptorSet* pDescriptorCopies);

#ifdef __cplusplus
} // extern "C"
#endif
