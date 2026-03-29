#pragma once
/**
 * @file vk_device.h
 * @brief VkInstance, VkPhysicalDevice, and VkDevice wrappers over MTLDevice.
 *
 * Object graph:
 *
 *   MvInstance
 *     └── MvPhysicalDevice   (one per MTLDevice on the system)
 *           └── MvDevice     (logical device created by the app)
 *                 ├── MvQueue×N
 *                 ├── ShaderCache
 *                 ├── MemoryManager
 *                 └── CommandPool×(per-thread)
 *
 * Dispatchable handle layout (loader ABI requirement):
 *   The first field MUST be a pointer to the loader dispatch table.
 *   We put a dummy pointer there; the loader fills it in.
 */

#include <vulkan/vulkan.h>
#include <memory>
#include <vector>
#include <string>

// Forward declarations for Objective-C types.
#ifdef __OBJC__
@protocol MTLDevice;
@protocol MTLCommandQueue;
#else
using id_MTLDevice = void*;
using id_MTLCommandQueue = void*;
#endif

namespace mvrvb {

class MemoryManager;
class ShaderCache;

// ── Loader ABI: first member must be void* ────────────────────────────────────
struct LoaderDispatch { void* loaderTable{nullptr}; };

// ── VkQueue wrapper ───────────────────────────────────────────────────────────
struct MvQueue : LoaderDispatch {
#ifdef __OBJC__
    id<MTLCommandQueue> queue{nullptr};
#else
    void* queue{nullptr};
#endif
    uint32_t familyIndex{0};
    uint32_t queueIndex{0};
};

// ── VkDevice wrapper ──────────────────────────────────────────────────────────
struct MvDevice : LoaderDispatch {
#ifdef __OBJC__
    id<MTLDevice>       mtlDevice{nullptr};
    id<MTLCommandQueue> commandQueue{nullptr};
    id<MTLCommandQueue> blitQueue{nullptr};
    id<MTLCommandQueue> computeQueue{nullptr};
#else
    void* mtlDevice{nullptr};
    void* commandQueue{nullptr};
    void* blitQueue{nullptr};
    void* computeQueue{nullptr};
#endif
    std::vector<MvQueue*>   queues;
    std::unique_ptr<MemoryManager> memManager;
    std::unique_ptr<ShaderCache>   shaderCache;

    bool hasUnifiedMemory{true};      ///< Apple Silicon = true
    bool supportsBC{true};            ///< BC compression (macOS = true)
    bool supportsMeshShaders{false};  ///< Metal 3+
    uint64_t maxBufferLength{};
    uint32_t maxTextureSize{16384};
    uint32_t maxComputeWorkgroupSize{1024};

    ~MvDevice();
};

// ── VkPhysicalDevice wrapper ──────────────────────────────────────────────────
struct MvPhysicalDevice : LoaderDispatch {
#ifdef __OBJC__
    id<MTLDevice> mtlDevice{nullptr};
#else
    void* mtlDevice{nullptr};
#endif
    VkPhysicalDeviceProperties   properties{};
    VkPhysicalDeviceFeatures     features{};
    VkPhysicalDeviceMemoryProperties memProperties{};

    void populateFromMTLDevice();
    void populateMemoryTypes();
};

// ── VkInstance wrapper ────────────────────────────────────────────────────────
struct MvInstance : LoaderDispatch {
    std::vector<MvPhysicalDevice*> physicalDevices;
    uint32_t apiVersionRequested{VK_API_VERSION_1_2};
    std::string appName;
    bool validationEnabled{false};

    MvInstance();
    ~MvInstance();
};

// ── Handle casts ─────────────────────────────────────────────────────────────
inline MvInstance*        toMv(VkInstance h)        { return reinterpret_cast<MvInstance*>(h);        }
inline MvPhysicalDevice*  toMv(VkPhysicalDevice h)  { return reinterpret_cast<MvPhysicalDevice*>(h);  }
inline MvDevice*          toMv(VkDevice h)          { return reinterpret_cast<MvDevice*>(h);          }
inline MvQueue*           toMv(VkQueue h)           { return reinterpret_cast<MvQueue*>(h);           }
inline VkInstance         toVk(MvInstance* p)       { return reinterpret_cast<VkInstance>(p);         }
inline VkPhysicalDevice   toVk(MvPhysicalDevice* p) { return reinterpret_cast<VkPhysicalDevice>(p);   }
inline VkDevice           toVk(MvDevice* p)         { return reinterpret_cast<VkDevice>(p);           }
inline VkQueue            toVk(MvQueue* p)          { return reinterpret_cast<VkQueue>(p);            }

} // namespace mvrvb
