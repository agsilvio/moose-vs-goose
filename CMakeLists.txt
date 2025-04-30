cmake_minimum_required(VERSION 3.12)
project(moose-vs-goose)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


file(COPY assets DESTINATION ${CMAKE_BINARY_DIR})

set(SDLTTF_FREETYPE_VENDORED ON)
set(SDLTTF_VENDORED ON )
set(SDLTTF_HARFBUZZ OFF)
set(SDLTTF_PLUTOSVG OFF)

include(FetchContent)
FetchContent_Declare(
	remoteSdl3
	GIT_REPOSITORY "https://github.com/libsdl-org/SDL.git"
	GIT_TAG "release-3.2.8"
	GIT_SHALLOW TRUE
	GIT_PROGRESS TRUE
	EXCLUDE_FROM_ALL
)
FetchContent_Declare(
	remoteSdl3Ttf
	GIT_REPOSITORY "https://github.com/libsdl-org/SDL_ttf.git"
	GIT_TAG "release-3.2.0"
	GIT_SHALLOW TRUE
	GIT_PROGRESS TRUE
	EXCLUDE_FROM_ALL
)

FetchContent_Declare(
	remoteSdl3Image
	GIT_REPOSITORY "https://github.com/libsdl-org/SDL_Image.git"
	GIT_TAG "release-3.2.4"
	GIT_SHALLOW TRUE
	GIT_PROGRESS TRUE
	EXCLUDE_FROM_ALL
)

FetchContent_Declare(
	remoteSdl3Mixer
	GIT_REPOSITORY "https://github.com/libsdl-org/SDL_Mixer.git"
	GIT_TAG "main"
	GIT_SHALLOW TRUE
	GIT_PROGRESS TRUE
	EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(remoteSdl3)
FetchContent_MakeAvailable(remoteSdl3Ttf)
FetchContent_MakeAvailable(remoteSdl3Image)
FetchContent_MakeAvailable(remoteSdl3Mixer)

include_directories(moose-vs-goose ${remoteSdl3_SOURCE_DIR}/include EXCLUDE_FROM_ALL)
include_directories(moose-vs-goose ${remoteSdl3Ttf_SOURCE_DIR}/include EXCLUDE_FROM_ALL)
include_directories(moose-vs-goose ${remoteSdl3Image_SOURCE_DIR}/include EXCLUDE_FROM_ALL)
include_directories(moose-vs-goose ${remoteSdl3Mixer_SOURCE_DIR}/include EXCLUDE_FROM_ALL)

add_executable(moose-vs-goose main.c)
set_target_properties(moose-vs-goose PROPERTIES C_STANDARD 11)
target_link_libraries(moose-vs-goose PRIVATE SDL3::SDL3 m)
target_link_libraries(moose-vs-goose PRIVATE SDL3_mixer::SDL3_mixer m)
target_link_libraries(moose-vs-goose PRIVATE SDL3_ttf::SDL3_ttf m)
target_link_libraries(moose-vs-goose PRIVATE SDL3_image::SDL3_image m)

if (EMSCRIPTEN)
	#control memory
	target_link_options(${CMAKE_PROJECT_NAME} PRIVATE -sINITIAL_MEMORY=16777216)

	# Output to index.html for easier deployments
	set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "index")

	# Create a html webpage
	set_property(TARGET ${CMAKE_PROJECT_NAME} PROPERTY SUFFIX ".html")

	# Use custom emscripten shell
	target_link_options(${CMAKE_PROJECT_NAME}
			PRIVATE
			--shell-file ${CMAKE_SOURCE_DIR}/shell.html
			--preload-file assets 
	)
endif ()
