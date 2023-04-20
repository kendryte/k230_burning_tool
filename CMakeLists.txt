cmake_minimum_required(VERSION 3.5)

if(POLICY CMP0077)
	cmake_policy(SET CMP0077 NEW)
endif()

set(BUILD_PARENT On)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# set(CMAKE_VERBOSE_MAKEFILE ON)

project(K230BurningToolMeta)

if(WIN32)
	set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/dist")
elseif(LINUX)
	set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/dist/usr")
endif()

include(${CMAKE_TOOLCHAIN_FILE})

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/canaan-burn")

# add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/cli")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/gui")

# add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/test-binary")
