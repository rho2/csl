#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <assert.h>

#include <elf.h>

#ifdef SQLITE_AVAILABLE
#include <sqlite3.h>
#endif

#include "csl.h"

extern const StringView HTML_TABLE_START;
extern const StringView HTML_TABLE_END;

typedef struct {
    size_t size;
    size_t capacity;
    LogHeader **headers;
    uint32_t *ids;
    size_t sentinel_index;
} HeaderList;

void header_list_init(HeaderList *list) {
    list->sentinel_index = 0;
    list->size = 0;
    list->capacity = 1;
    list->headers = malloc(list->capacity * sizeof(list->headers[0]));
    list->ids = malloc(list->capacity * sizeof(list->ids[0]));
}

void header_list_append(HeaderList *list, LogHeader *header) {
    if (list->size == list->capacity) {
        list->capacity *= 2;

        LogHeader **new_headers = realloc(list->headers, list->capacity * sizeof(list->headers[0]));
        uint32_t *new_ids =  realloc(list->ids, list->capacity * sizeof(list->ids[0]));

        if (new_headers == nullptr || new_ids == nullptr) {
            printf("Unexpected allocation error\n");
            exit(EXIT_FAILURE);
        }

        list->headers = new_headers;
        list->ids = new_ids;
    }
    list->headers[list->size] = header;
    list->ids[list->size] = 0;

    if (header->category == '~') {
        list->sentinel_index = list->size;
    }

    list->size += 1;
}

uint32_t header_list_lookup_by_id(HeaderList *list, uint32_t id) {
    for (size_t i = 0; i < list->size; ++i) {
        if (list->ids[i] == id) return i;
    }
    return UINT32_MAX;
}

void header_list_free(HeaderList *list) {
    free(list->headers);
    free(list->ids);

    list->headers = nullptr;
    list->ids = nullptr;

    list->size = 0;
    list->capacity = 0;
    list->sentinel_index = 0;
}

void header_list_fill_ids(HeaderList *list) {
    LogHeader *sentinel = list->headers[list->sentinel_index];

    for (size_t i = 0; i < list->size; ++i) {
        list->ids[i] = (int32_t)((char *) list->headers[i] - (char *)sentinel);
    }
}

static void fix_string(const char **broken_string, const char *file_content) {
    intptr_t offset = (intptr_t)*broken_string;

    if (offset == 0) *broken_string = nullptr;
    *broken_string = file_content + offset;
}

void header_list_fix_string(HeaderList *list, const char * file_content) {
    for (size_t i = 0; i < list->size; ++i) {
        LogHeader *h = list->headers[i];
        fix_string(&h->fmt_str.data, file_content);
        fix_string(&h->filename.data, file_content);
        fix_string(&h->function.data, file_content);
    }
}

typedef struct {
    union {
        FILE *f;
#ifdef SQLITE_AVAILABLE
        sqlite3 *db;
#endif
    };
    const char * filename;
    size_t msg_count;
} FileFormatter;

void init_formatter_file(FileFormatter *fmt, const char *default_filename, const char *modes) {
    if (fmt->filename == nullptr)   fmt->filename = default_filename;
    fmt->f = fopen(fmt->filename, modes);
}

void deinit_formatter_file(FileFormatter *fmt) {
    fclose(fmt->f);
}

void init_formatter_json(FileFormatter *fmt) {
    init_formatter_file(fmt, "log.json", "w");
    fprintf(fmt->f, "{\n");
    fprintf(fmt->f, "  \"messages\": [\n");
}

void deinit_formatter_json(FileFormatter *fmt) {
    fprintf(fmt->f, "\n  ]\n}\n");
    deinit_formatter_file(fmt);
}

