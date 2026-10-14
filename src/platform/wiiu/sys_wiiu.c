/*
sys_wiiu.c - Wii U platform hooks for Xash3D FWGS
Copyright (C) 2026 Half-Life Wii U port contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <unistd.h>
#include <sys/iosupport.h>
#include <sys/stat.h>

#include <coreinit/debug.h>
#include <coreinit/fastmutex.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <coreinit/exception.h>
#include <coreinit/context.h>
#include <whb/log.h>
#include <whb/log_cafe.h>
#include <whb/log_udp.h>

#include "platform/platform.h"

// Where the user copies their game files. "valve" (and later other game folders) go inside it.
#define WIIU_BASEDIR      "fs:/vol/external01/halflife"
// Optional file with extra command line arguments, one line, e.g. "-dev 2 -console"
#define WIIU_CMDLINE_FILE WIIU_BASEDIR "/cmdline.txt"
// The .wuhb's own read-only files (content/ folder at build time)
#define WIIU_CONTENTDIR   "fs:/vol/content"

#define WIIU_MAX_ARGS 64

static char *wiiu_argv[WIIU_MAX_ARGS + 1];
static char wiiu_cmdline[1024];
static qboolean wiiu_log_ready;

/*
==================
stdout / stderr

wut doesn't connect stdout/stderr to anything. Route them to WHBLog, which writes
to OSReport (shown in Cemu's log with "Coreinit Logging" on) and UDP port 4405
(udplogserver from devkitPro tools, for real hardware).
==================
*/
static OSFastMutex wiiu_log_mutex;
static char wiiu_log_line[1024];
static size_t wiiu_log_len;

// buffers output into whole lines, so each log line is one OSReport / UDP packet
static ssize_t WiiU_StdWrite( struct _reent *r, void *fd, const char *ptr, size_t len )
{
	(void)r;
	(void)fd;

	OSFastMutex_Lock( &wiiu_log_mutex );

	for( size_t i = 0; i < len; i++ )
	{
		const char c = ptr[i];

		if( c != '\n' )
			wiiu_log_line[wiiu_log_len++] = c;

		if( c == '\n' || wiiu_log_len == sizeof( wiiu_log_line ) - 1 )
		{
			wiiu_log_line[wiiu_log_len] = 0;
			if( wiiu_log_len > 0 )
				WHBLogPrint( wiiu_log_line );
			wiiu_log_len = 0;
		}
	}

	OSFastMutex_Unlock( &wiiu_log_mutex );
	return len;
}

static const devoptab_t wiiu_stdout_devoptab =
{
	.name    = "stdout_whb",
	.write_r = WiiU_StdWrite,
};

static void WiiU_InitLogging( void )
{
	if( wiiu_log_ready )
		return;

	OSFastMutex_Init( &wiiu_log_mutex, "wiiu_log" );
	WHBLogCafeInit( );
	WHBLogUdpInit( );

	devoptab_list[STD_OUT] = &wiiu_stdout_devoptab;
	devoptab_list[STD_ERR] = &wiiu_stdout_devoptab;
	setvbuf( stdout, NULL, _IOLBF, 0 );
	setvbuf( stderr, NULL, _IONBF, 0 );

	wiiu_log_ready = true;
}

