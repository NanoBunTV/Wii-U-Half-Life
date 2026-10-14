/*
gx2gl_gl.h - OpenGL types and the enum values gx2gl understands (standard OpenGL values)
*/
#pragma once

#include <stdint.h>

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef void GLvoid;
typedef signed char GLbyte;
typedef short GLshort;
typedef int GLint;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned int GLuint;
typedef int GLsizei;
typedef float GLfloat;
typedef float GLclampf;
typedef double GLdouble;
typedef double GLclampd;

#define GL_FALSE 0
#define GL_TRUE  1
#define GL_NONE  0

// primitives
#define GL_POINTS         0x0000
#define GL_LINES          0x0001
#define GL_LINE_LOOP      0x0002
#define GL_LINE_STRIP     0x0003
#define GL_TRIANGLES      0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TRIANGLE_FAN   0x0006
#define GL_QUADS          0x0007
#define GL_QUAD_STRIP     0x0008
#define GL_POLYGON        0x0009

// compare functions
#define GL_NEVER    0x0200
#define GL_LESS     0x0201
#define GL_EQUAL    0x0202
#define GL_LEQUAL   0x0203
#define GL_GREATER  0x0204
#define GL_NOTEQUAL 0x0205
#define GL_GEQUAL   0x0206
#define GL_ALWAYS   0x0207

// blend factors
#define GL_ZERO                0
#define GL_ONE                 1
#define GL_SRC_COLOR           0x0300
#define GL_ONE_MINUS_SRC_COLOR 0x0301
#define GL_SRC_ALPHA           0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_DST_ALPHA           0x0304
#define GL_ONE_MINUS_DST_ALPHA 0x0305
#define GL_DST_COLOR           0x0306
#define GL_ONE_MINUS_DST_COLOR 0x0307
#define GL_SRC_ALPHA_SATURATE  0x0308

// faces
#define GL_FRONT          0x0404
#define GL_BACK           0x0405
#define GL_FRONT_AND_BACK 0x0408
#define GL_CW             0x0900
#define GL_CCW            0x0901

// capabilities
#define GL_POINT_SMOOTH        0x0B10
#define GL_LINE_SMOOTH         0x0B20
#define GL_POLYGON_SMOOTH      0x0B41
#define GL_CULL_FACE           0x0B44
#define GL_LIGHTING            0x0B50
#define GL_FOG                 0x0B60
#define GL_DEPTH_TEST          0x0B71
#define GL_STENCIL_TEST        0x0B90
#define GL_NORMALIZE           0x0BA1
#define GL_ALPHA_TEST          0x0BC0
#define GL_DITHER              0x0BD0
#define GL_BLEND               0x0BE2
#define GL_SCISSOR_TEST        0x0C11
#define GL_TEXTURE_GEN_S       0x0C60
#define GL_TEXTURE_GEN_T       0x0C61
#define GL_TEXTURE_GEN_R       0x0C62
#define GL_TEXTURE_GEN_Q       0x0C63
#define GL_TEXTURE_1D          0x0DE0
#define GL_TEXTURE_2D          0x0DE1
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_TEXTURE_3D          0x806F
#define GL_MULTISAMPLE         0x809D
#define GL_TEXTURE_RECTANGLE   0x84F5
#define GL_TEXTURE_CUBE_MAP    0x8513

// client arrays
#define GL_VERTEX_ARRAY        0x8074
#define GL_NORMAL_ARRAY        0x8075
#define GL_COLOR_ARRAY         0x8076
#define GL_TEXTURE_COORD_ARRAY 0x8078

// data types
#define GL_BYTE           0x1400
#define GL_UNSIGNED_BYTE  0x1401
#define GL_SHORT          0x1402
#define GL_UNSIGNED_SHORT 0x1403
#define GL_INT            0x1404
#define GL_UNSIGNED_INT   0x1405
#define GL_FLOAT          0x1406
#define GL_DOUBLE         0x140A

// matrices
#define GL_MODELVIEW         0x1700
#define GL_PROJECTION        0x1701
#define GL_TEXTURE           0x1702
#define GL_MODELVIEW_MATRIX  0x0BA6
#define GL_PROJECTION_MATRIX 0x0BA7
#define GL_TEXTURE_MATRIX    0x0BA8

// clear
#define GL_DEPTH_BUFFER_BIT   0x00000100
#define GL_STENCIL_BUFFER_BIT 0x00000400
#define GL_COLOR_BUFFER_BIT   0x00004000

