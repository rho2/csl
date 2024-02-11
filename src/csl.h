#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define GET_NTH_ARG(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, N, ...) N
constexpr int CSL_MAX_ARG_COUNT = 10;

#define TYPE_TAG(X) _Generic((X),   \
    uint8_t: TYPE_U8,               \
    uint32_t: TYPE_U32,             \
    int32_t : TYPE_I32,             \
    float:    TYPE_F32,             \
    const char *: TYPE_CSTRING,     \
    char *: TYPE_CSTRING            \
)

typedef enum: uint8_t {
    TYPE_U8,
    TYPE_U32,
    TYPE_I32,
    TYPE_F32,
    TYPE_CSTRING,
    TYPE_COUNT
} DataType;

typedef union {
    int32_t val_int;
    uint32_t val_uint;
    uint8_t val_uint8;
    float val_float;
    const char * val_cstring;
} LoggingValueU;

static inline LoggingValueU logging_value_i32(int32_t v)            { return (LoggingValueU) {.val_int = v}; }
static inline LoggingValueU logging_value_u32(uint32_t v)           { return (LoggingValueU) {.val_uint = v}; }
static inline LoggingValueU logging_value_u8(uint8_t v)             { return (LoggingValueU) {.val_uint8 = v}; }
static inline LoggingValueU logging_value_float(float v)            { return (LoggingValueU) {.val_float = v}; }
static inline LoggingValueU logging_value_cstring(const char *v)    { return (LoggingValueU) {.val_cstring = v}; }

#define LOGGING_VALUE_G(X) _Generic((X),    \
    uint8_t: logging_value_u8,              \
    uint32_t: logging_value_u32,            \
    int32_t : logging_value_i32,            \
    float:    logging_value_float,          \
    const char *: logging_value_cstring,    \
    char *: logging_value_cstring           \
) ((X))

typedef enum: uint8_t {
    LL_TRACE,
    LL_DEBUG,
    LL_INFO,
    LL_WARNING,
    LL_ERROR,
    LL_CRITICAL,
    LL_FATAL,
    LL_COUNT,
} LogLevel;

#define SV(X) {.byte_count = sizeof(X) - 1, .data = "" X}
typedef struct {
    size_t byte_count;
    const char *data;
} StringView;

typedef struct {
    size_t byte_count;
    char *data;
} MemoryView;

#define LOGGING_HEADER_MAGIC_NUMBER {'[', 'C', '#', 'S', '%', 'L', '*', ']'}
typedef struct {
    char MARKER[8];
    StringView fmt_str;

    size_t arg_count;
    DataType types[CSL_MAX_ARG_COUNT];

    StringView filename;
    StringView function;
    int line;

    int32_t  id;

    LogLevel level;
    char category;
} LogHeader;

constexpr uint32_t LOGGING_FILE_HEADER_MAGIC_NUMBER = 0x43534c4c;
constexpr int32_t LOGGING_FILE_HEADER_VERSION_NUMBER = 1;
constexpr int LOGGING_FILE_HEADER_RESERVED_COUNT = 24;

int args_find_position(const char *name, int argc, char **argv);
const char* args_get_value(const char *name, int argc, char **argv);

size_t read_binary_u8(uint8_t *v,       FILE *f);
size_t read_binary_i32(int32_t *v,      FILE *f);
size_t read_binary_u32(uint32_t *v,     FILE *f);
size_t read_binary_f32(float *v,        FILE *f);
size_t read_binary_cstring(char **v,    FILE *f);
size_t read_binary_logging_value(LoggingValueU *v, DataType type, FILE *f);

void write_binary_u8(uint8_t v,             FILE *f);
void write_binary_i32(int32_t v,            FILE *f);
void write_binary_u32(uint32_t v,           FILE *f);
void write_binary_f32(float v,              FILE *f);
void write_binary_cstring(const char *v,    FILE *f);
//void write_binary_logging_value(LoggingValueU *v, DataType type, FILE *f);

extern const StringView LOG_LEVEL_NAMES[];
extern const char LOG_LEVEL_NAMES_SHORT[];
extern const StringView DATA_TYPE_NAMES[];


#define LOG(FMT, LVL, ...)                                                              \
do {                                                                                    \
LogHeader* h_tmp = &(static LogHeader) {                                                \
    .MARKER = LOGGING_HEADER_MAGIC_NUMBER,                                              \
    .fmt_str = SV(FMT),                                                                 \
    .arg_count = GET_NTH_ARG(__VA_OPT__(,) __VA_ARGS__, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0),  \
    .types = {CALL_MACRO_X_FOR_EACH(TYPE_TAG __VA_OPT__(,) __VA_ARGS__)},               \
    .filename = SV(__FILE__),                                                           \
    .function = {.byte_count = sizeof(__func__) - 1, .data = __func__},                 \
    .line = __LINE__,                                                                   \
    .level = LVL,                                                                       \
    .id = 0\
    \
};                                                                                              \
csl_log_call(                                                                                       \
    h_tmp,                                                                                      \
    (LoggingValueU[]) { CALL_MACRO_X_FOR_EACH(LOGGING_VALUE_G __VA_OPT__(,) __VA_ARGS__) }      \
);                                                                                              \
} while(0)

#define CALL_MACRO_X_FOR_EACH(x, ...) \
    GET_NTH_ARG("", ##__VA_ARGS__, \
    _fe_9, _fe_8, _fe_7, _fe_6, _fe_5, _fe_4, _fe_3, _fe_2, _fe_1, _fe_0)(x, ##__VA_ARGS__)

#define _fe_0(_call)
#define _fe_1(_call, x) _call(x)
#define _fe_2(_call, x, ...) _call((x)), _fe_1(_call, __VA_ARGS__)
#define _fe_3(_call, x, ...) _call((x)), _fe_2(_call, __VA_ARGS__)
#define _fe_4(_call, x, ...) _call((x)), _fe_3(_call, __VA_ARGS__)
#define _fe_5(_call, x, ...) _call((x)), _fe_4(_call, __VA_ARGS__)
#define _fe_6(_call, x, ...) _call((x)), _fe_5(_call, __VA_ARGS__)
#define _fe_7(_call, x, ...) _call((x)), _fe_6(_call, __VA_ARGS__)
#define _fe_8(_call, x, ...) _call((x)), _fe_7(_call, __VA_ARGS__)
#define _fe_9(_call, x, ...) _call((x)), _fe_8(_call, __VA_ARGS__)

void csl_easy_init(const char *filename, LogLevel level);
void csl_easy_end();
void csl_log_call(const LogHeader *header, LoggingValueU *values);
