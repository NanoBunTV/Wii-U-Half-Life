# ThirdParty.cmake - static libraries from xash3d-fwgs/3rdparty and public/
# Mirrors the wscript files of each subproject (see Docs/ARCHITECTURE.md).

set(X3P "${XASH_DIR}/3rdparty")

# --- library_suffix (platform/arch names, build.h lives here) ---
add_library(library_suffix STATIC "${X3P}/library_suffix/src/library_suffix.c")
target_include_directories(library_suffix PUBLIC "${X3P}/library_suffix/include")

# --- public (crtlib, mathlib, ...) ---
set(XASH_SDK_INCLUDES
	"${XASH_DIR}/public"
	"${XASH_DIR}/common"
	"${XASH_DIR}/pm_shared"
	"${XASH_DIR}/engine"
	"${X3P}/library_suffix/include"
)
file(GLOB PUBLIC_SOURCES "${XASH_DIR}/public/*.c")
list(FILTER PUBLIC_SOURCES EXCLUDE REGEX "build_vcs\\.c$")
add_library(public STATIC ${PUBLIC_SOURCES})
target_include_directories(public PUBLIC ${XASH_SDK_INCLUDES})
target_link_libraries(public PUBLIC library_suffix)

add_library(build_vcs STATIC "${XASH_DIR}/public/build_vcs.c")
target_include_directories(build_vcs PRIVATE ${XASH_SDK_INCLUDES})
target_compile_definitions(build_vcs PRIVATE
	XASH_BUILD_COMMIT="${XASH_GIT_COMMIT}"
	XASH_BUILD_BRANCH="wiiu"
	XASH_BUILD_COMMIT_DATE="${XASH_GIT_DATE}")

# --- miniz (zlib replacement) ---
add_library(miniz STATIC "${X3P}/miniz/miniz.c")
target_include_directories(miniz PUBLIC "${X3P}/miniz")
target_compile_definitions(miniz PUBLIC MINIZ_NO_ARCHIVE_APIS XASH_MINIZ=1)

# --- bzip2 ---
set(BZ "${X3P}/bzip2/bzip2")
set(BZ_VERSION "1.1.0-fwgs")
configure_file("${BZ}/bz_version.h.in" "${CMAKE_BINARY_DIR}/gen/bzip2/bz_version.h" @ONLY)
add_library(bzip2 STATIC
	"${BZ}/blocksort.c" "${BZ}/huffman.c" "${BZ}/crctable.c" "${BZ}/randtable.c"
	"${BZ}/compress.c" "${BZ}/decompress.c" "${BZ}/bzlib.c")
target_include_directories(bzip2 PUBLIC "${BZ}" PRIVATE "${CMAKE_BINARY_DIR}/gen/bzip2")
target_compile_definitions(bzip2 PUBLIC BZ_NO_STDIO=1 BZ_UNIX=1)

# --- mpg123 (config.h generated like 3rdparty/mpg123/wscript) ---
set(MPG "${X3P}/mpg123")
set(MPG123_CONFIG
	PORTABLE_API=1 IEEE_FLOAT=1 GAPLESS=1 FRAME_INDEX=1 INDEX_SIZE=1000 USE_NEW_HUFFTABLE=1
	NO_LAYER1=1 NO_LAYER2=1 NO_8BIT=1 NO_32BIT=1 NO_REAL=1 NO_SYNTH32=1 NO_NTOM=1 NO_DOWNSAMPLE=1
	NO_EQUALIZER=1 NO_ICY=1 NO_STRING=1 NO_ID3V2=1 NO_MOREINFO=1 NO_WARNING=1 NO_ERRORMSG=1
	NO_ERETURN=1 HAVE_STRERROR=1 HAVE_SYS_TYPES_H=1 HAVE_UNISTD_H=1 HAVE_STRTOK_R=1 WORDS_BIGENDIAN=1)
file(STRINGS "${MPG}/mpg123/src/config.h.in" mpg_lines)
set(mpg_out "#include \"build.h\"\n")
foreach(line IN LISTS mpg_lines)
	if(line MATCHES "^[ \t]*#[ \t]*undef[ \t]+([A-Za-z0-9_]+)[ \t]*$")
		set(name "${CMAKE_MATCH_1}")
		set(value "")
		foreach(kv IN LISTS MPG123_CONFIG)
			if(kv MATCHES "^${name}=(.*)$")
				set(value "${CMAKE_MATCH_1}")
			endif()
		endforeach()
		if(value STREQUAL "")
			string(APPEND mpg_out "/* #undef ${name} */\n")
		else()
			string(APPEND mpg_out "#define ${name} ${value}\n")
		endif()
	else()
		string(APPEND mpg_out "${line}\n")
	endif()
