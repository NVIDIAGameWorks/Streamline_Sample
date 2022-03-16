set(rtreflections_package_path ${DEPENDENCIES_ROOT}/chk/gfsdk_rtreflections/${rtreflections_package_version})
if (NOT EXISTS ${rtreflections_package_path}/include/GFSDK_RTReflections.h)
  message(FATAL_ERROR "GFSDK_RTReflections package not installed --- please run update-dependencies.bat/update-dependencies.sh")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)

	add_library(rtreflections_d3d12 IMPORTED SHARED GLOBAL)

	set_target_properties(rtreflections_d3d12 PROPERTIES
	                      IMPORTED_LOCATION ${rtreflections_package_path}/lib/GFSDK_RTReflections_D3D12.win64.dll
	                      IMPORTED_IMPLIB ${rtreflections_package_path}/lib/GFSDK_RTReflections_D3D12.win64.lib)

	target_include_directories(rtreflections_d3d12 INTERFACE ${rtreflections_package_path}/include)

endif()
