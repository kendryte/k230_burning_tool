cmake_minimum_required(VERSION 3.5)

project(DevTest VERSION 0.1 LANGUAGES CXX)

file(GLOB_RECURSE PROJECT_SOURCES "src/*.h" "src/*.cpp" "src/*.c")
add_executable(test ${PROJECT_SOURCES})

target_include_directories(test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../canaan-burn/include/public")
set_target_properties(test PROPERTIES LINKER_LANGUAGE CXX)

add_dependencies(test canaan_burn)
target_link_libraries(test PRIVATE canaan_burn)

# if(LINUX)
# target_link_libraries(test PRIVATE canaan_burn udev)
# endif()
install(
	TARGETS test
	RUNTIME DESTINATION bin
	LIBRARY DESTINATION lib
	ARCHIVE DESTINATION lib
)