void handle_message_json(FileFormatter *fmt, LogHeader *header, int32_t id, uint32_t timestamp, LoggingValueU *values) {
    if (fmt->msg_count != 0 ){
        fputs(",\n", fmt->f);
    }

    fprintf(fmt->f, "    {\n");
    fprintf(fmt->f, "      \"fmt_str\": \"%s\",\n", header->fmt_str.data); //TODO: escape
    fprintf(fmt->f, "      \"id\": %d,\n", id);
    fprintf(fmt->f, "      \"timestamp\": %u,\n", timestamp);
    fprintf(fmt->f, "      \"level\": {\n");
    fprintf(fmt->f, "        \"name\": \"%s\",\n", LOG_LEVEL_NAMES[header->level].data);
    fprintf(fmt->f, "        \"numeric\": %d\n", header->level);
    fprintf(fmt->f, "      },\n");
    fprintf(fmt->f, "      \"location\": {\n");
    fprintf(fmt->f, "        \"filename\": \"%s\",\n", header->filename.data); //TODO: escape
    fprintf(fmt->f, "        \"function\": \"%s\",\n", header->function.data); //TODO: escape
    fprintf(fmt->f, "        \"line\": %d\n", header->line);
    fprintf(fmt->f, "      },\n");
    fprintf(fmt->f, "      \"args\": [\n");

    for (size_t i = 0; i < header->arg_count; ++i) {
        switch (header->types[i]) {
            case TYPE_U8:
                fprintf(fmt->f, "        %u", values[i].val_uint8);
                break;
            case TYPE_U32:
                fprintf(fmt->f, "        %u", values[i].val_uint);
                break;
            case TYPE_I32:
                fprintf(fmt->f, "        %d", values[i].val_int);
                break;
            case TYPE_F32:
                fprintf(fmt->f, "        %f", values[i].val_float);
                break;
            case TYPE_CSTRING:
                // TODO: correctly encode string here
                fprintf(fmt->f, "        \"%s\"", values[i].val_cstring);
                break;
            case TYPE_COUNT:
                unreachable();
        }
        fputs((i < header->arg_count - 1)? ",\n": "\n", fmt->f);
    }

    fprintf(fmt->f, "      ]\n");
    fprintf(fmt->f, "   }");
}

void init_formatter_xml(FileFormatter *fmt) {
    init_formatter_file(fmt, "log.xml", "w");
    fprintf(fmt->f, "<log>\n");
}

void deinit_formatter_xml(FileFormatter *fmt) {
    fprintf(fmt->f, "</log>\n");
    deinit_formatter_file(fmt);
}

void handle_message_xml(FileFormatter *fmt, LogHeader *header, int32_t id, uint32_t timestamp, LoggingValueU *values) {
    fprintf(fmt->f, "  <message>\n");

    fprintf(fmt->f, "    <fmt_str>%s</fmt_str>\n", header->fmt_str.data); //TODO: escape
    fprintf(fmt->f, "    <id>%d</id>\n", id);
    fprintf(fmt->f, "    <level numeric=\"%d\">%s</level>\n", header->level, LOG_LEVEL_NAMES[header->level].data);
    fprintf(fmt->f, "    <timestamp>%u</timestamp>\n", timestamp);
    fprintf(fmt->f, "    <location>\n");
    fprintf(fmt->f, "       <filename>%s</filename>\n", header->filename.data);  //TODO: escape
    fprintf(fmt->f, "       <function>%s</function>\n", header->function.data); //TODO: escape
    fprintf(fmt->f, "       <line>%d</line>\n", header->line);
    fprintf(fmt->f, "    </location>\n");
    fprintf(fmt->f, "    <args>\n");

    for (size_t i = 0; i < header->arg_count; ++i) {
        switch (header->types[i]) {
            case TYPE_U8:
                fprintf(fmt->f, "       <u8>%u</u8>\n", values[i].val_uint8);
                break;
            case TYPE_U32:
                fprintf(fmt->f, "       <u32>%u</u32>\n", values[i].val_uint);
                break;
            case TYPE_I32:
                fprintf(fmt->f, "       <i32>%d</i32>\n", values[i].val_int);
                break;
            case TYPE_F32:
                fprintf(fmt->f, "       <f32>%f</f32>\n", values[i].val_float);
                break;
            case TYPE_CSTRING:
                // TODO: correctly encode string here
                fprintf(fmt->f, "       <string>%s</string>\n", values[i].val_cstring);
                break;
            case TYPE_COUNT:
                unreachable();
        }
    }
    fprintf(fmt->f, "    </args>\n");
    fprintf(fmt->f, "  </message>\n");
}

