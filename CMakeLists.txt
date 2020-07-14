cmake_minimum_required(VERSION 3.0)
project(triplane VERSION 1.0.8)
set(TRIPLANE_SP_VERSION "SP2-SNAPSHOT")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_MODULE_PATH ${CMAKE_BINARY_DIR}/cmake-modules)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

if(NOT DEFINED TRIPLANE_DATA)
    set(TRIPLANE_DATA ".")
endif()

if (${CMAKE_SYSTEM_NAME} MATCHES "Emscripten")
    set(EMCC 1)
endif()

# Dependencies
if (NOT EMCC)
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
endif()

# Compiler flags
if (MSVC)
    add_compile_options(/MP /MT /WX /wd4996)
elseif (EMCC)
    set(MUSIC_DIR "web_music/")
    file(COPY ${CMAKE_SOURCE_DIR}/${MUSIC_DIR} DESTINATION ${CMAKE_BINARY_DIR}/${MUSIC_DIR})
    # fokker.dks must be separately built or borrowed from elsewhere for EMCC builds - see README!
    set(USE_FLAGS "-O3 -s ASYNCIFY -s USE_SDL=2 -s USE_SDL_MIXER=2 -s TOTAL_MEMORY=64MB --preload-file fokker.dks --preload-file ${MUSIC_DIR} --shell-file ${CMAKE_SOURCE_DIR}/src/web/shell.html")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${USE_FLAGS}")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${USE_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${USE_FLAGS} -lidbfs.js")
    set(CMAKE_EXECUTABLE_SUFFIX .html)
else()
    # Warnings to be fixed
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
endif()

# Common variables
set(COMMON_DEFINITIONS
    TRIPLANE_DATA=\"${TRIPLANE_DATA}\"
    TRIPLANE_VERSION=\"${PROJECT_VERSION}\"
    TRIPLANE_SP_VERSION=\"${TRIPLANE_SP_VERSION}\"
    HAVE_SDL_MIXER=1)

# Common library
add_library(common STATIC
    src/gfx/bitmap.cpp
    src/gfx/bitmap.h
    src/gfx/font.cpp
    src/gfx/font.h
    src/gfx/gfx.cpp
    src/gfx/gfx.h
    src/io/dksfile.cpp
    src/io/dksfile.h
    src/io/mouse.cpp
    src/io/mouse.h
    src/io/sdl_compat.cpp
    src/io/sdl_compat.h
    src/io/timing.cpp
    src/io/timing.h
    src/io/video.cpp
    src/io/video.h
    src/util/random.cpp
    src/util/random.h
    src/util/wutil.cpp
    src/util/wutil.h)

if (MSVC)
    target_sources(common PRIVATE
        src/util/msvc.h)
endif()

target_compile_definitions(common PRIVATE
    ${COMMON_DEFINITIONS})

if (NOT EMCC)
    target_include_directories(common SYSTEM PUBLIC
        ${SDL2_MIXER_INCLUDE_DIR}
        ${SDL2_INCLUDE_DIR})

    target_link_libraries(common
        ${SDL2_LIBRARY}
        ${SDL2_MIXER_LIBRARIES})
endif()

target_include_directories(common SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src)

if (NOT EMCC)
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
endif()

# Triplane executable
add_executable(${PROJECT_NAME}
    src/gfx/extra.cpp
    src/gfx/extra.h
    src/gfx/fades.cpp
    src/gfx/fades.h
    src/io/joystick.cpp
    src/io/joystick.h
    src/io/sound.cpp
    src/io/sound.h
    src/menus/tripmenu.cpp
    src/menus/tripmenu.h
    src/settings.cpp
    src/settings.h
    src/triplane.cpp
    src/triplane.h
    src/world/fobjects.cpp
    src/world/fobjects.h
    src/world/plane.cpp
    src/world/plane.h
    src/world/terrain.cpp
    src/world/terrain.h
    src/world/tmexept.cpp
    src/world/tmexept.h
    src/world/tripai.cpp
    src/world/tripai.h
    src/world/tripaudio.cpp
    src/world/tripaudio.h
    src/world/tripmis.cpp)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    ${COMMON_DEFINITIONS})

target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(${PROJECT_NAME}
    common)

if (NOT EMCC)
    add_dependencies(${PROJECT_NAME} generate-dks)
endif()

if (MSVC)
    target_link_libraries(${PROJECT_NAME} winmm.lib)
else()
    target_link_libraries(${PROJECT_NAME} m)
endif()

if (NOT EMCC)
    # Level editor
    add_executable(lvledit
        src/tools/lvledit/lvledit.cpp)

    target_link_libraries(lvledit
        common
        ${SDL2_LIBRARY})

    target_include_directories(lvledit SYSTEM PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${SDL2_INCLUDE_DIR})

    # Image tools
    add_executable(pgdview
        src/tools/pgdview/pgdview.cpp)

    target_link_libraries(pgdview
        common)

    add_executable(pcx2pgd
        src/tools/pcx2pgd/pcx2pgd.cpp)
endif()
