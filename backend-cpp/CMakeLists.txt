cmake_minimum_required(VERSION 3.25)

project(bridge_report_backend VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Drogon CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

add_library(bridge_report_backend_core
    src/assessment/AssessmentService.cpp
    src/assessment/AssessmentConfirmationService.cpp
    src/archive/ArchivePaths.cpp
    src/archive/ExtractedPhotoArchive.cpp
    src/archive/SourceDbReference.cpp
    src/archive/TemporaryWordStorage.cpp
    src/archive/WordInputArchive.cpp
    src/auth/PasswordHash.cpp
    src/config/AppConfig.cpp
    src/contracts/AnnualInspectionContract.cpp
    src/db/AuthRepository.cpp
    src/db/BridgeAdministrationRepository.cpp
    src/db/BridgeDeletionRepository.cpp
    src/db/ComponentArchiveRepository.cpp
    src/db/ComponentInventoryRepository.cpp
    src/db/ComponentRangeSplitRepository.cpp
    src/db/DbClientFactory.cpp
    src/db/CommitLatch.cpp
    src/db/DefectThreadRepository.cpp
    src/db/EditLockRepository.cpp
    src/db/InspectionYearDeletionRepository.cpp
    src/db/ImportRecordDeletionRepository.cpp
    src/db/ReviewRepository.cpp
    src/db/RatingTreeRepository.cpp
    src/db/StandardRepository.cpp
    src/db/WorkspaceRepository.cpp
    src/db/WordImportRepository.cpp
    src/db/ImportBindingRepository.cpp
    src/deletion/ArchiveFileCleanupCoordinator.cpp
    src/deletion/ArchiveFileDeletionCore.cpp
    src/deletion/ArchivedFileDeletionQueue.cpp
    src/deletion/BridgeDeletionModels.cpp
    src/deletion/InspectionYearDeletionModels.cpp
    src/deletion/ImportRecordDeletionModels.cpp
    src/deletion/TemporaryWordCleanupCoordinator.cpp
    src/http/AuthRoutes.cpp
    src/http/AssessmentRoutes.cpp
    src/http/BridgeAdministrationRoutes.cpp
    src/http/ComponentArchiveRoutes.cpp
    src/http/ComponentInventoryRoutes.cpp
    src/http/Cors.cpp
    src/http/DefectPhotoRoutes.cpp
    src/http/DefectThreadRoutes.cpp
    src/http/DefectMatchingRoutes.cpp
    src/http/EditLockRoutes.cpp
    src/http/ImportBindingRoutes.cpp
    src/http/RoutePreflightAudit.cpp
    src/http/ImportConfirmRoutes.cpp
    src/http/ImportRecordDeletionRoutes.cpp
    src/http/InspectionYearDeletionRoutes.cpp
    src/http/RatingTreeRoutes.cpp
    src/http/ReviewRoutes.cpp
    src/http/StandardRoutes.cpp
    src/http/WordImportRoutes.cpp
    src/http/WorkspaceRoutes.cpp
    src/identity/SystemNumber.cpp
    src/inventory/ComponentInventoryGenerator.cpp
    src/inventory/ComponentInventoryModels.cpp
    src/inventory/ComponentRangeParser.cpp
    src/inventory/ComponentMatcher.cpp
    src/inventory/ComponentCategoryLexicon.cpp
    src/inventory/NumberingTemplate.cpp
    src/inventory/ComponentPartCatalog.cpp
    src/inventory/SideComponentPair.cpp
    src/rating_tree/RatingTreeCompiler.cpp
    src/rating_tree/RatingTreeMatchText.cpp
    src/rating_tree/RatingTreeModels.cpp
    src/rating_tree/RatingTreePackageLoader.cpp
    src/rating_tree/RatingTreeResolver.cpp
    src/review/ConfirmPlan.cpp
    src/review/ComponentRangeSplitPlanner.cpp
    src/review/ContractCompatibility.cpp
    src/review/DefectRatingTreeMatching.cpp
    src/review/DraftValidation.cpp
    src/review/PreflightReport.cpp
    src/review/ReviewModels.cpp
    src/review/ReviewStatistics.cpp
    src/review/ThreadCanonicalKey.cpp
    src/review/ThreadTriageGrouping.cpp
    src/review/ThreadSuggestions.cpp
    src/review/WorkspaceModels.cpp
    src/runtime/RuntimePaths.cpp
    src/standards/StandardModels.cpp
    src/standards/AssessmentModels.cpp
    src/standards/DefectIndicatorResolver.cpp
    src/standards/H21Evaluator.cpp
    src/standards/Jtg5120MaintenanceStandard.cpp
    src/standards/StandardCatalogModels.cpp
    src/standards/StandardPackageLoader.cpp
    src/standards/StandardRegistry.cpp
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
    tests/test_archive_file_cleanup_coordinator.cpp
    tests/test_archive_file_deletion_core.cpp
    tests/test_archive_paths.cpp
    tests/test_assessment_routes.cpp
    tests/test_assessment_confirmation_service.cpp
    tests/test_assessment_service.cpp
    tests/test_archived_file_deletion_queue.cpp
    tests/test_bridge_deletion_models.cpp
    tests/test_bridge_administration_repository.cpp
    tests/test_bridge_administration_routes.cpp
    tests/test_bridge_deletion_repository.cpp
    tests/test_component_archive_repository.cpp
    tests/test_component_archive_routes.cpp
    tests/test_component_inventory_generator.cpp
    tests/test_component_inventory_repository.cpp
    tests/test_component_inventory_routes.cpp
    tests/test_import_binding_routes.cpp
    tests/test_component_range_parser.cpp
    tests/test_component_range_split_planner.cpp
    tests/test_component_matcher.cpp
    tests/test_h21_bridge_evaluation.cpp
    tests/test_h21_component_evaluation.cpp
    tests/test_h21_grade_and_controls.cpp
    tests/test_h21_part_evaluation.cpp
    tests/test_confirm_plan.cpp
    tests/test_commit_latch.cpp
    tests/test_defect_matching_routes.cpp
    tests/test_defect_photo_routes.cpp
    tests/test_defect_rating_tree_matching.cpp
    tests/test_defect_thread_repository.cpp
    tests/test_defect_indicator_resolver.cpp
    tests/test_defect_thread_routes.cpp
    tests/test_contract_compatibility.cpp
    tests/test_db_client_factory.cpp
    tests/test_draft_validation.cpp
    tests/test_edit_lock_repository.cpp
    tests/test_extracted_photo_archive.cpp
    tests/bench_defect_matching_chain.cpp
    tests/test_route_preflight_audit.cpp
    tests/test_side_component_pair.cpp
    tests/test_source_db_reference.cpp
    tests/test_two_sided_binding_end_to_end.cpp
    tests/test_inspection_year_deletion_models.cpp
    tests/test_inspection_year_deletion_repository.cpp
    tests/test_inspection_year_deletion_routes.cpp
    tests/test_import_record_deletion_models.cpp
    tests/test_import_record_deletion_repository.cpp
    tests/test_import_record_deletion_routes.cpp
    tests/test_jtg_5120_package.cpp
    tests/test_numbering_template.cpp
    tests/test_component_part_catalog.cpp
    tests/test_component_category_lexicon.cpp
    tests/test_jtg_5120_maintenance_standard.cpp
    tests/test_jtg_h21_package.cpp
    tests/test_password_hash.cpp
    tests/test_preflight_report.cpp
    tests/test_photo_content.cpp
    tests/test_review_models.cpp
    tests/test_review_repository.cpp
    tests/test_review_statistics.cpp
    tests/test_rating_tree_compiler.cpp
    tests/test_rating_tree_package_loader.cpp
    tests/test_rating_tree_repository.cpp
    tests/test_rating_tree_resolver.cpp
    tests/test_rating_tree_routes.cpp
    tests/test_system_number.cpp
    tests/test_standard_package_loader.cpp
    tests/test_standard_repository.cpp
    tests/test_standard_routes.cpp
    tests/test_standard_registry.cpp
    tests/test_thread_canonical_key.cpp
    tests/test_thread_triage_grouping.cpp
    tests/test_thread_triage_fixture.cpp
    tests/test_thread_suggestions.cpp
    tests/test_temporary_word_cleanup_coordinator.cpp
    tests/test_word_import_routes.cpp
    tests/test_word_import_repository.cpp
    tests/test_import_binding_repository.cpp
    tests/test_word_input_archive.cpp
    tests/test_workspace_models.cpp
    tests/test_workspace_repository.cpp
    tests/test_workspace_routes.cpp
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
