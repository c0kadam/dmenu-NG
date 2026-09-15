cmake_minimum_required(VERSION 3.22)

project(
	dmenu
	VERSION 1.3.1
	LANGUAGES CXX
)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if("${PROJECT_SOURCE_DIR}" STREQUAL "${PROJECT_BINARY_DIR}")
	message(FATAL_ERROR "in-source builds are not allowed")
endif()

macro(set_from_environment VARIABLE)
	if(NOT DEFINED ${VARIABLE} AND DEFINED ENV{${VARIABLE}})
		set(${VARIABLE} "$ENV{${VARIABLE}}")
	endif()
endmacro()

set_from_environment(CompiledPluginsPath)
set_from_environment(CommonLibSSEPath_NG)
if(DEFINED CommonLibSSEPath_NG)
	set(CommonLibSSEPath_NG "${CommonLibSSEPath_NG}" CACHE PATH "Pinned CommonLibSSE-NG source directory")
endif()

set(DMENU_COMMONLIBSSE_NG_VERSION "7.2.0")
set(DMENU_COMMONLIBSSE_NG_REVISION "7a60f4de794095d7b0f8928d1b930a52e9a7da83")

if(NOT DEFINED CommonLibSSEPath_NG OR NOT EXISTS "${CommonLibSSEPath_NG}/CMakeLists.txt")
	message(FATAL_ERROR
		"CommonLibSSEPath_NG must point to CommonLibSSE-NG ${DMENU_COMMONLIBSSE_NG_VERSION} "
		"at revision ${DMENU_COMMONLIBSSE_NG_REVISION}")
endif()

file(READ "${CommonLibSSEPath_NG}/CMakeLists.txt" COMMONLIBSSE_NG_CMAKE)
if(NOT COMMONLIBSSE_NG_CMAKE MATCHES "VERSION[ \t\r\n]+${DMENU_COMMONLIBSSE_NG_VERSION}")
	message(FATAL_ERROR "CommonLibSSE-NG ${DMENU_COMMONLIBSSE_NG_VERSION} is required")
endif()

find_package(Git REQUIRED)
execute_process(
	COMMAND "${GIT_EXECUTABLE}" -C "${CommonLibSSEPath_NG}" rev-parse HEAD
	RESULT_VARIABLE COMMONLIBSSE_NG_GIT_RESULT
	OUTPUT_VARIABLE COMMONLIBSSE_NG_HEAD
	OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT COMMONLIBSSE_NG_GIT_RESULT EQUAL 0 OR
	NOT "${COMMONLIBSSE_NG_HEAD}" STREQUAL "${DMENU_COMMONLIBSSE_NG_REVISION}")
	message(FATAL_ERROR
		"CommonLibSSE-NG must be checked out at ${DMENU_COMMONLIBSSE_NG_REVISION}; "
		"found '${COMMONLIBSSE_NG_HEAD}'")
endif()

if(DEFINED CompiledPluginsPath)
	set(DEFAULT_COPY_OUTPUT ON)
else()
	set(DEFAULT_COPY_OUTPUT OFF)
endif()

option(COPY_OUTPUT "Copy the output of build operations to the game directory" ${DEFAULT_COPY_OUTPUT})
if(COPY_OUTPUT AND NOT DEFINED CompiledPluginsPath)
	message(FATAL_ERROR "CompiledPluginsPath is required when COPY_OUTPUT is ON")
endif()
option(ENABLE_SKYRIM_SE "Enable support for Skyrim SE in the dynamic runtime feature." ON)
option(ENABLE_SKYRIM_AE "Enable support for Skyrim AE in the dynamic runtime feature." ON)
option(ENABLE_SKYRIM_VR "Enable support for Skyrim VR in the dynamic runtime feature." OFF)
set(BUILD_TESTS OFF)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

add_subdirectory(src)
include(cmake/packaging.cmake)
