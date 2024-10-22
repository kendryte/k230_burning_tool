cmake_minimum_required(VERSION 3.5)

get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

project(
	BurningLibrary
	VERSION 0.1
	LANGUAGES C)

set(BUILD_SHARED_LIBS ON)
set(LIBUSB_INSTALL_TARGETS OFF)
add_subdirectory(3rdparty/libusb-cmake)

# ##############################################################################
function(push_source_code target src_dir)
	file(GLOB_RECURSE NEW_SOURCES "${src_dir}/*.c")

	if(WIN32)
		list(FILTER NEW_SOURCES EXCLUDE REGEX "\\.macos\\.c$")
		list(FILTER NEW_SOURCES EXCLUDE REGEX "\\.linux\\.c$")
		list(FILTER NEW_SOURCES EXCLUDE REGEX "\\.posix\\.c$")
	elseif(APPLE)
		list(FILTER NEW_SOURCES EXCLUDE REGEX "\\.win32\\.c$")
		list(FILTER NEW_SOURCES EXCLUDE REGEX "\\.linux\\.c$")
	elseif(UNIX)
		list(FILTER NEW_SOURCES EXCLUDE REGEX "\\.win32\\.c$")
		list(FILTER NEW_SOURCES EXCLUDE REGEX "\\.macos\\.c$")
	else()
		message(FATAL_ERROR "不支持当前平台")
	endif()

	target_sources("${target}" PRIVATE "${NEW_SOURCES}")

	# set(inc_dirs "${CMAKE_CURRENT_LIST_DIR}/${src_dir}/include")

	foreach(dir IN LISTS ARGN)
		list(APPEND inc_dirs "${CMAKE_CURRENT_LIST_DIR}/${dir}")
	endforeach()

	set(EXOPT -Wall -Wextra -pedantic -Werror -Wno-format-security)

	foreach(file IN LISTS NEW_SOURCES)
		set_source_files_properties("${file}" PROPERTIES
			# INCLUDE_DIRECTORIES "${inc_dirs}"
			COMPILE_OPTIONS "${EXOPT}" # -Wno-error=format-zero-length=0
			COMPILE_DEFINITIONS _GNU_SOURCE
		)
	endforeach()
endfunction()

add_library(canaan_burn SHARED)

set_target_properties(canaan_burn PROPERTIES
						C_STANDARD 23
						LINKER_LANGUAGE C
						OUTPUT_NAME kburn
                      	INSTALL_RPATH "\$ORIGIN"
                      	BUILD_WITH_INSTALL_RPATH TRUE)

push_source_code(canaan_burn lib)
push_source_code(canaan_burn usb)
push_source_code(canaan_burn monitor)
push_source_code(canaan_burn burn)

target_include_directories(canaan_burn PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")

target_compile_definitions(canaan_burn PRIVATE "PROJECT_ROOT=\"${PROJECT_ROOT}\"" KBURN_ASSERT_SIZE BURN_LIB_COMPILING)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
	target_compile_definitions(canaan_burn PRIVATE "IS_DEBUG=0")
else()
	target_compile_definitions(canaan_burn PRIVATE "IS_DEBUG=1")
endif()

if(${DISABLE_TERM_HYPERLINK})
	target_compile_definitions(canaan_burn PRIVATE DISABLE_TERM_HYPERLINK)
endif()

if(DEFINED ENV{CI})
	target_compile_options(canaan_burn PRIVATE -Wno-error=attributes -Wno-error=format-security)

	if(WIN32)
		target_compile_options(canaan_burn PRIVATE -Wno-error=format)
	endif()
endif()

if(APPLE)
	target_compile_options(canaan_burn PRIVATE -Wno-gnu-zero-variadic-macro-arguments)
	target_compile_definitions(canaan_burn PRIVATE APPLE)
endif()

add_dependencies(canaan_burn usb-1.0)
target_link_libraries(canaan_burn PUBLIC usb-1.0)
target_include_directories(canaan_burn PUBLIC ${LIBUSB_GEN_INCLUDES})

target_link_libraries(canaan_burn PUBLIC pthread)

if(WIN32)
	set(WIN32LIBS ws2_32 Setupapi)
	target_link_libraries(canaan_burn PRIVATE "${WIN32LIBS}")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT WIN32)
	set(SANITIZE_ADDRESS "On")

	# set(SANITIZE_MEMORY "On")

	# set(SANITIZE_THREAD "On")

	# set(SANITIZE_UNDEFINED "On")
	set(SANITIZE_LINK_STATIC "Off")

	set(CMAKE_MODULE_PATH
		"${CMAKE_CURRENT_LIST_DIR}/3rdparty/sanitizers-cmake/cmake"
		${CMAKE_MODULE_PATH})
	find_package(Sanitizers REQUIRED)

	add_sanitizers(canaan_burn)
endif()

install(TARGETS canaan_burn
	RUNTIME DESTINATION bin)

install(TARGETS canaan_burn
	EXPORT canaan_burn
	LIBRARY DESTINATION lib
	ARCHIVE DESTINATION lib
	RUNTIME DESTINATION bin
)

# manual install libusb
install(TARGETS usb-1.0
    LIBRARY DESTINATION bin
)

install(DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/include/public" DESTINATION "include")
