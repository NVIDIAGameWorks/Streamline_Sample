
file(GLOB donut_core_src
    include/donut/core/chunk/*.h
    include/donut/core/math/*.h
    include/donut/core/vfs/VFS.h
    include/donut/core/*.h
    src/core/chunk/*.cpp
    src/core/math/*.cpp
    src/core/vfs/VFS.cpp
    src/core/*.cpp
)

add_library(donut_core STATIC EXCLUDE_FROM_ALL ${donut_core_src})
target_include_directories(donut_core PUBLIC include)
target_link_libraries(donut_core jsoncpp)

if(NOT WIN32)
    target_link_libraries(donut_core stdc++fs)
endif()

if(WIN32)
    target_compile_definitions(donut_core PUBLIC NOMINMAX)
endif()

if(DONUT_WITH_SQLITE)
    target_sources(donut_core PRIVATE 
    	include/donut/core/vfs/SQLiteFS.h
    	src/core/vfs/SQLiteFS.cpp
	)
    target_link_libraries(donut_core sqlite)
    target_compile_definitions(donut_core PUBLIC DONUT_WITH_SQLITE)
endif()

if(DONUT_WITH_BCRYPT)
    target_compile_definitions(donut_core PUBLIC DONUT_WITH_BCRYPT)
endif()

if(DONUT_WITH_LZ4)
    target_link_libraries(donut_core lz4)
    target_compile_definitions(donut_core PUBLIC DONUT_WITH_LZ4)
endif()

set_target_properties(donut_core PROPERTIES FOLDER Donut)
