function(parse_cmake_cache CACHE_FILE)
    file(STRINGS "${CACHE_FILE}" lines REGEX "=")

    foreach(line IN LISTS lines)
        string(FIND "${line}" ":" start_of_type)
        string(FIND "${line}" "=" start_of_assign)

        if(start_of_type EQUAL -1)
            set(start_of_type "${start_of_assign}")
        endif()

        string(SUBSTRING "${line}" 0 ${start_of_type} name)
        math(EXPR start_of_value "${start_of_assign} + 1")
        string(SUBSTRING "${line}" ${start_of_value} -1 value)

        set(${name} "${value}" PARENT_SCOPE)
    endforeach()
endfunction()

parse_cmake_cache("${CMAKE_CACHEFILE_DIR}/CMakeCache.txt")

get_filename_component(DIST_DIR "." ABSOLUTE)

# Build and deploy the .app bundle first
if(APPLE)
    set(APP_NAME "${EXECUTABLE_NAME}.app")
    set(DMG_NAME "${EXECUTABLE_NAME}.dmg")
    set(DEPLOY_DIR "${CMAKE_CACHEFILE_DIR}/gui")

    # Step 1: Use macdeployqt to prepare the .app bundle
    find_program(MACDEPLOY "macdeployqt" REQUIRED)
    message(STATUS "Preparing ${APP_NAME} with macdeployqt")

    execute_process(
        COMMAND ${MACDEPLOY} ${APP_NAME} -verbose=1
        WORKING_DIRECTORY "${DEPLOY_DIR}"
        COMMAND_ECHO STDOUT
        COMMAND_ERROR_IS_FATAL ANY
    )

    # Step 2: Sign the .app bundle
    find_program(CODESIGN "codesign")
    find_program(XCRUN "xcrun")
    
    if(CODESIGN AND XCRUN)
        message(STATUS "Signing ${APP_NAME}")

        # Deep sign with hardened runtime
        execute_process(
            COMMAND ${CODESIGN} --deep --force --verify --verbose --timestamp 
                    --options=runtime --sign "Developer ID Application" "${APP_NAME}"
            WORKING_DIRECTORY "${DEPLOY_DIR}"
            COMMAND_ECHO STDOUT
            COMMAND_ERROR_IS_FATAL ANY
        )

        # Step 3: Notarize the .app (optional)
        set(ZIP_NAME "${EXECUTABLE_NAME}.zip")
        message(STATUS "Creating zip for notarization: ${ZIP_NAME}")
        
        execute_process(
            COMMAND ${XCRUN} ditto -c -k --sequesterRsrc --keepParent "${APP_NAME}" "${ZIP_NAME}"
            WORKING_DIRECTORY "${DEPLOY_DIR}"
            COMMAND_ECHO STDOUT
        )

        message(STATUS "Submitting ${ZIP_NAME} for notarization")
        execute_process(
            COMMAND ${XCRUN} notarytool submit "${ZIP_NAME}" 
                    --keychain-profile "AC_PASSWORD" --wait
            WORKING_DIRECTORY "${DEPLOY_DIR}"
            COMMAND_ECHO STDOUT
            COMMAND_ERROR_IS_FATAL ANY
        )

        message(STATUS "Stapling notarization ticket to ${APP_NAME}")
        execute_process(
            COMMAND ${XCRUN} stapler staple "${APP_NAME}"
            WORKING_DIRECTORY "${DEPLOY_DIR}"
            COMMAND_ECHO STDOUT
        )

        # Step 4: Create DMG from signed .app
        message(STATUS "Creating DMG: ${DMG_NAME}")
		# Fallback to hdiutil if custom script not available
		execute_process(
			COMMAND hdiutil create -volname "${EXECUTABLE_NAME}" 
					-srcfolder "${APP_NAME}" -ov -format UDZO "${DMG_NAME}"
			WORKING_DIRECTORY "${DEPLOY_DIR}"
			COMMAND_ECHO STDOUT
			COMMAND_ERROR_IS_FATAL ANY
		)

        # Step 5: Sign and notarize the DMG
        message(STATUS "Signing DMG: ${DMG_NAME}")
        execute_process(
            COMMAND ${CODESIGN} --force --verify --verbose --timestamp 
                    --sign "Developer ID Application" "${DMG_NAME}"
            WORKING_DIRECTORY "${DEPLOY_DIR}"
            COMMAND_ECHO STDOUT
        )

        message(STATUS "Submitting DMG for notarization")
        execute_process(
            COMMAND ${XCRUN} notarytool submit "${DMG_NAME}" 
                    --keychain-profile "AC_PASSWORD" --wait
            WORKING_DIRECTORY "${DEPLOY_DIR}"
            COMMAND_ECHO STDOUT
        )

        message(STATUS "Stapling notarization ticket to DMG")
        execute_process(
            COMMAND ${XCRUN} stapler staple "${DMG_NAME}"
            WORKING_DIRECTORY "${DEPLOY_DIR}"
            COMMAND_ECHO STDOUT
        )
    else()
        message(WARNING "codesign or xcrun not found - skipping signing and notarization")
        # Just create the DMG without signing
        execute_process(
            COMMAND hdiutil create -volname "${EXECUTABLE_NAME}" 
                    -srcfolder "${APP_NAME}" -ov -format UDZO "${DMG_NAME}"
            WORKING_DIRECTORY "${DEPLOY_DIR}"
            COMMAND_ECHO STDOUT
            COMMAND_ERROR_IS_FATAL ANY
        )
    endif()
endif()
