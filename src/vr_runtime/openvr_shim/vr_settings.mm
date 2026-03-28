/**
 * @file vr_settings.mm
 * @brief IVRSettings backed by NSUserDefaults under "MetalVRBridge" suite.
 */
#include "vr_settings.h"
#import <Foundation/Foundation.h>
#include <cstring>

static NSUserDefaults* gDefaults() {
    static NSUserDefaults* d = [[NSUserDefaults alloc]
        initWithSuiteName:@"com.metalvrbridge.settings"];
    return d;
}

static NSString* key(const char* section, const char* k) {
    return [NSString stringWithFormat:@"%s/%s", section, k];
}

namespace mvrvb {

bool    MvVRSettings::getBool(const char* s, const char* k, uint32_t* e) const {
    if (e) *e = 0;
    return [gDefaults() boolForKey:key(s,k)];
}
int32_t MvVRSettings::getInt32(const char* s, const char* k, uint32_t* e) const {
    if (e) *e = 0;
    return (int32_t)[gDefaults() integerForKey:key(s,k)];
}
float   MvVRSettings::getFloat(const char* s, const char* k, uint32_t* e) const {
    if (e) *e = 0;
    return (float)[gDefaults() floatForKey:key(s,k)];
}
void    MvVRSettings::getString(const char* s, const char* k, char* buf, uint32_t len, uint32_t* e) const {
    if (e) *e = 0;
    NSString* v = [gDefaults() stringForKey:key(s,k)];
    if (buf && len > 0) {
        const char* c = v ? [v UTF8String] : "";
        std::strncpy(buf, c, len - 1); buf[len-1] = '\0';
    }
}

void MvVRSettings::setBool  (const char* s, const char* k, bool v)    { [gDefaults() setBool:v     forKey:key(s,k)]; }
void MvVRSettings::setInt32 (const char* s, const char* k, int32_t v) { [gDefaults() setInteger:v  forKey:key(s,k)]; }
void MvVRSettings::setFloat (const char* s, const char* k, float v)   { [gDefaults() setFloat:v    forKey:key(s,k)]; }
void MvVRSettings::setString(const char* s, const char* k, const char* v) {
    [gDefaults() setObject:[NSString stringWithUTF8String:v?v:""] forKey:key(s,k)];
}

void MvVRSettings::removeSection(const char* section) {
    NSDictionary* all = [gDefaults() dictionaryRepresentation];
    NSString* prefix = [NSString stringWithFormat:@"%s/", section];
    for (NSString* k in all.allKeys) {
        if ([k hasPrefix:prefix]) [gDefaults() removeObjectForKey:k];
    }
}

void MvVRSettings::removeKeyInSection(const char* section, const char* k) {
    [gDefaults() removeObjectForKey:key(section, k)];
}

} // namespace mvrvb