/*
==================
WiiU_GetArgv

Called first thing from main(). Sets up logging and appends the arguments from
cmdline.txt on the SD card, since there is no other way to pass them.
==================
*/
int WiiU_GetArgv( int in_argc, char **in_argv, char ***out_argv )
{
	int argc = 0;
	FILE *f;

	WiiU_InitLogging( );

	for( int i = 0; i < in_argc && argc < WIIU_MAX_ARGS; i++ )
		wiiu_argv[argc++] = in_argv[i];

	if( argc == 0 )
		wiiu_argv[argc++] = (char *)"xash";

	f = fopen( WIIU_CMDLINE_FILE, "r" );
	if( f )
	{
		size_t len = fread( wiiu_cmdline, 1, sizeof( wiiu_cmdline ) - 1, f );
		char *p = wiiu_cmdline;

		fclose( f );
		wiiu_cmdline[len] = 0;

		while( *p && argc < WIIU_MAX_ARGS )
		{
			while( *p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' )
				*p++ = 0;

			if( !*p )
				break;

			wiiu_argv[argc++] = p;

			while( *p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' )
				p++;
		}
	}

	wiiu_argv[argc] = NULL;
	*out_argv = wiiu_argv;

	// files shipped inside the .wuhb (engine extras.pk3 in valve/) are layered under the SD card's
	// game folders as the engine's read-only root
	setenv( "XASH3D_RODIR", WIIU_CONTENTDIR, 0 );

	printf( "Half-Life Wii U: %d argument(s), base directory %s\n", argc, WIIU_BASEDIR );

	return argc;
}

/*
==================
WiiU_RunOnBigStack

The RPX main thread only gets a 64 KB stack (stackSize in the RPX header; elf2rpl has no option to
change it). Xash expects a desktop-sized stack and overflows that quickly, corrupting the heap.
So the engine runs on its own thread with a large stack, on the same core (CPU1, where ProcUI and
the HOME menu expect the app's main thread), and the real main thread just waits for it.
==================
*/
#define WIIU_ENGINE_STACK_SIZE ( 8 * 1024 * 1024 )

static OSThread wiiu_engine_thread __attribute__(( aligned( 16 )));
static int ( *wiiu_engine_main )( void );

static int WiiU_EngineThreadMain( int argc, const char **argv )
{
	(void)argc;
	(void)argv;
	return wiiu_engine_main( );
}

int WiiU_RunOnBigStack( int ( *fn )( void ))
{
	uint8_t *stack = memalign( 16, WIIU_ENGINE_STACK_SIZE );
	int ret = 0;

	if( !stack )
	{
		printf( "%s: can't allocate the engine stack, running on the main thread\n", __func__ );
		return fn( );
	}

	wiiu_engine_main = fn;

	if( !OSCreateThread( &wiiu_engine_thread, WiiU_EngineThreadMain, 0, NULL,
		stack + WIIU_ENGINE_STACK_SIZE, WIIU_ENGINE_STACK_SIZE,
		OSGetThreadPriority( OSGetCurrentThread( )), OS_THREAD_ATTRIB_AFFINITY_CPU1 ))
	{
		printf( "%s: OSCreateThread failed, running on the main thread\n", __func__ );
		free( stack );
		return fn( );
	}

	OSSetThreadName( &wiiu_engine_thread, "Xash engine" );
	OSResumeThread( &wiiu_engine_thread );
	OSJoinThread( &wiiu_engine_thread, &ret );

	free( stack );
	return ret;
}

qboolean WiiU_GetBasePath( char *buf, const size_t buflen )
{
	struct stat st;

	if( stat( WIIU_BASEDIR, &st ) == 0 && S_ISDIR( st.st_mode ))
	{
		Q_strncpy( buf, WIIU_BASEDIR, buflen );
		return true;
	}

	// SD card missing or game files not copied yet
	printf( "Can't find %s. Copy your Half-Life \"valve\" folder to sd:/halflife/valve\n", WIIU_BASEDIR );
	return false;
}

/*
==================
Hang watchdog (debugging)

Started with "-watchdog <seconds>" in cmdline.txt. Every <seconds> it suspends the main thread and
logs where it is: PC, LR and the return addresses on its stack. Turn them into function names with
  powerpc-eabi-addr2line -f -C -e build/halflife.elf <addresses>
(see Docs/DEBUGGING.md).
==================
*/
static OSThread *wiiu_main_thread;
static OSThread wiiu_watchdog_thread;
static uint8_t wiiu_watchdog_stack[16 * 1024] __attribute__(( aligned( 16 )));
static int wiiu_watchdog_interval;

static qboolean WiiU_IsStackAddress( uint32_t addr )
{
	return addr >= 0x10000000 && addr < 0x50000000 && ( addr & 3 ) == 0;
}

static int WiiU_WatchdogMain( int argc, const char **argv )
{
	(void)argc;
	(void)argv;

	for( ;; )
	{
		OSContext *ctx;
		uint32_t sp, trace[16];
		int depth = 0;
		char line[256];
		size_t len;

		OSSleepTicks( OSSecondsToTicks( wiiu_watchdog_interval ));

		WHBLogPrintf( "watchdog: main thread pc %08x lr %08x (before suspend)",
			wiiu_main_thread->context.srr0, wiiu_main_thread->context.lr );
		OSSuspendThread( wiiu_main_thread );
		while( !OSIsThreadSuspended( wiiu_main_thread ))
			OSSleepTicks( OSMillisecondsToTicks( 1 ));

		ctx = &wiiu_main_thread->context;
		sp = ctx->gpr[1];

		// PowerPC EABI back chain: [sp] = caller's sp, [caller's sp + 4] = saved LR
		while( depth < (int)( sizeof( trace ) / sizeof( trace[0] )) && WiiU_IsStackAddress( sp ))
		{
			uint32_t next = *(uint32_t *)sp;

			if( !WiiU_IsStackAddress( next ) || next <= sp )
				break;

			trace[depth++] = *(uint32_t *)( next + 4 );
			sp = next;
		}

		len = snprintf( line, sizeof( line ), "watchdog: main thread pc %08x lr %08x stack", ctx->srr0, ctx->lr );
		for( int i = 0; i < depth && len < sizeof( line ) - 10; i++ )
			len += snprintf( line + len, sizeof( line ) - len, " %08x", trace[i] );

		OSResumeThread( wiiu_main_thread );

		WHBLogPrint( line );
	}

	return 0;
}

static void WiiU_StartWatchdog( void )
{
	string val;

	if( !Sys_GetParmFromCmdLine( "-watchdog", val ))
		return;

	wiiu_watchdog_interval = Q_atoi( val );
	if( wiiu_watchdog_interval <= 0 )
		wiiu_watchdog_interval = 10;

	wiiu_main_thread = OSGetCurrentThread( );

	// run on another core than the engine's main thread, at a higher priority so it gets to run
	if( OSCreateThread( &wiiu_watchdog_thread, WiiU_WatchdogMain, 0, NULL,
		wiiu_watchdog_stack + sizeof( wiiu_watchdog_stack ), sizeof( wiiu_watchdog_stack ),
		0, OS_THREAD_ATTRIB_AFFINITY_CPU2 ))
	{
		OSSetThreadName( &wiiu_watchdog_thread, "Xash watchdog" );
		OSResumeThread( &wiiu_watchdog_thread );
		Con_Printf( "Hang watchdog running, reports every %d s\n", wiiu_watchdog_interval );
	}
}

/*
==================
Crash handler

Logs the exception, PC, LR, the faulting data address and the return addresses on the stack, then
stops with OSFatal. Decode the addresses with addr2line like the watchdog output.
==================
*/
static void WiiU_LogCrash( const char *type, OSContext *ctx )
{
	uint32_t sp = ctx->gpr[1];
	char line[256];
	size_t len;
	int depth = 0;

	OSReport( "*** %s exception: pc %08x lr %08x dar %08x dsisr %08x\n", type, ctx->srr0, ctx->lr, ctx->dar, ctx->dsisr );

	len = snprintf( line, sizeof( line ), "*** stack:" );
	while( depth < 16 && WiiU_IsStackAddress( sp ) && len < sizeof( line ) - 10 )
	{
		uint32_t next = *(uint32_t *)sp;

		if( !WiiU_IsStackAddress( next ) || next <= sp )
			break;

		len += snprintf( line + len, sizeof( line ) - len, " %08x", *(uint32_t *)( next + 4 ));
		sp = next;
		depth++;
	}
	OSReport( "%s\n", line );

	for( int i = 0; i < 32; i += 4 )
		OSReport( "*** r%-2d %08x %08x %08x %08x\n", i, ctx->gpr[i], ctx->gpr[i + 1], ctx->gpr[i + 2], ctx->gpr[i + 3] );

	snprintf( line, sizeof( line ), "Half-Life crashed (%s at %08x, lr %08x).\nSee the log for details.", type, ctx->srr0, ctx->lr );
	OSFatal( line );
}

static BOOL WiiU_DSIHandler( OSContext *ctx ) { WiiU_LogCrash( "DSI", ctx ); return FALSE; }
static BOOL WiiU_ISIHandler( OSContext *ctx ) { WiiU_LogCrash( "ISI", ctx ); return FALSE; }
static BOOL WiiU_ProgramHandler( OSContext *ctx ) { WiiU_LogCrash( "program", ctx ); return FALSE; }
static BOOL WiiU_AlignmentHandler( OSContext *ctx ) { WiiU_LogCrash( "alignment", ctx ); return FALSE; }

static void WiiU_InitCrashHandler( void )
{
	OSSetExceptionCallbackEx( OS_EXCEPTION_MODE_GLOBAL_ALL_CORES, OS_EXCEPTION_TYPE_DSI, WiiU_DSIHandler );
	OSSetExceptionCallbackEx( OS_EXCEPTION_MODE_GLOBAL_ALL_CORES, OS_EXCEPTION_TYPE_ISI, WiiU_ISIHandler );
	OSSetExceptionCallbackEx( OS_EXCEPTION_MODE_GLOBAL_ALL_CORES, OS_EXCEPTION_TYPE_PROGRAM, WiiU_ProgramHandler );
	OSSetExceptionCallbackEx( OS_EXCEPTION_MODE_GLOBAL_ALL_CORES, OS_EXCEPTION_TYPE_ALIGNMENT, WiiU_AlignmentHandler );
}

void WiiU_Init( void )
{
	WiiU_InitLogging( );
	WiiU_InitCrashHandler( );
	WiiU_StartWatchdog( );
}

void WiiU_Shutdown( void )
{
	fflush( stdout );
	fflush( stderr );

	if( wiiu_log_ready )
	{
		WHBLogUdpDeinit( );
		WHBLogCafeDeinit( );
		wiiu_log_ready = false;
	}
}

void Platform_ShellExecute( const char *path, const char *parms )
{
	// there is no shell or browser to hand URLs to
	Con_Reportf( "%s: not supported on Wii U (%s)\n", __func__, path );
}
