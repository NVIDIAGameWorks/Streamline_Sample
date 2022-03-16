set(__assimp_package_path ${DEPENDENCIES_ROOT}/chk/assimp/${assimp_package_version})
if (NOT EXISTS ${__assimp_package_path}/assimp-import.cmake)
  message(FATAL_ERROR "assimp package not installed --- please run update-dependencies.bat/update-dependencies.sh")
endif()

include(${__assimp_package_path}/assimp-import.cmake)
