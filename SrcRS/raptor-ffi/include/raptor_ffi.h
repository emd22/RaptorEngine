#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum RxLogLevel
{
    RX_LOG_PRINT = 0,
    RX_LOG_INFO = 1,
    RX_LOG_WARNING = 2,
    RX_LOG_ERROR = 3,
};

enum RxValueKind
{
    RX_KIND_NONE = 0,
    RX_KIND_INT = 1,
    RX_KIND_FLOAT = 2,
    RX_KIND_STRING = 3,
    RX_KIND_STRUCT = 4,
};

typedef struct RxHost
{
    void* user;
    int32_t (*read_include)(void* user, const char* path, const char* extension, uint8_t** data, size_t* length);
    void (*release_include)(void* user, uint8_t* data);
    void (*log)(void* user, int32_t level, int32_t category, const char* message, size_t length);
} RxHost;

typedef struct RxPrimitive
{
    int64_t int_value;
    const char* string_value;
    size_t string_length;
    float float_value;
    uint8_t kind;
} RxPrimitive;

typedef struct RxEntry
{
    const char* name;
    size_t name_length;
    RxPrimitive value;
    const struct RxEntry* members;
    size_t member_count;
    const RxPrimitive* array;
    size_t array_count;
    uint8_t is_array;
} RxEntry;

typedef struct RxConfig RxConfig;

RxConfig* rx_config_parse(const uint8_t* data, size_t length, const char* prelude_path, const char* include_extension,
                          const RxHost* host);
int32_t rx_config_has_errors(const RxConfig* config);
size_t rx_config_entry_count(const RxConfig* config);
const RxEntry* rx_config_entries(const RxConfig* config);
void rx_config_free(RxConfig* config);

#ifdef __cplusplus
}
#endif
