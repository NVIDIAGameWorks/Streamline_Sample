if (NOT DEFINED __nvapi_install)
    set(__nvapi_install ${CMAKE_CURRENT_LIST_DIR})
endif()

add_library(nvapi IMPORTED STATIC GLOBAL)
if (WIN32)
	if(CMAKE_SIZEOF_VOID_P EQUAL 8)
	  	set_target_properties(nvapi PROPERTIES
	                          IMPORTED_LOCATION ${__nvapi_install}/amd64/nvapi64.lib)
	else()
		set_target_properties(nvapi PROPERTIES
	                          IMPORTED_LOCATION ${__nvapi_install}/x86/nvapi.lib)
	endif()
else ()
    message(FATAL_ERROR "Need nvapi binary path for current platform --- please fix me!")
endif()

target_include_directories(nvapi INTERFACE ${__nvapi_install})
