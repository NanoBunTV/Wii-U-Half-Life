# Half-Life for Wii U - top-level build
#
# Builds Xash3D FWGS + hlsdk-portable into one statically linked Wii U executable (.rpx / .wuhb).
# Configure with the devkitPro Wii U toolchain; use build.ps1 on Windows (see Docs/BUILDING.md).

cmake_minimum_required(VERSION 3.20)

project(HalfLifeWiiU C CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "CafeOS")
	message(FATAL_ERROR "Configure with -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/WiiU.cmake (or run build.ps1)")
endif()

set(APP_NAME        "Half-Life")
set(APP_SHORT_NAME  "Half-Life")
set(APP_AUTHOR      "Xash3D FWGS / Wii U port")
set(APP_VERSION     "0.1.0")

set(XASH_DIR  "${CMAKE_SOURCE_DIR}/external/xash3d-fwgs"  CACHE PATH "Xash3D FWGS source")
set(HLSDK_DIR "${CMAKE_SOURCE_DIR}/external/hlsdk-portable" CACHE PATH "hlsdk-portable source")
set(HLSDK_BSHIFT_DIR "${CMAKE_SOURCE_DIR}/external/hlsdk-bshift" CACHE PATH "hlsdk-portable, bshift branch (Blue Shift)")
set(HLSDK_OPFOR_DIR  "${CMAKE_SOURCE_DIR}/external/hlsdk-opfor"  CACHE PATH "hlsdk-portable, opfor branch (Opposing Force)")

if(NOT EXISTS "${XASH_DIR}/engine/common/host.c" OR NOT EXISTS "${HLSDK_DIR}/dlls/cbase.cpp"
	OR NOT EXISTS "${HLSDK_BSHIFT_DIR}/dlls/cbase.cpp" OR NOT EXISTS "${HLSDK_OPFOR_DIR}/dlls/cbase.cpp")
	message(FATAL_ERROR "Engine / game sources missing. Run scripts/fetch-deps.ps1 first.")
endif()

find_program(XASH_READELF NAMES powerpc-eabi-readelf HINTS "${DEVKITPPC}/bin" REQUIRED)

