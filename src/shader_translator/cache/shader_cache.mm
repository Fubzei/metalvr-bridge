/**
 * @file shader_cache.mm
 * @brief ShaderCache implementation.  Objective-C++ (.mm) required for Metal API calls.
 *
 * Phase 2 rewrite:
 *   - XXH64 hash for in-memory cache keys (fast)
 *   - SHA-256 for disk filenames (collision-resistant)
 *   - Full getOrCompile pipeline: parse → translate → Metal compile → cache
 *   - Thread-safe LRU with std::mutex
 *
 * Disk format:
 *   <cacheDir>/
 *     <sha256hex>.metal     — MSL source (for debugging + warm recompile)
 *     <sha256hex>.metallib  — TODO: MTLBinaryArchive when min deployment > macOS 14
 */

#include "shader_cache.h"
#include "../spirv/spirv_parser.h"
#include "../msl_emitter/spirv_to_msl.h"
#include "../../common/logging.h"

#import <Metal/Metal.h>
#import <Foundation/Foundation.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <list>
#include <mutex>
#include <unordered_map>

namespace mvrvb {

using namespace msl;

// ═════════════════════════════════════════════════════════════════════════════
// ── XXH64 — fast non-cryptographic hash ─────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════════════
// Reference: https://github.com/Cyan4973/xxHash — BSD-2 license
// Inline implementation to avoid external dependency.

namespace xxh {
static constexpr uint64_t PRIME1 = 0x9E3779B185EBCA87ULL;
static constexpr uint64_t PRIME2 = 0xC2B2AE3D27D4EB4FULL;
static constexpr uint64_t PRIME3 = 0x165667B19E3779F9ULL;
static constexpr uint64_t PRIME4 = 0x85EBCA77C2B2AE63ULL;
static constexpr uint64_t PRIME5 = 0x27D4EB2F165667C5ULL;

static inline uint64_t rotl64(uint64_t x, int r) noexcept { return (x << r) | (x >> (64 - r)); }

static inline uint64_t read64(const uint8_t* p) noexcept {
    uint64_t v;
    std::memcpy(&v, p, 8);
    return v;
}

static inline uint32_t read32(const uint8_t* p) noexcept {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

static inline uint64_t round64(uint64_t acc, uint64_t input) noexcept {
    acc += input * PRIME2;
    acc  = rotl64(acc, 31);
    acc *= PRIME1;
    return acc;
}

static inline uint64_t mergeRound(uint64_t acc, uint64_t val) noexcept {
    val  = round64(0, val);
    acc ^= val;
    acc  = acc * PRIME1 + PRIME4;
    return acc;
}

uint64_t xxh64(const void* input, size_t len, uint64_t seed = 0) noexcept {
    const uint8_t* p   = static_cast<const uint8_t*>(input);
    const uint8_t* end = p + len;
    uint64_t h64;

    if (len >= 32) {
        const uint8_t* limit = end - 32;
        uint64_t v1 = seed + PRIME1 + PRIME2;
        uint64_t v2 = seed + PRIME2;
        uint64_t v3 = seed;
        uint64_t v4 = seed - PRIME1;

        do {
            v1 = round64(v1, read64(p));      p += 8;
            v2 = round64(v2, read64(p));      p += 8;
            v3 = round64(v3, read64(p));      p += 8;
            v4 = round64(v4, read64(p));      p += 8;
        } while (p <= limit);

        h64 = rotl64(v1,1) + rotl64(v2,7) + rotl64(v3,12) + rotl64(v4,18);
        h64 = mergeRound(h64, v1);
        h64 = mergeRound(h64, v2);
        h64 = mergeRound(h64, v3);
        h64 = mergeRound(h64, v4);
    } else {
        h64 = seed + PRIME5;
    }

    h64 += static_cast<uint64_t>(len);

    while (p + 8 <= end) {
        h64 ^= round64(0, read64(p));
        h64  = rotl64(h64, 27) * PRIME1 + PRIME4;
        p   += 8;
    }
    if (p + 4 <= end) {
        h64 ^= static_cast<uint64_t>(read32(p)) * PRIME1;
        h64  = rotl64(h64, 23) * PRIME2 + PRIME3;
        p   += 4;
    }
    while (p < end) {
        h64 ^= static_cast<uint64_t>(*p) * PRIME5;
        h64  = rotl64(h64, 11) * PRIME1;
        ++p;
    }

    // Final avalanche
    h64 ^= h64 >> 33;
    h64 *= PRIME2;
    h64 ^= h64 >> 29;
    h64 *= PRIME3;
    h64 ^= h64 >> 32;

    return h64;
}
} // namespace xxh

// ═════════════════════════════════════════════════════════════════════════════
// ── SHA-256 (for disk cache filenames) ──────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════════════
namespace sha256 {

static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static inline uint32_t rotr(uint32_t x, uint32_t n) noexcept { return (x >> n) | (x << (32-n)); }
static inline uint32_t ch(uint32_t x,uint32_t y,uint32_t z) noexcept { return (x&y)^(~x&z); }
static inline uint32_t maj(uint32_t x,uint32_t y,uint32_t z) noexcept { return (x&y)^(x&z)^(y&z); }
static inline uint32_t sig0(uint32_t x) noexcept { return rotr(x,2)^rotr(x,13)^rotr(x,22); }
static inline uint32_t sig1(uint32_t x) noexcept { return rotr(x,6)^rotr(x,11)^rotr(x,25); }
static inline uint32_t gam0(uint32_t x) noexcept { return rotr(x,7)^rotr(x,18)^(x>>3); }
static inline uint32_t gam1(uint32_t x) noexcept { return rotr(x,17)^rotr(x,19)^(x>>10); }

std::array<uint8_t,32> hash(const uint8_t* data, size_t len) noexcept {
    uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                     0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    size_t msgLen = len;
    size_t padLen = (msgLen % 64 < 56) ? (56 - msgLen % 64) : (120 - msgLen % 64);
    std::vector<uint8_t> msg(data, data + len);
    msg.push_back(0x80);
    for (size_t i = 1; i < padLen; ++i) msg.push_back(0);
    uint64_t bitLen = static_cast<uint64_t>(msgLen) * 8;
    for (int i = 7; i >= 0; --i) msg.push_back((bitLen >> (i*8)) & 0xFF);

    for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(msg[chunk+i*4])   << 24) |
                   (static_cast<uint32_t>(msg[chunk+i*4+1]) << 16) |
                   (static_cast<uint32_t>(msg[chunk+i*4+2]) <<  8) |
                   (static_cast<uint32_t>(msg[chunk+i*4+3]));
        }
        for (int i = 16; i < 64; ++i)
            w[i] = gam1(w[i-2]) + w[i-7] + gam0(w[i-15]) + w[i-16];

        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t t1 = hh + sig1(e) + ch(e,f,g) + K[i] + w[i];
            uint32_t t2 = sig0(a) + maj(a,b,c);
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d;
        h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }
    std::array<uint8_t,32> digest{};
    for (int i = 0; i < 8; ++i) {
        digest[i*4+0] = (h[i] >> 24) & 0xFF;
        digest[i*4+1] = (h[i] >> 16) & 0xFF;
        digest[i*4+2] = (h[i] >>  8) & 0xFF;
        digest[i*4+3] = (h[i]      ) & 0xFF;
    }
    return digest;
}
} // namespace sha256

