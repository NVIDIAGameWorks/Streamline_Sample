set(__hbao_plus_package_path ${DEPENDENCIES_ROOT}/chk/hbao_plus/${hbao_plus_package_version})
if (NOT EXISTS ${__hbao_plus_package_path}/hbao_plus-import.cmake)
  message(FATAL_ERROR "hbao_plus package not installed --- please run update-dependencies.bat")
endif()

include(${__hbao_plus_package_path}/hbao_plus-import.cmake)
