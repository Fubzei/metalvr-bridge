include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)

FetchContent_MakeAvailable(googletest)

add_executable(mvrvb_unit_tests
    unit/format_table_tests.cpp
    unit/spirv_parser_tests.cpp
    unit/spirv_to_msl_tests.cpp
)

target_link_libraries(mvrvb_unit_tests PRIVATE
    GTest::gtest_main
    mvrvb_common
    mvrvb_format_table
    mvrvb_msl_emitter
    mvrvb_spirv_parser
)

target_compile_features(mvrvb_unit_tests PRIVATE cxx_std_20)

include(GoogleTest)
gtest_discover_tests(mvrvb_unit_tests)
