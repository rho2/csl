#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>

#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>

#include "csl.h"

static LogHeader SentinelLogHeader = {
        .MARKER = LOGGING_HEADER_MAGIC_NUMBER,
        .category = '~',
};

uint32_t get_current_time_ms() {
    struct timeval ts;
    gettimeofday(&ts, NULL);
    return (uint32_t) (ts.tv_sec * 1000 + ts.tv_usec / 1000);
}

static inline int32_t get_logging_id(const LogHeader *header) {
    // NOTE: maybe choose a bigger data type here, int32_t might not be able to hold every offset
    if (header->id != 0) return header->id;
    return (int32_t)((char*)header - (char*)&SentinelLogHeader);
}

typedef struct {
    FILE *logfile;
    LogLevel level;
    LogLevel flush_level;
} Logger;

static Logger GLOBAL_LOGGER = {
    .level = LL_INFO,
    .flush_level = LL_INFO,
};

char build_id_end __attribute__((section(".note.gnu.build-id#"))) = '!';

void csl_easy_init(const char *filename, LogLevel level) {
    GLOBAL_LOGGER.logfile = fopen(filename, "wb");
    write_binary_u32(LOGGING_FILE_HEADER_MAGIC_NUMBER,GLOBAL_LOGGER.logfile);
    write_binary_u32(LOGGING_FILE_HEADER_VERSION_NUMBER,GLOBAL_LOGGER.logfile);

    // Would be better to open own file here and parse build id similar to the log reader
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
    fwrite(&build_id_end -20, 1, 20, GLOBAL_LOGGER.logfile);
#pragma GCC diagnostic pop
    // Pad build_id to 32 bytes
    for (int i = 0; i < 12; ++i) {
        write_binary_u8(0, GLOBAL_LOGGER.logfile);
    }

    for (int i = 0; i < LOGGING_FILE_HEADER_RESERVED_COUNT; ++i) {
        write_binary_u8(0, GLOBAL_LOGGER.logfile);
    }
    GLOBAL_LOGGER.level = level;
    GLOBAL_LOGGER.flush_level = LL_TRACE;
}

void csl_easy_end() {
    fclose(GLOBAL_LOGGER.logfile);
}

void csl_log_call(const LogHeader *header, LoggingValueU *values) {
    Logger *logger = &GLOBAL_LOGGER;

    if (header->level < logger->level) return;

    int32_t logging_id = get_logging_id(header);
    uint32_t timestamp = (int)get_current_time_ms();

    write_binary_i32(logging_id, logger->logfile);
    write_binary_u32(timestamp, logger->logfile);

    for (size_t i = 0; i < header->arg_count; ++i) {
        switch (header->types[i]) {
            case TYPE_I32:
                write_binary_i32(values[i].val_int, logger->logfile);
                break;
            case TYPE_F32:
                write_binary_f32(values[i].val_float, logger->logfile);
                break;
            case TYPE_CSTRING:
                write_binary_cstring(values[i].val_cstring, logger->logfile);
                break;
            case TYPE_U8:
                write_binary_u8(values[i].val_uint8, logger->logfile);
                break;
            case TYPE_U32:
                write_binary_u32(values[i].val_uint, logger->logfile);
                break;
            case TYPE_COUNT:
                unreachable();
        }
    }

    if (header->level >= logger->flush_level)
        fflush(logger->logfile);
}


