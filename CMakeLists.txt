cmake_minimum_required(VERSION 3.10)

project(auctrum C)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -DNDEBUG")

set(CMAKE_PREFIX_PATH
    "D:/dev/libs/sdl/sdl3/;D:/dev/libs/sdl/sdl3_image/;D:/dev/libs/sdl/sdl3_ttf/"
    CACHE STRING "Prefixes where CMake should look for packages"
)

set(SNDFILE_INC_DIR "D:/dev/libs/audio/libsndfile/include/")
set(SNDFILE_LIB "D:/dev/libs/audio/libsndfile/lib/sndfile.lib")

set(KISSFFT_INC_DIR "D:/dev/libs/math/kissfft/include")
set(KISSFFT_LIB "D:/dev/libs/math/kissfft/lib/libkissfft-float.dll.a")

find_package(SDL3 REQUIRED CONFIG)
find_package(SDL3_image REQUIRED CONFIG)
find_package(SDL3_ttf REQUIRED CONFIG)

file(GLOB SRC_FILES CONFIGURE_DEPENDS "src/*.c")

add_executable(ac ${SRC_FILES})

set_target_properties(ac PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

target_include_directories(ac PRIVATE
    ${KISSFFT_INC_DIR}
    ${SNDFILE_INC_DIR}
)

target_link_libraries(ac PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    SDL3_ttf::SDL3_ttf
    ${KISSFFT_LIB}
    ${SNDFILE_LIB}
)

if(WIN32 AND (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang"))
  target_link_options(ac PRIVATE -mwindows)
endif()
