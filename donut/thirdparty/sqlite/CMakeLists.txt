
add_library(sqlite STATIC EXCLUDE_FROM_ALL sqlite3.c sqlite3.h)

target_include_directories(sqlite INTERFACE .)