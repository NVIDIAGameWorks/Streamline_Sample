
file(GLOB donut_engine_src
    include/donut/engine/*.h
    src/engine/*.cpp
    src/engine/*.c
    src/engine/*.h
)

add_library(donut_engine STATIC EXCLUDE_FROM_ALL ${donut_engine_src})
target_include_directories(donut_engine PUBLIC include)

target_link_libraries(donut_engine nvrhi jsoncpp stb)

if(WIN32)
    target_compile_definitions(donut_engine PUBLIC NOMINMAX)
endif()

if (DONUT_WITH_ASSIMP)
    target_link_libraries(donut_engine assimp)
    target_compile_definitions(donut_engine PUBLIC DONUT_WITH_ASSIMP)
endif()

if (DONUT_WITH_AUDIO)
    target_link_libraries(donut_engine Xaudio2)
    target_compile_definitions(donut_engine PUBLIC DONUT_WITH_AUDIO)
endif()

set_target_properties(donut_engine PROPERTIES FOLDER Donut)
