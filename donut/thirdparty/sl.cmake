set(SL_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/sl-sdk" CACHE STRING "SL SDK Root Directory")

if (WIN32)
  add_library(sl IMPORTED SHARED GLOBAL)

  # set the list of DLLs that need copying to target folder of application
  set(__SL_DLLS_LIST "${SL_SDK_ROOT}/bin/x64/*.dll")
  
  set_property(TARGET sl APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
  set_property(TARGET sl APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)

  set_property(TARGET sl PROPERTY IMPORTED_IMPLIB ${SL_SDK_ROOT}/lib/x64/sl.interposer.lib)
  set_target_properties(sl PROPERTIES IMPORTED_LOCATION ${SL_SDK_ROOT}/bin/x64/sl.interposer.dll)
    
else ()
  message(FATAL_ERROR "No SL Linux support yet")
endif()

set_property(TARGET sl APPEND PROPERTY EXTRA_DLLS "${__SL_DLLS_LIST}")

target_include_directories(sl INTERFACE ${SL_SDK_ROOT}/include)
