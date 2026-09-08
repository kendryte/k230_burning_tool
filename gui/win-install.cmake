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

execute_process(
	COMMAND "${WINDEPLOYQT_EXECUTABLE}"
		--release
		--compiler-runtime
		--no-translations
		"${EXECUTABLE_PATH}"
	WORKING_DIRECTORY "${BIN_DIR}"
	RESULT_VARIABLE deploy_result
	COMMAND_ECHO STDOUT
)
if(NOT deploy_result STREQUAL "0")
	message(FATAL_ERROR "windeployqt failed: ${deploy_result}")
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
