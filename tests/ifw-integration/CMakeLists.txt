cmake_minimum_required(VERSION 3.16)
project(IfwIntegrationTests LANGUAGES CXX)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Widgets Network Test)
enable_testing()
add_executable(test-ifw-integration
    test-ifw-integration.cpp
    ../../gui/src/common/IfwInstallation.cpp
    ../../gui/src/common/UpdateChecker.cpp
)
target_include_directories(test-ifw-integration PRIVATE ../../gui/src)
target_link_libraries(test-ifw-integration PRIVATE Qt6::Widgets Qt6::Network Qt6::Test)
add_test(NAME ifw-integration COMMAND test-ifw-integration)
set_tests_properties(ifw-integration PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
