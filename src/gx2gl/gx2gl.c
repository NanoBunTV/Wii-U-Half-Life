/*
gx2gl.c - a small OpenGL 1.x subset on top of GX2, enough for Xash3D's ref_gl

How it works (see also Docs/ARCHITECTURE.md):
- One fixed-function shader pair (shaders/fixed.vsh/.psh) with uniform-controlled texture environments
  for two texture units, alpha test (GX2's own) and exponential fog.
- Immediate mode (glBegin/glEnd) and client vertex arrays are converted on the CPU into triangle (or
  line/point) lists of one vertex format, written into a vertex ring buffer. Consecutive primitives
  with the same state go into one draw call; any state change flushes the batch first.
- The ring buffer has two halves, one per frame in flight; the GPU timestamp of a frame is waited on
  before its half is reused. Memory freed during a frame (texture uploads, deleted textures) is
  released the same way.
- Textures are tiled GX2 textures (RGBA8). Uploads go through a linear staging surface and
  GX2CopySurface(Ex), so the GPU does the tiling.
- Rendering goes to our own colour/depth buffer of the requested size; on swap it's copied (scaled) to
  the TV and GamePad scan buffers, which SDL's video driver owns.
- Nothing is sent to the GPU while the app is in the background (HOME menu).
*/
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>

#include <gx2/clear.h>
#include <gx2/context.h>
#include <gx2/display.h>
#include <gx2/draw.h>
#include <gx2/enum.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/sampler.h>
#include <gx2/shaders.h>
#include <gx2/state.h>
#include <gx2/surface.h>
#include <gx2/swap.h>
#include <gx2/texture.h>
#include <gx2/utils.h>
#include <whb/gfx.h>
#include <whb/log.h>
#include <proc_ui/procui.h>

#include "gx2gl.h"
#include "gx2gl_gl.h"
#include "shaders/gx2gl_shaders.h"

#define GX2GL_UNITS        2
#define GX2GL_RING_SIZE    ( 6 * 1024 * 1024 ) // per frame in flight
#define GX2GL_MAX_IMM      65536
#define GX2GL_MAX_TEXTURES 65536

#define MV_STACK   32
#define PROJ_STACK 8
#define TEX_STACK  8

typedef struct
{
	float x, y, z;
	float r, g, b, a;
	float s0, t0;
	float s1, t1;
	float pad;
} gxvert_t; // 48 bytes, see the attribute setup in GX2GL_CreateContext

typedef struct
{
	GX2Texture tex;
	GX2Sampler sampler;
	int allocated;
	int width, height;
	int numLevels; // allocated mip levels
	GLenum internalFormat;
	GLenum minFilter, magFilter, wrapS, wrapT;
	int samplerDirty;
} gltex_t;

typedef struct
{
	int size;
	GLenum type;
	int stride;
	const void *ptr;
	int enabled;
} glarray_t;

typedef struct
{
	GLenum mode;        // GL_TEXTURE_ENV_MODE
	GLenum combineRGB;
	GLenum combineAlpha;
	GLenum source0Alpha;
	float rgbScale;
} texenv_t;

typedef struct
{
	void **ptrs;
	int count, max;
} freelist_t;

typedef enum
{
	BATCH_TRIANGLES,
	BATCH_LINES,
	BATCH_POINTS,
} batchtype_t;

static struct
{
	int initialized;
	int stateLost;     // GX2 context must be set again before drawing
	int shown;         // TV/DRC output enabled after the first frame
	int swapInterval;

	GX2ContextState *ctx;
	GX2ColorBuffer cb;
	GX2DepthBuffer db;
	int width, height;
	WHBGfxShaderGroup shader;

	// vertex ring: two halves, one per frame in flight
	gxvert_t *ring;
	uint32_t ringVerts;      // vertices per half
	uint32_t ringUsed;       // vertices used in the current half
	int slot;                // current half
	OSTime fence[2];
	freelist_t deferred[2];  // freed once the GPU is done with that frame

	// current batch
	gxvert_t *batchStart;
	uint32_t batchCount;
	batchtype_t batchType;

	// immediate mode
	GLenum immMode;
	int inBegin;
	gxvert_t *imm;
	int immCount;
	float color[4];
	float texcoord[GX2GL_UNITS][2];

	// client arrays
	glarray_t vertexArray, colorArray, texcoordArray[GX2GL_UNITS];
	int clientUnit;

	// matrices
	GLenum matrixMode;
	float mv[MV_STACK][16];
	float proj[PROJ_STACK][16];
	float texm[GX2GL_UNITS][TEX_STACK][16];
	int mvDepth, projDepth, texDepth[GX2GL_UNITS];
	int texIdentity[GX2GL_UNITS];

	// textures
	gltex_t **textures;
	gltex_t white;
	int activeUnit;
	GLuint bound[GX2GL_UNITS];
	int tex2d[GX2GL_UNITS];
	texenv_t env[GX2GL_UNITS];
	int unpackAlignment;
	GLuint nextTexture;

	// fixed-function state
	int blend, depthTest, cullFace, alphaTest, fog, scissorTest, polygonOffset;
	GLenum blendSrc, blendDst;
	GLenum alphaFunc;
	float alphaRef;
	GLenum depthFunc;
	int depthMask;
	GLenum cullMode, frontFace;
	float offsetFactor, offsetUnits;
	int colorMask;
	int viewport[4], scissor[4];
	float depthNear, depthFar;
	float clearColor[4];
	float clearDepth;
	GLenum fogMode;
	float fogDensity, fogStart, fogEnd, fogColor[4];
	float pointSize, lineWidth;
} g;

/*
=============
helpers
=============
*/
static void *GX2GL_Alloc( size_t align, size_t size )
{
	void *p = memalign( align, size );

	if( !p )
		WHBLogPrintf( "gx2gl: out of memory (%u bytes)", (unsigned)size );

	return p;
}

static void GX2GL_DeferFree( void *ptr )
{
	freelist_t *list = &g.deferred[g.slot];

	if( !ptr )
		return;

	if( list->count == list->max )
	{
		int max = list->max ? list->max * 2 : 64;
		void **ptrs = realloc( list->ptrs, max * sizeof( *ptrs ));

		if( !ptrs )
			return; // leak rather than free memory the GPU may still use

		list->ptrs = ptrs;
		list->max = max;
	}

	list->ptrs[list->count++] = ptr;
}

static void GX2GL_FreeDeferred( int slot )
{
	freelist_t *list = &g.deferred[slot];

	for( int i = 0; i < list->count; i++ )
		free( list->ptrs[i] );

	list->count = 0;
}