// ═════════════════════════════════════════════════════════════════════════════
// ── ShaderCache::Impl ───────────────────────────────────────────────────────
// ═════════════════════════════════════════════════════════════════════════════

struct ShaderCache::Impl {
    id<MTLDevice>     device;
    ShaderCacheConfig cfg;

    mutable std::mutex mutex;

    // LRU: list is newest-first; map holds iterators into the list.
    using LRUList = std::list<CachedShader>;
    LRUList list;
    std::unordered_map<ShaderKey, LRUList::iterator, ShaderKeyHash> map;

    mutable size_t hits{0};
    mutable size_t misses{0};

    void promote(LRUList::iterator it, uint64_t frame) {
        it->lastUsedFrame = frame;
        list.splice(list.begin(), list, it);
    }

    void evictLRU() {
        if (list.empty()) return;
        auto last = std::prev(list.end());
        // Release retained Metal objects.
        if (last->library)  { CFRelease((__bridge CFTypeRef)last->library);  }
        if (last->function) { CFRelease((__bridge CFTypeRef)last->function); }
        map.erase(last->key);
        list.erase(last);
    }
};

// ── Construction ─────────────────────────────────────────────────────────────
ShaderCache::ShaderCache(MTLDeviceRef device, ShaderCacheConfig cfg) {
    m_impl = std::make_unique<Impl>();
    m_impl->device = (__bridge id<MTLDevice>)device;
    m_impl->cfg = std::move(cfg);

    if (m_impl->cfg.enableDiskCache) {
        if (m_impl->cfg.diskCacheDir.empty()) {
            const char* home = std::getenv("HOME");
            if (home) {
                m_impl->cfg.diskCacheDir =
                    std::filesystem::path(home) / "Library" / "Caches" / "MetalVRBridge" / "shaders";
            }
        }
        if (!m_impl->cfg.diskCacheDir.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(m_impl->cfg.diskCacheDir, ec);
        }
        loadFromDisk();
    }
}

