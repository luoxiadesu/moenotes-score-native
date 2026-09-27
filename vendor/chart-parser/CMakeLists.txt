cmake_minimum_required(VERSION 3.16)
project(moenotes-chart-parser VERSION 0.3.0 LANGUAGES C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(MOENOTES_BUILD_CLI "Build the MoeNotes chart command line tool" ON)
option(MOENOTES_BUILD_TESTS "Build C regression tests" ON)
option(MOENOTES_BUILD_FUZZER "Build the Clang libFuzzer target" OFF)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(ZLIB REQUIRED)

add_library(moenotes_chart_parser STATIC
    src/moenotes_chart_parser.c
    third_party/yyjson.c)
add_library(moenotes::chart_parser ALIAS moenotes_chart_parser)
set_target_properties(moenotes_chart_parser PROPERTIES EXPORT_NAME chart_parser)
target_include_directories(moenotes_chart_parser
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
target_link_libraries(moenotes_chart_parser PUBLIC ZLIB::ZLIB)
if(MSVC)
    target_compile_definitions(moenotes_chart_parser PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(moenotes_chart_parser PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(moenotes_chart_parser PRIVATE -ffp-contract=off)
    target_link_libraries(moenotes_chart_parser PUBLIC m)
endif()

install(TARGETS moenotes_chart_parser EXPORT moenotes-chart-parser-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/moenotes_chart_parser.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/moenotes-chart-parser)
# The static library contains yyjson; retain its upstream license on installation.
install(FILES third_party/LICENSE.yyjson
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/moenotes-chart-parser)
install(FILES README.md CHANGELOG.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(DIRECTORY docs/ DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs)
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES examples/basic.c DESTINATION ${CMAKE_INSTALL_DOCDIR}/examples)
install(FILES include/moenotes_chart_parser.h DESTINATION ${CMAKE_INSTALL_DOCDIR}/include)
install(FILES third_party/LICENSE.yyjson DESTINATION ${CMAKE_INSTALL_DOCDIR}/third_party)

set(MOENOTES_CMAKE_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/moenotes-chart-parser)
configure_package_config_file(cmake/moenotes-chart-parser-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/moenotes-chart-parser-config.cmake
    INSTALL_DESTINATION ${MOENOTES_CMAKE_INSTALL_DIR})
# Pre-1.0 minor releases may contain explicitly documented breaking changes.
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/moenotes-chart-parser-config-version.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMinorVersion)
install(EXPORT moenotes-chart-parser-targets
    NAMESPACE moenotes:: DESTINATION ${MOENOTES_CMAKE_INSTALL_DIR})
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/moenotes-chart-parser-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/moenotes-chart-parser-config-version.cmake
    DESTINATION ${MOENOTES_CMAKE_INSTALL_DIR})

if(MOENOTES_BUILD_CLI)
    add_executable(moenotes-chart-parser cli/moenotes_chart_parser_cli.c)
    target_link_libraries(moenotes-chart-parser PRIVATE moenotes_chart_parser)
    install(TARGETS moenotes-chart-parser RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(MOENOTES_BUILD_TESTS)
    enable_testing()
    add_executable(moenotes-chart-parser-test tests/test_moenotes_chart_parser.c)
    target_link_libraries(moenotes-chart-parser-test PRIVATE moenotes_chart_parser)
    add_test(NAME moenotes-chart-parser-regression COMMAND moenotes-chart-parser-test)
    add_executable(moenotes-chart-parser-features tests/test_features.c)
    target_link_libraries(moenotes-chart-parser-features PRIVATE moenotes_chart_parser)
    add_test(NAME moenotes-chart-parser-features COMMAND moenotes-chart-parser-features)
    add_executable(moenotes-chart-parser-conversion tests/test_conversion.c)
    target_link_libraries(moenotes-chart-parser-conversion PRIVATE moenotes_chart_parser)
    add_test(NAME moenotes-chart-parser-conversion COMMAND moenotes-chart-parser-conversion)
    add_executable(moenotes-chart-parser-api-contract tests/test_api_contract.c)
    target_link_libraries(moenotes-chart-parser-api-contract PRIVATE moenotes_chart_parser)
    target_compile_definitions(moenotes-chart-parser-api-contract PRIVATE
        EXPECTED_VERSION="${PROJECT_VERSION}")
    add_test(NAME moenotes-chart-parser-api-contract COMMAND moenotes-chart-parser-api-contract)
    add_executable(moenotes-chart-parser-completion tests/test_completion.c)
    target_link_libraries(moenotes-chart-parser-completion PRIVATE moenotes_chart_parser)
    add_test(NAME moenotes-chart-parser-completion COMMAND moenotes-chart-parser-completion)
    add_executable(moenotes-chart-parser-example examples/basic.c)
    target_link_libraries(moenotes-chart-parser-example PRIVATE moenotes_chart_parser)
    add_test(NAME moenotes-chart-parser-example COMMAND moenotes-chart-parser-example)
    if(MOENOTES_BUILD_CLI)
        add_test(NAME moenotes-chart-parser-version COMMAND moenotes-chart-parser --version)
        set_tests_properties(moenotes-chart-parser-version PROPERTIES
            PASS_REGULAR_EXPRESSION "moenotes-chart-parser ${PROJECT_VERSION}")
    endif()
    if(MSVC)
        target_compile_options(moenotes-chart-parser-test PRIVATE /UNDEBUG)
        target_compile_options(moenotes-chart-parser-features PRIVATE /UNDEBUG)
        target_compile_options(moenotes-chart-parser-conversion PRIVATE /UNDEBUG)
        target_compile_options(moenotes-chart-parser-api-contract PRIVATE /UNDEBUG)
        target_compile_options(moenotes-chart-parser-completion PRIVATE /UNDEBUG)
    else()
        target_compile_options(moenotes-chart-parser-test PRIVATE -UNDEBUG)
        target_compile_options(moenotes-chart-parser-features PRIVATE -UNDEBUG)
        target_compile_options(moenotes-chart-parser-conversion PRIVATE -UNDEBUG)
        target_compile_options(moenotes-chart-parser-api-contract PRIVATE -UNDEBUG)
        target_compile_options(moenotes-chart-parser-completion PRIVATE -UNDEBUG)
    endif()
endif()

if(MOENOTES_BUILD_FUZZER)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "MOENOTES_BUILD_FUZZER requires Clang")
    endif()
    target_compile_options(moenotes_chart_parser PRIVATE -fsanitize=fuzzer-no-link,address,undefined)
    target_link_options(moenotes_chart_parser INTERFACE -fsanitize=address,undefined)
    add_executable(moenotes-chart-parser-fuzz tests/fuzz_parser.c)
    target_link_libraries(moenotes-chart-parser-fuzz PRIVATE moenotes_chart_parser)
    target_compile_options(moenotes-chart-parser-fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
    target_link_options(moenotes-chart-parser-fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
endif()
