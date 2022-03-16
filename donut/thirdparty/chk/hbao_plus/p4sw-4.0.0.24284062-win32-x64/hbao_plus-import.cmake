# this file will be part of the binary package and is used to generate the imported targets for hbao_plus

if (NOT DEFINED __hbao_plus_install)
  set(__hbao_plus_install ${CMAKE_CURRENT_LIST_DIR})
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(__hbao_plus_platform "win64")
else()
  set(__hbao_plus_platform "win32")
endif()

add_library(hbao_plus_d3d11 IMPORTED SHARED GLOBAL)
add_library(hbao_plus_d3d12 IMPORTED SHARED GLOBAL)
if (WIN32)
    set_target_properties(hbao_plus_d3d11 PROPERTIES
                          IMPORTED_LOCATION ${__hbao_plus_install}/lib/GFSDK_SSAO_D3D11.${__hbao_plus_platform}.dll
                          IMPORTED_IMPLIB ${__hbao_plus_install}/lib/GFSDK_SSAO_D3D11.${__hbao_plus_platform}.lib)
    set_target_properties(hbao_plus_d3d12 PROPERTIES
                          IMPORTED_LOCATION ${__hbao_plus_install}/lib/GFSDK_SSAO_D3D12.${__hbao_plus_platform}.dll
                          IMPORTED_IMPLIB ${__hbao_plus_install}/lib/GFSDK_SSAO_D3D12.${__hbao_plus_platform}.lib)
else ()
    message(FATAL_ERROR "Need hbao_plus binary path for current platform --- please fix me!")
endif()

target_include_directories(hbao_plus_d3d11 INTERFACE ${__hbao_plus_install}/include)
target_include_directories(hbao_plus_d3d12 INTERFACE ${__hbao_plus_install}/include)
