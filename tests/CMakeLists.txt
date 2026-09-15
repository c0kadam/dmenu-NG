# SPDX-License-Identifier: GPL-3.0-or-later
#
# Copyright (c) 2026 C0kadam
# See LICENSE and EXCEPTIONS.md for the project terms.

add_executable(
	dmenu_runtime_compatibility_tests
	RuntimeCompatibilityTests.cpp
	"${PROJECT_SOURCE_DIR}/src/bin/HookValidation.cpp"
)

target_compile_features(dmenu_runtime_compatibility_tests PRIVATE cxx_std_23)
target_include_directories(dmenu_runtime_compatibility_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/bin")

if(MSVC)
	target_compile_options(dmenu_runtime_compatibility_tests PRIVATE /utf-8 /W4)
endif()

add_test(NAME dmenu_runtime_compatibility_tests COMMAND dmenu_runtime_compatibility_tests)
