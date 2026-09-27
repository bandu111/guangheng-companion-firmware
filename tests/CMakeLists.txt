cmake_minimum_required(VERSION 3.16)
project(gh_model_tests C)

enable_testing()
if(NOT DEFINED ENV{IDF_PATH})
    message(FATAL_ERROR "IDF_PATH is required to build snapshot parser host tests")
endif()
set(CJSON_DIR "$ENV{IDF_PATH}/components/json/cJSON")
add_executable(gh_model_tests
    gh_model_test.c
    ../main/gh_model.c
    ../main/gh_lift_detector.c
    ../main/gh_snapshot_parser.c
    "${CJSON_DIR}/cJSON.c")
target_include_directories(gh_model_tests PRIVATE ../main/include "${CJSON_DIR}")
if(MSVC)
    target_compile_options(gh_model_tests PRIVATE /UNDEBUG /utf-8)
else()
    target_compile_options(gh_model_tests PRIVATE -UNDEBUG)
endif()
add_test(NAME gh_model_tests COMMAND gh_model_tests)
