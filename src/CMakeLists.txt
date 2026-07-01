set(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")

set(SOURCE_DIR "${ROOT_DIR}/src")
file(GLOB_RECURSE SOURCE_FILES "${SOURCE_DIR}/*.cpp" "${SOURCE_DIR}/*.h" "${SOURCE_DIR}/*.hpp")	

# 对 SourceFile 变量里面的所有文件分类(保留资源管理器的目录结构)
foreach(fileItem ${SOURCE_FILES})       
    # Get the directory of the source file
    get_filename_component(PARENT_DIR "${fileItem}" DIRECTORY)
    # Remove common directory prefix to make the group
    string(REPLACE "${CMAKE_CURRENT_SOURCE_DIR}" "" GROUP "${PARENT_DIR}")
    # Make sure we are using windows slashes
    string(REPLACE "/" "\\" GROUP "${GROUP}")
    # Group into "Source Files" and "Header Files"
    set(GROUP "${GROUP}")
    source_group("${GROUP}" FILES "${fileItem}")
endforeach()

source_group(TREE "${ROOT_DIR}" FILES ${SOURCE_FILES})

set(VERSION_HEADER "${CMAKE_CURRENT_BINARY_DIR}/src/Plugin.h")
configure_file(
	"${ROOT_DIR}/cmake/Plugin.h.in"
	"${VERSION_HEADER}"
	@ONLY
)

source_group("src" FILES "${VERSION_HEADER}")

configure_file(
	"${ROOT_DIR}/cmake/version.rc.in"
	"${CMAKE_CURRENT_BINARY_DIR}/version.rc"
	@ONLY
)

# Use a dedicated target name to avoid clashes with any
# externally defined targets that may also be called "${PROJECT_NAME}".
set(DMENU_TARGET "${PROJECT_NAME}_plugin")

# Include extra project files in the IDE only if they exist.
set(DMENU_EXTRA_FILES "")
if(EXISTS "${ROOT_DIR}/.clang-format")
	list(APPEND DMENU_EXTRA_FILES "${ROOT_DIR}/.clang-format")
endif()
if(EXISTS "${ROOT_DIR}/.editorconfig")
	list(APPEND DMENU_EXTRA_FILES "${ROOT_DIR}/.editorconfig")
endif()

add_library(
	"${DMENU_TARGET}"
	SHARED
	${SOURCE_FILES}
	"${VERSION_HEADER}"
	"${CMAKE_CURRENT_BINARY_DIR}/version.rc"
	${DMENU_EXTRA_FILES}
)

# Keep the actual DLL name as "dmenu" for runtime compatibility.
set_target_properties(
	"${DMENU_TARGET}"
	PROPERTIES
		OUTPUT_NAME "${PROJECT_NAME}"
)

target_compile_features(
	"${DMENU_TARGET}"
	PRIVATE
		cxx_std_20
)

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
	target_compile_options(
		"${DMENU_TARGET}"
		PRIVATE
			"/sdl"	# Enable Additional Security Checks
			"/utf-8"	# Set Source and Executable character sets to UTF-8
			"/Zi"	# Debug Information Format

			"/await"

			"/permissive-"	# Standards conformance
			"/Zc:preprocessor"	# Enable preprocessor conformance mode

			"/wd4200" # nonstandard extension used : zero-sized array in struct/union

			"$<$<CONFIG:DEBUG>:>"
			"$<$<CONFIG:RELEASE>:/Zc:inline;/JMC-;/Ob3>"
	)

	target_link_options(
		"${DMENU_TARGET}"
		PRIVATE
			"$<$<CONFIG:DEBUG>:/INCREMENTAL;/OPT:NOREF;/OPT:NOICF>"
			"$<$<CONFIG:RELEASE>:/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/DEBUG:FULL>"
	)
endif()

target_include_directories(
	"${DMENU_TARGET}"
	PRIVATE
		"${CMAKE_CURRENT_BINARY_DIR}/src"
		"${SOURCE_DIR}"
)

set(SKSE_SUPPORT_XBYAK ON)

add_subdirectory("$ENV{CommonLibSSEPath_NG}" CommonLibSSE EXCLUDE_FROM_ALL)

find_package(xbyak REQUIRED CONFIG)

find_package(imgui REQUIRED)

find_package(nlohmann_json REQUIRED)

target_link_libraries(
	"${DMENU_TARGET}"
	PRIVATE
		CommonLibSSE::CommonLibSSE
		xbyak::xbyak
		imgui::imgui
		nlohmann_json::nlohmann_json
		imm32
		ole32
		windowscodecs
		mfplat
		mfreadwrite
		mfuuid
)

target_precompile_headers(
	"${DMENU_TARGET}"
	PRIVATE
		"${SOURCE_DIR}/PCH.h"
)

install(
	FILES
		"$<TARGET_FILE:${DMENU_TARGET}>"
	DESTINATION "SKSE/Plugins"
	COMPONENT "main"
)

install(
	FILES
		"$<TARGET_PDB_FILE:${DMENU_TARGET}>"
	DESTINATION "/"
	COMPONENT "pdbs"
)

if("${COPY_OUTPUT}")
	add_custom_command(
		TARGET "${DMENU_TARGET}"
		POST_BUILD
		COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${DMENU_TARGET}>" "${CompiledPluginsPath}/SKSE/Plugins/"
		COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_PDB_FILE:${DMENU_TARGET}>" "${CompiledPluginsPath}/SKSE/Plugins/"
		VERBATIM
	)
endif()
