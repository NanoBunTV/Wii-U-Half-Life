# HLSDK.cmake - game code (hlsdk-portable and its bshift / opfor branches) as static "libraries":
# server and client for Half-Life, server_<game> / client_<game> for the others
#
# The source lists are read from hlsdk-portable's own CMakeLists.txt files, so updating hlsdk-portable
# picks up added/removed files without editing this file.

# read_cmake_list(<file> <list name in file> <out var>) : the entries of `set (NAME ...)` or
# `list(APPEND NAME ...)` blocks in <file>
function(read_cmake_list file name out)
	file(READ "${file}" content)
	set(result "")
	string(REGEX MATCHALL "(set|list)[ \t]*\\([ \t]*(APPEND[ \t]+)?${name}[ \t\r\n]+[^)]*\\)" blocks "${content}")
	foreach(block IN LISTS blocks)
		string(REGEX REPLACE "^(set|list)[ \t]*\\([ \t]*(APPEND[ \t]+)?${name}" "" block "${block}")
		string(REGEX REPLACE "\\)$" "" block "${block}")
		string(REGEX REPLACE "#[^\n]*" "" block "${block}")
		string(REGEX REPLACE "[ \t\r\n]+" ";" block "${block}")
		foreach(item IN LISTS block)
			if(NOT item STREQUAL "" AND NOT item MATCHES "^\\$\\{")
				list(APPEND result "${item}")
			endif()
		endforeach()
	endforeach()
	set(${out} "${result}" PARENT_SCOPE)
endfunction()

# FreeVGUI (with our big-endian fixes, freevgui.patch) is built once from hlsdk-portable and linked into
# every client. Each client's relocatable link keeps its own copy, so they don't clash
file(GLOB FREEVGUI_SOURCES
	"${HLSDK_DIR}/freevgui/*.cpp"
	"${HLSDK_DIR}/freevgui/controls/*.cpp"
	"${HLSDK_DIR}/freevgui/platform/common/*.cpp"
	"${HLSDK_DIR}/freevgui/platform/posix/*.cpp"
	"${HLSDK_DIR}/freevgui/platform/xash3d-fwgs/*.cpp")

add_library(vgui_obj OBJECT ${FREEVGUI_SOURCES})
target_include_directories(vgui_obj PRIVATE "${HLSDK_DIR}/freevgui" "${HLSDK_DIR}/engine" "${HLSDK_DIR}/utils/vgui/include")
target_compile_definitions(vgui_obj PRIVATE INTERNAL_VGUI_SUPPORT)
target_compile_options(vgui_obj PRIVATE -fvisibility=hidden -w)

# --- vcs_info (game_shared) ---
add_library(hlsdk_vcs_info OBJECT "${HLSDK_DIR}/game_shared/vcs_info.c")
target_include_directories(hlsdk_vcs_info PRIVATE "${HLSDK_DIR}/game_shared")
target_compile_definitions(hlsdk_vcs_info PRIVATE XASH_BUILD_COMMIT="${HLSDK_GIT_COMMIT}" XASH_BUILD_BRANCH="wiiu")

# hlsdk_game(<suffix> <source dir>): object libraries server_obj<suffix> / client_obj<suffix> and the static
# libraries server<suffix> / client<suffix> (the engine picks them by game folder, see lib_common.c)
function(hlsdk_game suffix dir)
	# mod_options.txt: ON options become defines (CLIENT_WEAPONS, CROWBAR_FIX_RAPID_CROWBAR, ...)
	set(defines "")
	file(STRINGS "${dir}/mod_options.txt" mod_options REGEX "^[A-Za-z0-9_-]+=ON ")
	foreach(line IN LISTS mod_options)
		if(line MATCHES "^([A-Za-z0-9_-]+)=ON ")
			list(APPEND defines "${CMAKE_MATCH_1}")
		endif()
	endforeach()
	message(STATUS "hlsdk${suffix} mod options: ${defines}")

	list(APPEND defines
		_LINUX
		stricmp=strcasecmp strnicmp=strncasecmp _snprintf=snprintf _vsnprintf=vsnprintf
		HAVE_STRLCPY HAVE_STRLCAT HAVE_TGMATH_H)
	set(flags -fno-exceptions -fvisibility=hidden -Wno-invalid-offsetof -fno-strict-aliasing $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)

	# Opposing Force keeps its own code in dlls/gearbox
	set(extra_inc "")
	if(EXISTS "${dir}/dlls/gearbox")
		set(extra_inc "${dir}/dlls/gearbox")
	endif()

	# --- server ---
	read_cmake_list("${dir}/dlls/CMakeLists.txt" SVDLL_SOURCES sv_rel)
	list(FILTER sv_rel EXCLUDE REGEX "(strlcpy|strlcat)\\.c$|\\.def$")
	list(TRANSFORM sv_rel PREPEND "${dir}/dlls/")
	add_library(server_obj${suffix} OBJECT ${sv_rel})
	target_include_directories(server_obj${suffix} PRIVATE
		"${dir}/dlls" "${dir}/dlls/wpn_shared" "${dir}/common" "${dir}/engine"
		"${dir}/pm_shared" "${dir}/game_shared" "${dir}/public" ${extra_inc})
	target_compile_definitions(server_obj${suffix} PRIVATE ${defines})
	target_compile_options(server_obj${suffix} PRIVATE ${flags})

	# --- client (with FreeVGUI linked in: INTERNAL_VGUI_SUPPORT) ---
	read_cmake_list("${dir}/cl_dll/CMakeLists.txt" CLDLL_SOURCES cl_rel)
	list(FILTER cl_rel EXCLUDE REGEX "(strlcpy|strlcat)\\.c$")
	list(TRANSFORM cl_rel PREPEND "${dir}/cl_dll/")
	add_library(client_obj${suffix} OBJECT ${cl_rel})
	target_include_directories(client_obj${suffix} PRIVATE
		"${dir}/cl_dll" "${dir}/cl_dll/hl" "${dir}/dlls" "${dir}/dlls/wpn_shared"
		"${dir}/common" "${dir}/engine" "${dir}/pm_shared" "${dir}/game_shared"
		"${dir}/public" "${dir}/utils/vgui/include" ${extra_inc})
	target_compile_definitions(client_obj${suffix} PRIVATE ${defines} CLIENT_DLL LINUX INTERNAL_VGUI_SUPPORT)
	target_compile_options(client_obj${suffix} PRIVATE ${flags})

	xash_static_library(server${suffix} OBJLIBS server_obj${suffix} hlsdk_vcs_info EXPORTS_FROM_OBJECTS)
	xash_static_library(client${suffix} OBJLIBS client_obj${suffix} vgui_obj hlsdk_vcs_info EXPORTS_FROM_OBJECTS)
endfunction()
