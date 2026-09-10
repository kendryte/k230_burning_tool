cmake_minimum_required(VERSION 3.18)

if(NOT DEFINED INSTALL_PREFIX OR INSTALL_PREFIX STREQUAL "")
	message(FATAL_ERROR "INSTALL_PREFIX is required")
endif()
if(NOT DEFINED EXECUTABLE_NAME OR EXECUTABLE_NAME STREQUAL "")
	message(FATAL_ERROR "EXECUTABLE_NAME is required")
endif()

set(BIN_DIR "${INSTALL_PREFIX}/bin")
set(EXECUTABLE_PATH "${BIN_DIR}/${EXECUTABLE_NAME}.exe")
if(NOT EXISTS "${EXECUTABLE_PATH}")
	message(FATAL_ERROR "Installed executable not found: ${EXECUTABLE_PATH}")
endif()

if(DEFINED QT_QMAKE_PATH AND NOT QT_QMAKE_PATH STREQUAL "")
	get_filename_component(QT_BIN_DIR "${QT_QMAKE_PATH}" DIRECTORY)
endif()
find_program(WINDEPLOYQT_EXECUTABLE
	NAMES windeployqt.exe windeployqt
	HINTS "${QT_BIN_DIR}"
	REQUIRED
)

set(COMPILER_RUNTIME_OPTION --compiler-runtime)
if(DEFINED ENV{MSYSTEM})
	set(COMPILER_RUNTIME_OPTION --no-compiler-runtime)
endif()
execute_process(
	COMMAND "${WINDEPLOYQT_EXECUTABLE}"
		--release
		${COMPILER_RUNTIME_OPTION}
		--no-translations
		"${EXECUTABLE_PATH}"
	WORKING_DIRECTORY "${BIN_DIR}"
	RESULT_VARIABLE deploy_result
	COMMAND_ECHO STDOUT
)
if(NOT deploy_result STREQUAL "0")
	message(FATAL_ERROR "windeployqt failed: ${deploy_result}")
endif()

# MSYS2 Qt/Clang needs libc++, libunwind, and winpthreads in addition to Qt.
# Resolve the installed binaries' dependency closure using the matching tools.
if(DEFINED ENV{MSYSTEM})
	find_program(OBJDUMP_EXECUTABLE NAMES llvm-objdump objdump
		HINTS "${TOOLCHAIN_RUNTIME_DIR}" "${QT_BIN_DIR}" REQUIRED)
	set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM "windows+pe")
	set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL "objdump")
	set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND "${OBJDUMP_EXECUTABLE}")
	file(GLOB_RECURSE deployed_dlls "${BIN_DIR}/*.dll")
	file(GET_RUNTIME_DEPENDENCIES
		EXECUTABLES "${EXECUTABLE_PATH}"
		LIBRARIES ${deployed_dlls}
		DIRECTORIES "${BIN_DIR}" "${QT_BIN_DIR}" "${TOOLCHAIN_RUNTIME_DIR}"
		PRE_EXCLUDE_REGEXES "[Aa][Pp][Ii]-[Mm][Ss]-.*" "[Ee][Xx][Tt]-[Mm][Ss]-.*"
		POST_EXCLUDE_REGEXES ".*[/\\\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\].*"
		RESOLVED_DEPENDENCIES_VAR runtime_dlls
		UNRESOLVED_DEPENDENCIES_VAR missing_dlls
	)
	if(missing_dlls)
		message(FATAL_ERROR "Unresolved Windows runtime dependencies: ${missing_dlls}")
	endif()
	foreach(runtime IN LISTS runtime_dlls)
		get_filename_component(runtime_dir "${runtime}" DIRECTORY)
		if(NOT runtime_dir STREQUAL BIN_DIR)
			file(INSTALL "${runtime}" DESTINATION "${BIN_DIR}")
		endif()
	endforeach()
endif()

file(GLOB KBURN_DLLS LIST_DIRECTORIES FALSE "${BIN_DIR}/*kburn*.dll")
file(GLOB LIBUSB_DLLS LIST_DIRECTORIES FALSE "${BIN_DIR}/*usb*.dll")
file(GLOB QT_CORE_DLLS LIST_DIRECTORIES FALSE "${BIN_DIR}/Qt6Core*.dll")
file(GLOB QT_WIDGET_DLLS LIST_DIRECTORIES FALSE "${BIN_DIR}/Qt6Widgets*.dll")
if(NOT KBURN_DLLS)
	message(FATAL_ERROR "Installed kburn DLL was not found under ${BIN_DIR}")
endif()
if(NOT LIBUSB_DLLS)
	message(FATAL_ERROR "Installed libusb DLL was not found under ${BIN_DIR}")
endif()
if(NOT QT_CORE_DLLS OR NOT QT_WIDGET_DLLS)
	message(FATAL_ERROR "windeployqt did not install the required Qt runtime DLLs")
endif()
if(NOT EXISTS "${BIN_DIR}/platforms/qwindows.dll")
	message(FATAL_ERROR "windeployqt did not install platforms/qwindows.dll")
endif()

# Release archives contain runtime files only.
file(REMOVE_RECURSE "${INSTALL_PREFIX}/include")
file(REMOVE_RECURSE "${INSTALL_PREFIX}/lib")
message(STATUS "Prepared Windows runtime layout in ${BIN_DIR}")
