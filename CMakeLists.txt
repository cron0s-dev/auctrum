cmake_minimum_required(VERSION 3.24)

project(auctrum
    VERSION 1.0
    LANGUAGES C
)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(FetchContent)

find_package(SDL3 CONFIG QUIET)

if(NOT SDL3_FOUND)
    message(STATUS "Fetching SDL3")

    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG main
    )

    FetchContent_MakeAvailable(SDL3)
endif()

find_package(SDL3_image CONFIG QUIET)

if(NOT SDL3_image_FOUND)
    message(STATUS "Fetching SDL3_image")

    FetchContent_Declare(
        SDL3_image
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_image.git
        GIT_TAG main
    )

    FetchContent_MakeAvailable(SDL3_image)
endif()

find_package(SDL3_ttf CONFIG QUIET)

if(NOT SDL3_ttf_FOUND)
    message(STATUS "Fetching SDL3_ttf")

    FetchContent_Declare(
        SDL3_ttf
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
        GIT_TAG main
    )

    FetchContent_MakeAvailable(SDL3_ttf)
endif()

find_package(SndFile CONFIG QUIET)

if(NOT SndFile_FOUND)
    message(STATUS "Fetching libsndfile")

    FetchContent_Declare(
        libsndfile
        GIT_REPOSITORY https://github.com/libsndfile/libsndfile.git
        GIT_TAG master
    )

    FetchContent_MakeAvailable(libsndfile)
endif()

FetchContent_Declare(
    kissfft
    GIT_REPOSITORY https://github.com/mborgerding/kissfft.git
    GIT_TAG master
)

FetchContent_MakeAvailable(kissfft)

file(GLOB_RECURSE SOURCES
    CONFIGURE_DEPENDS
    src/*.c
)


add_executable(ac ${SOURCES})


target_link_libraries(ac PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    SDL3_ttf::SDL3_ttf
    sndfile
    kissfft
)


target_include_directories(ac PRIVATE
    ${kissfft_SOURCE_DIR}
)

if(MSVC)
    target_compile_options(ac PRIVATE /W4)
else()
    target_compile_options(ac PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

set_target_properties(ac PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY
        "${CMAKE_BINARY_DIR}/bin"
)


# Windows GUI subsystem
if(WIN32 AND (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang"))
    target_link_options(ac PRIVATE -mwindows)
endif()