# C Structured Logging
Log structured data to a compact binary file.

# Usage
```c
// Initialize the logging
csl_easy_init("log.bin", LL_INFO);

// Log some stuff
LOG("{}", LL_INFO, 1);
LOG("{} - {}", LL_INFO, 1.0f, "test");
LOG("{}", LL_INFO, "end");

// End the logging
csl_easy_end();
```

# Convert log file
The log messages in a log file can be converted to different formats using the log_printer executable:
```bash
# program needs to be the executable that produced the log file
./log_printer --program <program> --log log.bin --format string
./log_printer --program <program> --log log.bin --format html
./log_printer --program <program> --log log.bin --format sqlite

# see all available formats using ./log_printer --help
```

# Compatibility
Needs C23, currently only works with GCC13 (needs [N3038](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3038.htm) and [N3018](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3018.htm))

# How this it work?
The logging program only logs an id and the data that is unique each message (timestamp + the values that should be logged).

Each call to the LOG macro creates a static struct in the program which contains the remaining information like the format string, log_level, type information or source location.
The log_printer program uses this information to then format the message or convert it to another format like json, xml or sqlite.