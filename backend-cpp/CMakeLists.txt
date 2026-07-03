cmake_minimum_required(VERSION 3.25)

project(bridge_report_backend VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Drogon CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

add_library(bridge_report_backend_core
    src/archive/ArchivePaths.cpp
    src/config/AppConfig.cpp
    src/http/Cors.cpp
    src/identity/SystemNumber.cpp
    src/runtime/RuntimePaths.cpp
)

target_include_directories(bridge_report_backend_core
    PUBLIC
        include
)

target_link_libraries(bridge_report_backend_core
    PUBLIC
        Drogon::Drogon
)

add_executable(bridge-report-backend
    src/main.cpp
)

target_link_libraries(bridge-report-backend
    PRIVATE
        bridge_report_backend_core
        Drogon::Drogon
)

enable_testing()

add_executable(bridge_report_backend_tests
    tests/test_app_config.cpp
    tests/test_archive_paths.cpp
    tests/test_system_number.cpp
)

target_link_libraries(bridge_report_backend_tests
    PRIVATE
        bridge_report_backend_core
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(bridge_report_backend_tests)
