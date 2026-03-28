#pragma once
/**
 * @file vr_settings.h
 * @brief IVRSettings_003 — key/value settings store backed by NSUserDefaults.
 */
#include <cstdint>
#include <string>

namespace mvrvb {

class MvVRSettings {
public:
    bool   getBool  (const char* section, const char* key, uint32_t* err = nullptr) const;
    int32_t getInt32(const char* section, const char* key, uint32_t* err = nullptr) const;
    float  getFloat (const char* section, const char* key, uint32_t* err = nullptr) const;
    void   getString(const char* section, const char* key, char* buf, uint32_t bufLen, uint32_t* err = nullptr) const;

    void setBool  (const char* section, const char* key, bool    val);
    void setInt32 (const char* section, const char* key, int32_t val);
    void setFloat (const char* section, const char* key, float   val);
    void setString(const char* section, const char* key, const char* val);

    void removeSection(const char* section);
    void removeKeyInSection(const char* section, const char* key);
};

} // namespace mvrvb
