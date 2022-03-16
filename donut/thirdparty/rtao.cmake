set(rtao_package_path ${DEPENDENCIES_ROOT}/chk/rtao/${rtao_package_version})
if (NOT EXISTS ${rtao_package_path}/include/GFSDK_RTAO.h)
  message(FATAL_ERROR "RTAO package not installed --- please run update-dependencies.bat/update-dependencies.sh")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)

	add_library(rtao_d3d12 IMPORTED SHARED GLOBAL)

	set_target_properties(rtao_d3d12 PROPERTIES
	                      IMPORTED_LOCATION ${rtao_package_path}/lib/GFSDK_RTAO_D3D12.win64.dll
	                      IMPORTED_IMPLIB ${rtao_package_path}/lib/GFSDK_RTAO_D3D12.win64.lib)

	target_include_directories(rtao_d3d12 INTERFACE ${rtao_package_path}/include)

endif()