void init_formatter_html(FileFormatter *fmt) {
    init_formatter_file(fmt, "log.html", "w");
    fwrite(HTML_TABLE_START.data, 1, HTML_TABLE_START.byte_count, fmt->f);
}

void deinit_formatter_html(FileFormatter *fmt) {
    fwrite(HTML_TABLE_END.data, 1, HTML_TABLE_END.byte_count, fmt->f);
    deinit_formatter_file(fmt);
}

void handle_message_html(FileFormatter *fmt, LogHeader *header, int32_t id, uint32_t timestamp, LoggingValueU *values) {
    fputs("    <tr>\n", fmt->f);
    fprintf(fmt->f, "        <td>%zu</td>\n", fmt->msg_count);
    fprintf(fmt->f, "        <td>%s</td>\n", LOG_LEVEL_NAMES[header->level].data);
    fprintf(fmt->f, "        <td>%u</td>\n", timestamp);
    fprintf(fmt->f, "        <td>%s</td>\n", header->filename.data);
    fprintf(fmt->f, "        <td>%s</td>\n", header->function.data);
    fprintf(fmt->f, "        <td>%d</td>\n", header->line);
    fprintf(fmt->f, "        <td>%d</td>\n", id);
    fprintf(fmt->f, "        <td>%s</td>\n", header->fmt_str.data);

    for (size_t i = 0; i < CSL_MAX_ARG_COUNT; ++i) {
        if (i >= header->arg_count) {
            fputs("        <td></td>\n", fmt->f);
            continue;
        }

        switch (header->types[i]) {
            case TYPE_U8:
                fprintf(fmt->f, "        <td>%u</td>", values[i].val_uint8);
                break;
            case TYPE_U32:
                fprintf(fmt->f, "        <td>%u</td>", values[i].val_uint);
                break;
            case TYPE_I32:
                fprintf(fmt->f, "        <td>%d</td>", values[i].val_int);
                break;
            case TYPE_F32:
                fprintf(fmt->f, "        <td>%f</td>", values[i].val_float);
                break;
            case TYPE_CSTRING:
                fprintf(fmt->f, "        <td>%s</td>", values[i].val_cstring); // TODO: encode
                break;
            case TYPE_COUNT:
                unreachable();
        }
    }
    fputs("    </tr>\n", fmt->f);
}

#ifdef SQLITE_AVAILABLE
#define sqlite_error_check(rc, db) sqlite_error_check_((rc), (db), __LINE__)
static void sqlite_error_check_(int rc, sqlite3 *db, int line) {
    if (rc == SQLITE_OK) return;

    fprintf(stderr, "SQLite error at line %d: %s\n", line, sqlite3_errmsg(db));
    sqlite3_close(db);
    exit(EXIT_FAILURE);
}

void init_formatter_sqlite(FileFormatter *fmt) {
    if (fmt->filename == nullptr) fmt->filename = "log.db";
    int rc = sqlite3_open(fmt->filename, &fmt->db);
    sqlite_error_check(rc, fmt->db);

    rc = sqlite3_exec(fmt->db, "DROP TABLE IF EXISTS LogMeta;", 0, 0, NULL);
    sqlite_error_check(rc, fmt->db);

    const char *CREATE_MT = "CREATE TABLE LogMeta(LoggingId INT PRIMARY KEY, Level INT, Line INT, Filename TEXT, Function TEXT, Format TEXT);";
    rc = sqlite3_exec(fmt->db, CREATE_MT, 0, 0, NULL);
    sqlite_error_check(rc, fmt->db);

    rc = sqlite3_exec(fmt->db, "DROP TABLE IF EXISTS LogItems;", 0, 0, NULL);
    sqlite_error_check(rc, fmt->db);

    static_assert(CSL_MAX_ARG_COUNT == 10);
    const char *CREATE_T = "CREATE TABLE LogItems(ID INTEGER PRIMARY KEY, LoggingId INT, Timestamp INT, arg0 INT, arg1 INT, arg2 INT, arg3 INT, arg4 INT, arg5 INT, arg6 INT, arg7 INT, arg8 INT, arg9 INT)";
    rc = sqlite3_exec(fmt->db, CREATE_T, 0, 0, NULL);
    sqlite_error_check(rc, fmt->db);
}