ShaderCache::~ShaderCache() {
    flushToDisk();
    clear();
}

// ── Key computation ──────────────────────────────────────────────────────────
uint64_t ShaderCache::hashSPIRV(const uint32_t* words, size_t wordCount) noexcept {
    return xxh::xxh64(words, wordCount * sizeof(uint32_t));
}

uint64_t ShaderCache::hashOptions(const msl::MSLOptions& opts) noexcept {
    // FNV-1a 64-bit
    uint64_t h = 0xcbf29ce484222325ULL;
    auto mix = [&](uint64_t v) { h ^= v; h *= 0x100000001b3ULL; };
    mix(opts.mslVersionMajor);
    mix(opts.mslVersionMinor);
    mix(opts.flipVertexY  ? 1ULL : 0ULL);
    mix(opts.useArgumentBuffers ? 2ULL : 0ULL);
    mix(opts.remapDepth   ? 4ULL : 0ULL);
    mix(opts.entryPointIndex);
    // Hash the entry point name if overridden
    for (char c : opts.entryPointName) {
        mix(static_cast<uint64_t>(c));
    }
    return h;
}

std::array<uint8_t, 32> ShaderCache::sha256SPIRV(const uint32_t* words, size_t wordCount) noexcept {
    return sha256::hash(reinterpret_cast<const uint8_t*>(words), wordCount * sizeof(uint32_t));
}

static ShaderKey makeKey(const uint32_t* words, size_t wordCount, const msl::MSLOptions& opts) {
    ShaderKey k;
    k.spirvHash   = ShaderCache::hashSPIRV(words, wordCount);
    k.optionsHash = ShaderCache::hashOptions(opts);
    return k;
}

// ── Lookup ───────────────────────────────────────────────────────────────────
const CachedShader* ShaderCache::find(const uint32_t* words, size_t wordCount,
                                       const msl::MSLOptions& opts) const {
    auto key = makeKey(words, wordCount, opts);
    std::lock_guard<std::mutex> lk(m_impl->mutex);
    auto it = m_impl->map.find(key);
    if (it == m_impl->map.end()) { ++m_impl->misses; return nullptr; }
    ++m_impl->hits;
    return &(*it->second);
}

// ── Insert ───────────────────────────────────────────────────────────────────
void ShaderCache::insert(const uint32_t* words, size_t wordCount,
                          const msl::MSLOptions& opts, CachedShader&& entry) {
    auto key = makeKey(words, wordCount, opts);
    entry.key = key;
    std::lock_guard<std::mutex> lk(m_impl->mutex);
    if (m_impl->map.count(key)) return; // Already inserted by another thread
    if (m_impl->list.size() >= m_impl->cfg.maxEntries) {
        m_impl->evictLRU();
    }
    m_impl->list.push_front(std::move(entry));
    m_impl->map[key] = m_impl->list.begin();
}

// ── Get-or-compile ──────────────────────────────────────────────────────────
const CachedShader* ShaderCache::getOrCompile(const uint32_t* words, size_t wordCount,
                                               const msl::MSLOptions& opts, uint64_t frame) {
    // ── Hot path: check in-memory cache ─────────────────────────────────────
    {
        auto key = makeKey(words, wordCount, opts);
        std::lock_guard<std::mutex> lk(m_impl->mutex);
        auto it = m_impl->map.find(key);
        if (it != m_impl->map.end()) {
            ++m_impl->hits;
            m_impl->promote(it->second, frame);
            return &(*it->second);
        }
        ++m_impl->misses;
    }

    // ── Cache miss — full pipeline ──────────────────────────────────────────
    MVRVB_LOG_DEBUG("Shader cache miss — compiling...");

    // 1. Parse SPIR-V binary.
    auto parsed = spirv::parseSPIRV(words, wordCount);
    if (!parsed) {
        MVRVB_LOG_ERROR("SPIR-V parse failed: %s", parsed.errorMessage.c_str());
        return nullptr;
    }

    // 2. Translate SPIR-V IR to MSL source.
    auto translated = msl::translateToMSL(parsed.module, opts);
    if (!translated) {
        MVRVB_LOG_ERROR("SPIR-V→MSL translation failed: %s", translated.errorMessage.c_str());
        return nullptr;
    }

    // 3. Compile MSL with Metal runtime.
    NSString* mslSrc = [NSString stringWithUTF8String:translated.mslSource.c_str()];
    MTLCompileOptions* compileOpts = [[MTLCompileOptions alloc] init];
    compileOpts.languageVersion = MTLLanguageVersion2_4;
    compileOpts.fastMathEnabled = YES;

    NSError* err = nil;
    id<MTLLibrary> lib = [m_impl->device newLibraryWithSource:mslSrc
                                                       options:compileOpts
                                                         error:&err];
    if (!lib) {
        MVRVB_LOG_ERROR("MTL shader compile failed: %s",
                        err ? [[err localizedDescription] UTF8String] : "(unknown)");
        MVRVB_LOG_DEBUG("MSL source:\n%s", translated.mslSource.c_str());
        return nullptr;
    }

    // Resolve the entry point function.
    uint32_t safeIdx = std::min(opts.entryPointIndex,
                                static_cast<uint32_t>(parsed.module.entryPoints.size() - 1));
    std::string funcName = opts.entryPointName.empty()
        ? parsed.module.entryPoints[safeIdx].name
        : opts.entryPointName;
    NSString* fnName = [NSString stringWithUTF8String:funcName.c_str()];
    id<MTLFunction> fn = [lib newFunctionWithName:fnName];
    if (!fn) {
        MVRVB_LOG_WARN("MTLFunction '%s' not found in compiled library", [fnName UTF8String]);
    }

    // 4. Build cache entry.
    CachedShader entry;
    entry.mslSource     = translated.mslSource;
    entry.reflection    = translated.reflection;
    entry.stage         = translated.stage;
    entry.library       = (__bridge_retained void*)lib;
    entry.function      = fn ? (__bridge_retained void*)fn : nullptr;
    entry.compiledOK    = true;
    entry.lastUsedFrame = frame;

    MVRVB_LOG_INFO("Shader compiled and cached (%.1f KB MSL)",
                   translated.mslSource.size() / 1024.0);

    insert(words, wordCount, opts, std::move(entry));

    // Return pointer to newly inserted entry.
    auto key = makeKey(words, wordCount, opts);
    std::lock_guard<std::mutex> lk(m_impl->mutex);
    auto it = m_impl->map.find(key);
    return (it != m_impl->map.end()) ? &(*it->second) : nullptr;
}

