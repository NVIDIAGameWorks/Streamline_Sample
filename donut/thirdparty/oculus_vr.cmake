set(__oculus_vr_package_path ${DEPENDENCIES_ROOT}/chk/ovr_sdk_win/${oculus_vr_package_version})

string(REGEX REPLACE "\\\\" "/" __oculus_vr_package_path ${__oculus_vr_package_path}) 

file(GLOB ovr_shim_src
	${__oculus_vr_package_path}/LibOVR/Incude/*.h
	${__oculus_vr_package_path}/LibOVR/Shim/*.h
	${__oculus_vr_package_path}/LibOVR/Shim/*.c
	${__oculus_vr_package_path}/LibOVR/Shim/*.cpp
)

add_library(oculus_vr STATIC EXCLUDE_FROM_ALL ${ovr_shim_src})

target_include_directories(oculus_vr PUBLIC ${__oculus_vr_package_path}/LibOVR/Include)
