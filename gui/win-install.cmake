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

function(get_lib_path LIB_NAME LIB_VAR)
    # Find the library

    
    if(NOT TEMP_VAR)
        message(FATAL_ERROR "Cannot find ${LIB_NAME} in ${MINGW_PATH}/bin")
    endif()

    # Set the output variable
    set(${LIB_VAR} ${TEMP_VAR} PARENT_SCOPE)
endfunction()

parse_cmake_cache("${CMAKE_CACHEFILE_DIR}/CMakeCache.txt")

get_filename_component(DIST_DIR "." ABSOLUTE)
message(VERBOSE "RUN windeployqt.exe ${EXECUTABLE_NAME} IN ${DIST_DIR}/bin")

execute_process(
	COMMAND "windeployqt.exe" "${EXECUTABLE_NAME}.exe"
	WORKING_DIRECTORY "${DIST_DIR}/bin"
	COMMAND_ECHO STDOUT
	COMMAND_ERROR_IS_FATAL ANY
)

# Extract the path of the C++ compiler
get_filename_component(COMPILER_PATH ${CMAKE_CXX_COMPILER} DIRECTORY)
# Assume MinGW root directory is the parent directory of the compiler's directory
get_filename_component(MINGW_PATH ${COMPILER_PATH} DIRECTORY)
message(STATUS "MINGW_PATH ${MINGW_PATH}")

find_file(LIBWINPTHREAD NAMES "libwinpthread-1.dll" PATHS
	"${MINGW_PATH}/bin"
	PATH_SUFFIXES bin
	NO_DEFAULT_PATH
)
file(INSTALL ${LIBWINPTHREAD} DESTINATION "${DIST_DIR}/bin")

find_file(LIB_GCC_SEH NAMES "libgcc_s_seh-1.dll" PATHS
	"${MINGW_PATH}/bin"
	PATH_SUFFIXES bin
	NO_DEFAULT_PATH
)
file(INSTALL ${LIB_GCC_SEH} DESTINATION "${DIST_DIR}/bin")

find_file(LIBSTDCPP6 NAMES "libstdc++-6.dll" PATHS
	"${MINGW_PATH}/bin"
	PATH_SUFFIXES bin
	NO_DEFAULT_PATH
)
file(INSTALL ${LIBSTDCPP6} DESTINATION "${DIST_DIR}/bin")

set(SYSTEM32_PATH "$ENV{SystemRoot}/system32")

if(NOT EXISTS "${SYSTEM32_PATH}/notepad.exe")
	message(FATAL_ERROR "invalid windows install: system32 \"${SYSTEM32_PATH}\" not valid")
endif()

foreach(i vcruntime140.dll ucrtbase.dll)
	file(INSTALL "${SYSTEM32_PATH}/${i}" DESTINATION "${DIST_DIR}/bin")
endforeach()

file(REMOVE_RECURSE "${DIST_DIR}/include")
file(REMOVE_RECURSE "${DIST_DIR}/lib")