void deinit_formatter_sqlite(FileFormatter *fmt) {
    sqlite3_close(fmt->db);
}

void handle_message_sqlite(FileFormatter *fmt, LogHeader *header, int32_t id, uint32_t timestamp, LoggingValueU *values) {
    if (header->category != '~') {
        const char *INSERT_META_MSG = "INSERT INTO LogMeta VALUES(?, ?, ?, ?, ?, ?)";
        sqlite3_stmt *stmt;
        int rc = sqlite3_prepare_v3(fmt->db, INSERT_META_MSG, -1, 0, &stmt, NULL);
        sqlite_error_check(rc, fmt->db);

        // TODO: check return codes of those
        sqlite3_bind_int(stmt, 1, id);
        sqlite3_bind_int(stmt, 2, (int)header->level);
        sqlite3_bind_int(stmt, 3, (int)header->line);
        sqlite3_bind_text(stmt, 4,header->filename.data, (int)header->filename.byte_count, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5,header->function.data, (int)header->function.byte_count, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 6,header->fmt_str.data, (int)header->fmt_str.byte_count, SQLITE_STATIC);

        sqlite3_step(stmt); // TODO: error check
        sqlite3_finalize(stmt);

        header->category = '~';
    }

    static_assert(CSL_MAX_ARG_COUNT == 10);
    const char *INSERT_MSG = "INSERT INTO LogItems VALUES(? , ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";

    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v3(fmt->db, INSERT_MSG, -1, 0, &stmt, NULL);
    sqlite_error_check(rc, fmt->db);

    // TODO: check return codes of those
    sqlite3_bind_int64(stmt, 1, (long long)fmt->msg_count);
    sqlite3_bind_int(stmt, 2, id);
    sqlite3_bind_int64(stmt, 3, timestamp);

    for (int i = 0; i < CSL_MAX_ARG_COUNT; ++i) {
        if (i >= header->arg_count) {
            sqlite3_bind_null(stmt, i + 4);
            continue;
        }

        switch (header->types[i]) {
            case TYPE_U8:
                sqlite3_bind_int(stmt, i + 4, values[i].val_uint8);
                break;
            case TYPE_U32:
                sqlite3_bind_int64(stmt, i + 4, values[i].val_uint);
                break;
            case TYPE_I32:
                sqlite3_bind_int(stmt, i + 4, values[i].val_int);
                break;
            case TYPE_F32:
                sqlite3_bind_double(stmt, i + 4, values[i].val_float);
                break;
            case TYPE_CSTRING:
                sqlite3_bind_text(stmt, i + 4, values[i].val_cstring, -1, SQLITE_TRANSIENT);
                break;
            case TYPE_COUNT:
                unreachable();
        }
    }

    sqlite3_step(stmt); // TODO: error check
    sqlite3_finalize(stmt);
}
#endif

void init_formatter_string(FileFormatter *fmt) {
    init_formatter_file(fmt, "log.txt", "w");
}

void deinit_formatter_string(FileFormatter *fmt) {
    deinit_formatter_file(fmt);
}