function(git_info dir out_commit out_date)
	execute_process(COMMAND git -C "${dir}" rev-parse --short HEAD OUTPUT_VARIABLE c OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
	execute_process(COMMAND git -C "${dir}" log -1 --format=%ci OUTPUT_VARIABLE d OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
	if(NOT c)
		set(c "unknown")
	endif()
	set(${out_commit} "${c}" PARENT_SCOPE)
	set(${out_date} "${d}" PARENT_SCOPE)
endfunction()
git_info("${XASH_DIR}" XASH_GIT_COMMIT XASH_GIT_DATE)
git_info("${HLSDK_DIR}" HLSDK_GIT_COMMIT HLSDK_GIT_DATE)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)       # gnu11
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# Defines the engine's waf configure step would detect (newlib + wut has all of these)
add_compile_definitions(
	_GNU_SOURCE
	"ALLOCA_H=<alloca.h>"
	HAVE_TGMATH_H=1
	HAVE_STRNCASECMP=1
	HAVE_STRCASECMP=1
	HAVE_STRCASESTR=1
	HAVE_STRCHRNUL=1
	HAVE_STRLCPY=1
	HAVE_STRLCAT=1
	HAVE_STRNLEN=1
	XASH_STATIC_LIBS=1
)
add_compile_options(-fno-strict-aliasing -fsigned-char -Wno-unused-parameter)

include(cmake/XashStaticLib.cmake)
include(cmake/ThirdParty.cmake)
include(cmake/HLSDK.cmake)

set(XASH_ENGINE_INCLUDES
	"${XASH_DIR}/engine"
	"${XASH_DIR}/engine/common"
	"${XASH_DIR}/engine/common/imagelib"
	"${XASH_DIR}/engine/server"
	"${XASH_DIR}/engine/client"
	"${XASH_DIR}/engine/client/vgui"
	"${XASH_DIR}/engine/common/soundlib"
	"${XASH_DIR}/engine/platform"
	"${XASH_DIR}/filesystem"
	${XASH_SDK_INCLUDES}
)

# ---------------------------------------------------------------------------------------------------
# filesystem_stdio
# ---------------------------------------------------------------------------------------------------
file(GLOB FS_SOURCES "${XASH_DIR}/filesystem/*.c" "${XASH_DIR}/filesystem/*.cpp")
add_library(filesystem_obj OBJECT ${FS_SOURCES})
target_include_directories(filesystem_obj PRIVATE "${XASH_DIR}/filesystem" ${XASH_SDK_INCLUDES} "${XASH_DIR}/3rdparty/miniz")
target_compile_definitions(filesystem_obj PRIVATE MINIZ_NO_ARCHIVE_APIS XASH_MINIZ=1)
target_compile_options(filesystem_obj PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)
xash_static_library(filesystem_stdio OBJLIBS filesystem_obj EXPORTS_FILE "${CMAKE_SOURCE_DIR}/exports/filesystem_stdio.txt")

# ---------------------------------------------------------------------------------------------------
# ref_soft (software renderer, the fallback)
# ---------------------------------------------------------------------------------------------------
file(GLOB REF_SOFT_SOURCES "${XASH_DIR}/ref/common/*.c" "${XASH_DIR}/ref/soft/*.c")
add_library(ref_soft_obj OBJECT ${REF_SOFT_SOURCES})
target_include_directories(ref_soft_obj PRIVATE "${XASH_DIR}/ref/soft" "${XASH_DIR}/ref/common" ${XASH_ENGINE_INCLUDES})
target_compile_definitions(ref_soft_obj PRIVATE REF_DLL=1)
xash_static_library(ref_soft OBJLIBS ref_soft_obj EXPORTS_FILE "${CMAKE_SOURCE_DIR}/exports/ref.txt")

# ---------------------------------------------------------------------------------------------------
# ref_gl (OpenGL renderer, unchanged; its GL calls go to gx2gl in the engine, see src/gx2gl)
# ---------------------------------------------------------------------------------------------------
file(GLOB REF_GL_SOURCES "${XASH_DIR}/ref/common/*.c" "${XASH_DIR}/ref/gl/*.c" "${XASH_DIR}/ref/gl/gl2_shim/*.c")
add_library(ref_gl_obj OBJECT ${REF_GL_SOURCES})
# src: gx2gl.h for the frame phase timing (GX2GL_PROF)
target_include_directories(ref_gl_obj PRIVATE "${XASH_DIR}/ref/gl" "${XASH_DIR}/ref/common" ${XASH_ENGINE_INCLUDES} "${CMAKE_SOURCE_DIR}/src")
target_compile_definitions(ref_gl_obj PRIVATE REF_DLL=1)
xash_static_library(ref_gl OBJLIBS ref_gl_obj EXPORTS_FILE "${CMAKE_SOURCE_DIR}/exports/ref.txt")

# ---------------------------------------------------------------------------------------------------
# menu (mainui_cpp)
# ---------------------------------------------------------------------------------------------------
set(MAINUI "${XASH_DIR}/3rdparty/mainui")
file(GLOB MENU_SOURCES
	"${MAINUI}/*.cpp" "${MAINUI}/miniutl/*.cpp" "${MAINUI}/font/*.cpp" "${MAINUI}/menus/*.cpp"
	"${MAINUI}/menus/dynamic/*.cpp" "${MAINUI}/model/*.cpp" "${MAINUI}/controls/*.cpp")
add_library(menu_obj OBJECT ${MENU_SOURCES})
target_include_directories(menu_obj PRIVATE
	"${MAINUI}" "${MAINUI}/miniutl" "${MAINUI}/font" "${MAINUI}/controls" "${MAINUI}/menus" "${MAINUI}/model"
	"${MAINUI}/sdk_includes/common" "${MAINUI}/sdk_includes/engine" "${MAINUI}/sdk_includes/public"
	"${MAINUI}/sdk_includes/pm_shared")
target_compile_definitions(menu_obj PRIVATE "STDINT_H=<cstdint>" MAINUI_USE_CUSTOM_FONT_RENDER=1 MAINUI_USE_STB=1)
target_compile_options(menu_obj PRIVATE -fno-rtti -fno-exceptions -fvisibility=hidden)
xash_static_library(menu OBJLIBS menu_obj EXPORTS_FILE "${CMAKE_SOURCE_DIR}/exports/menu.txt")

# ---------------------------------------------------------------------------------------------------
# game code
# ---------------------------------------------------------------------------------------------------
# Half-Life ("client" / "server"), Blue Shift and Opposing Force, all linked in; the engine picks by game
# folder (COM_WiiUGameLibrarySuffix in lib_common.c)
hlsdk_game("" "${HLSDK_DIR}")
hlsdk_game("_bshift" "${HLSDK_BSHIFT_DIR}")
hlsdk_game("_opfor" "${HLSDK_OPFOR_DIR}")

# ---------------------------------------------------------------------------------------------------
# engine executable
# ---------------------------------------------------------------------------------------------------
set(GEN_DIR "${CMAKE_BINARY_DIR}/gen/engine")
file(MAKE_DIRECTORY "${GEN_DIR}")
xash_write_library_tables("${GEN_DIR}")

file(GLOB ENGINE_SOURCES
	"${XASH_DIR}/engine/common/*.c"
	"${XASH_DIR}/engine/common/imagelib/*.c"
	"${XASH_DIR}/engine/common/soundlib/*.c"
	"${XASH_DIR}/engine/server/*.c"
	"${XASH_DIR}/engine/common/http/*.c"
	"${XASH_DIR}/engine/platform/posix/*.c"
	"${XASH_DIR}/engine/platform/sdl2/*.c"
	"${XASH_DIR}/engine/client/*.c"
	"${XASH_DIR}/engine/client/*/*.c"
	"${XASH_DIR}/engine/client/*/*/*.c")
list(APPEND ENGINE_SOURCES
	"${XASH_DIR}/engine/platform/misc/lib_static.c"
	"${CMAKE_SOURCE_DIR}/src/platform/wiiu/sys_wiiu.c"
	"${CMAKE_SOURCE_DIR}/src/gx2gl/gx2gl.c")

get_property(XASH_RELOC_OBJECTS GLOBAL PROPERTY XASH_STATIC_LIB_OBJECTS)
get_property(XASH_RELOC_TARGETS GLOBAL PROPERTY XASH_STATIC_LIB_TARGETS)
set_source_files_properties(${XASH_RELOC_OBJECTS} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)

find_package(SDL2 REQUIRED)

# Cemu workaround (see Docs/DEBUGGING.md, "Recompiler crash in _Mem_Realloc"): with -O2, Cemu's
# multi-core CPU modes crash inside _Mem_Realloc; the single-core interpreter runs the same code fine.
set_source_files_properties("${XASH_DIR}/engine/common/zone.c" PROPERTIES COMPILE_OPTIONS "-O0")

add_executable(halflife ${ENGINE_SOURCES} ${XASH_RELOC_OBJECTS})
add_dependencies(halflife ${XASH_RELOC_TARGETS})
target_include_directories(halflife PRIVATE ${XASH_ENGINE_INCLUDES} "${GEN_DIR}" "${CMAKE_SOURCE_DIR}/src")
target_compile_definitions(halflife PRIVATE
	XASH_ENABLE_MAIN=1
	ENGINE_DLL=1
	XASH_SDL=2
	XASH_REF_GL_ENABLED=1
	XASH_REF_SOFT_ENABLED=1
	XASH_GAMEDIR="valve")
target_link_libraries(halflife PRIVATE
	public build_vcs library_suffix miniz bzip2 mpg123 opusfile opus vorbisfile vorbis ogg MultiEmulator
	SDL2::SDL2 m stdc++)
target_link_options(halflife PRIVATE -Wl,--gc-sections)
# SDL's GamePad driver is the only VPADRead caller (reading twice steals samples); the engine picks the
# gyro out of SDL's reads (__wrap_VPADRead in joy_sdl2.c)
target_link_options(halflife PRIVATE -Wl,--wrap=VPADRead)

# ---------------------------------------------------------------------------------------------------
# .wuhb content: engine data that isn't part of the game files (menu buttons, fonts, gamepad mappings)
# Mounted as the engine's read-only root (XASH3D_RODIR = fs:/vol/content, see sys_wiiu.c)
# ---------------------------------------------------------------------------------------------------
set(EXTRAS_SRC "${XASH_DIR}/3rdparty/extras/xash-extras")
set(CONTENT_DIR "${CMAKE_BINARY_DIR}/content")
file(MAKE_DIRECTORY "${CONTENT_DIR}/valve") # wut_create_wuhb checks that it exists at configure time
file(GLOB_RECURSE EXTRAS_FILES RELATIVE "${EXTRAS_SRC}" "${EXTRAS_SRC}/*")
list(FILTER EXTRAS_FILES EXCLUDE REGEX "^(scripts/|\\.)|\\.svg$|/\\.")
list(TRANSFORM EXTRAS_FILES PREPEND "${EXTRAS_SRC}/" OUTPUT_VARIABLE EXTRAS_DEPS)
add_custom_command(
	OUTPUT "${CONTENT_DIR}/valve/extras.pk3"
	COMMAND "${CMAKE_COMMAND}" -E make_directory "${CONTENT_DIR}/valve"
	COMMAND "${CMAKE_COMMAND}" -E tar cf "${CONTENT_DIR}/valve/extras.pk3" --format=zip ${EXTRAS_FILES}
	WORKING_DIRECTORY "${EXTRAS_SRC}"
	DEPENDS ${EXTRAS_DEPS}
	COMMENT "Packing extras.pk3"
	VERBATIM)
add_custom_target(wuhb_content DEPENDS "${CONTENT_DIR}/valve/extras.pk3")

wut_create_rpx(halflife)
wut_create_wuhb(halflife
	CONTENT "${CONTENT_DIR}"
	NAME "${APP_NAME}"
	SHORTNAME "${APP_SHORT_NAME}"
	AUTHOR "${APP_AUTHOR}"
	ICON "${CMAKE_SOURCE_DIR}/meta/icon.png"
	TVSPLASH "${CMAKE_SOURCE_DIR}/meta/tv-splash.png"
	DRCSPLASH "${CMAKE_SOURCE_DIR}/meta/drc-splash.png"
)
add_dependencies(halflife wuhb_content)

# The boot sound (meta/bootSound.btsnd.gz) isn't something wuhbtool can add: build.ps1 puts it into every
# .wuhb afterwards (scripts/wuhb-add-file.ps1). Art sources and generator: meta/source, meta/art/make_art.ps1
#
# Blue Shift and Opposing Force: the same program in its own .wuhb, so each game has its own icon on the
# Wii U Menu. Their content folder adds game.txt with the game folder; sys_wiiu.c turns it into -game
function(game_wuhb name gamedir title icon)
	set(dir "${CMAKE_BINARY_DIR}/content_${name}")
	file(MAKE_DIRECTORY "${dir}/valve")
	file(WRITE "${CMAKE_BINARY_DIR}/game_${name}.txt.tmp" "${gamedir}") # not in the content folder: it's packed
	file(COPY_FILE "${CMAKE_BINARY_DIR}/game_${name}.txt.tmp" "${dir}/game.txt" ONLY_IF_DIFFERENT)
	file(REMOVE "${dir}/game.txt.tmp") # left there by older builds
	add_custom_command(
		OUTPUT "${dir}/valve/extras.pk3"
		COMMAND "${CMAKE_COMMAND}" -E copy "${CONTENT_DIR}/valve/extras.pk3" "${dir}/valve/extras.pk3"
		DEPENDS "${CONTENT_DIR}/valve/extras.pk3"
		VERBATIM)
	add_custom_target(wuhb_content_${name} DEPENDS "${dir}/valve/extras.pk3")
	wut_create_wuhb(${name}_wuhb
		TARGET halflife
		OUTPUT "${CMAKE_BINARY_DIR}/${name}.wuhb"
		CONTENT "${dir}"
		NAME "${title}"
		SHORTNAME "${title}"
		AUTHOR "${APP_AUTHOR}"
		ICON "${CMAKE_SOURCE_DIR}/meta/${icon}"
		TVSPLASH "${CMAKE_SOURCE_DIR}/meta/tv-splash.png"
		DRCSPLASH "${CMAKE_SOURCE_DIR}/meta/drc-splash.png"
	)
	add_dependencies(${name}_wuhb wuhb_content_${name})
endfunction()
game_wuhb(bshift bshift "Half-Life: Blue Shift" icon-bshift.png)
game_wuhb(opfor gearbox "Half-Life: Opposing Force" icon-opfor.png)