endforeach()
file(WRITE "${CMAKE_BINARY_DIR}/gen/mpg123/config.h.tmp" "${mpg_out}")
file(COPY_FILE "${CMAKE_BINARY_DIR}/gen/mpg123/config.h.tmp" "${CMAKE_BINARY_DIR}/gen/mpg123/config.h" ONLY_IF_DIFFERENT)
set(MPGL "${MPG}/mpg123/src/libmpg123")
add_library(mpg123 STATIC
	"${MPG}/mpg123/src/compat/compat_str.c"
	"${MPGL}/dct64.c" "${MPGL}/equalizer.c" "${MPGL}/feature.c" "${MPGL}/format.c" "${MPGL}/frame.c"
	"${MPGL}/id3.c" "${MPGL}/index.c" "${MPGL}/layer3.c" "${MPGL}/libmpg123.c" "${MPGL}/optimize.c"
	"${MPGL}/parse.c" "${MPGL}/readers.c" "${MPGL}/synth.c" "${MPGL}/tabinit.c" "${MPG}/stub.c")
target_include_directories(mpg123
	PUBLIC "${MPG}/mpg123/src/include"
	PRIVATE "${CMAKE_BINARY_DIR}/gen/mpg123" "${MPG}" "${MPGL}" "${X3P}/library_suffix/include")
target_compile_definitions(mpg123 PRIVATE OPT_GENERIC REAL_IS_FLOAT)

# --- libogg ---
set(INCLUDE_INTTYPES_H 1)
set(INCLUDE_SYS_TYPES_H 0)
set(INCLUDE_STDINT_H 0)
set(SIZE16 int16_t)
set(USIZE16 uint16_t)
set(SIZE32 int32_t)
set(USIZE32 uint32_t)
set(SIZE64 int64_t)
set(USIZE64 uint64_t)
configure_file("${X3P}/libogg/libogg/include/ogg/config_types.h.in" "${CMAKE_BINARY_DIR}/gen/ogg/ogg/config_types.h" @ONLY)
file(GLOB OGG_SOURCES "${X3P}/libogg/libogg/src/*.c")
add_library(ogg STATIC ${OGG_SOURCES})
target_include_directories(ogg PUBLIC "${X3P}/libogg/libogg/include" "${CMAKE_BINARY_DIR}/gen/ogg")

# --- vorbis / vorbisfile ---
set(VB "${X3P}/vorbis/vorbis-src/lib")
add_library(vorbis STATIC
	"${VB}/mdct.c" "${VB}/smallft.c" "${VB}/block.c" "${VB}/envelope.c" "${VB}/window.c" "${VB}/lsp.c"
	"${VB}/lpc.c" "${VB}/analysis.c" "${VB}/synthesis.c" "${VB}/psy.c" "${VB}/info.c" "${VB}/floor1.c"
	"${VB}/floor0.c" "${VB}/res0.c" "${VB}/mapping0.c" "${VB}/registry.c" "${VB}/codebook.c"
	"${VB}/sharedbook.c" "${VB}/lookup.c" "${VB}/bitrate.c")
target_include_directories(vorbis PUBLIC "${X3P}/vorbis/vorbis-src/include")
target_compile_definitions(vorbis PRIVATE USE_MEMORY_H=1)
target_link_libraries(vorbis PUBLIC ogg)
add_library(vorbisfile STATIC "${VB}/vorbisfile.c")
target_link_libraries(vorbisfile PUBLIC vorbis)

# --- opus / opusfile ---
file(GLOB OPUS_SOURCES
	"${X3P}/opus/opus/src/*.c" "${X3P}/opus/opus/celt/*.c"
	"${X3P}/opus/opus/silk/*.c" "${X3P}/opus/opus/silk/float/*.c")
list(FILTER OPUS_SOURCES EXCLUDE REGEX "(repacketizer_demo|opus_demo|opus_compare|opus_custom_demo)\\.c$")
add_library(opus STATIC ${OPUS_SOURCES})
target_include_directories(opus
	PUBLIC "${X3P}/opus/opus/include"
	PRIVATE "${X3P}/opus/opus" "${X3P}/opus/opus/celt" "${X3P}/opus/opus/silk" "${X3P}/opus/opus/silk/float")
target_compile_definitions(opus PRIVATE OPUS_BUILD FLOAT_APPROX PACKAGE_VERSION="1.4.0" CUSTOM_MODES
	ENABLE_HARDENING HAVE_LRINT=1 HAVE_LRINTF=1 VAR_ARRAYS=1)

set(OPF "${X3P}/opusfile/opusfile/src")
add_library(opusfile STATIC "${OPF}/info.c" "${OPF}/internal.c" "${OPF}/opusfile.c" "${OPF}/stream.c")
target_include_directories(opusfile PUBLIC "${X3P}/opusfile/opusfile/include")
target_link_libraries(opusfile PUBLIC ogg opus)

# --- MultiEmulator (client id generation) ---
file(GLOB MULTIEMU_SOURCES "${X3P}/MultiEmulator/src/*.c")
add_library(MultiEmulator STATIC ${MULTIEMU_SOURCES})
target_include_directories(MultiEmulator PUBLIC "${X3P}/MultiEmulator/include" PRIVATE "${X3P}/MultiEmulator/src")

# third-party code isn't ours to fix: keep its warnings out of the build log
foreach(t bzip2 mpg123 ogg vorbis vorbisfile opus opusfile MultiEmulator miniz)
	target_compile_options(${t} PRIVATE -w)
endforeach()