static void Mat_Identity( float *m )
{
	memset( m, 0, 16 * sizeof( float ));
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

// out = a * b, column-major (OpenGL order)
static void Mat_Mul( float *out, const float *a, const float *b )
{
	float r[16];

	for( int c = 0; c < 4; c++ )
	{
		for( int row = 0; row < 4; row++ )
		{
			r[c * 4 + row] = a[0 * 4 + row] * b[c * 4 + 0] + a[1 * 4 + row] * b[c * 4 + 1]
				+ a[2 * 4 + row] * b[c * 4 + 2] + a[3 * 4 + row] * b[c * 4 + 3];
		}
	}

	memcpy( out, r, sizeof( r ));
}

static int Mat_IsIdentity( const float *m )
{
	for( int i = 0; i < 16; i++ )
	{
		if( m[i] != (( i % 5 ) == 0 ? 1.0f : 0.0f ))
			return 0;
	}
	return 1;
}

static float *Mat_Current( void )
{
	switch( g.matrixMode )
	{
	case GL_PROJECTION: return g.proj[g.projDepth];
	case GL_TEXTURE: return g.texm[g.activeUnit][g.texDepth[g.activeUnit]];
	default: return g.mv[g.mvDepth];
	}
}

static gltex_t *GX2GL_GetTexture( GLuint id, int create )
{
	if( id == 0 || id >= GX2GL_MAX_TEXTURES )
		return NULL;

	if( !g.textures[id] && create )
	{
		gltex_t *t = calloc( 1, sizeof( *t ));

		if( !t )
			return NULL;

		t->minFilter = GL_NEAREST_MIPMAP_LINEAR;
		t->magFilter = GL_LINEAR;
		t->wrapS = t->wrapT = GL_REPEAT;
		t->samplerDirty = 1;
		g.textures[id] = t;
	}

	return g.textures[id];
}

// the GPU is only ours in the foreground. Checked live, not once per frame: after HOME -> Close the
// engine shuts the renderer down while in the background, and waiting on the GPU then never returns
static int GX2GL_Active( void )
{
	return ProcUIInForeground( );
}

/*
=============
batching
=============
*/
static void GX2GL_ApplyState( void );

static void GX2GL_Flush( void )
{
	static const GX2PrimitiveMode modes[] =
	{
		GX2_PRIMITIVE_MODE_TRIANGLES,
		GX2_PRIMITIVE_MODE_LINES,
		GX2_PRIMITIVE_MODE_POINTS,
	};
	uint32_t size;

	if( !g.batchCount )
		return;

	if( GX2GL_Active( ) )
	{
		GX2GL_ApplyState();

		size = g.batchCount * sizeof( gxvert_t );
		GX2Invalidate( GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, g.batchStart, size );
		GX2SetAttribBuffer( 0, size, sizeof( gxvert_t ), g.batchStart );
		GX2DrawEx( modes[g.batchType], g.batchCount, 0, 1 );
	}

	g.batchCount = 0;
	g.batchStart = NULL;
}

// a state change that affects drawing: draw what was batched with the old state
#define STATE_CHANGE() GX2GL_Flush()

// room for count more vertices in the current batch
static gxvert_t *GX2GL_BatchAlloc( batchtype_t type, uint32_t count )
{
	gxvert_t *out;

	if( g.batchCount && g.batchType != type )
		GX2GL_Flush();

	if( g.ringUsed + count > g.ringVerts )
	{
		// the frame used up its half: wait for the GPU and start over
		GX2GL_Flush();
		if( GX2GL_Active( ) )
			GX2DrawDone();
		g.ringUsed = 0;

		if( count > g.ringVerts )
			return NULL;
	}

	out = g.ring + g.slot * g.ringVerts + g.ringUsed;

	if( !g.batchCount )
	{
		g.batchStart = out;
		g.batchType = type;
	}

	g.ringUsed += count;
	g.batchCount += count;
	return out;
}

static void GX2GL_ApplyTexMatrix( gxvert_t *v, int n )
{
	for( int unit = 0; unit < GX2GL_UNITS; unit++ )
	{
		const float *m;

		if( g.texIdentity[unit] )
			continue;

		m = g.texm[unit][g.texDepth[unit]];

		for( int i = 0; i < n; i++ )
		{
			float *st = unit ? &v[i].s1 : &v[i].s0;
			float s = st[0], t = st[1];

			st[0] = m[0] * s + m[4] * t + m[12];
			st[1] = m[1] * s + m[5] * t + m[13];
		}
	}
}

// turn a GL primitive into the current batch
static void GX2GL_EmitPrimitive( GLenum mode, gxvert_t *v, int n )
{
	gxvert_t *out;
	int i;

	GX2GL_ApplyTexMatrix( v, n );

	switch( mode )
	{
	case GL_TRIANGLES:
		n -= n % 3;
		if( n <= 0 || !( out = GX2GL_BatchAlloc( BATCH_TRIANGLES, n )))
			return;
		memcpy( out, v, n * sizeof( *v ));
		break;
	case GL_TRIANGLE_STRIP:
		if( n < 3 || !( out = GX2GL_BatchAlloc( BATCH_TRIANGLES, ( n - 2 ) * 3 )))
			return;
		for( i = 0; i < n - 2; i++ )
		{
			// keep the winding of every other triangle
			*out++ = v[( i & 1 ) ? i + 1 : i];
			*out++ = v[( i & 1 ) ? i : i + 1];
			*out++ = v[i + 2];
		}
		break;
	case GL_TRIANGLE_FAN:
	case GL_POLYGON:
		if( n < 3 || !( out = GX2GL_BatchAlloc( BATCH_TRIANGLES, ( n - 2 ) * 3 )))
			return;
		for( i = 1; i < n - 1; i++ )
		{
			*out++ = v[0];
			*out++ = v[i];
			*out++ = v[i + 1];
		}
		break;
	case GL_QUADS:
		n -= n % 4;
		if( n <= 0 || !( out = GX2GL_BatchAlloc( BATCH_TRIANGLES, n / 4 * 6 )))
			return;
		for( i = 0; i < n; i += 4 )
		{
			*out++ = v[i];
			*out++ = v[i + 1];
			*out++ = v[i + 2];
			*out++ = v[i];
			*out++ = v[i + 2];
			*out++ = v[i + 3];
		}
		break;
	case GL_QUAD_STRIP:
		if( n < 4 || !( out = GX2GL_BatchAlloc( BATCH_TRIANGLES, ( n / 2 - 1 ) * 6 )))
			return;
		for( i = 0; i + 3 < n; i += 2 )
		{
			*out++ = v[i];
			*out++ = v[i + 1];
			*out++ = v[i + 3];
			*out++ = v[i];
			*out++ = v[i + 3];
			*out++ = v[i + 2];
		}
		break;
	case GL_LINES:
		n -= n % 2;
		if( n <= 0 || !( out = GX2GL_BatchAlloc( BATCH_LINES, n )))
			return;
		memcpy( out, v, n * sizeof( *v ));
		break;
	case GL_LINE_STRIP:
	case GL_LINE_LOOP:
	{
		int segs = n - 1 + ( mode == GL_LINE_LOOP && n > 2 );

		if( n < 2 || !( out = GX2GL_BatchAlloc( BATCH_LINES, segs * 2 )))
			return;
		for( i = 0; i < segs; i++ )
		{
			*out++ = v[i];
			*out++ = v[( i + 1 ) % n];
		}
		break;
	}
	case GL_POINTS:
		if( n <= 0 || !( out = GX2GL_BatchAlloc( BATCH_POINTS, n )))
			return;
		memcpy( out, v, n * sizeof( *v ));
		break;
	}
}

/*
=============
GX2GL_ApplyState

send the whole fixed-function state before a draw
=============
*/
static GX2CompareFunction GX2GL_CompareFunc( GLenum func )
{
	switch( func )
	{
	case GL_NEVER: return GX2_COMPARE_FUNC_NEVER;
	case GL_LESS: return GX2_COMPARE_FUNC_LESS;
	// ref_gl draws lightmaps as a second pass with GL_EQUAL over the textured surfaces; in Cemu many
	// surfaces failed it (the lightmap pass vanished), LEQUAL draws them the same way
	case GL_EQUAL: return GX2_COMPARE_FUNC_LEQUAL;
	case GL_LEQUAL: return GX2_COMPARE_FUNC_LEQUAL;
	case GL_GREATER: return GX2_COMPARE_FUNC_GREATER;
	case GL_NOTEQUAL: return GX2_COMPARE_FUNC_NOT_EQUAL;
	case GL_GEQUAL: return GX2_COMPARE_FUNC_GEQUAL;
	default: return GX2_COMPARE_FUNC_ALWAYS;
	}
}

static GX2BlendMode GX2GL_BlendMode( GLenum factor )
{
	switch( factor )
	{
	case GL_ZERO: return GX2_BLEND_MODE_ZERO;
	case GL_ONE: return GX2_BLEND_MODE_ONE;
	case GL_SRC_COLOR: return GX2_BLEND_MODE_SRC_COLOR;
	case GL_ONE_MINUS_SRC_COLOR: return GX2_BLEND_MODE_INV_SRC_COLOR;
	case GL_SRC_ALPHA: return GX2_BLEND_MODE_SRC_ALPHA;
	case GL_ONE_MINUS_SRC_ALPHA: return GX2_BLEND_MODE_INV_SRC_ALPHA;
	case GL_DST_ALPHA: return GX2_BLEND_MODE_DST_ALPHA;
	case GL_ONE_MINUS_DST_ALPHA: return GX2_BLEND_MODE_INV_DST_ALPHA;
	case GL_DST_COLOR: return GX2_BLEND_MODE_DST_COLOR;
	case GL_ONE_MINUS_DST_COLOR: return GX2_BLEND_MODE_INV_DST_COLOR;
	case GL_SRC_ALPHA_SATURATE: return GX2_BLEND_MODE_SRC_ALPHA_SAT;
	default: return GX2_BLEND_MODE_ONE;
	}
}

static GX2TexClampMode GX2GL_ClampMode( GLenum wrap )
{
	switch( wrap )
	{
	case GL_CLAMP:
	case GL_CLAMP_TO_EDGE: return GX2_TEX_CLAMP_MODE_CLAMP;
	case GL_CLAMP_TO_BORDER: return GX2_TEX_CLAMP_MODE_CLAMP_BORDER;
	case GL_MIRRORED_REPEAT: return GX2_TEX_CLAMP_MODE_MIRROR;
	default: return GX2_TEX_CLAMP_MODE_WRAP;
	}
}

static void GX2GL_UpdateSampler( gltex_t *t )
{
	GX2TexXYFilterMode mag = t->magFilter == GL_NEAREST ? GX2_TEX_XY_FILTER_MODE_POINT : GX2_TEX_XY_FILTER_MODE_LINEAR;
	GX2TexXYFilterMode min;
	GX2TexMipFilterMode mip;

	switch( t->minFilter )
	{
	case GL_NEAREST: min = GX2_TEX_XY_FILTER_MODE_POINT; mip = GX2_TEX_MIP_FILTER_MODE_NONE; break;
	case GL_LINEAR: min = GX2_TEX_XY_FILTER_MODE_LINEAR; mip = GX2_TEX_MIP_FILTER_MODE_NONE; break;
	case GL_NEAREST_MIPMAP_NEAREST: min = GX2_TEX_XY_FILTER_MODE_POINT; mip = GX2_TEX_MIP_FILTER_MODE_POINT; break;
	case GL_LINEAR_MIPMAP_NEAREST: min = GX2_TEX_XY_FILTER_MODE_LINEAR; mip = GX2_TEX_MIP_FILTER_MODE_POINT; break;
	case GL_NEAREST_MIPMAP_LINEAR: min = GX2_TEX_XY_FILTER_MODE_POINT; mip = GX2_TEX_MIP_FILTER_MODE_LINEAR; break;
	default: min = GX2_TEX_XY_FILTER_MODE_LINEAR; mip = GX2_TEX_MIP_FILTER_MODE_LINEAR; break;
	}

	GX2InitSampler( &t->sampler, GX2_TEX_CLAMP_MODE_WRAP, GX2_TEX_XY_FILTER_MODE_LINEAR );
	GX2InitSamplerXYFilter( &t->sampler, mag, min, GX2_TEX_ANISO_RATIO_NONE );
	GX2InitSamplerZMFilter( &t->sampler, GX2_TEX_Z_FILTER_MODE_NONE, mip );
	GX2InitSamplerClamping( &t->sampler, GX2GL_ClampMode( t->wrapS ), GX2GL_ClampMode( t->wrapT ), GX2_TEX_CLAMP_MODE_WRAP );
	GX2InitSamplerLOD( &t->sampler, 0.0f, mip == GX2_TEX_MIP_FILTER_MODE_NONE ? 0.0f : 16.0f, 0.0f );
	t->samplerDirty = 0;
}

// texture environment weights for the pixel shader, see shaders/fixed.psh
static void GX2GL_EnvWeights( int unit, int enabled, float *k )
{
	const texenv_t *env = &g.env[unit];
	float scale = 1.0f;
	int c;

	memset( k, 0, 16 * sizeof( float )); // K0..K3, 4 components each

#define K( n, comp ) k[( n ) * 4 + ( comp )]

	if( !enabled )
	{
		for( c = 0; c < 4; c++ )
			K( 2, c ) = 1.0f;
		return;
	}

	switch( env->mode )
	{
	case GL_REPLACE:
		for( c = 0; c < 4; c++ )
			K( 1, c ) = 1.0f;
		break;
	case GL_DECAL:
		for( c = 0; c < 3; c++ )
			K( 2, c ) = K( 3, c ) = 1.0f;
		K( 2, 3 ) = 1.0f;
		break;
	case GL_ADD:
		for( c = 0; c < 3; c++ )
			K( 1, c ) = K( 2, c ) = 1.0f;
		K( 0, 3 ) = 1.0f;
		break;
	case GL_COMBINE:
		switch( env->combineRGB )
		{
		case GL_REPLACE: K( 1, 0 ) = K( 1, 1 ) = K( 1, 2 ) = 1.0f; break;
		case GL_ADD: K( 1, 0 ) = K( 1, 1 ) = K( 1, 2 ) = K( 2, 0 ) = K( 2, 1 ) = K( 2, 2 ) = 1.0f; break;
		default: K( 0, 0 ) = K( 0, 1 ) = K( 0, 2 ) = 1.0f; break; // modulate previous * texture
		}
		switch( env->combineAlpha )
		{
		case GL_REPLACE:
			if( env->source0Alpha == GL_TEXTURE )
				K( 1, 3 ) = 1.0f;
			else
				K( 2, 3 ) = 1.0f;
			break;
		default: K( 0, 3 ) = 1.0f; break;
		}
		scale = env->rgbScale;
		break;
	default: // GL_MODULATE, GL_BLEND
		for( c = 0; c < 4; c++ )
			K( 0, c ) = 1.0f;
		break;
	}

	if( scale != 1.0f )
	{
		for( int n = 0; n < 4; n++ )
			for( c = 0; c < 3; c++ )
				K( n, c ) *= scale;
	}
#undef K
}

static void GX2GL_ApplyState( void )
{
	float mvp[16], vsu[24], psu[40];
	int unit;

	if( g.stateLost )
	{
		GX2SetContextState( g.ctx );
		GX2SetColorBuffer( &g.cb, GX2_RENDER_TARGET_0 );
		GX2SetDepthBuffer( &g.db );
		GX2SetShaderMode( GX2_SHADER_MODE_UNIFORM_REGISTER );
		GX2SetFetchShader( &g.shader.fetchShader );
		GX2SetVertexShader( g.shader.vertexShader );
		GX2SetPixelShader( g.shader.pixelShader );
		g.stateLost = 0;
	}

	// viewport and scissor (GL's origin is bottom-left)
	GX2SetViewport( g.viewport[0], g.height - ( g.viewport[1] + g.viewport[3] ), g.viewport[2], g.viewport[3], g.depthNear, g.depthFar );

	if( g.scissorTest )
	{
		int x = g.scissor[0], y = g.height - ( g.scissor[1] + g.scissor[3] ), w = g.scissor[2], h = g.scissor[3];

		if( x < 0 ) { w += x; x = 0; }
		if( y < 0 ) { h += y; y = 0; }
		if( x + w > g.width ) w = g.width - x;
		if( y + h > g.height ) h = g.height - y;
		GX2SetScissor( x, y, w > 0 ? w : 0, h > 0 ? h : 0 );
	}
	else
	{
		GX2SetScissor( 0, 0, g.width, g.height );
	}

	GX2SetDepthOnlyControl( g.depthTest, g.depthTest && g.depthMask, GX2GL_CompareFunc( g.depthFunc ));
	GX2SetColorControl( GX2_LOGIC_OP_COPY, g.blend ? 1 : 0, FALSE, TRUE );
	GX2SetBlendControl( GX2_RENDER_TARGET_0, GX2GL_BlendMode( g.blendSrc ), GX2GL_BlendMode( g.blendDst ),
		GX2_BLEND_COMBINE_MODE_ADD, FALSE, GX2_BLEND_MODE_ONE, GX2_BLEND_MODE_ZERO, GX2_BLEND_COMBINE_MODE_ADD );
	GX2SetAlphaTest( g.alphaTest, GX2GL_CompareFunc( g.alphaFunc ), g.alphaRef );
	GX2SetTargetChannelMasks( (GX2ChannelMask)g.colorMask, 0, 0, 0, 0, 0, 0, 0 );

	GX2SetPolygonControl( g.frontFace == GL_CW ? GX2_FRONT_FACE_CW : GX2_FRONT_FACE_CCW,
		g.cullFace && ( g.cullMode == GL_FRONT || g.cullMode == GL_FRONT_AND_BACK ),
		g.cullFace && ( g.cullMode == GL_BACK || g.cullMode == GL_FRONT_AND_BACK ),
		FALSE, GX2_POLYGON_MODE_TRIANGLE, GX2_POLYGON_MODE_TRIANGLE,
		g.polygonOffset, g.polygonOffset, FALSE );
	if( g.polygonOffset )
		GX2SetPolygonOffset( g.offsetUnits, g.offsetFactor * 16.0f, g.offsetUnits, g.offsetFactor * 16.0f, 0.0f );

	if( g.batchType == BATCH_POINTS )
		GX2SetPointSize( g.pointSize, g.pointSize );
	else if( g.batchType == BATCH_LINES )
		GX2SetLineWidth( g.lineWidth );

	// textures
	for( unit = 0; unit < GX2GL_UNITS; unit++ )
	{
		gltex_t *t = g.tex2d[unit] ? GX2GL_GetTexture( g.bound[unit], 0 ) : NULL;

		if( !t || !t->allocated )
			t = &g.white;
		else if( t->samplerDirty )
			GX2GL_UpdateSampler( t );

		GX2SetPixelTexture( &t->tex, unit );
		GX2SetPixelSampler( &t->sampler, unit );
	}

	// vertex shader uniforms: MVP, modelview row 2 (eye z), fog coefficient
	Mat_Mul( mvp, g.proj[g.projDepth], g.mv[g.mvDepth] );
	memcpy( vsu, mvp, sizeof( mvp ));
	vsu[16] = g.mv[g.mvDepth][2];
	vsu[17] = g.mv[g.mvDepth][6];
	vsu[18] = g.mv[g.mvDepth][10];
	vsu[19] = g.mv[g.mvDepth][14];
	vsu[20] = g.fog ? g.fogDensity * 1.4426950f : 0.0f; // 2^( density * log2(e) * z ), z < 0 in front
	vsu[21] = vsu[22] = vsu[23] = 0.0f;
	GX2SetVertexUniformReg( 0, 24, vsu );

	// pixel shader uniforms: environment weights, alpha reference (unused, GX2 does the alpha test), fog colour
	GX2GL_EnvWeights( 0, g.tex2d[0], &psu[0] );
	GX2GL_EnvWeights( 1, g.tex2d[1], &psu[16] );
	psu[32] = -1.0f;
	psu[33] = psu[34] = psu[35] = 0.0f;
	psu[36] = g.fogColor[0];
	psu[37] = g.fogColor[1];
	psu[38] = g.fogColor[2];
	psu[39] = 1.0f;
	GX2SetPixelUniformReg( 0, 40, psu );
}

/*
=============
textures
=============
*/
static int GX2GL_FormatBytes( GLenum format )
{
	switch( format )
	{
	case GL_RGBA:
	case GL_BGRA: return 4;
	case GL_RGB:
	case GL_BGR: return 3;
	case GL_LUMINANCE_ALPHA: return 2;
	default: return 1;
	}
}

// convert one row of client pixels to RGBA8, applying what the internal format keeps
static void GX2GL_ConvertRow( uint8_t *dst, const uint8_t *src, int w, GLenum format, GLenum internalFormat )
{
	for( int x = 0; x < w; x++, dst += 4 )
	{
		uint8_t r, gr, b, a;

		switch( format )
		{
		case GL_RGBA: r = src[0]; gr = src[1]; b = src[2]; a = src[3]; src += 4; break;
		case GL_BGRA: b = src[0]; gr = src[1]; r = src[2]; a = src[3]; src += 4; break;
		case GL_RGB: r = src[0]; gr = src[1]; b = src[2]; a = 255; src += 3; break;
		case GL_BGR: b = src[0]; gr = src[1]; r = src[2]; a = 255; src += 3; break;
		case GL_LUMINANCE_ALPHA: r = gr = b = src[0]; a = src[1]; src += 2; break;
		case GL_ALPHA: r = gr = b = 255; a = src[0]; src += 1; break;
		default: r = gr = b = src[0]; a = 255; src += 1; break; // GL_LUMINANCE
		}

		switch( internalFormat )
		{
		case GL_RGB: case GL_RGB5: case GL_RGB8: case 3:
			a = 255;
			break;
		case GL_LUMINANCE: case GL_LUMINANCE8: case 1:
			gr = b = r;
			a = 255;
			break;
		case GL_INTENSITY: case GL_INTENSITY8:
			gr = b = a = r;
			break;
		case GL_LUMINANCE_ALPHA: case GL_LUMINANCE8_ALPHA8: case 2:
			gr = b = r;
			break;
		case GL_ALPHA: case GL_ALPHA8:
			r = gr = b = 0;
			break;
		}

		dst[0] = r;
		dst[1] = gr;
		dst[2] = b;
		dst[3] = a;
	}
}

static void GX2GL_InitSurface( GX2Surface *s, int w, int h, int levels, GX2TileMode tile )
{
	memset( s, 0, sizeof( *s ));
	s->dim = GX2_SURFACE_DIM_TEXTURE_2D;
	s->width = w;
	s->height = h;
	s->depth = 1;
	s->mipLevels = levels;
	s->format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
	s->aa = GX2_AA_MODE1X;
	s->use = GX2_SURFACE_USE_TEXTURE;
	s->tileMode = tile;
	GX2CalcSurfaceSizeAndAlignment( s );
}

static void GX2GL_FreeTextureMemory( gltex_t *t )
{
	if( !t->allocated )
		return;

	GX2GL_DeferFree( t->tex.surface.image );
	GX2GL_DeferFree( t->tex.surface.mipmaps );
	t->tex.surface.image = t->tex.surface.mipmaps = NULL;
	t->allocated = 0;
}

static int GX2GL_AllocTexture( gltex_t *t, int w, int h )
{
	int levels = 1;

	GX2GL_FreeTextureMemory( t );

	while(( w >> levels ) || ( h >> levels ))
		levels++;

	memset( &t->tex, 0, sizeof( t->tex ));
	GX2GL_InitSurface( &t->tex.surface, w, h, levels, GX2_TILE_MODE_DEFAULT );

	t->tex.surface.image = GX2GL_Alloc( t->tex.surface.alignment, t->tex.surface.imageSize );
	if( t->tex.surface.mipmapSize )
		t->tex.surface.mipmaps = GX2GL_Alloc( t->tex.surface.alignment, t->tex.surface.mipmapSize );

	if( !t->tex.surface.image || ( t->tex.surface.mipmapSize && !t->tex.surface.mipmaps ))
	{
		free( t->tex.surface.image );
		free( t->tex.surface.mipmaps );
		t->tex.surface.image = t->tex.surface.mipmaps = NULL;
		return 0;
	}

	memset( t->tex.surface.image, 0, t->tex.surface.imageSize );
	GX2Invalidate( GX2_INVALIDATE_MODE_CPU_TEXTURE, t->tex.surface.image, t->tex.surface.imageSize );
	if( t->tex.surface.mipmaps )
	{
		memset( t->tex.surface.mipmaps, 0, t->tex.surface.mipmapSize );
		GX2Invalidate( GX2_INVALIDATE_MODE_CPU_TEXTURE, t->tex.surface.mipmaps, t->tex.surface.mipmapSize );
	}

	t->tex.viewFirstMip = 0;
	t->tex.viewNumMips = 1;
	t->tex.viewFirstSlice = 0;
	t->tex.viewNumSlices = 1;
	t->tex.compMap = GX2_COMP_MAP( GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A );
	GX2InitTextureRegs( &t->tex );

	t->allocated = 1;
	t->width = w;
	t->height = h;
	t->numLevels = levels;
	t->samplerDirty = 1;
	return 1;
}

// copy client pixels into a mip level through a linear staging surface
static void GX2GL_Upload( gltex_t *t, int level, int x, int y, int w, int h, GLenum format, const void *pixels )
{
	GX2Surface staging;
	const uint8_t *src = pixels;
	int srcPitch;
	int levelW = t->width >> level, levelH = t->height >> level;

	if( !GX2GL_Active( ) || !pixels || w <= 0 || h <= 0 || level >= t->numLevels )
		return;

	if( levelW < 1 ) levelW = 1;
	if( levelH < 1 ) levelH = 1;

	GX2GL_InitSurface( &staging, w, h, 1, GX2_TILE_MODE_LINEAR_ALIGNED );
	staging.image = GX2GL_Alloc( staging.alignment, staging.imageSize );
	if( !staging.image )
		return;

	srcPitch = w * GX2GL_FormatBytes( format );
	srcPitch = ( srcPitch + g.unpackAlignment - 1 ) & ~( g.unpackAlignment - 1 );

	for( int row = 0; row < h; row++ )
		GX2GL_ConvertRow( (uint8_t *)staging.image + row * staging.pitch * 4, src + row * srcPitch, w, format, t->internalFormat );

	GX2Invalidate( GX2_INVALIDATE_MODE_CPU_TEXTURE, staging.image, staging.imageSize );

	if( g.stateLost )
	{
		GX2SetContextState( g.ctx );
		g.stateLost = 0;
	}

	if( x == 0 && y == 0 && w == levelW && h == levelH )
	{
		GX2CopySurface( &staging, 0, 0, &t->tex.surface, level, 0 );
	}
	else
	{
		GX2Rect rect = { 0, 0, w, h };
		GX2Point point = { x, y };
		GX2CopySurfaceEx( &staging, 0, 0, &t->tex.surface, level, 0, 1, &rect, &point );
	}

	// the copy changes GPU state, and the texture cache must see the new texels
	g.stateLost = 1;
	GX2Invalidate( GX2_INVALIDATE_MODE_TEXTURE, t->tex.surface.image, t->tex.surface.imageSize );
	if( t->tex.surface.mipmaps )
		GX2Invalidate( GX2_INVALIDATE_MODE_TEXTURE, t->tex.surface.mipmaps, t->tex.surface.mipmapSize );

	GX2GL_DeferFree( staging.image );
}

/*
=============
GL entry points (the names end in _, GX2GL_GetProcAddress maps the real names to them)
=============
*/
static void glClearColor_( GLclampf r, GLclampf gr, GLclampf b, GLclampf a )
{
	g.clearColor[0] = r; g.clearColor[1] = gr; g.clearColor[2] = b; g.clearColor[3] = a;
}

static void glClear_( GLbitfield mask )
{
	GX2GL_Flush();

	if( !GX2GL_Active( ) )
		return;

	if( g.stateLost )
	{
		GX2SetContextState( g.ctx );
		g.stateLost = 0;
	}

	if( mask & GL_COLOR_BUFFER_BIT )
		GX2ClearColor( &g.cb, g.clearColor[0], g.clearColor[1], g.clearColor[2], g.clearColor[3] );
	if( mask & GL_DEPTH_BUFFER_BIT )
		GX2ClearDepthStencilEx( &g.db, g.clearDepth, 0, GX2_CLEAR_FLAGS_DEPTH );

	g.stateLost = 1; // clears change GPU state
}

static void glAlphaFunc_( GLenum func, GLclampf ref )
{
	if( g.alphaFunc != func || g.alphaRef != ref )
	{
		STATE_CHANGE();
		g.alphaFunc = func;
		g.alphaRef = ref;
	}
}

static void glBlendFunc_( GLenum s, GLenum d )
{
	if( g.blendSrc != s || g.blendDst != d )
	{
		STATE_CHANGE();
		g.blendSrc = s;
		g.blendDst = d;
	}
}

static void glCullFace_( GLenum mode )
{
	if( g.cullMode != mode )
	{
		STATE_CHANGE();
		g.cullMode = mode;
	}
}

static void glFrontFace_( GLenum mode )
{
	if( g.frontFace != mode )
	{
		STATE_CHANGE();
		g.frontFace = mode;
	}
}

static int *GX2GL_Cap( GLenum cap )
{
	switch( cap )
	{
	case GL_BLEND: return &g.blend;
	case GL_DEPTH_TEST: return &g.depthTest;
	case GL_CULL_FACE: return &g.cullFace;
	case GL_ALPHA_TEST: return &g.alphaTest;
	case GL_FOG: return &g.fog;
	case GL_SCISSOR_TEST: return &g.scissorTest;
	case GL_POLYGON_OFFSET_FILL: return &g.polygonOffset;
	case GL_TEXTURE_2D: return &g.tex2d[g.activeUnit];
	default: return NULL;
	}
}

static void GX2GL_SetCap( GLenum cap, int value )
{
	int *p = GX2GL_Cap( cap );

	if( p && *p != value )
	{
		STATE_CHANGE();
		*p = value;
	}
}

static void glEnable_( GLenum cap ) { GX2GL_SetCap( cap, 1 ); }
static void glDisable_( GLenum cap ) { GX2GL_SetCap( cap, 0 ); }

static GLboolean glIsEnabled_( GLenum cap )
{
	int *p = GX2GL_Cap( cap );
	return p ? (GLboolean)*p : GL_FALSE;
}

static glarray_t *GX2GL_Array( GLenum array )
{
	switch( array )
	{
	case GL_VERTEX_ARRAY: return &g.vertexArray;
	case GL_COLOR_ARRAY: return &g.colorArray;
	case GL_TEXTURE_COORD_ARRAY: return &g.texcoordArray[g.clientUnit];
	default: return NULL;
	}
}

static void glEnableClientState_( GLenum array )
{
	glarray_t *a = GX2GL_Array( array );
	if( a ) a->enabled = 1;
}

static void glDisableClientState_( GLenum array )
{
	glarray_t *a = GX2GL_Array( array );
	if( a ) a->enabled = 0;
}

static void glGetFloatv_( GLenum pname, GLfloat *params )
{
	switch( pname )
	{
	case GL_MODELVIEW_MATRIX: memcpy( params, g.mv[g.mvDepth], 16 * sizeof( float )); break;
	case GL_PROJECTION_MATRIX: memcpy( params, g.proj[g.projDepth], 16 * sizeof( float )); break;
	case GL_TEXTURE_MATRIX: memcpy( params, g.texm[g.activeUnit][g.texDepth[g.activeUnit]], 16 * sizeof( float )); break;
	case GL_FOG_COLOR: memcpy( params, g.fogColor, 4 * sizeof( float )); break;
	default: params[0] = 0.0f; break;
	}
}

static void glGetIntegerv_( GLenum pname, GLint *params )
{
	switch( pname )
	{
	case GL_MAX_TEXTURE_SIZE: params[0] = 4096; break;
	case GL_MAX_TEXTURE_UNITS: params[0] = GX2GL_UNITS; break;
	case GL_VIEWPORT: memcpy( params, g.viewport, 4 * sizeof( int )); break;
	case GL_MAJOR_VERSION: params[0] = 1; break;
	case GL_MINOR_VERSION: params[0] = 3; break;
	case GL_RED_BITS: case GL_GREEN_BITS: case GL_BLUE_BITS: case GL_ALPHA_BITS: params[0] = 8; break;
	case GL_DEPTH_BITS: params[0] = 24; break;
	case GL_ACTIVE_TEXTURE: params[0] = GL_TEXTURE0 + g.activeUnit; break;
	case GL_CLIENT_ACTIVE_TEXTURE: params[0] = GL_TEXTURE0 + g.clientUnit; break;
	default: params[0] = 0; break;
	}
}

static void glGetBooleanv_( GLenum pname, GLboolean *params )
{
	GLint v = 0;
	glGetIntegerv_( pname, &v );
	params[0] = v ? GL_TRUE : GL_FALSE;
}

static void glGetDoublev_( GLenum pname, GLdouble *params )
{
	float f[16] = { 0 };
	int n = ( pname == GL_MODELVIEW_MATRIX || pname == GL_PROJECTION_MATRIX || pname == GL_TEXTURE_MATRIX ) ? 16 : 1;

	glGetFloatv_( pname, f );
	for( int i = 0; i < n; i++ )
		params[i] = f[i];
}

static GLenum glGetError_( void ) { return GL_NO_ERROR; }

static const GLubyte *glGetString_( GLenum name )
{
	switch( name )
	{
	case GL_VENDOR: return (const GLubyte *)"Nintendo Wii U";
	case GL_RENDERER: return (const GLubyte *)"GX2 (gx2gl)";
	case GL_VERSION: return (const GLubyte *)"1.3 gx2gl";
	case GL_EXTENSIONS: return (const GLubyte *)"GL_ARB_multitexture GL_ARB_texture_env_combine GL_ARB_texture_env_add "
		"GL_EXT_texture_edge_clamp GL_ARB_texture_non_power_of_two";
	default: return (const GLubyte *)"";
	}
}

static void glFinish_( void ) { GX2GL_Flush(); }
static void glFlush_( void ) { GX2GL_Flush(); }

static void glClearDepth_( GLclampd depth ) { g.clearDepth = (float)depth; }

static void glDepthFunc_( GLenum func )
{
	if( g.depthFunc != func )
	{
		STATE_CHANGE();
		g.depthFunc = func;
	}
}

static void glDepthMask_( GLboolean flag )
{
	if( g.depthMask != !!flag )
	{
		STATE_CHANGE();
		g.depthMask = !!flag;
	}
}

static void glDepthRange_( GLclampd n, GLclampd f )
{
	if( g.depthNear != (float)n || g.depthFar != (float)f )
	{
		STATE_CHANGE();
		g.depthNear = (float)n;
		g.depthFar = (float)f;
	}
}

static void glColorMask_( GLboolean r, GLboolean gr, GLboolean b, GLboolean a )
{
	int mask = ( r ? 1 : 0 ) | ( gr ? 2 : 0 ) | ( b ? 4 : 0 ) | ( a ? 8 : 0 );

	if( g.colorMask != mask )
	{
		STATE_CHANGE();
		g.colorMask = mask;
	}
}

static void glViewport_( GLint x, GLint y, GLsizei w, GLsizei h )
{
	if( g.viewport[0] != x || g.viewport[1] != y || g.viewport[2] != w || g.viewport[3] != h )
	{
		STATE_CHANGE();
		g.viewport[0] = x; g.viewport[1] = y; g.viewport[2] = w; g.viewport[3] = h;
	}
}

static void glScissor_( GLint x, GLint y, GLsizei w, GLsizei h )
{
	STATE_CHANGE();
	g.scissor[0] = x; g.scissor[1] = y; g.scissor[2] = w; g.scissor[3] = h;
}

static void glPolygonOffset_( GLfloat factor, GLfloat units )
{
	if( g.offsetFactor != factor || g.offsetUnits != units )
	{
		STATE_CHANGE();
		g.offsetFactor = factor;
		g.offsetUnits = units;
	}
}

static void glPointSize_( GLfloat size )
{
	if( g.pointSize != size )
	{
		STATE_CHANGE();
		g.pointSize = size;
	}
}

static void glLineWidth_( GLfloat width )
{
	if( g.lineWidth != width )
	{
		STATE_CHANGE();
		g.lineWidth = width;
	}
}

// client arrays
static void GX2GL_SetArray( glarray_t *a, GLint size, GLenum type, GLsizei stride, const GLvoid *ptr )
{
	a->size = size;
	a->type = type;
	a->stride = stride;
	a->ptr = ptr;
}

static void glVertexPointer_( GLint size, GLenum type, GLsizei stride, const GLvoid *ptr ) { GX2GL_SetArray( &g.vertexArray, size, type, stride, ptr ); }
static void glColorPointer_( GLint size, GLenum type, GLsizei stride, const GLvoid *ptr ) { GX2GL_SetArray( &g.colorArray, size, type, stride, ptr ); }
static void glTexCoordPointer_( GLint size, GLenum type, GLsizei stride, const GLvoid *ptr ) { GX2GL_SetArray( &g.texcoordArray[g.clientUnit], size, type, stride, ptr ); }
static void glNormalPointer_( GLenum type, GLsizei stride, const GLvoid *ptr ) { (void)type; (void)stride; (void)ptr; }
static void glIndexPointer_( GLenum type, GLsizei stride, const GLvoid *ptr ) { (void)type; (void)stride; (void)ptr; }

static int GX2GL_TypeSize( GLenum type )
{
	switch( type )
	{
	case GL_BYTE: case GL_UNSIGNED_BYTE: return 1;
	case GL_SHORT: case GL_UNSIGNED_SHORT: return 2;
	case GL_DOUBLE: return 8;
	default: return 4;
	}
}

static float GX2GL_ReadComponent( const uint8_t *p, GLenum type, int normalized )
{
	switch( type )
	{
	case GL_UNSIGNED_BYTE: return normalized ? *p / 255.0f : *p;
	case GL_BYTE: return normalized ? *(const int8_t *)p / 127.0f : *(const int8_t *)p;
	case GL_SHORT: return *(const int16_t *)p;
	case GL_UNSIGNED_SHORT: return *(const uint16_t *)p;
	case GL_INT: return (float)*(const int32_t *)p;
	case GL_UNSIGNED_INT: return (float)*(const uint32_t *)p;
	case GL_DOUBLE: return (float)*(const double *)p;
	default: return *(const float *)p;
	}
}

static void GX2GL_ReadArray( const glarray_t *a, int index, float *out, int maxComps, int normalized )
{
	int comp = GX2GL_TypeSize( a->type );
	int stride = a->stride ? a->stride : a->size * comp;
	const uint8_t *p = (const uint8_t *)a->ptr + index * stride;
	int n = a->size < maxComps ? a->size : maxComps;

	for( int i = 0; i < n; i++ )
		out[i] = GX2GL_ReadComponent( p + i * comp, a->type, normalized );
}

static void GX2GL_ArrayVertex( int index, gxvert_t *v )
{
	float pos[3] = { 0.0f, 0.0f, 0.0f };
	float col[4] = { g.color[0], g.color[1], g.color[2], g.color[3] };
	float tc[GX2GL_UNITS][2];

	for( int unit = 0; unit < GX2GL_UNITS; unit++ )
	{
		tc[unit][0] = g.texcoord[unit][0];
		tc[unit][1] = g.texcoord[unit][1];
		if( g.texcoordArray[unit].enabled )
			GX2GL_ReadArray( &g.texcoordArray[unit], index, tc[unit], 2, 0 );
	}

	if( g.vertexArray.enabled )
		GX2GL_ReadArray( &g.vertexArray, index, pos, 3, 0 );
	if( g.colorArray.enabled )
	{
		col[3] = 1.0f;
		GX2GL_ReadArray( &g.colorArray, index, col, 4, 1 );
	}

	v->x = pos[0]; v->y = pos[1]; v->z = pos[2];
	v->r = col[0]; v->g = col[1]; v->b = col[2]; v->a = col[3];
	v->s0 = tc[0][0]; v->t0 = tc[0][1];
	v->s1 = tc[1][0]; v->t1 = tc[1][1];
	v->pad = 0.0f;
}

static void glArrayElement_( GLint i )
{
	if( g.immCount < GX2GL_MAX_IMM )
		GX2GL_ArrayVertex( i, &g.imm[g.immCount++] );
}

static void glDrawArrays_( GLenum mode, GLint first, GLsizei count )
{
	if( count > GX2GL_MAX_IMM )
		count = GX2GL_MAX_IMM;

	for( int i = 0; i < count; i++ )
		GX2GL_ArrayVertex( first + i, &g.imm[i] );

	GX2GL_EmitPrimitive( mode, g.imm, count );
}

static void glDrawElements_( GLenum mode, GLsizei count, GLenum type, const GLvoid *indices )
{
	if( count > GX2GL_MAX_IMM )
		count = GX2GL_MAX_IMM;

	for( int i = 0; i < count; i++ )
	{
		int index;

		switch( type )
		{
		case GL_UNSIGNED_BYTE: index = ((const uint8_t *)indices)[i]; break;
		case GL_UNSIGNED_SHORT: index = ((const uint16_t *)indices)[i]; break;
		default: index = ((const uint32_t *)indices)[i]; break;
		}

		GX2GL_ArrayVertex( index, &g.imm[i] );
	}

	GX2GL_EmitPrimitive( mode, g.imm, count );
}

static void glDrawRangeElements_( GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const GLvoid *indices )
{
	(void)start; (void)end;
	glDrawElements_( mode, count, type, indices );
}

// immediate mode
static void glBegin_( GLenum mode )
{
	g.immMode = mode;
	g.immCount = 0;
	g.inBegin = 1;
}

static void glEnd_( void )
{
	g.inBegin = 0;
	GX2GL_EmitPrimitive( g.immMode, g.imm, g.immCount );
	g.immCount = 0;
}

static void GX2GL_Vertex( float x, float y, float z )
{
	gxvert_t *v;

	if( g.immCount >= GX2GL_MAX_IMM )
		return;

	v = &g.imm[g.immCount++];
	v->x = x; v->y = y; v->z = z;
	v->r = g.color[0]; v->g = g.color[1]; v->b = g.color[2]; v->a = g.color[3];
	v->s0 = g.texcoord[0][0]; v->t0 = g.texcoord[0][1];
	v->s1 = g.texcoord[1][0]; v->t1 = g.texcoord[1][1];
	v->pad = 0.0f;
}

static void glVertex2f_( GLfloat x, GLfloat y ) { GX2GL_Vertex( x, y, 0.0f ); }
static void glVertex3f_( GLfloat x, GLfloat y, GLfloat z ) { GX2GL_Vertex( x, y, z ); }
static void glVertex3fv_( const GLfloat *v ) { GX2GL_Vertex( v[0], v[1], v[2] ); }

static void glColor4f_( GLfloat r, GLfloat gr, GLfloat b, GLfloat a ) { g.color[0] = r; g.color[1] = gr; g.color[2] = b; g.color[3] = a; }
static void glColor3f_( GLfloat r, GLfloat gr, GLfloat b ) { glColor4f_( r, gr, b, 1.0f ); }
static void glColor3fv_( const GLfloat *v ) { glColor4f_( v[0], v[1], v[2], 1.0f ); }
static void glColor4fv_( const GLfloat *v ) { glColor4f_( v[0], v[1], v[2], v[3] ); }
static void glColor3ub_( GLubyte r, GLubyte gr, GLubyte b ) { glColor4f_( r / 255.0f, gr / 255.0f, b / 255.0f, 1.0f ); }
static void glColor4ub_( GLubyte r, GLubyte gr, GLubyte b, GLubyte a ) { glColor4f_( r / 255.0f, gr / 255.0f, b / 255.0f, a / 255.0f ); }
static void glColor4ubv_( const GLubyte *v ) { glColor4ub_( v[0], v[1], v[2], v[3] ); }

static void GX2GL_TexCoord( int unit, float s, float t )
{
	if( unit < 0 || unit >= GX2GL_UNITS )
		return;
	g.texcoord[unit][0] = s;
	g.texcoord[unit][1] = t;
}

static void glTexCoord1f_( GLfloat s ) { GX2GL_TexCoord( 0, s, 0.0f ); }
static void glTexCoord2f_( GLfloat s, GLfloat t ) { GX2GL_TexCoord( 0, s, t ); }
static void glTexCoord3f_( GLfloat s, GLfloat t, GLfloat r ) { (void)r; GX2GL_TexCoord( 0, s, t ); }
static void glTexCoord4f_( GLfloat s, GLfloat t, GLfloat r, GLfloat q ) { (void)r; (void)q; GX2GL_TexCoord( 0, s, t ); }
static void glTexCoord1fv_( const GLfloat *v ) { GX2GL_TexCoord( 0, v[0], 0.0f ); }
static void glTexCoord2fv_( const GLfloat *v ) { GX2GL_TexCoord( 0, v[0], v[1] ); }
static void glTexCoord3fv_( const GLfloat *v ) { GX2GL_TexCoord( 0, v[0], v[1] ); }
static void glTexCoord4fv_( const GLfloat *v ) { GX2GL_TexCoord( 0, v[0], v[1] ); }

static void glMultiTexCoord1f_( GLenum target, GLfloat s ) { GX2GL_TexCoord( target - GL_TEXTURE0, s, 0.0f ); }
static void glMultiTexCoord2f_( GLenum target, GLfloat s, GLfloat t ) { GX2GL_TexCoord( target - GL_TEXTURE0, s, t ); }
static void glMultiTexCoord3f_( GLenum target, GLfloat s, GLfloat t, GLfloat r ) { (void)r; GX2GL_TexCoord( target - GL_TEXTURE0, s, t ); }
static void glMultiTexCoord4f_( GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q ) { (void)r; (void)q; GX2GL_TexCoord( target - GL_TEXTURE0, s, t ); }

static void glNormal3f_( GLfloat x, GLfloat y, GLfloat z ) { (void)x; (void)y; (void)z; }
static void glNormal3fv_( const GLfloat *v ) { (void)v; }

// matrices
static void GX2GL_MatrixChanged( void )
{
	if( g.matrixMode == GL_TEXTURE )
		g.texIdentity[g.activeUnit] = Mat_IsIdentity( Mat_Current( ));
}

static void glMatrixMode_( GLenum mode ) { g.matrixMode = mode; }

static void GX2GL_MatrixWillChange( void )
{
	// the texture matrix is applied on the CPU when vertices are emitted, the others are uniforms
	if( g.matrixMode != GL_TEXTURE )
		STATE_CHANGE();
}

static void glLoadIdentity_( void )
{
	GX2GL_MatrixWillChange();
	Mat_Identity( Mat_Current( ));
	GX2GL_MatrixChanged();
}

static void glLoadMatrixf_( const GLfloat *m )
{
	GX2GL_MatrixWillChange();
	memcpy( Mat_Current( ), m, 16 * sizeof( float ));
	GX2GL_MatrixChanged();
}

static void glLoadMatrixd_( const GLdouble *m )
{
	float f[16];
	for( int i = 0; i < 16; i++ )
		f[i] = (float)m[i];
	glLoadMatrixf_( f );
}

static void glMultMatrixf_( const GLfloat *m )
{
	float *cur = Mat_Current( );

	GX2GL_MatrixWillChange();
	Mat_Mul( cur, cur, m );
	GX2GL_MatrixChanged();
}

static void glMultMatrixd_( const GLdouble *m )
{
	float f[16];
	for( int i = 0; i < 16; i++ )
		f[i] = (float)m[i];
	glMultMatrixf_( f );
}

static void glPushMatrix_( void )
{
	switch( g.matrixMode )
	{
	case GL_PROJECTION:
		if( g.projDepth + 1 < PROJ_STACK )
		{
			memcpy( g.proj[g.projDepth + 1], g.proj[g.projDepth], 16 * sizeof( float ));
			g.projDepth++;
		}
		break;
	case GL_TEXTURE:
	{
		int u = g.activeUnit;
		if( g.texDepth[u] + 1 < TEX_STACK )
		{
			memcpy( g.texm[u][g.texDepth[u] + 1], g.texm[u][g.texDepth[u]], 16 * sizeof( float ));
			g.texDepth[u]++;
		}
		break;
	}
	default:
		if( g.mvDepth + 1 < MV_STACK )
		{
			memcpy( g.mv[g.mvDepth + 1], g.mv[g.mvDepth], 16 * sizeof( float ));
			g.mvDepth++;
		}
		break;
	}
}

static void glPopMatrix_( void )
{
	GX2GL_MatrixWillChange();

	switch( g.matrixMode )
	{
	case GL_PROJECTION:
		if( g.projDepth > 0 ) g.projDepth--;
		break;
	case GL_TEXTURE:
		if( g.texDepth[g.activeUnit] > 0 ) g.texDepth[g.activeUnit]--;
		break;
	default:
		if( g.mvDepth > 0 ) g.mvDepth--;
		break;
	}

	GX2GL_MatrixChanged();
}

static void glOrtho_( GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f )
{
	float m[16];

	Mat_Identity( m );
	m[0] = (float)( 2.0 / ( r - l ));
	m[5] = (float)( 2.0 / ( t - b ));
	m[10] = (float)( -2.0 / ( f - n ));
	m[12] = (float)( -( r + l ) / ( r - l ));
	m[13] = (float)( -( t + b ) / ( t - b ));
	m[14] = (float)( -( f + n ) / ( f - n ));
	glMultMatrixf_( m );
}

static void glFrustum_( GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f )
{
	float m[16];

	memset( m, 0, sizeof( m ));
	m[0] = (float)( 2.0 * n / ( r - l ));
	m[5] = (float)( 2.0 * n / ( t - b ));
	m[8] = (float)(( r + l ) / ( r - l ));
	m[9] = (float)(( t + b ) / ( t - b ));
	m[10] = (float)( -( f + n ) / ( f - n ));
	m[11] = -1.0f;
	m[14] = (float)( -2.0 * f * n / ( f - n ));
	glMultMatrixf_( m );
}

static void glRotatef_( GLfloat angle, GLfloat x, GLfloat y, GLfloat z )
{
	float m[16], len = sqrtf( x * x + y * y + z * z ), c, s, ic;

	if( len == 0.0f )
		return;

	x /= len; y /= len; z /= len;
	c = cosf( angle * (float)M_PI / 180.0f );
	s = sinf( angle * (float)M_PI / 180.0f );
	ic = 1.0f - c;

	Mat_Identity( m );
	m[0] = x * x * ic + c;     m[4] = x * y * ic - z * s; m[8] = x * z * ic + y * s;
	m[1] = y * x * ic + z * s; m[5] = y * y * ic + c;     m[9] = y * z * ic - x * s;
	m[2] = x * z * ic - y * s; m[6] = y * z * ic + x * s; m[10] = z * z * ic + c;
	glMultMatrixf_( m );
}

static void glRotated_( GLdouble a, GLdouble x, GLdouble y, GLdouble z ) { glRotatef_( (float)a, (float)x, (float)y, (float)z ); }

static void glScalef_( GLfloat x, GLfloat y, GLfloat z )
{
	float m[16];
	Mat_Identity( m );
	m[0] = x; m[5] = y; m[10] = z;
	glMultMatrixf_( m );
}

static void glScaled_( GLdouble x, GLdouble y, GLdouble z ) { glScalef_( (float)x, (float)y, (float)z ); }

static void glTranslatef_( GLfloat x, GLfloat y, GLfloat z )
{
	float m[16];
	Mat_Identity( m );
	m[12] = x; m[13] = y; m[14] = z;
	glMultMatrixf_( m );
}

static void glTranslated_( GLdouble x, GLdouble y, GLdouble z ) { glTranslatef_( (float)x, (float)y, (float)z ); }

// texture environment
static void GX2GL_TexEnv( GLenum pname, float param )
{
	texenv_t *env = &g.env[g.activeUnit];
	GLenum e = (GLenum)param;

	STATE_CHANGE();

	switch( pname )
	{
	case GL_TEXTURE_ENV_MODE: env->mode = e; break;
	case GL_COMBINE_RGB: env->combineRGB = e; break;
	case GL_COMBINE_ALPHA: env->combineAlpha = e; break;
	case GL_SOURCE0_ALPHA: env->source0Alpha = e; break;
	case GL_RGB_SCALE: env->rgbScale = param; break;
	}
}

static void glTexEnvf_( GLenum target, GLenum pname, GLfloat param ) { (void)target; GX2GL_TexEnv( pname, param ); }
static void glTexEnvi_( GLenum target, GLenum pname, GLint param ) { (void)target; GX2GL_TexEnv( pname, (float)param ); }
static void glTexEnvfv_( GLenum target, GLenum pname, const GLfloat *params ) { (void)target; if( pname != GL_TEXTURE_ENV_COLOR ) GX2GL_TexEnv( pname, params[0] ); }
static void glGetTexEnviv_( GLenum target, GLenum pname, GLint *params )
{
	(void)target;
	params[0] = pname == GL_TEXTURE_ENV_MODE ? (GLint)g.env[g.activeUnit].mode : 0;
}

// texture objects
static void glGenTextures_( GLsizei n, GLuint *textures )
{
	for( int i = 0; i < n; i++ )
	{
		while( g.nextTexture < GX2GL_MAX_TEXTURES && g.textures[g.nextTexture] )
			g.nextTexture++;
		textures[i] = g.nextTexture < GX2GL_MAX_TEXTURES ? g.nextTexture++ : 0;
	}
}

static void glDeleteTextures_( GLsizei n, const GLuint *textures )
{
	STATE_CHANGE();

	for( int i = 0; i < n; i++ )
	{
		gltex_t *t = GX2GL_GetTexture( textures[i], 0 );

		if( !t )
			continue;

		GX2GL_FreeTextureMemory( t );
		free( t );
		g.textures[textures[i]] = NULL;

		for( int unit = 0; unit < GX2GL_UNITS; unit++ )
		{
			if( g.bound[unit] == textures[i] )
				g.bound[unit] = 0;
		}
	}
}

static void glBindTexture_( GLenum target, GLuint texture )
{
	(void)target;

	if( g.bound[g.activeUnit] != texture )
	{
		STATE_CHANGE();
		g.bound[g.activeUnit] = texture;
		GX2GL_GetTexture( texture, 1 );
	}
}

static GLboolean glIsTexture_( GLuint texture ) { return GX2GL_GetTexture( texture, 0 ) != NULL; }

static void glTexParameterf_( GLenum target, GLenum pname, GLfloat param )
{
	gltex_t *t = GX2GL_GetTexture( g.bound[g.activeUnit], 1 );
	GLenum e = (GLenum)param;

	(void)target;
	if( !t )
		return;

	STATE_CHANGE();

	switch( pname )
	{
	case GL_TEXTURE_MIN_FILTER: t->minFilter = e; break;
	case GL_TEXTURE_MAG_FILTER: t->magFilter = e; break;
	case GL_TEXTURE_WRAP_S: t->wrapS = e; break;
	case GL_TEXTURE_WRAP_T: t->wrapT = e; break;
	default: return;
	}

	t->samplerDirty = 1;
}

static void glTexParameteri_( GLenum target, GLenum pname, GLint param ) { glTexParameterf_( target, pname, (float)param ); }
static void glTexParameterfv_( GLenum target, GLenum pname, const GLfloat *params ) { glTexParameterf_( target, pname, params[0] ); }

static void glTexImage2D_( GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels )
{
	gltex_t *t = GX2GL_GetTexture( g.bound[g.activeUnit], 1 );

	(void)border; (void)type;

	if( target != GL_TEXTURE_2D || !t || width <= 0 || height <= 0 )
		return;

	STATE_CHANGE();

	if( level == 0 )
	{
		t->internalFormat = internalformat;

		if( !t->allocated || t->width != width || t->height != height )
		{
			if( !GX2GL_AllocTexture( t, width, height ))
				return;
		}
	}
	else if( !t->allocated )
	{
		return;
	}

	GX2GL_Upload( t, level, 0, 0, width, height, format, pixels );

	if( level < t->numLevels && (uint32_t)level + 1 > t->tex.viewNumMips )
	{
		t->tex.viewNumMips = level + 1;
		GX2InitTextureRegs( &t->tex );
	}
}

static void glTexSubImage2D_( GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels )
{
	gltex_t *t = GX2GL_GetTexture( g.bound[g.activeUnit], 0 );

	(void)type;

	if( target != GL_TEXTURE_2D || !t || !t->allocated )
		return;

	STATE_CHANGE();
	GX2GL_Upload( t, level, xoffset, yoffset, width, height, format, pixels );
}

static void glPixelStorei_( GLenum pname, GLint param )
{
	if( pname == GL_UNPACK_ALIGNMENT && ( param == 1 || param == 2 || param == 4 || param == 8 ))
		g.unpackAlignment = param;
}

static void glPixelStoref_( GLenum pname, GLfloat param ) { glPixelStorei_( pname, (GLint)param ); }

static void glActiveTexture_( GLenum texture )
{
	int unit = texture - GL_TEXTURE0;
	if( unit >= 0 && unit < GX2GL_UNITS )
		g.activeUnit = unit;
}

static void glClientActiveTexture_( GLenum texture )
{
	int unit = texture - GL_TEXTURE0;
	if( unit >= 0 && unit < GX2GL_UNITS )
		g.clientUnit = unit;
}

// fog
static void glFogf_( GLenum pname, GLfloat param )
{
	STATE_CHANGE();

	switch( pname )
	{
	case GL_FOG_MODE: g.fogMode = (GLenum)param; break;
	case GL_FOG_DENSITY: g.fogDensity = param; break;
	case GL_FOG_START: g.fogStart = param; break;
	case GL_FOG_END: g.fogEnd = param; break;
	}
}

static void glFogi_( GLenum pname, GLint param ) { glFogf_( pname, (float)param ); }

static void glFogfv_( GLenum pname, const GLfloat *params )
{
	if( pname == GL_FOG_COLOR )
	{
		STATE_CHANGE();
		memcpy( g.fogColor, params, 4 * sizeof( float ));
	}
	else
	{
		glFogf_( pname, params[0] );
	}
}

static void glReadPixels_( GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, GLvoid *pixels )
{
	(void)x; (void)y; (void)type;
	// not supported yet: screenshots come out black
	if( pixels && w > 0 && h > 0 )
		memset( pixels, 0, (size_t)w * h * GX2GL_FormatBytes( format ));
}

// accepted but not supported
static void glDrawBuffer_( GLenum mode ) { (void)mode; }
static void glReadBuffer_( GLenum mode ) { (void)mode; }
static void glAccum_( GLenum op, GLfloat value ) { (void)op; (void)value; }
static void glHint_( GLenum target, GLenum mode ) { (void)target; (void)mode; }
static void glShadeModel_( GLenum mode ) { (void)mode; }
static void glPolygonMode_( GLenum face, GLenum mode ) { (void)face; (void)mode; }
static void glPolygonStipple_( const GLubyte *mask ) { (void)mask; }
static void glClipPlane_( GLenum plane, const GLdouble *eq ) { (void)plane; (void)eq; }
static void glGetClipPlane_( GLenum plane, GLdouble *eq ) { (void)plane; memset( eq, 0, 4 * sizeof( *eq )); }
static void glStencilFunc_( GLenum func, GLint ref, GLuint mask ) { (void)func; (void)ref; (void)mask; }
static void glStencilMask_( GLuint mask ) { (void)mask; }
static void glStencilOp_( GLenum fail, GLenum zfail, GLenum zpass ) { (void)fail; (void)zfail; (void)zpass; }
static void glClearStencil_( GLint s ) { (void)s; }
static GLboolean glIsList_( GLuint list ) { (void)list; return GL_FALSE; }
static void glTexGenf_( GLenum coord, GLenum pname, GLfloat param ) { (void)coord; (void)pname; (void)param; }
static void glTexGenfv_( GLenum coord, GLenum pname, const GLfloat *params ) { (void)coord; (void)pname; (void)params; }
static void glTexGeni_( GLenum coord, GLenum pname, GLint param ) { (void)coord; (void)pname; (void)param; }
static void glRasterPos2f_( GLfloat x, GLfloat y ) { (void)x; (void)y; }
static void glDrawPixels_( GLsizei w, GLsizei h, GLenum format, GLenum type, const GLvoid *pixels ) { (void)w; (void)h; (void)format; (void)type; (void)pixels; }
static void glTexImage1D_( GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const GLvoid *pixels ) { (void)target; (void)level; (void)internalformat; (void)width; (void)border; (void)format; (void)type; (void)pixels; }
static void glTexSubImage1D_( GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const GLvoid *pixels ) { (void)target; (void)level; (void)xoffset; (void)width; (void)format; (void)type; (void)pixels; }
static void glCopyTexImage1D_( GLenum target, GLint level, GLenum internalFormat, GLint x, GLint y, GLsizei width, GLint border ) { (void)target; (void)level; (void)internalFormat; (void)x; (void)y; (void)width; (void)border; }
static void glCopyTexImage2D_( GLenum target, GLint level, GLenum internalFormat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border ) { (void)target; (void)level; (void)internalFormat; (void)x; (void)y; (void)width; (void)height; (void)border; }
static void glCopyTexSubImage1D_( GLenum target, GLint level, GLint xoffset, GLint x, GLint y, GLsizei width ) { (void)target; (void)level; (void)xoffset; (void)x; (void)y; (void)width; }
static void glCopyTexSubImage2D_( GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height ) { (void)target; (void)level; (void)xoffset; (void)yoffset; (void)x; (void)y; (void)width; (void)height; }
static void glGetTexImage_( GLenum target, GLint level, GLenum format, GLenum type, GLvoid *pixels ) { (void)target; (void)level; (void)format; (void)type; (void)pixels; }

static void glGetTexLevelParameteriv_( GLenum target, GLint level, GLenum pname, GLint *params )
{
	gltex_t *t = GX2GL_GetTexture( g.bound[g.activeUnit], 0 );

	(void)target;
	params[0] = 0;

	if( !t || !t->allocated )
		return;

	switch( pname )
	{
	case GL_TEXTURE_WIDTH: params[0] = t->width >> level ? t->width >> level : 1; break;
	case GL_TEXTURE_HEIGHT: params[0] = t->height >> level ? t->height >> level : 1; break;
	case GL_TEXTURE_INTERNAL_FORMAT: params[0] = t->internalFormat; break;
	}
}

static void glGetTexLevelParameterfv_( GLenum target, GLint level, GLenum pname, GLfloat *params )
{
	GLint v;
	glGetTexLevelParameteriv_( target, level, pname, &v );
	params[0] = (float)v;
}

/*
=============
GX2GL_GetProcAddress
=============
*/
typedef struct
{
	const char *name;
	void *func;
} glfunc_t;

#define F( name ) { #name, (void *)name##_ }
#define FA( alias, name ) { #alias, (void *)name##_ }

static const glfunc_t gx2gl_funcs[] =
{
	F( glClearColor ), F( glClear ), F( glAlphaFunc ), F( glBlendFunc ), F( glCullFace ),
	F( glDrawBuffer ), F( glReadBuffer ), F( glAccum ), F( glEnable ), F( glDisable ),
	F( glEnableClientState ), F( glDisableClientState ), F( glGetBooleanv ), F( glGetDoublev ),
	F( glGetFloatv ), F( glGetIntegerv ), F( glGetError ), F( glGetString ), F( glFinish ),
	F( glFlush ), F( glClearDepth ), F( glDepthFunc ), F( glDepthMask ), F( glDepthRange ),
	F( glFrontFace ), F( glDrawElements ), F( glDrawArrays ), F( glColorMask ), F( glIndexPointer ),
	F( glVertexPointer ), F( glNormalPointer ), F( glColorPointer ), F( glTexCoordPointer ),
	F( glArrayElement ), F( glColor3f ), F( glColor3fv ), F( glColor4f ), F( glColor4fv ),
	F( glColor3ub ), F( glColor4ub ), F( glColor4ubv ), F( glTexCoord1f ), F( glTexCoord2f ),
	F( glTexCoord3f ), F( glTexCoord4f ), F( glTexCoord1fv ), F( glTexCoord2fv ), F( glTexCoord3fv ),
	F( glTexCoord4fv ), F( glTexGenf ), F( glTexGenfv ), F( glTexGeni ), F( glVertex2f ),
	F( glVertex3f ), F( glVertex3fv ), F( glNormal3f ), F( glNormal3fv ), F( glBegin ), F( glEnd ),
	F( glLineWidth ), F( glPointSize ), F( glMatrixMode ), F( glOrtho ), F( glRasterPos2f ),
	F( glFrustum ), F( glViewport ), F( glPushMatrix ), F( glPopMatrix ), F( glLoadIdentity ),
	F( glLoadMatrixd ), F( glLoadMatrixf ), F( glMultMatrixd ), F( glMultMatrixf ), F( glRotated ),
	F( glRotatef ), F( glScaled ), F( glScalef ), F( glTranslated ), F( glTranslatef ),
	F( glReadPixels ), F( glDrawPixels ), F( glStencilFunc ), F( glStencilMask ), F( glStencilOp ),
	F( glClearStencil ), F( glIsEnabled ), F( glIsList ), F( glIsTexture ), F( glTexEnvf ),
	F( glTexEnvfv ), F( glTexEnvi ), F( glTexParameterf ), F( glTexParameterfv ), F( glTexParameteri ),
	F( glHint ), F( glPixelStoref ), F( glPixelStorei ), F( glGenTextures ), F( glDeleteTextures ),
	F( glBindTexture ), F( glTexImage1D ), F( glTexImage2D ), F( glTexSubImage1D ), F( glTexSubImage2D ),
	F( glCopyTexImage1D ), F( glCopyTexImage2D ), F( glCopyTexSubImage1D ), F( glCopyTexSubImage2D ),
	F( glScissor ), F( glGetTexImage ), F( glGetTexEnviv ), F( glPolygonOffset ), F( glPolygonMode ),
	F( glPolygonStipple ), F( glClipPlane ), F( glGetClipPlane ), F( glShadeModel ),
	F( glGetTexLevelParameteriv ), F( glGetTexLevelParameterfv ), F( glFogfv ), F( glFogf ), F( glFogi ),

	F( glMultiTexCoord1f ), F( glMultiTexCoord2f ), F( glMultiTexCoord3f ), F( glMultiTexCoord4f ),
	F( glActiveTexture ), FA( glActiveTextureARB, glActiveTexture ),
	F( glClientActiveTexture ), FA( glClientActiveTextureARB, glClientActiveTexture ),
	F( glDrawRangeElements ),
};

#undef F
#undef FA

void *GX2GL_GetProcAddress( const char *name )
{
	for( size_t i = 0; i < sizeof( gx2gl_funcs ) / sizeof( gx2gl_funcs[0] ); i++ )
	{
		if( !strcmp( gx2gl_funcs[i].name, name ))
			return gx2gl_funcs[i].func;
	}

	return NULL;
}

/*
=============
context
=============
*/
static void GX2GL_SetDefaultState( void )
{
	g.color[0] = g.color[1] = g.color[2] = g.color[3] = 1.0f;
	g.matrixMode = GL_MODELVIEW;
	Mat_Identity( g.mv[0] );
	Mat_Identity( g.proj[0] );
	for( int unit = 0; unit < GX2GL_UNITS; unit++ )
	{
		Mat_Identity( g.texm[unit][0] );
		g.texIdentity[unit] = 1;
		g.env[unit].mode = GL_MODULATE;
		g.env[unit].combineRGB = GL_MODULATE;
		g.env[unit].combineAlpha = GL_MODULATE;
		g.env[unit].source0Alpha = GL_TEXTURE;
		g.env[unit].rgbScale = 1.0f;
	}
	g.unpackAlignment = 4;
	g.nextTexture = 1;
	g.blendSrc = GL_ONE;
	g.blendDst = GL_ZERO;
	g.alphaFunc = GL_ALWAYS;
	g.depthFunc = GL_LESS;
	g.depthMask = 1;
	g.cullMode = GL_BACK;
	g.frontFace = GL_CCW;
	g.colorMask = 0xF;
	g.viewport[2] = g.scissor[2] = g.width;
	g.viewport[3] = g.scissor[3] = g.height;
	g.depthNear = 0.0f;
	g.depthFar = 1.0f;
	g.clearDepth = 1.0f;
	g.fogMode = GL_EXP;
	g.fogDensity = 1.0f;
	g.fogEnd = 1.0f;
	g.pointSize = g.lineWidth = 1.0f;
}

int GX2GL_CreateContext( int width, int height )
{
	static const uint8_t white[4] = { 255, 255, 255, 255 };

	if( g.initialized )
		GX2GL_DestroyContext();

	memset( &g, 0, sizeof( g ));
	g.width = width;
	g.height = height;
	g.swapInterval = 1;

	g.textures = calloc( GX2GL_MAX_TEXTURES, sizeof( *g.textures ));
	g.imm = malloc( GX2GL_MAX_IMM * sizeof( *g.imm ));
	g.ring = GX2GL_Alloc( GX2_VERTEX_BUFFER_ALIGNMENT, 2 * GX2GL_RING_SIZE );
	g.ctx = GX2GL_Alloc( GX2_CONTEXT_STATE_ALIGNMENT, sizeof( *g.ctx ));
	if( !g.textures || !g.imm || !g.ring || !g.ctx )
		goto fail;
	g.ringVerts = GX2GL_RING_SIZE / sizeof( gxvert_t );

	// render targets
	g.cb.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
	g.cb.surface.width = width;
	g.cb.surface.height = height;
	g.cb.surface.depth = 1;
	g.cb.surface.mipLevels = 1;
	g.cb.surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
	g.cb.surface.aa = GX2_AA_MODE1X;
	g.cb.surface.use = GX2_SURFACE_USE_TEXTURE_COLOR_BUFFER_TV;
	g.cb.surface.tileMode = GX2_TILE_MODE_DEFAULT;
	g.cb.viewNumSlices = 1;
	GX2CalcSurfaceSizeAndAlignment( &g.cb.surface );
	GX2InitColorBufferRegs( &g.cb );
	g.cb.surface.image = GX2GL_Alloc( g.cb.surface.alignment, g.cb.surface.imageSize );

	g.db.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
	g.db.surface.width = width;
	g.db.surface.height = height;
	g.db.surface.depth = 1;
	g.db.surface.mipLevels = 1;
	g.db.surface.format = GX2_SURFACE_FORMAT_FLOAT_R32;
	g.db.surface.aa = GX2_AA_MODE1X;
	g.db.surface.use = GX2_SURFACE_USE_DEPTH_BUFFER;
	g.db.surface.tileMode = GX2_TILE_MODE_DEFAULT;
	g.db.viewNumSlices = 1;
	g.db.depthClear = 1.0f;
	GX2CalcSurfaceSizeAndAlignment( &g.db.surface );
	GX2InitDepthBufferRegs( &g.db );
	g.db.surface.image = GX2GL_Alloc( g.db.surface.alignment, g.db.surface.imageSize );

	if( !g.cb.surface.image || !g.db.surface.image )
		goto fail;
	GX2Invalidate( GX2_INVALIDATE_MODE_CPU, g.cb.surface.image, g.cb.surface.imageSize );
	GX2Invalidate( GX2_INVALIDATE_MODE_CPU, g.db.surface.image, g.db.surface.imageSize );

	// shaders: position, colour, texcoord 0, texcoord 1, see gxvert_t
	if( !WHBGfxLoadGFDShaderGroup( &g.shader, 0, gx2gl_shader_fixed )
		|| !WHBGfxInitShaderAttribute( &g.shader, "a_position", 0, 0, GX2_ATTRIB_FORMAT_FLOAT_32_32_32 )
		|| !WHBGfxInitShaderAttribute( &g.shader, "a_color", 0, 12, GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32 )
		|| !WHBGfxInitShaderAttribute( &g.shader, "a_texcoord0", 0, 28, GX2_ATTRIB_FORMAT_FLOAT_32_32 )
		|| !WHBGfxInitShaderAttribute( &g.shader, "a_texcoord1", 0, 36, GX2_ATTRIB_FORMAT_FLOAT_32_32 )
		|| !WHBGfxInitFetchShader( &g.shader ))
	{
		WHBLogPrint( "gx2gl: failed to load the shaders" );
		goto fail;
	}

	GX2SetupContextStateEx( g.ctx, TRUE );
	GX2SetContextState( g.ctx );
	g.stateLost = 1;

	GX2GL_SetDefaultState();

	// the texture used by disabled units
	g.white.minFilter = g.white.magFilter = GL_NEAREST;
	g.white.wrapS = g.white.wrapT = GL_REPEAT;
	g.white.internalFormat = GL_RGBA;
	if( !GX2GL_AllocTexture( &g.white, 1, 1 ))
		goto fail;
	GX2GL_Upload( &g.white, 0, 0, 0, 1, 1, GL_RGBA, white );
	GX2GL_UpdateSampler( &g.white );

	GX2SetSwapInterval( g.swapInterval );

	g.initialized = 1;
	WHBLogPrintf( "gx2gl: context %dx%d created", width, height );
	return 1;

fail:
	WHBLogPrint( "gx2gl: context creation failed" );
	GX2GL_DestroyContext();
	return 0;
}

void GX2GL_DestroyContext( void )
{
	GX2GL_Flush();

	if( GX2GL_Active( ) )
		GX2DrawDone();

	GX2GL_FreeDeferred( 0 );
	GX2GL_FreeDeferred( 1 );
	free( g.deferred[0].ptrs );
	free( g.deferred[1].ptrs );

	if( g.textures )
	{
		for( int i = 0; i < GX2GL_MAX_TEXTURES; i++ )
		{
			if( g.textures[i] )
			{
				free( g.textures[i]->tex.surface.image );
				free( g.textures[i]->tex.surface.mipmaps );
				free( g.textures[i] );
			}
		}
		free( g.textures );
	}

	free( g.white.tex.surface.image );
	free( g.white.tex.surface.mipmaps );

	if( g.shader.vertexShader )
		WHBGfxFreeShaderGroup( &g.shader );

	free( g.cb.surface.image );
	free( g.db.surface.image );
	free( g.ctx );
	free( g.ring );
	free( g.imm );

	memset( &g, 0, sizeof( g ));
}

void GX2GL_SetSwapInterval( int interval )
{
	g.swapInterval = interval;
	GX2SetSwapInterval( interval );
}

void GX2GL_SwapBuffers( void )
{
	if( !g.initialized )
		return;

	GX2GL_Flush();

	if( GX2GL_Active( ) )
	{
		GX2CopyColorBufferToScanBuffer( &g.cb, GX2_SCAN_TARGET_TV );
		GX2CopyColorBufferToScanBuffer( &g.cb, GX2_SCAN_TARGET_DRC );
		GX2SwapScanBuffers();
		GX2Flush();
		g.fence[g.slot] = GX2GetLastSubmittedTimeStamp();

		if( !g.shown )
		{
			GX2SetTVEnable( TRUE );
			GX2SetDRCEnable( TRUE );
			g.shown = 1;
		}

		if( g.swapInterval )
			GX2WaitForFlip();
	}

	g.stateLost = 1;

	// next frame: reuse the other half once the GPU is done with it
	g.slot ^= 1;
	if( GX2GL_Active( ) && g.fence[g.slot] )
		GX2WaitTimeStamp( g.fence[g.slot] );
	GX2GL_FreeDeferred( g.slot );
	g.ringUsed = 0;

}
