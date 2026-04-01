#pragma once
/**
 * @file shader_cache.h
 * @brief Thread-safe, LRU shader cache: SPIR-V hash → compiled MTLLibrary.
 *
 * Phase 2 rewrite:
 *   - XXH64 hashing (fast, non-cryptographic) for cache keys
 *   - SHA-256 still used for disk cache filenames (collision-resistant)
 *   - In-memory LRU cache of MTLFunction objects (hot path — zero compilation)
 *   - On-disk cache of MSL source + metallib (warm start)
 *   - Full getOrCompile() pipeline: SPIR-V → parse → MSL → compile → cache
 *
 * Cache key: XXH64 of SPIR-V bytecode combined with MSLOptions hash.
 * This ensures specialization constant changes produce different entries.
 *
 * All methods are thread-safe.
 */

#include "../msl_emitter/spirv_to_msl.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// Forward-declare Objective-C types to avoid pulling in Metal.h here.
#ifdef __OBJC__
#import <Metal/Metal.h>
using MTLLibraryRef   = id<MTLLibrary>;
using MTLFunctionRef  = id<MTLFunction>;
using MTLDeviceRef    = id<MTLDevice>;
#else
// Opaque type shims for pure-C++ headers.
using MTLLibraryRef   = void*;
using MTLFunctionRef  = void*;
using MTLDeviceRef    = void*;
#endif

namespace mvrvb {

// ── Cache key ────────────────────────────────────────────────────────────────
struct ShaderKey {
    uint64_t spirvHash{};     ///< XXH64 of SPIR-V binary
    uint64_t optionsHash{};   ///< FNV-1a of MSLOptions fields

    bool operator==(const ShaderKey& o) const noexcept {
        return spirvHash == o.spirvHash && optionsHash == o.optionsHash;
    }
};

struct ShaderKeyHash {
    size_t operator()(const ShaderKey& k) const noexcept {
        // Combine the two 64-bit hashes with a bit mix
        size_t h = k.spirvHash;
        h ^= k.optionsHash + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        return h;
    }
};

// ── Cached shader entry ──────────────────────────────────────────────────────
struct CachedShader {
    ShaderKey                key;
    std::string              mslSource;
    msl::MSLReflection       reflection;
    spirv::ShaderStage       stage{spirv::ShaderStage::Unknown};
    MTLLibraryRef            library{nullptr};   ///< Retained MTLLibrary*
    MTLFunctionRef           function{nullptr};  ///< Retained MTLFunction*
    uint64_t                 lastUsedFrame{0};
    bool                     compiledOK{false};
};

// ── Cache configuration ──────────────────────────────────────────────────────
struct ShaderCacheConfig {
    size_t maxEntries{512};              ///< LRU capacity
    std::filesystem::path diskCacheDir;  ///< Empty → no disk cache
    bool   enableDiskCache{true};
    bool   saveMSLSource{true};          ///< Save .metal files alongside binary archives
};

// ── Main cache ───────────────────────────────────────────────────────────────
class ShaderCache {
public:
    explicit ShaderCache(MTLDeviceRef device, ShaderCacheConfig cfg = {});
    ~ShaderCache();

    // Non-copyable, non-movable (holds mutex + ObjC resources)
    ShaderCache(const ShaderCache&) = delete;
    ShaderCache& operator=(const ShaderCache&) = delete;

    /**
     * @brief Look up a compiled shader by SPIR-V bytecode.
     *
     * Returns nullptr on cache miss. Does NOT compile.
     * Thread-safe.
     */
    const CachedShader* find(const uint32_t* spirvWords, size_t wordCount,
                              const msl::MSLOptions& options) const;

    /**
     * @brief Insert a fully translated + compiled shader into the cache.
     *
     * Takes ownership of library/function references.
     * Evicts the LRU entry if at capacity.
     * Thread-safe.
     */
    void insert(const uint32_t* spirvWords, size_t wordCount,
                const msl::MSLOptions& options,
                CachedShader&& entry);

    /**
     * @brief Full pipeline: translate SPIR-V → MSL → compile → cache.
     *
     * Checks the cache first; only compiles on miss.
     * This is the main entry point used by the Vulkan ICD.
     *
     * @param frame   Current frame number (for LRU timestamps).
     * @return Pointer to the cached entry, or nullptr on compilation failure.
     */
    const CachedShader* getOrCompile(const uint32_t* spirvWords, size_t wordCount,
                                      const msl::MSLOptions& options,
                                      uint64_t frame = 0);

    /// Save all compiled shaders to the disk cache.
    void flushToDisk() const;

    /// Load all pre-compiled entries from the disk cache.
    void loadFromDisk();

    /// Clear all in-memory entries (does not clear disk).
    void clear();

    [[nodiscard]] size_t size()     const noexcept;
    [[nodiscard]] size_t hitCount() const noexcept;
    [[nodiscard]] size_t missCount()const noexcept;

    /// Compute XXH64 hash of a SPIR-V binary (fast, non-cryptographic).
    static uint64_t hashSPIRV(const uint32_t* words, size_t wordCount) noexcept;

    /// Compute a hash of MSLOptions for cache key differentiation.
    static uint64_t hashOptions(const msl::MSLOptions& opts) noexcept;

    /// Compute SHA-256 hash (for disk cache filenames — collision-resistant).
    static std::array<uint8_t, 32> sha256SPIRV(const uint32_t* words, size_t wordCount) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mvrvb
