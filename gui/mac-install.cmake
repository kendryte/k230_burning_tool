cmake_minimum_required(VERSION 3.18)

if(NOT DEFINED INSTALL_PREFIX OR INSTALL_PREFIX STREQUAL "")
	message(FATAL_ERROR "INSTALL_PREFIX is required")
endif()
if(NOT DEFINED EXECUTABLE_NAME OR EXECUTABLE_NAME STREQUAL "")
	message(FATAL_ERROR "EXECUTABLE_NAME is required")
endif()

set(APP_PATH "${INSTALL_PREFIX}/bin/${EXECUTABLE_NAME}.app")
if(NOT IS_DIRECTORY "${APP_PATH}")
	message(FATAL_ERROR "Installed application bundle not found: ${APP_PATH}")
endif()

find_program(MACDEPLOYQT_EXECUTABLE macdeployqt REQUIRED)
find_program(HDIUTIL_EXECUTABLE hdiutil REQUIRED)
find_program(FILE_EXECUTABLE file REQUIRED)
find_program(OTOOL_EXECUTABLE otool REQUIRED)

execute_process(
	COMMAND "${MACDEPLOYQT_EXECUTABLE}" "${APP_PATH}"
		-verbose=1
		"-libpath=${APP_PATH}/Contents/Frameworks"
	RESULT_VARIABLE deploy_result
	COMMAND_ECHO STDOUT
)
if(NOT deploy_result STREQUAL "0")
	message(FATAL_ERROR "macdeployqt failed: ${deploy_result}")
endif()

function(is_macho path result_var)
	execute_process(
		COMMAND "${FILE_EXECUTABLE}" -b "${path}"
		OUTPUT_VARIABLE file_description
		OUTPUT_STRIP_TRAILING_WHITESPACE
		RESULT_VARIABLE file_result
	)
	if(file_result STREQUAL "0" AND file_description MATCHES "Mach-O")
		set(${result_var} TRUE PARENT_SCOPE)
	else()
		set(${result_var} FALSE PARENT_SCOPE)
	endif()
endfunction()

# Reject bundles which still depend on build/install-tree or Homebrew paths.
file(GLOB_RECURSE bundle_files LIST_DIRECTORIES FALSE "${APP_PATH}/*")
foreach(candidate IN LISTS bundle_files)
	if(IS_SYMLINK "${candidate}")
		continue()
	endif()
	is_macho("${candidate}" candidate_is_macho)
	if(NOT candidate_is_macho)
		continue()
	endif()

	execute_process(
		COMMAND "${OTOOL_EXECUTABLE}" -L "${candidate}"
		OUTPUT_VARIABLE linked_libraries
		OUTPUT_STRIP_TRAILING_WHITESPACE
		RESULT_VARIABLE otool_result
	)
	if(NOT otool_result STREQUAL "0")
		message(FATAL_ERROR "Failed to inspect ${candidate} with otool")
	endif()
	string(REPLACE "\n" ";" dependency_lines "${linked_libraries}")
	foreach(dependency_line IN LISTS dependency_lines)
		string(STRIP "${dependency_line}" dependency_line)
		if(dependency_line MATCHES ":$")
			continue()
		endif()
		foreach(forbidden_path IN ITEMS "${INSTALL_PREFIX}" "${CMAKE_CACHEFILE_DIR}" "/opt/homebrew/" "/usr/local/")
			if(forbidden_path STREQUAL "")
				continue()
			endif()
			string(FIND "${dependency_line}" "${forbidden_path}" forbidden_position)
			if(NOT forbidden_position EQUAL -1)
				message(FATAL_ERROR
					"${candidate} has an unbundled dependency:\n${dependency_line}")
			endif()
		endforeach()
	endforeach()
endforeach()

if(DEFINED NOTARY_PROFILE AND NOT NOTARY_PROFILE STREQUAL ""
		AND (NOT DEFINED SIGN_IDENTITY OR SIGN_IDENTITY STREQUAL ""))
	message(FATAL_ERROR "Notarization requires a signing identity")
endif()

if(DEFINED SIGN_IDENTITY AND NOT SIGN_IDENTITY STREQUAL "")
	find_program(CODESIGN_EXECUTABLE codesign REQUIRED)
	execute_process(
		COMMAND "${CODESIGN_EXECUTABLE}"
			--deep --force --timestamp --options runtime
			--sign "${SIGN_IDENTITY}" "${APP_PATH}"
		RESULT_VARIABLE sign_result
		COMMAND_ECHO STDOUT
	)
	if(NOT sign_result STREQUAL "0")
		message(FATAL_ERROR "Failed to sign ${APP_PATH}: ${sign_result}")
	endif()
	execute_process(
		COMMAND "${CODESIGN_EXECUTABLE}" --verify --deep --strict --verbose=2
			"${APP_PATH}"
		RESULT_VARIABLE verify_result
		COMMAND_ECHO STDOUT
	)
	if(NOT verify_result STREQUAL "0")
		message(FATAL_ERROR "Signature verification failed for ${APP_PATH}")
	endif()
