include(../compileshaders.cmake)

file(GLOB donut_shaders
	"*.hlsl"
	"passes/*.hlsl"
	"../include/donut/shaders/*.hlsli"
	"../include/donut/shaders/*.h"
)
donut_compile_shaders_all_platforms(
	TARGET donut_shaders
	CONFIG ${CMAKE_CURRENT_LIST_DIR}/DonutShaders.cfg
	FOLDER Donut
	OUTPUT_BASE ${CMAKE_CURRENT_BINARY_DIR}
	SOURCES ${donut_shaders}
)

file(GLOB HLSL_FILES "*.hlsl")
install(FILES CMakeLists.txt;DonutShaders.cfg;${HLSL_FILES} DESTINATION "donut/shaders")
install(DIRECTORY passes DESTINATION "donut/shaders")
if (DONUT_WITH_DX11 OR DONUT_WITH_DX12)
	install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/dxbc;${CMAKE_CURRENT_BINARY_DIR}/dxil DESTINATION ${CMAKE_INSTALL_BINDIR}/../donut/shaders)
endif()
if (DONUT_WITH_VULKAN)
	install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/spirv DESTINATION ${CMAKE_INSTALL_BINDIR}/../donut/shaders)
endif()