void handle_message_string(FileFormatter *fmt, LogHeader *header, int32_t id, uint32_t timestamp, LoggingValueU *values) {
    size_t current_arg = 0;
    size_t last_start = 0;
    fprintf(fmt->f, "[%c] [%u] %s:%d | ", LOG_LEVEL_NAMES_SHORT[header->level], timestamp, header->filename.data, header->line);

    for (int i = 0; i < header->fmt_str.byte_count; ++i) {
        if (header->fmt_str.data[i] != '{') continue;
        assert(header->fmt_str.data[i+1] == '}');

        fwrite(header->fmt_str.data + last_start, 1, i - last_start, fmt->f);
        last_start = i + 2;

        switch (header->types[current_arg]) {
            case TYPE_U8:
                fprintf(fmt->f, "%u", values[current_arg].val_uint8);
                break;
            case TYPE_U32:
                fprintf(fmt->f, "%u", values[current_arg].val_uint);
                break;
            case TYPE_I32:
                fprintf(fmt->f, "%d", values[current_arg].val_int);
                break;
            case TYPE_F32:
                fprintf(fmt->f, "%f", values[current_arg].val_float);
                break;
            case TYPE_CSTRING:
                fprintf(fmt->f, "%s", values[current_arg].val_cstring);
                break;
            case TYPE_COUNT:
                unreachable();
        }
        current_arg += 1;
    }

    if (current_arg != header->arg_count) {
        printf("Invalid format string for message with id %d\n", id);
    }

    fputc('\n', fmt->f);
}

enum OutputFormat {
    OUTPUT_FMT_STRING,
    OUTPUT_FMT_JSON,
    OUTPUT_FMT_XML,
    OUTPUT_FMT_HTML,
#ifdef SQLITE_AVAILABLE
    OUTPUT_FMT_SQLITE,
#endif
    OUTPUT_FMT_COUNT
};

const char *OUTPUT_FMT_NAMES[] = {
        "string",
        "json",
        "xml",
        "html",
#ifdef SQLITE_AVAILABLE
        "sqlite",
#endif
};
static_assert(sizeof OUTPUT_FMT_NAMES == sizeof(OUTPUT_FMT_NAMES[0]) * OUTPUT_FMT_COUNT);

void init_formatter(FileFormatter *formatter, enum OutputFormat format) {
    switch (format) {
        case OUTPUT_FMT_STRING:
            init_formatter_string(formatter);
            break;
        case OUTPUT_FMT_JSON:
            init_formatter_json(formatter);
            break;
        case OUTPUT_FMT_XML:
            init_formatter_xml(formatter);
            break;
        case OUTPUT_FMT_HTML:
            init_formatter_html(formatter);
            break;
#ifdef SQLITE_AVAILABLE
        case OUTPUT_FMT_SQLITE:
            init_formatter_sqlite(formatter);
            break;
#endif
        case OUTPUT_FMT_COUNT:
            unreachable();
    }
}

void deinit_formatter(FileFormatter *formatter, enum OutputFormat format) {
    switch (format) {
        case OUTPUT_FMT_STRING:
            deinit_formatter_string(formatter);
            break;
        case OUTPUT_FMT_JSON:
            deinit_formatter_json(formatter);
            break;
        case OUTPUT_FMT_XML:
            deinit_formatter_xml(formatter);
            break;
        case OUTPUT_FMT_HTML:
            deinit_formatter_html(formatter);
            break;
#ifdef SQLITE_AVAILABLE
        case OUTPUT_FMT_SQLITE:
            deinit_formatter_sqlite(formatter);
            break;
#endif
        case OUTPUT_FMT_COUNT:
            unreachable();
    }
}

void handle_message(FileFormatter *fmt, enum OutputFormat format, LogHeader *header, int32_t id, uint32_t timestamp, LoggingValueU *values) {
    switch (format) {
        case OUTPUT_FMT_STRING: handle_message_string(fmt, header, id, timestamp, values); break;
        case OUTPUT_FMT_JSON:   handle_message_json(fmt, header, id, timestamp, values); break;
        case OUTPUT_FMT_XML:    handle_message_xml(fmt, header, id, timestamp, values); break;
        case OUTPUT_FMT_HTML:   handle_message_html(fmt, header, id, timestamp, values); break;
#ifdef SQLITE_AVAILABLE
        case OUTPUT_FMT_SQLITE: handle_message_sqlite(fmt, header, id, timestamp, values); break;
#endif
        case OUTPUT_FMT_COUNT:
            unreachable();
    }
}

void print_help(int argc, char **argv) {
    printf("Usage: %s [--format fmt] [--outfile file] --program executable --log log_file\n", argv[0]);
    puts("Available formats:");
    for (int i = 0; i < OUTPUT_FMT_COUNT; ++i) {
        printf("  %s%s\n", OUTPUT_FMT_NAMES[i], (i == 0)?" (default)" : "");
    }
}

