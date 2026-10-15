/*
gx2gl.h - a small OpenGL 1.x subset on top of GX2, enough for Xash3D's ref_gl

The engine calls these instead of SDL's OpenGL functions on Wii U (vid_sdl2.c), and ref_gl finds the
GL functions through GX2GL_GetProcAddress. See Docs/ARCHITECTURE.md.
*/
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// create the GL context: render buffers of the given size (scaled to the TV and GamePad on swap),
// shaders, vertex memory. SDL's video driver must already be initialised (it owns GX2 and the scan
// buffers). Returns 0 on failure
int GX2GL_CreateContext( int width, int height );
void GX2GL_DestroyContext( void );

// show the frame on the TV and the GamePad. keyboardWindow: the SDL_Window while SDL's on-screen
// keyboard is shown (drawn over the frame like SDL's own present does), otherwise NULL
void GX2GL_SwapBuffers( void *keyboardWindow );

// 0 = don't wait for vsync
void GX2GL_SetSwapInterval( int interval );

void *GX2GL_GetProcAddress( const char *name );

// glGetString( GX2GL_STATS ): performance counters averaged over the last half second, shown by
// ref_gl's r_speeds 6 on Wii U (the value is from GL's reserved range, not used by real GL)
#define GX2GL_STATS 0x6A00

// CPU time of the frame's phases for the same display. The engine and ref_gl (XASH_WIIU code) wrap a
// phase in t = GX2GL_ProfStart( ) ... GX2GL_ProfEnd( phase, t ); phases may contain other phases
enum
{
	GX2GL_PROF_SERVER,   // Host_ServerFrame (game code, physics)
	GX2GL_PROF_CLIENT,   // Host_ClientFrame, includes SOUND and SCREEN
	GX2GL_PROF_SOUND,    // SND_UpdateSound (mixing)
	GX2GL_PROF_SCREEN,   // SCR_UpdateScreen, includes the 3D view, the HUD, menus and the swap's waits
	GX2GL_PROF_VIEW,     // R_RenderFrame: the 3D view
	GX2GL_PROF_WORLD,    // R_DrawWorld
	GX2GL_PROF_ENTITIES, // R_DrawEntitiesOnList (models, sprites, particles, beams, view model)
	GX2GL_PROF_STUDIO,   // studio models, all of it (also the game's bone setup in client.dll)
	GX2GL_PROF_STUDIO_POINTS, // the models' vertex transform, lighting and drawing (R_StudioDrawPoints)
	GX2GL_PROF_INPUT,    // Host_InputFrame and Host_ClientBegin (input, the client.dll's movement command)
	GX2GL_PROF_IN_EVENTS, // Host_InputFrame (SDL events, controller polling), includes PADS
	GX2GL_PROF_PADS,     // VPADRead, KPADReadEx and WPADProbe (wrapped in joy_sdl2.c)
	GX2GL_PROF_IN_MOVE,  // the client.dll's CL_CreateMove and the engine's controller/gyro move
	GX2GL_PROF_IN_PREDICT, // CL_PredictMovement for the new command
	GX2GL_PROF_CL_DLL,   // the client.dll's HUD_Frame
	GX2GL_PROF_CL_PACKETS, // CL_ReadPackets (the local server's updates, events, sounds)
	GX2GL_PROF_CL_PREDICT, // CL_RedoPrediction (player movement and weapon prediction)
	GX2GL_PROF_CL_EMIT,  // CL_EmitEntities (interpolation, the client.dll's AddEntity), includes CL_TEMPENTS
	GX2GL_PROF_CL_TEMPENTS, // the client.dll's CreateEntities and the temporary entities (shells, gibs, sprites)
	GX2GL_PROF_COUNT
};
unsigned long long GX2GL_ProfStart( void );
void GX2GL_ProfEnd( int phase, unsigned long long start );

// per-item times within a phase; r_speeds 6 shows the most expensive item of each group
enum
{
	GX2GL_ITEMS_SVC,   // server message types in CL_ParseServerMessage (svc_*)
	GX2GL_ITEMS_TE,    // temporary entity types in CL_ParseTempEntity (TE_*)
	GX2GL_ITEMS_BEGIN, // the parts of Host_ClientBegin (see cl_main.c)
	GX2GL_ITEMS_COUNT
};
#define GX2GL_ITEMS_MAX 256
void GX2GL_ProfItem( int group, int item, unsigned long long start );

// GX2GL_PROF( GX2GL_PROF_SERVER, Host_ServerFrame( )); other platforms define it as just the call
#define GX2GL_PROF( phase, call ) do { unsigned long long prof_t_ = GX2GL_ProfStart( ); call; GX2GL_ProfEnd( phase, prof_t_ ); } while( 0 )

#ifdef __cplusplus
}
#endif
