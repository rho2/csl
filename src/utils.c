#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "csl.h"

int args_find_position(const char *name, int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        if (strcmp(name, argv[i]) == 0) return i;
    }
    return -1;
}

const char* args_get_value(const char *name, int argc, char **argv) {
    int pos = args_find_position(name, argc - 1, argv);
    if (pos < 0)   return nullptr;
    return argv[pos + 1];
}

size_t read_binary_u8(uint8_t *v,   FILE *f)    { return fread(v, 1, sizeof *v,f); }
size_t read_binary_i32(int32_t *v,  FILE *f)    { return fread(v, 1, sizeof *v,f); }
size_t read_binary_u32(uint32_t *v, FILE *f)    { return fread(v, 1, sizeof *v,f); }
size_t read_binary_f32(float *v,    FILE *f)    { return fread(v, 1, sizeof *v,f); }

size_t read_binary_cstring(char ** v, FILE *f) {
    uint32_t length;
    size_t read = read_binary_u32( &length, f);

    if(read == 0) {
        *v = nullptr;
        return 0;
    }

    *v = malloc(length);

    // TODO: handle partial reads
    read += fread(*v, 1, length, f);

    return read;
}

size_t read_binary_logging_value(LoggingValueU *v, DataType type, FILE* f) {
    switch (type) {
        case TYPE_U8:
            return read_binary_u8(&v->val_uint8, f);
        case TYPE_U32:
            return read_binary_u32(&v->val_uint, f);
        case TYPE_I32:
            return read_binary_i32(&v->val_int, f);
        case TYPE_F32:
            return read_binary_f32(&v->val_float, f);
        case TYPE_CSTRING:
            //NOTE: It would be better to read the string into some kine of string pool
            // instead of this unsafe casting here
            return read_binary_cstring((char **)&v->val_cstring, f);
        case TYPE_COUNT:
            unreachable();
    }
    unreachable();
}

void write_binary_u8(uint8_t v,     FILE *f) { fwrite(&v, sizeof v, 1, f); }
void write_binary_i32(int32_t v,    FILE *f) { fwrite(&v, sizeof v, 1, f); }
void write_binary_u32(uint32_t v,   FILE *f) { fwrite(&v, sizeof v, 1, f); }
void write_binary_f32(float v,      FILE *f) { fwrite(&v, sizeof v, 1, f); }

void write_binary_cstring(const char * v, FILE *f) {
    size_t length = strlen(v) + 1;
    write_binary_u32(length, f);
    fwrite(v, sizeof(char), length, f);
}

const StringView DATA_TYPE_NAMES[] = {
        SV("u8"),
        SV("u32"),
        SV("i32"),
        SV("f32"),
        SV("cstring"),
};
static_assert((sizeof DATA_TYPE_NAMES) == sizeof(DATA_TYPE_NAMES[0]) * TYPE_COUNT);

const StringView LOG_LEVEL_NAMES[] = {
        SV("TRACE"),
        SV("DEBUG"),
        SV("INFO"),
        SV("WARNING"),
        SV("ERROR"),
        SV("CRITICAL"),
        SV("FATAL"),
};
static_assert(sizeof LOG_LEVEL_NAMES == sizeof(LOG_LEVEL_NAMES[0]) * LL_COUNT);

const char LOG_LEVEL_NAMES_SHORT[] = {
        'T',
        'D',
        'I',
        'W',
        'E',
        'C',
        'F',
};
static_assert(sizeof LOG_LEVEL_NAMES_SHORT == sizeof(LOG_LEVEL_NAMES_SHORT[0]) * LL_COUNT);