typedef struct {
    Elf64_Nhdr note;
    char name[4];
    char build_id[];
} BuildIdHeader;

void print_n_bytes(const char *text, const char *data, size_t n) {
    printf("%s: ", text);
    for (size_t i = 0; i < n; ++i) {
        printf("%02x", (unsigned char)data[i]);
    }
    printf("\n");
}

char* read_file_content(const char *target_program_name) {
    int fd = open(target_program_name, O_RDONLY);

    struct stat file_stat;
    if (fstat(fd, &file_stat) == -1) {}

    size_t file_size = file_stat.st_size;
    char *file_content = (char *)malloc(file_size);

    // TODO: check that all bytes are read
    (void)!read(fd, file_content, file_size);
    close(fd);

    return file_content;
}

void parse_elf_section(char *file_content, MemoryView *data_section, MemoryView *build_id) {
    Elf64_Ehdr *elf_header = (Elf64_Ehdr *)file_content;
    Elf64_Shdr *section_header = (Elf64_Shdr *)(file_content + elf_header->e_shoff);

    char *section_names = file_content + section_header[elf_header->e_shstrndx].sh_offset;

    for (int i = 0; i < elf_header->e_shnum; i++) {
        const char *section_name = &section_names[section_header[i].sh_name];

//        printf("Section name: %s\n", section_name);
        if (strcmp(section_name, ".data") == 0) {
            data_section->byte_count = section_header[i].sh_size;
            data_section->data = file_content + section_header[i].sh_offset;
            continue;
        }

        if (strcmp(section_name, ".note.gnu.build-id") == 0) {
            BuildIdHeader *bih = (BuildIdHeader *)(file_content + section_header[i].sh_offset);

            assert(bih->note.n_namesz == 4); // Name should be "GNU"
            assert(bih->name[0] == 'G' && bih->name[1] == 'N' && bih->name[2] == 'U');
            assert(bih->note.n_type == NT_GNU_BUILD_ID);

            build_id->byte_count = bih->note.n_descsz;
            build_id->data = bih->build_id;

            print_n_bytes("Found Build ID", build_id->data,build_id->byte_count);
        }
    }
    if (build_id->byte_count == 0) {
        printf("WARN: no build id found in the program, can't verify that it produced the log file\n");
    }

    printf("Found .data at position %p with length %lu\n", (void*) data_section->data, data_section->byte_count);

}

void build_header_list(HeaderList *list, MemoryView data_section, char *file_content) {
    char HEADER_MARKER[] = LOGGING_HEADER_MAGIC_NUMBER;

    header_list_init(list);

    for (size_t i = 0; i < data_section.byte_count - sizeof(LogHeader) + 1; ++i) {
        if (data_section.data[i] != HEADER_MARKER[0]) continue;
        if (0 != memcmp(data_section.data + i, HEADER_MARKER, sizeof HEADER_MARKER)) continue;

        LogHeader *header = (LogHeader *)(data_section.data + i);
        printf("Found logging header at %lu, (%c)\n", i, header->category);

        header_list_append(list, header);
    }
    header_list_fill_ids(list);
    header_list_fix_string(list, file_content);
    puts("===============================================================================");

    for (size_t i = 0; i < list->size; ++i) {
        if (i == list->sentinel_index) continue;

        puts("------------------------------------------------------------");

        LogHeader *h = list->headers[i];

        printf("Logging header with id %d\n", list->ids[i]);

        printf("--> fmt_str: %s\n", h->fmt_str.data);
        printf("--> arg_count: %lu\n", h->arg_count);

        for (size_t j = 0; j < h->arg_count; ++j) {
            printf(" \\--> arg[%zu]: %s\n", j, DATA_TYPE_NAMES[h->types[j]].data);
        }

        printf("--> filename: %s\n", h->filename.data);
        printf("--> function: %s\n", h->function.data);
        printf("--> line: %d\n", h->line);

        printf("--> level: %s\n", LOG_LEVEL_NAMES[h->level].data);
        printf("--> category: %c\n", h->category);
    }

    puts("===============================================================================");
}