else()
	message(STATUS "K230_BURNING_MACOS_SIGN_IDENTITY is empty; packaging an unsigned app")
endif()

if(DEFINED NOTARY_PROFILE AND NOT NOTARY_PROFILE STREQUAL "")
	find_program(XCRUN_EXECUTABLE xcrun REQUIRED)
	find_program(DITTO_EXECUTABLE ditto REQUIRED)
	set(NOTARY_ZIP "${CMAKE_CACHEFILE_DIR}/${EXECUTABLE_NAME}-notary.zip")
	file(REMOVE "${NOTARY_ZIP}")
	execute_process(
		COMMAND "${DITTO_EXECUTABLE}" -c -k --sequesterRsrc --keepParent
			"${APP_PATH}" "${NOTARY_ZIP}"
		RESULT_VARIABLE zip_result
		COMMAND_ECHO STDOUT
	)
	if(NOT zip_result STREQUAL "0")
		message(FATAL_ERROR "Failed to create notarization ZIP")
	endif()
	execute_process(
		COMMAND "${XCRUN_EXECUTABLE}" notarytool submit "${NOTARY_ZIP}"
			--keychain-profile "${NOTARY_PROFILE}" --wait
		RESULT_VARIABLE notary_result
		COMMAND_ECHO STDOUT
	)
	file(REMOVE "${NOTARY_ZIP}")
	if(NOT notary_result STREQUAL "0")
		message(FATAL_ERROR "Application notarization failed")
	endif()
	execute_process(
		COMMAND "${XCRUN_EXECUTABLE}" stapler staple "${APP_PATH}"
		RESULT_VARIABLE staple_result
		COMMAND_ECHO STDOUT
	)
	if(NOT staple_result STREQUAL "0")
		message(FATAL_ERROR "Failed to staple ${APP_PATH}")
	endif()
endif()

if(NOT DEFINED DMG_PATH OR DMG_PATH STREQUAL "")
	set(DMG_PATH "${CMAKE_CACHEFILE_DIR}/${EXECUTABLE_NAME}.dmg")
endif()
get_filename_component(DMG_DIRECTORY "${DMG_PATH}" DIRECTORY)
file(MAKE_DIRECTORY "${DMG_DIRECTORY}")
file(REMOVE "${DMG_PATH}")
execute_process(
	COMMAND "${HDIUTIL_EXECUTABLE}" create
		-volname "${EXECUTABLE_NAME}"
		-srcfolder "${APP_PATH}"
		-ov -format UDZO "${DMG_PATH}"
	RESULT_VARIABLE dmg_result
	COMMAND_ECHO STDOUT
)
if(NOT dmg_result STREQUAL "0" OR NOT EXISTS "${DMG_PATH}")
	message(FATAL_ERROR "Failed to create ${DMG_PATH}")
endif()

if(DEFINED SIGN_IDENTITY AND NOT SIGN_IDENTITY STREQUAL "")
	execute_process(
		COMMAND "${CODESIGN_EXECUTABLE}" --force --timestamp
			--sign "${SIGN_IDENTITY}" "${DMG_PATH}"
		RESULT_VARIABLE dmg_sign_result
		COMMAND_ECHO STDOUT
	)
	if(NOT dmg_sign_result STREQUAL "0")
		message(FATAL_ERROR "Failed to sign ${DMG_PATH}")
	endif()
endif()

if(DEFINED NOTARY_PROFILE AND NOT NOTARY_PROFILE STREQUAL "")
	execute_process(
		COMMAND "${XCRUN_EXECUTABLE}" notarytool submit "${DMG_PATH}"
			--keychain-profile "${NOTARY_PROFILE}" --wait
		RESULT_VARIABLE dmg_notary_result
		COMMAND_ECHO STDOUT
	)
	if(NOT dmg_notary_result STREQUAL "0")
		message(FATAL_ERROR "DMG notarization failed")
	endif()
	execute_process(
		COMMAND "${XCRUN_EXECUTABLE}" stapler staple "${DMG_PATH}"
		RESULT_VARIABLE dmg_staple_result
		COMMAND_ECHO STDOUT
	)
	if(NOT dmg_staple_result STREQUAL "0")
		message(FATAL_ERROR "Failed to staple ${DMG_PATH}")
	endif()
endif()

message(STATUS "Created macOS artifact: ${DMG_PATH}")
