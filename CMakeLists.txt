cmake_minimum_required(VERSION 3.25)  # raylib 6.0's own CMakeLists requires 3.25
project(Nimecraft C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static CRT on MSVC, so nimecraft.exe needs no VC++ redistributable. Set before raylib is added.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(NIMECRAFT_SANITIZE "" CACHE STRING "Sanitizers for Linux builds: empty, address,undefined, or thread")
option(NIMECRAFT_COVERAGE "Build with GCC coverage counters (Linux GCC only)" OFF)

# ---- raylib 6.0, the only third-party code ----
include(FetchContent)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)  # let the plain variables below override raylib's option() defaults
set(BUILD_EXAMPLES OFF)
set(BUILD_SHARED_LIBS OFF)
FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 6.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(raylib)

# raylib and its bundled GLFW are third-party code we do not edit: keep their warnings out of
# our build logs, which must stay warning-free. Our own targets keep the full warning set below.
foreach(dep IN ITEMS raylib glfw)
    if(TARGET ${dep})
        if(MSVC)
            # GLFW adds /W3 itself; drop it so cl does not warn (D9025) that /w overrides it.
            get_target_property(dep_options ${dep} COMPILE_OPTIONS)
            if(dep_options)
                list(FILTER dep_options EXCLUDE REGEX "^/W[0-4]$")
                set_property(TARGET ${dep} PROPERTY COMPILE_OPTIONS ${dep_options})
            endif()
            target_compile_options(${dep} PRIVATE /w)
        else()
            target_compile_options(${dep} PRIVATE -w)
        endif()
        # Consumers see raylib.h/raymath.h as system headers, so raymath's C-style `{ 0 }`
        # initializers do not warn inside our C++ files (SYSTEM needs CMake 3.25).
        set_target_properties(${dep} PROPERTIES SYSTEM TRUE)
    endif()
endforeach()

# ---- Build options shared by our own targets (raylib keeps its own flags) ----
add_library(nimecraft_options INTERFACE)
if(MSVC)
    target_compile_options(nimecraft_options INTERFACE /W4 /permissive- /utf-8)
else()
    target_compile_options(nimecraft_options INTERFACE -Wall -Wextra -Wpedantic -Wshadow -ffp-contract=off)
endif()

if(NIMECRAFT_SANITIZE)
    if(WIN32 OR MSVC)
        message(FATAL_ERROR "NIMECRAFT_SANITIZE is supported on Linux only")
    endif()
    target_compile_options(nimecraft_options INTERFACE
        -fsanitize=${NIMECRAFT_SANITIZE} -fno-sanitize-recover=all -fno-omit-frame-pointer)
    target_link_options(nimecraft_options INTERFACE -fsanitize=${NIMECRAFT_SANITIZE})
endif()

if(NIMECRAFT_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR WIN32)
        message(FATAL_ERROR "NIMECRAFT_COVERAGE needs GCC on Linux")
    endif()
    target_compile_options(nimecraft_options INTERFACE --coverage -fprofile-update=atomic)
    target_link_options(nimecraft_options INTERFACE --coverage)
endif()

if(MINGW)
    # libstdc++, libgcc, and winpthread go inside the exe; only Windows system DLLs remain.
    target_link_options(nimecraft_options INTERFACE -static)
endif()

# ---- nimecraft_core: every src/*.cpp except main.cpp ----
file(GLOB NIMECRAFT_CORE_SOURCES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/*.cpp")
list(REMOVE_ITEM NIMECRAFT_CORE_SOURCES "${PROJECT_SOURCE_DIR}/src/main.cpp")
add_library(nimecraft_core STATIC ${NIMECRAFT_CORE_SOURCES})
target_include_directories(nimecraft_core PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(nimecraft_core PUBLIC raylib PRIVATE nimecraft_options)

# ---- nimecraft: the game ----
add_executable(nimecraft src/main.cpp)
target_link_libraries(nimecraft PRIVATE nimecraft_core nimecraft_options)
if(WIN32)
    # Release-type builds open no console window; Debug keeps it for logs.
    set_target_properties(nimecraft PROPERTIES WIN32_EXECUTABLE $<NOT:$<CONFIG:Debug>>)
    if(MSVC)
        # Keep main() as the entry point of the GUI-subsystem exe.
        target_link_options(nimecraft PRIVATE $<$<NOT:$<CONFIG:Debug>>:/ENTRY:mainCRTStartup>)
    endif()
endif()

# ---- nimecraft_tests ----
file(GLOB NIMECRAFT_TEST_SOURCES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/tests/*.cpp")
add_executable(nimecraft_tests ${NIMECRAFT_TEST_SOURCES})
target_link_libraries(nimecraft_tests PRIVATE nimecraft_core nimecraft_options)
