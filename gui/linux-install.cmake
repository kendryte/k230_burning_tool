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

# copy openssl libs.
foreach(lib ${SSL_LIBS})
    file(INSTALL ${lib} DESTINATION "${DIST_DIR}/lib" FOLLOW_SYMLINK_CHAIN)
endforeach()

# copy desktop and icon
set(_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/resources")
set(_ICON "${_SRC_DIR}/icon.png")
set(_DESKTOP "${_SRC_DIR}/K230BurningTool.desktop")
set(_APPRUN "${_SRC_DIR}/AppRun")

file(INSTALL ${_ICON} DESTINATION "${DIST_DIR}/share/icons/hicolor/256x256")
file(INSTALL ${_DESKTOP} DESTINATION "${DIST_DIR}/share/applications")
file(INSTALL ${_APPRUN} DESTINATION "${DIST_DIR}/../" PERMISSIONS OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

# deploy.
find_program(LINUXDEPLOY "linuxdeployqt" REQUIRED)
message("RUN ${LINUXDEPLOY} K230BurningTool IN ${DIST_DIR}/bin")

execute_process(
	COMMAND ${LINUXDEPLOY} "${DIST_DIR}/share/applications/K230BurningTool.desktop" -qmake="${QT_QMAKE_PATH}" -appimage
	WORKING_DIRECTORY "${CMAKE_CACHEFILE_DIR}"
	COMMAND_ECHO STDOUT
	COMMAND_ERROR_IS_FATAL ANY
)