int main(int argc, char **argv) {
    if (args_find_position("--help", argc, argv) > 0) {
        print_help(argc, argv);
        return EXIT_SUCCESS;
    }

    const char *target_program_name = args_get_value("--program", argc, argv);
    const char *log_file_name = args_get_value("--log", argc, argv);

    if (target_program_name == nullptr || log_file_name == nullptr) {
        print_help(argc, argv);
        return EXIT_FAILURE;
    }

    const char *output_filename = args_get_value("--outfile", argc, argv);
    const char *wanted_fmt_str = args_get_value("--format", argc, argv);
    enum OutputFormat wanted_format = OUTPUT_FMT_STRING;

    if (wanted_fmt_str != nullptr) {
        wanted_format = OUTPUT_FMT_COUNT;
        for (int i = 0; i < OUTPUT_FMT_COUNT; ++i) {
            if (strcmp(wanted_fmt_str, OUTPUT_FMT_NAMES[i]) == 0) {
                wanted_format = i;
                break;
            }
        }

        if (wanted_format == OUTPUT_FMT_COUNT) {
            printf("Unknown target format %s\n", wanted_fmt_str);
            print_help(argc, argv);
            return EXIT_FAILURE;
        }
    }

    char *file_content = read_file_content(target_program_name);

    MemoryView data_section = {};
    MemoryView build_id = {};
    parse_elf_section(file_content, &data_section, &build_id);

    HeaderList list;
    build_header_list(&list, data_section, file_content);

    FILE *log_file = fopen(log_file_name, "rb");

    FileFormatter formatter = {};
    formatter.filename = output_filename;

    init_formatter(&formatter, wanted_format);

    uint32_t magic_num;
    read_binary_u32(&magic_num, log_file);
    assert(magic_num == LOGGING_FILE_HEADER_MAGIC_NUMBER);

    uint32_t version_number;
    read_binary_u32(&version_number, log_file);
    assert(version_number == LOGGING_FILE_HEADER_VERSION_NUMBER);


    char logging_build_id[32] = {};
    (void)!fread(&logging_build_id, 1, 32, log_file); //TODO: handle error

    if (build_id.byte_count > 0
        && (build_id.byte_count >= 32 || memcmp(build_id.data, logging_build_id, build_id.byte_count) != 0)
        ) {
        puts("Warning: mismatch of build ids detected!");
        printf("ID in target program %s", target_program_name);
        print_n_bytes("", build_id.data,  build_id.byte_count);

        printf("ID log file %s was produced with", log_file_name);
        print_n_bytes("", logging_build_id,  build_id.byte_count);
        puts("===============================================================================");
//        return EXIT_FAILURE;
    }

    for (int i = 0; i < LOGGING_FILE_HEADER_RESERVED_COUNT; ++i) {
        uint8_t dummy;
        read_binary_u8(&dummy, log_file);
    }

    int32_t current_id;
    uint32_t current_timestamp;

    LoggingValueU current_values[CSL_MAX_ARG_COUNT];

    while (read_binary_i32(&current_id, log_file)) {
        read_binary_u32(&current_timestamp, log_file);
//        printf("Log message with id %d and timestamp %u\n", current_id, current_timestamp);

        uint32_t  h_index = header_list_lookup_by_id(&list, current_id);
        LogHeader *h = list.headers[h_index];

        for (size_t i = 0; i < h->arg_count; ++i) {
            read_binary_logging_value(&current_values[i], h->types[i], log_file);
        }

        handle_message(&formatter, wanted_format, h, current_id, current_timestamp, current_values);
        formatter.msg_count += 1;

        for (size_t i = 0; i < h->arg_count; ++i) {
            if (h->types[i] == TYPE_CSTRING) free((char *)current_values[i].val_cstring);
        }
    }
    deinit_formatter(&formatter, wanted_format);
    printf("Wrote %zu messages to file %s\n", formatter.msg_count, formatter.filename);
    fclose(log_file);

    header_list_free(&list);
    free(file_content);
}
