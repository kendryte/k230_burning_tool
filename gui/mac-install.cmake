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

# deploy.
find_program(MACDEPLOY "macdeployqt" REQUIRED)
message("RUN ${MACDEPLOY} K230BurningTool IN ${CMAKE_CACHEFILE_DIR}")

execute_process(
	COMMAND ${MACDEPLOY} ${EXECUTABLE_NAME}.app -verbose=1 -dmg
	WORKING_DIRECTORY "${CMAKE_CACHEFILE_DIR}/gui"
	COMMAND_ECHO STDOUT
	COMMAND_ERROR_IS_FATAL ANY
)
