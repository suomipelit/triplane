cmake_minimum_required(VERSION 3.0)
project(triplane VERSION 1.0.8)
set(TRIPLANE_SP_VERSION "SP1")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_MODULE_PATH ${CMAKE_BINARY_DIR}/cmake-modules)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

if(NOT DEFINED TRIPLANE_DATA)
    set(TRIPLANE_DATA ".")
endif()

# Dependencies
if(NOT EXISTS "${CMAKE_BINARY_DIR}/cmake-modules/FindSDL2.cmake")
    file(DOWNLOAD "https://github.com/tcbrindle/sdl2-cmake-scripts/raw/master/FindSDL2.cmake"
	"${CMAKE_BINARY_DIR}/cmake-modules/FindSDL2.cmake")
endif()
if(NOT EXISTS "${CMAKE_BINARY_DIR}/cmake-modules/FindSDL2_mixer.cmake")
    file(DOWNLOAD "https://github.com/tcbrindle/sdl2-cmake-scripts/raw/master/FindSDL2_mixer.cmake"
	"${CMAKE_BINARY_DIR}/cmake-modules/FindSDL2_mixer.cmake")
endif()

find_package(SDL2 REQUIRED)
find_package(SDL2_mixer REQUIRED)

# Compiler flags
if (WIN32)
    add_compile_options(/MP /MT /WX /wd4996)
else()
    # Warnings to be fixed
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
endif()

# Common variables
set(COMMON_INC ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${SDL2_INCLUDE_DIR}
    ${SDL2_MIXER_INCLUDE_DIRS})

set(COMMON_DEFINITIONS
    TRIPLANE_DATA=\"${TRIPLANE_DATA}\"
    TRIPLANE_VERSION=\"${PROJECT_VERSION}\"
    TRIPLANE_SP_VERSION=\"${TRIPLANE_SP_VERSION}\"
    HAVE_SDL_MIXER=1)

# Common library
add_library(common STATIC
    src/gfx/bitmap.cpp
    src/gfx/font.cpp
    src/gfx/gfx.cpp
    src/io/dksfile.cpp
    src/io/mouse.cpp
    src/io/sdl_compat.cpp
    src/io/timing.cpp
    src/io/video.cpp
    src/util/random.cpp
    src/util/wutil.cpp)

target_compile_definitions(common PRIVATE
    ${COMMON_DEFINITIONS})

target_include_directories(common SYSTEM PUBLIC
    ${COMMON_INC})

target_link_libraries(common
    ${SDL2_LIBRARY}
    ${SDL2_MIXER_LIBRARIES})

# DKS data packet creation tool
add_executable(dksbuild
    src/tools/dksbuild/dksbuild.cc)

# DKS data packet
add_custom_command(OUTPUT fokker.dks
    COMMAND dksbuild ${CMAKE_CURRENT_LIST_DIR}/data/fokker.lst
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/fokker.dks ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    DEPENDS dksbuild)

add_custom_target(generate-dks
    DEPENDS fokker.dks)

# Triplane executable
add_executable(${PROJECT_NAME}
    src/gfx/extra.cpp
    src/gfx/fades.cpp
    src/io/joystick.cpp
    src/io/sound.cpp
    src/menus/tripmenu.cpp
    src/settings.cpp
    src/triplane.cpp
    src/world/fobjects.cpp
    src/world/plane.cpp
    src/world/terrain.cpp
    src/world/tmexept.cpp
    src/world/tripai.cpp
    src/world/tripaudio.cpp
    src/world/tripmis.cpp)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    ${COMMON_DEFINITIONS})

target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC
    ${COMMON_INC})

target_link_libraries(${PROJECT_NAME}
    common)

add_dependencies(${PROJECT_NAME} generate-dks)

if (WIN32)
    target_link_libraries(${PROJECT_NAME} winmm.lib)
else()
    target_link_libraries(${PROJECT_NAME} m)
endif()
