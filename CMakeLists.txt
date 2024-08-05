cmake_minimum_required(VERSION 3.22)
include(FetchContent)

project(game
	VERSION 1.0.0
	DESCRIPTION "Shows how to integrate Box2D with raylib"
	HOMEPAGE_URL "https://box2d.org"
	LANGUAGES C
)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

SET(BOX2D_AVX2 OFF CACHE BOOL "Enable AVX2 (faster)")
FetchContent_Declare(
	box2d
	GIT_REPOSITORY https://github.com/erincatto/box2c.git
	GIT_TAG main
	GIT_SHALLOW TRUE
	GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(box2d)

FetchContent_Declare(
	raylib
	GIT_REPOSITORY https://github.com/raysan5/raylib.git
	GIT_TAG master
	GIT_SHALLOW TRUE
	GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(raylib)

include_directories(grug)

add_executable(game main.c grug/grug.c grug/grug.h)

target_compile_options(game PRIVATE
	-Wall -Wextra -Werror -Wpedantic -Wstrict-prototypes -Wshadow -Wuninitialized -Wfatal-errors -Wno-language-extension-token -g
	$<$<CONFIG:RELEASE>:-Ofast -march=native>
	$<$<CONFIG:DEBUG>:-fsanitize=address,undefined>
)
target_link_options(game PRIVATE
	-rdynamic
	$<$<CONFIG:DEBUG>:-fsanitize=address,undefined>
)

target_link_libraries(game PRIVATE box2d raylib)

if (MSVC)
	set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT game)
	set_property(TARGET game PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()
