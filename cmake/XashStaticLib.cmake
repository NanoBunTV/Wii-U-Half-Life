# XashStaticLib.cmake - link Xash3D "shared libraries" statically into one executable
#
# The Wii U has no dlopen, so the engine is built with XASH_STATIC_LIBS. Each library the engine
# would normally load (filesystem_stdio, ref_soft, menu, server, client) is compiled as an object
# library, then:
#   1. partially linked into one relocatable object (ld -r), together with a generated
#      link_helper.c that holds the table of exported functions (lib_<name>_exports[])
#   2. objcopy -G lib_<name>_exports makes every other symbol local, so the libraries can't clash
#      with each other or the engine (client and server both contain the weapon and pm_shared code,
#      every renderer defines GetRefAPI, and so on)
# The engine finds the tables through generated_library_tables.h (see engine/platform/misc/lib_static.c).
# This is the CMake version of upstream's scripts/waifulib/xshlib.py.
#
# Export lists come from either a text file (one symbol per line) or, for the hlsdk libraries, from
# the object files: every defined, global, default-visibility function (hlsdk builds with
# -fvisibility=hidden and marks its exports, including every LINK_ENTITY_TO_CLASS, as visible).

set(XASH_STATIC_LIBS_LIST "" CACHE INTERNAL "")

# xash_static_library(<name> OBJLIBS <object library targets...> [EXPORTS_FILE <file>] [EXPORTS_FROM_OBJECTS])
function(xash_static_library name)
	cmake_parse_arguments(ARG "EXPORTS_FROM_OBJECTS" "EXPORTS_FILE" "OBJLIBS" ${ARGN})
	set(objfiles "")
	foreach(t IN LISTS ARG_OBJLIBS)
		list(APPEND objfiles "$<TARGET_OBJECTS:${t}>")
	endforeach()

	set(outdir "${CMAKE_BINARY_DIR}/staticlibs/${name}")
	set(unstripped "${outdir}/${name}.unstripped.o")
	set(final "${outdir}/${name}.o")
	set(objlist "${outdir}/objects.rsp")

	if(ARG_EXPORTS_FILE)
		set(mode FILE)
		set(exports_src "${ARG_EXPORTS_FILE}")
	elseif(ARG_EXPORTS_FROM_OBJECTS)
		set(mode OBJECTS)
		set(exports_src "")
	else()
		message(FATAL_ERROR "xash_static_library(${name}): give EXPORTS_FILE or EXPORTS_FROM_OBJECTS")
	endif()

	# response file with the object paths, so long command lines and spaces in paths are fine
	file(GENERATE OUTPUT "${objlist}" CONTENT "$<JOIN:${objfiles},\n>\n")

	add_custom_command(
		OUTPUT "${final}"
		COMMAND "${CMAKE_COMMAND}"
			"-DNAME=${name}"
			"-DMODE=${mode}"
			"-DEXPORTS_FILE=${exports_src}"
			"-DOBJLIST=${objlist}"
			"-DOUTDIR=${outdir}"
			"-DUNSTRIPPED=${unstripped}"
			"-DFINAL=${final}"
			"-DCC=${CMAKE_C_COMPILER}"
			"-DCFLAGS=${CMAKE_C_FLAGS}"
			"-DLD=${CMAKE_LINKER}"
			"-DOBJCOPY=${CMAKE_OBJCOPY}"
			"-DREADELF=${XASH_READELF}"
			-P "${CMAKE_SOURCE_DIR}/cmake/xash_link_relocatable.cmake"
		DEPENDS ${ARG_OBJLIBS} ${objfiles} "${objlist}" ${exports_src}
			"${CMAKE_SOURCE_DIR}/cmake/xash_link_relocatable.cmake"
		COMMENT "Linking relocatable ${name}.o"
		VERBATIM
		COMMAND_EXPAND_LISTS
	)

	add_custom_target(${name}_reloc DEPENDS "${final}")
	set_property(GLOBAL APPEND PROPERTY XASH_STATIC_LIB_NAMES "${name}")
	set_property(GLOBAL APPEND PROPERTY XASH_STATIC_LIB_OBJECTS "${final}")
	set_property(GLOBAL APPEND PROPERTY XASH_STATIC_LIB_TARGETS "${name}_reloc")
endfunction()

# Writes generated_library_tables.h into <dir> for every library registered so far
function(xash_write_library_tables dir)
	get_property(names GLOBAL PROPERTY XASH_STATIC_LIB_NAMES)
	set(externs "")
	set(table "")
	foreach(n IN LISTS names)
		string(APPEND externs "extern table_t lib_${n}_exports[];\n")
		string(APPEND table "{ \"${n}\", &lib_${n}_exports },\n")
	endforeach()
	file(WRITE "${dir}/generated_library_tables.h.tmp"
		"${externs}struct {const char *name;void *func;} libs[] = {\n${table}{0,0}\n};\n")
	file(COPY_FILE "${dir}/generated_library_tables.h.tmp" "${dir}/generated_library_tables.h" ONLY_IF_DIFFERENT)
endfunction()
