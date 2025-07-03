cmake_minimum_required(VERSION 3.5)

if(POLICY CMP0087)
	cmake_policy(SET CMP0087 NEW)
endif()

if(DEFINED ENV{CI})
	set(IS_CI TRUE)
else()
	set(IS_CI FALSE)
endif()

# Get the Git commit hash and store it in the variable GITHUB_VERSION
execute_process(
    COMMAND git rev-parse HEAD
    OUTPUT_VARIABLE GITHUB_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(BUILD_FOR_AVALON_NANO3)
    set(TARGET_AVALON_NANO3_DEFAULT ON)
else()
    set(TARGET_AVALON_NANO3_DEFAULT OFF)
endif()

option(TARGET_AVALON_NANO3 "Build for Avalon Nano3" ${BUILD_FOR_AVALON_NANO3})

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# when bump version, chane here
project(K230BurningTool VERSION 2.2.0 LANGUAGES CXX)

set(MY_VERSION $DATE)
string(TIMESTAMP MY_VERSION_STRING "%Y-%m-%d %H:%M")
# set(GITHUB_VERSION "$ENV{GITHUB_SHA}")
configure_file("${CMAKE_CURRENT_LIST_DIR}/src/config.h.in" "${CMAKE_CURRENT_BINARY_DIR}/autogen/config.h")

find_package(
	Qt6
	COMPONENTS Widgets LinguistTools Network Svg
	REQUIRED)

# Print the Qt installation directory
get_target_property(QtCore_location Qt6::Core IMPORTED_LOCATION)
get_filename_component(Qt_INSTALL_DIR ${QtCore_location} DIRECTORY)
get_target_property(QT_QMAKE_PATH Qt6::qmake LOCATION)

message(STATUS "Qt installation directory: ${Qt_INSTALL_DIR}")

file(GLOB_RECURSE PROJECT_SOURCES "src/*.cpp" "src/*.h" "src/*.ui")
set(PROJECT_RESOURCES "${CMAKE_CURRENT_SOURCE_DIR}/resources/main.qrc")

if(WIN32)
	enable_language("RC")
	set(WIN32_RESOURCES "${CMAKE_CURRENT_SOURCE_DIR}/resources/main.rc")
endif()

qt_add_executable(K230BurningTool MANUAL_FINALIZATION "${PROJECT_SOURCES}" "${WIN32_RESOURCES}" "${PROJECT_RESOURCES}")

set(TS_FILES resources/translations/K230_qt_zh_CN.ts)
qt_add_translations(K230BurningTool TS_FILES ${TS_FILES})

target_include_directories(K230BurningTool PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src")

target_link_libraries(K230BurningTool PRIVATE Qt6::Widgets Qt6::Network Qt6::Svg)

if(WIN32)
	set_target_properties(K230BurningTool PROPERTIES
		WIN32_EXECUTABLE TRUE
	)
elseif(APPLE)
	set_target_properties(K230BurningTool PROPERTIES
		MACOSX_BUNDLE TRUE
		MACOSX_BUNDLE_GUI_IDENTIFIER kendryte.com
		MACOSX_BUNDLE_ICON_FILE icon.icns
		MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_LIST_DIR}/resources/Info.plist
		MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
		MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
	)

	if(TARGET_AVALON_NANO3)
		set(K230BurningTool_ICON "${CMAKE_CURRENT_LIST_DIR}/resources/avalon_nano3.icns")
	else()
		set(K230BurningTool_ICON "${CMAKE_CURRENT_LIST_DIR}/resources/icon.icns")
	endif()

	add_custom_command(TARGET K230BurningTool POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:K230BurningTool>/../Resources"
		COMMAND ${CMAKE_COMMAND} -E copy ${K230BurningTool_ICON} "$<TARGET_FILE_DIR:K230BurningTool>/../Resources/"
		COMMENT "Copying icon file to app bundle"
	)
endif()

# if(WIN32)
# 	target_link_options(K230BurningTool BEFORE PRIVATE -static-libgcc -static-libstdc++)
# endif()

qt_finalize_executable(K230BurningTool)

target_include_directories(
	K230BurningTool PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/K230BurningTool_autogen/include")

target_include_directories(K230BurningTool PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/autogen/")

if(NOT DEFINED BUILD_PARENT)
	include("${CMAKE_TOOLCHAIN_FILE}")
	add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../canaan-burn" "${CMAKE_CURRENT_BINARY_DIR}/canaan-burn.dist")
endif()

add_dependencies(K230BurningTool canaan_burn)
target_link_libraries(K230BurningTool PRIVATE canaan_burn)

target_compile_definitions(K230BurningTool PRIVATE "SETTINGS_CATEGORY=\"k230Burning\"")

if(TARGET_AVALON_NANO3)
	target_compile_definitions(K230BurningTool PRIVATE "IS_AVALON_NANO3=1")
	if(WIN32)
		set_target_properties(K230BurningTool PROPERTIES OUTPUT_NAME Avalon_Home_Series_Firmware_Upgrade_Tool)
	endif()
else()
	target_compile_definitions(K230BurningTool PRIVATE "IS_AVALON_NANO3=0")
	set_target_properties(K230BurningTool PROPERTIES OUTPUT_NAME K230BurningTool)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
	target_compile_definitions(K230BurningTool PRIVATE "IS_DEBUG=0")
else()
	target_compile_definitions(K230BurningTool PRIVATE "IS_DEBUG=1")
endif()

install(
	TARGETS K230BurningTool
	RUNTIME DESTINATION bin
	BUNDLE  DESTINATION bin
	LIBRARY DESTINATION lib
	ARCHIVE DESTINATION lib
)

get_target_property(EXECUTABLE_NAME K230BurningTool OUTPUT_NAME)

if(WIN32)
	install(CODE "
		execute_process(
			COMMAND \"${CMAKE_COMMAND}\"
			\"-DQT_QMAKE_PATH=${QT_QMAKE_PATH}\"
			\"-DCMAKE_CACHEFILE_DIR=${CMAKE_BINARY_DIR}\"
			\"-DEXECUTABLE_NAME=${EXECUTABLE_NAME}\"
			-P \"${CMAKE_CURRENT_LIST_DIR}/win-install.cmake\"
			WORKING_DIRECTORY \"\${CMAKE_INSTALL_PREFIX}\"
			COMMAND_ECHO STDOUT
			COMMAND_ERROR_IS_FATAL ANY
		)
	")
elseif(LINUX)
	find_package(OpenSSL)

	if(NOT OPENSSL_FOUND)
		message(FATAL_ERROR "COuld not find Openssl, Please install libssl-dev or others")
	endif()

	list(APPEND SSL_LIBS ${OPENSSL_LIBRARIES})

	install(CODE "
	execute_process(
		COMMAND \"${CMAKE_COMMAND}\"
		\"-DQT_QMAKE_PATH=${QT_QMAKE_PATH}\"
		\"-DCMAKE_CACHEFILE_DIR=${CMAKE_BINARY_DIR}\"
		\"-DSSL_LIBS=${SSL_LIBS}\"
		\"-DEXECUTABLE_NAME=${EXECUTABLE_NAME}\"
		-P \"${CMAKE_CURRENT_LIST_DIR}/linux-install.cmake\"
		WORKING_DIRECTORY \"\${CMAKE_INSTALL_PREFIX}\"
		COMMAND_ECHO STDOUT
		COMMAND_ERROR_IS_FATAL ANY
	)
	")
elseif(APPLE)
	install(CODE "
	execute_process(
		COMMAND \"${CMAKE_COMMAND}\"
		\"-DQT_QMAKE_PATH=${QT_QMAKE_PATH}\"
		\"-DCMAKE_CACHEFILE_DIR=${CMAKE_BINARY_DIR}\"
		\"-DEXECUTABLE_NAME=${EXECUTABLE_NAME}\"
		-P \"${CMAKE_CURRENT_LIST_DIR}/mac-install.cmake\"
		WORKING_DIRECTORY \"\${CMAKE_INSTALL_PREFIX}\"
		COMMAND_ECHO STDOUT
		COMMAND_ERROR_IS_FATAL ANY
	)
	")
endif()
