
file(GLOB donut_render_src
    LIST_DIRECTORIES false
    include/donut/render/*.h
    src/render/*.cpp
)

set(donut_src_hbao_plus
    include/donut/render/plugins/HbaoPlusPass.h
    src/render/plugins/HbaoPlusPass.cpp
)

set(donut_src_rtao
    include/donut/render/plugins/RtaoDenoiserPass.h
    src/render/plugins/RtaoDenoiserPass.cpp
)

set(donut_src_rtreflections
    include/donut/render/plugins/RtSpecularDenoiserPass.h
    src/render/plugins/RtSpecularDenoiserPass.cpp
)

set(donut_src_volume_lighting
    include/donut/render/plugins/VolumeLightingPass.h
    src/render/plugins/VolumeLightingPass.cpp
)

add_library(donut_render STATIC EXCLUDE_FROM_ALL ${donut_render_src})
target_include_directories(donut_render PUBLIC include)
target_link_libraries(donut_render donut_core donut_engine)

if(DONUT_WITH_NVAPI)
target_link_libraries(donut_render nvapi)
endif()

add_dependencies(donut_render donut_shaders)

if(DONUT_WITH_DX11)
target_compile_definitions(donut_render PUBLIC USE_DX11=1)
endif()

if(DONUT_WITH_DX12)
target_compile_definitions(donut_render PUBLIC USE_DX12=1)
endif()

if(DONUT_WITH_VULKAN)
target_compile_definitions(donut_render PUBLIC USE_VK=1)
endif()

if(DONUT_WITH_HBAO_PLUS)
    target_sources(donut_render PRIVATE ${donut_src_hbao_plus})
    if(DONUT_WITH_DX11)
    target_link_libraries(donut_render hbao_plus_d3d11)
    endif()
    if(DONUT_WITH_DX12)
    target_link_libraries(donut_render hbao_plus_d3d12)
    endif()
    target_compile_definitions(donut_render PUBLIC DONUT_WITH_HBAO_PLUS)
endif()

if(DONUT_WITH_RTAO AND DONUT_WITH_DX12)
    target_sources(donut_render PRIVATE ${donut_src_rtao})
    target_link_libraries(donut_render rtao_d3d12)
    target_compile_definitions(donut_render PUBLIC DONUT_WITH_RTAO)
endif()

if(DONUT_WITH_RT_REFLECTIONS AND DONUT_WITH_DX12)
    target_sources(donut_render PRIVATE ${donut_src_rtreflections})
    target_link_libraries(donut_render rtreflections_d3d12)
    target_compile_definitions(donut_render PUBLIC DONUT_WITH_RT_REFLECTIONS)
endif()

if(DONUT_WITH_VOLUME_LIGHTING)
    target_sources(donut_render PRIVATE ${donut_src_volume_lighting})

    if(DONUT_WITH_DX11 AND DONUT_WITH_DX12)
        message(SEND_ERROR "The GameWorks Volumetric Lightihg library cannot be used in an application that supports both DX11 and DX12")
    endif()

    if(DONUT_WITH_DX11)
    target_link_libraries(donut_render volumelighting_d3d11)
    endif()
    if(DONUT_WITH_DX12)
    target_link_libraries(donut_render volumelighting_d3d12)
    endif()
    target_compile_definitions(donut_render PUBLIC DONUT_WITH_VOLUME_LIGHTING)
endif()

if(DONUT_WITH_SL)
    target_link_libraries(donut_render sl)
endif()

if(UNIX)
    target_compile_definitions(donut_render PRIVATE LINUX)
endif()

set_target_properties(donut_render PROPERTIES FOLDER Donut)
