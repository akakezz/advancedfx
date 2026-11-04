cmake_minimum_required(VERSION 3.24)
project(CinematicCameraTests LANGUAGES CXX)
enable_testing()
add_executable(cinematic_tests
    test_cinematic.cpp
    ../../shared/CinematicPath.cpp
    ../../shared/AfxMath.cpp
)
target_compile_features(cinematic_tests PRIVATE cxx_std_17)
target_compile_definitions(cinematic_tests PRIVATE _USE_MATH_DEFINES=)
target_include_directories(cinematic_tests PRIVATE ../.. ../../AfxHookSource)
if(MSVC)
    target_compile_options(cinematic_tests PRIVATE /W4 /EHsc)
else()
    target_compile_definitions(cinematic_tests PRIVATE abstract=)
    target_compile_options(cinematic_tests PRIVATE -Wall -Wextra -Wpedantic -Wno-deprecated-copy)
endif()
add_test(NAME cinematic_core COMMAND cinematic_tests)
