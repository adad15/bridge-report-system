cmake_minimum_required(VERSION 3.25)

project(bridge_report_backend VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Drogon CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

add_library(bridge_report_backend_core
    src/archive/ArchivePaths.cpp
    src/archive/ExtractedPhotoArchive.cpp
    src/config/AppConfig.cpp
    src/contracts/AnnualInspectionContract.cpp
    src/db/DbClientFactory.cpp
    src/db/ReviewRepository.cpp
    src/http/Cors.cpp
    src/http/ImportConfirmRoutes.cpp
    src/http/ReviewRoutes.cpp
    src/identity/SystemNumber.cpp
    src/review/ConfirmPlan.cpp
    src/review/ContractCompatibility.cpp
    src/review/DraftValidation.cpp
    src/review/PreflightReport.cpp
    src/review/ReviewModels.cpp
    src/review/ReviewStatistics.cpp
    src/runtime/RuntimePaths.cpp
)

target_include_directories(bridge_report_backend_core
    PUBLIC
        include
)

target_link_libraries(bridge_report_backend_core
    PUBLIC
        Drogon::Drogon
        OpenSSL::Crypto
)

if(MSVC)
    target_compile_options(bridge_report_backend_core PUBLIC /utf-8)
endif()

add_executable(bridge-report-backend
    src/main.cpp
)

target_link_libraries(bridge-report-backend
    PRIVATE
        bridge_report_backend_core
        Drogon::Drogon
)

if(MSVC)
    target_compile_options(bridge-report-backend PRIVATE /utf-8)
endif()

enable_testing()

add_executable(bridge_report_backend_tests
    tests/test_annual_inspection_contract.cpp
    tests/test_app_config.cpp
    tests/test_archive_paths.cpp
    tests/test_confirm_plan.cpp
    tests/test_contract_compatibility.cpp
    tests/test_db_client_factory.cpp
    tests/test_draft_validation.cpp
    tests/test_extracted_photo_archive.cpp
    tests/test_preflight_report.cpp
    tests/test_review_models.cpp
    tests/test_review_repository.cpp
    tests/test_review_statistics.cpp
    tests/test_system_number.cpp
)

target_link_libraries(bridge_report_backend_tests
    PRIVATE
        bridge_report_backend_core
        GTest::gtest_main
)

target_compile_definitions(bridge_report_backend_tests
    PRIVATE
        BRIDGE_REPORT_REPOSITORY_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
)

if(MSVC)
    target_compile_options(bridge_report_backend_tests PRIVATE /utf-8)
endif()

include(GoogleTest)
gtest_discover_tests(bridge_report_backend_tests)