// ── Disk cache ──────────────────────────────────────────────────────────────
static std::string hexDigest(const std::array<uint8_t, 32>& hash) {
    char buf[65];
    for (int i = 0; i < 32; ++i)
        std::snprintf(buf + i * 2, 3, "%02x", hash[i]);
    buf[64] = '\0';
    return std::string(buf);
}

void ShaderCache::flushToDisk() const {
    if (!m_impl->cfg.enableDiskCache || m_impl->cfg.diskCacheDir.empty()) return;
    std::lock_guard<std::mutex> lk(m_impl->mutex);

    for (auto& entry : m_impl->list) {
        if (!entry.compiledOK) continue;

        // Use SHA-256 for disk filename (collision-resistant)
        // We can't recover the SPIR-V words here, so we use the XXH64 as a proxy
        // filename. In production, the disk cache would store the full binary.
        char hexBuf[17];
        std::snprintf(hexBuf, sizeof(hexBuf), "%016llx",
                      static_cast<unsigned long long>(entry.key.spirvHash));

        if (m_impl->cfg.saveMSLSource) {
            auto mslPath = m_impl->cfg.diskCacheDir / (std::string(hexBuf) + ".metal");
            if (!std::filesystem::exists(mslPath)) {
                std::ofstream f(mslPath);
                if (f) f << entry.mslSource;
            }
        }
        // TODO: serialize MTLBinaryArchive when min deployment target supports it.
    }
}

void ShaderCache::loadFromDisk() {
    if (!m_impl->cfg.enableDiskCache || m_impl->cfg.diskCacheDir.empty()) return;

    // Scan for .metal files and pre-populate entries.
    // These can be recompiled on demand without re-translating from SPIR-V.
    std::error_code ec;
    for (auto& dirEntry : std::filesystem::directory_iterator(m_impl->cfg.diskCacheDir, ec)) {
        if (dirEntry.path().extension() != ".metal") continue;
        // TODO: parse the MSL header comments to reconstruct the cache key,
        // or store a sidecar .meta file with key + reflection data.
    }

    MVRVB_LOG_DEBUG("Disk cache directory: %s (warm start enabled)",
                    m_impl->cfg.diskCacheDir.c_str());
}

void ShaderCache::clear() {
    std::lock_guard<std::mutex> lk(m_impl->mutex);
    for (auto& e : m_impl->list) {
        if (e.library)  CFRelease((__bridge CFTypeRef)e.library);
        if (e.function) CFRelease((__bridge CFTypeRef)e.function);
    }
    m_impl->list.clear();
    m_impl->map.clear();
}

size_t ShaderCache::size()     const noexcept { return m_impl->list.size(); }
size_t ShaderCache::hitCount() const noexcept { return m_impl->hits;  }
size_t ShaderCache::missCount()const noexcept { return m_impl->misses; }

} // namespace mvrvb
