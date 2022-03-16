
file(GLOB lz4_src
    "lz4/lib/*.c"
    "lz4/lib/*.h"
)

add_library(lz4 STATIC EXCLUDE_FROM_ALL ${lz4_src})
target_include_directories(lz4 INTERFACE lz4/lib)
