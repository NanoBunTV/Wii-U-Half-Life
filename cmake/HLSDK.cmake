# HLSDK.cmake - Half-Life game code (hlsdk-portable) as two static "libraries": server and client
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

# mod_options.txt: ON options become defines (CLIENT_WEAPONS, CROWBAR_FIX_RAPID_CROWBAR, ...)
set(HLSDK_DEFINES "")
file(STRINGS "${HLSDK_DIR}/mod_options.txt" mod_options REGEX "^[A-Za-z0-9_-]+=ON ")
foreach(line IN LISTS mod_options)
	if(line MATCHES "^([A-Za-z0-9_-]+)=ON ")
		list(APPEND HLSDK_DEFINES "${CMAKE_MATCH_1}")
	endif()
endforeach()
message(STATUS "hlsdk mod options: ${HLSDK_DEFINES}")

set(HLSDK_COMMON_DEFINES
	${HLSDK_DEFINES}
	_LINUX
	stricmp=strcasecmp strnicmp=strncasecmp _snprintf=snprintf _vsnprintf=vsnprintf
	HAVE_STRLCPY HAVE_STRLCAT HAVE_TGMATH_H)

set(HLSDK_COMMON_FLAGS -fno-exceptions -fvisibility=hidden -Wno-invalid-offsetof -fno-strict-aliasing)

# --- vcs_info (game_shared) ---
add_library(hlsdk_vcs_info OBJECT "${HLSDK_DIR}/game_shared/vcs_info.c")
target_include_directories(hlsdk_vcs_info PRIVATE "${HLSDK_DIR}/game_shared")
target_compile_definitions(hlsdk_vcs_info PRIVATE XASH_BUILD_COMMIT="${HLSDK_GIT_COMMIT}" XASH_BUILD_BRANCH="wiiu")

# --- server ---
read_cmake_list("${HLSDK_DIR}/dlls/CMakeLists.txt" SVDLL_SOURCES sv_rel)
list(FILTER sv_rel EXCLUDE REGEX "(strlcpy|strlcat)\\.c$|\\.def$")
list(TRANSFORM sv_rel PREPEND "${HLSDK_DIR}/dlls/")
add_library(server_obj OBJECT ${sv_rel})
target_include_directories(server_obj PRIVATE
	"${HLSDK_DIR}/dlls" "${HLSDK_DIR}/dlls/wpn_shared" "${HLSDK_DIR}/common" "${HLSDK_DIR}/engine"
	"${HLSDK_DIR}/pm_shared" "${HLSDK_DIR}/game_shared" "${HLSDK_DIR}/public")
target_compile_definitions(server_obj PRIVATE ${HLSDK_COMMON_DEFINES})
target_compile_options(server_obj PRIVATE ${HLSDK_COMMON_FLAGS} $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)

# --- client (with FreeVGUI linked in: INTERNAL_VGUI_SUPPORT) ---
read_cmake_list("${HLSDK_DIR}/cl_dll/CMakeLists.txt" CLDLL_SOURCES cl_rel)
list(FILTER cl_rel EXCLUDE REGEX "(strlcpy|strlcat)\\.c$")
list(TRANSFORM cl_rel PREPEND "${HLSDK_DIR}/cl_dll/")

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

add_library(client_obj OBJECT ${cl_rel})
target_include_directories(client_obj PRIVATE
	"${HLSDK_DIR}/cl_dll" "${HLSDK_DIR}/cl_dll/hl" "${HLSDK_DIR}/dlls" "${HLSDK_DIR}/dlls/wpn_shared"
	"${HLSDK_DIR}/common" "${HLSDK_DIR}/engine" "${HLSDK_DIR}/pm_shared" "${HLSDK_DIR}/game_shared"
	"${HLSDK_DIR}/public" "${HLSDK_DIR}/utils/vgui/include")
target_compile_definitions(client_obj PRIVATE ${HLSDK_COMMON_DEFINES} CLIENT_DLL LINUX INTERNAL_VGUI_SUPPORT)
target_compile_options(client_obj PRIVATE ${HLSDK_COMMON_FLAGS} $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
