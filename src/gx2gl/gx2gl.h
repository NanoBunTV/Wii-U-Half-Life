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

#ifdef __cplusplus
}
#endif
