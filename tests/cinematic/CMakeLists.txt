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

# These tests exercise the real Windows message/raw-input implementation using
# fake camera/menu dependencies; they do not require an injected game process.
if(WIN32 AND MSVC)
    add_executable(cinematic_input_tests
        test_cinematic.cpp
        ../../shared/MirvInput.cpp
        ../../shared/AfxMath.cpp
        ../../shared/AfxConsole.cpp
        ../../shared/StringTools.cpp
    )
    target_compile_features(cinematic_input_tests PRIVATE cxx_std_17)
    target_compile_definitions(cinematic_input_tests PRIVATE AFX_CAMPATH_INPUT_TESTS _USE_MATH_DEFINES=)
    target_include_directories(cinematic_input_tests PRIVATE ../.. ../../AfxHookSource)
    target_compile_options(cinematic_input_tests PRIVATE /W4 /EHsc)
    target_link_libraries(cinematic_input_tests PRIVATE user32)
    add_test(NAME cinematic_input COMMAND cinematic_input_tests)
endif()