// pixel formats
#define GL_ALPHA               0x1906
#define GL_RGB                 0x1907
#define GL_RGBA                0x1908
#define GL_LUMINANCE           0x1909
#define GL_LUMINANCE_ALPHA     0x190A
#define GL_ALPHA8              0x803C
#define GL_LUMINANCE8          0x8040
#define GL_LUMINANCE8_ALPHA8   0x8045
#define GL_INTENSITY           0x8049
#define GL_INTENSITY8          0x804B
#define GL_RGB5                0x8050
#define GL_RGB8                0x8051
#define GL_RGBA4               0x8056
#define GL_RGBA8               0x8058
#define GL_BGR                 0x80E0
#define GL_BGRA                0x80E1

// textures
#define GL_TEXTURE_BORDER_COLOR 0x1004
#define GL_TEXTURE_MAG_FILTER   0x2800
#define GL_TEXTURE_MIN_FILTER   0x2801
#define GL_TEXTURE_WRAP_S       0x2802
#define GL_TEXTURE_WRAP_T       0x2803
#define GL_TEXTURE_WRAP_R       0x8072
#define GL_NEAREST                0x2600
#define GL_LINEAR                 0x2601
#define GL_NEAREST_MIPMAP_NEAREST 0x2700
#define GL_LINEAR_MIPMAP_NEAREST  0x2701
#define GL_NEAREST_MIPMAP_LINEAR  0x2702
#define GL_LINEAR_MIPMAP_LINEAR   0x2703
#define GL_CLAMP                  0x2900
#define GL_REPEAT                 0x2901
#define GL_CLAMP_TO_BORDER        0x812D
#define GL_CLAMP_TO_EDGE          0x812F
#define GL_MIRRORED_REPEAT        0x8370
#define GL_TEXTURE_MIN_LOD        0x813A
#define GL_TEXTURE_MAX_LOD        0x813B
#define GL_TEXTURE_BASE_LEVEL     0x813C
#define GL_TEXTURE_MAX_LEVEL      0x813D
#define GL_TEXTURE_LOD_BIAS       0x8501
#define GL_TEXTURE_WIDTH          0x1000
#define GL_TEXTURE_HEIGHT         0x1001
#define GL_TEXTURE_INTERNAL_FORMAT 0x1003

// texture environment
#define GL_TEXTURE_ENV        0x2300
#define GL_TEXTURE_ENV_MODE   0x2200
#define GL_TEXTURE_ENV_COLOR  0x2201
#define GL_ADD                0x0104
#define GL_REPLACE            0x1E01
#define GL_MODULATE           0x2100
#define GL_DECAL              0x2101
#define GL_ALPHA_SCALE        0x0D1C
#define GL_COMBINE            0x8570
#define GL_COMBINE_RGB        0x8571
#define GL_COMBINE_ALPHA      0x8572
#define GL_RGB_SCALE          0x8573
#define GL_ADD_SIGNED         0x8574
#define GL_INTERPOLATE        0x8575
#define GL_CONSTANT           0x8576
#define GL_PRIMARY_COLOR      0x8577
#define GL_PREVIOUS           0x8578
#define GL_SOURCE0_RGB        0x8580
#define GL_SOURCE1_RGB        0x8581
#define GL_SOURCE0_ALPHA      0x8588
#define GL_SOURCE1_ALPHA      0x8589

// fog
#define GL_EXP         0x0800
#define GL_EXP2        0x0801
#define GL_FOG_DENSITY 0x0B62
#define GL_FOG_START   0x0B63
#define GL_FOG_END     0x0B64
#define GL_FOG_MODE    0x0B65
#define GL_FOG_COLOR   0x0B66

// multitexture
#define GL_TEXTURE0              0x84C0
#define GL_ACTIVE_TEXTURE        0x84E0
#define GL_CLIENT_ACTIVE_TEXTURE 0x84E1
#define GL_MAX_TEXTURE_UNITS     0x84E2

// strings
#define GL_VENDOR     0x1F00
#define GL_RENDERER   0x1F01
#define GL_VERSION    0x1F02
#define GL_EXTENSIONS 0x1F03

// queries
#define GL_VIEWPORT         0x0BA2
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_PACK_ALIGNMENT   0x0D05
#define GL_MAX_TEXTURE_SIZE 0x0D33
#define GL_RED_BITS         0x0D52
#define GL_GREEN_BITS       0x0D53
#define GL_BLUE_BITS        0x0D54
#define GL_ALPHA_BITS       0x0D55
#define GL_DEPTH_BITS       0x0D56
#define GL_STENCIL_BITS     0x0D57
#define GL_MAJOR_VERSION    0x821B
#define GL_MINOR_VERSION    0x821C

// polygon mode
#define GL_POINT 0x1B00
#define GL_LINE  0x1B01
#define GL_FILL  0x1B02

#define GL_NO_ERROR 0
