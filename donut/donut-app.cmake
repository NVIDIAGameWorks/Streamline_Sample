
file(GLOB donut_app_src
    LIST_DIRECTORIES false
    include/donut/app/*.h
    src/app/*.cpp
)

file(GLOB donut_app_vr_src
    include/donut/app/vr/*.h
    src/app/vr/*.cpp
)

add_library(donut_app STATIC EXCLUDE_FROM_ALL ${donut_app_src})
target_include_directories(donut_app PUBLIC include)
target_link_libraries(donut_app donut_core glfw imgui)

if (DONUT_WITH_SL)
    target_include_directories(donut_app INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/sl-sdk/include)
    target_compile_definitions(donut_app PUBLIC USE_SL=1)
    
    target_link_libraries(donut_app 
        debug sl
        optimized sl)
endif()

if(DONUT_WITH_DX11)
target_sources(donut_app PRIVATE src/app/dx11/DeviceManager_DX11.cpp)
target_compile_definitions(donut_app PUBLIC USE_DX11=1)
target_link_libraries(donut_app nvrhi_d3d11)
endif()

if(DONUT_WITH_DX12)
target_sources(donut_app PRIVATE src/app/dx12/DeviceManager_DX12.cpp)
target_compile_definitions(donut_app PUBLIC USE_DX12=1)
target_link_libraries(donut_app nvrhi_d3d12)
endif()

if(DONUT_USE_DXIL_ON_DX12)
target_compile_definitions(donut_app PRIVATE DONUT_USE_DXIL_ON_DX12=1)
endif()

if(DONUT_WITH_VULKAN)
target_sources(donut_app PRIVATE src/app/vulkan/DeviceManager_VK.cpp)
target_compile_definitions(donut_app PUBLIC USE_VK=1)
target_link_libraries(donut_app nvrhi_vk)
endif()

add_dependencies(donut_app donut_shaders)

if(DONUT_WITH_OCULUS_VR AND (DONUT_WITH_DX11 OR DONUT_WITH_DX12))
    target_sources(donut_app PRIVATE ${donut_app_vr_src})
    target_compile_definitions(donut_app PUBLIC DONUT_WITH_VR DONUT_WITH_OCULUS_VR)
    target_link_libraries(donut_app oculus_vr)
endif()

set_target_properties(donut_app PROPERTIES FOLDER Donut)
