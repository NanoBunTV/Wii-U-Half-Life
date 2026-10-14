; gx2gl fixed-function vertex shader (assembled with latte-assembler, see scripts/build-shaders.ps1)
;
; Inputs (one interleaved buffer, see gx2gl.c GX2GL_Vertex):
;   R1 = position (xyz, w = 1 from the fetch shader), R2 = colour, R3 = texcoord unit 0, R4 = texcoord unit 1
; Uniforms (registers, 4 floats each):
;   C0-C3 = modelview-projection matrix, columns (OpenGL memory order)
;   C4    = row 2 of the modelview matrix (eye-space z = dot( C4, position ))
;   C5.x  = fog coefficient k: fog factor = clamp( 2^( k * z_eye )), k = density * log2(e) for GL_EXP, 0 = no fog
; Outputs: PARAM0 colour, PARAM1 texcoord 0, PARAM2 texcoord 1, PARAM3.x fog factor
;
; Hardware rules this follows by hand (the assembler doesn't check them): one constant register per
; instruction group, and no two different GPRs read on the same channel in the same source slot.

; $MODE = "UniformRegister"

; $SPI_VS_OUT_CONFIG.VS_EXPORT_COUNT = 3
; $NUM_SPI_VS_OUT_ID = 1
; $SPI_VS_OUT_ID[0].SEMANTIC_0 = 0
; $SPI_VS_OUT_ID[0].SEMANTIC_1 = 1
; $SPI_VS_OUT_ID[0].SEMANTIC_2 = 2
; $SPI_VS_OUT_ID[0].SEMANTIC_3 = 3

; $UNIFORM_VARS[0].name = "u_mvp"
; $UNIFORM_VARS[0].type = "mat4"
; $UNIFORM_VARS[0].count = 1
; $UNIFORM_VARS[0].block = -1
; $UNIFORM_VARS[0].offset = 0
; $UNIFORM_VARS[1].name = "u_eyez"
; $UNIFORM_VARS[1].type = "vec4"
; $UNIFORM_VARS[1].count = 1
; $UNIFORM_VARS[1].block = -1
; $UNIFORM_VARS[1].offset = 16
; $UNIFORM_VARS[2].name = "u_fog"
; $UNIFORM_VARS[2].type = "vec4"
; $UNIFORM_VARS[2].count = 1
; $UNIFORM_VARS[2].block = -1
; $UNIFORM_VARS[2].offset = 20

; $ATTRIB_VARS[0].name = "a_position"
; $ATTRIB_VARS[0].type = "vec3"
; $ATTRIB_VARS[0].location = 0
; $ATTRIB_VARS[1].name = "a_color"
; $ATTRIB_VARS[1].type = "vec4"
; $ATTRIB_VARS[1].location = 1
; $ATTRIB_VARS[2].name = "a_texcoord0"
; $ATTRIB_VARS[2].type = "vec2"
; $ATTRIB_VARS[2].location = 2
; $ATTRIB_VARS[3].name = "a_texcoord1"
; $ATTRIB_VARS[3].type = "vec2"
; $ATTRIB_VARS[3].location = 3

00 CALL_FS NO_BARRIER
01 ALU: ADDR(32) CNT(25)
    0  x: MUL    ____,   1.0f, C3.x
       y: MUL    ____,   1.0f, C3.y
       z: MUL    ____,   1.0f, C3.z
       w: MUL    ____,   1.0f, C3.w
    1  x: MULADD R127.x, R1.z, C2.x, PV0.x
       y: MULADD R127.y, R1.z, C2.y, PV0.y
       z: MULADD R127.z, R1.z, C2.z, PV0.z
       w: MULADD R127.w, R1.z, C2.w, PV0.w
    2  x: MULADD R127.x, R1.y, C1.x, PV1.x
       y: MULADD R127.y, R1.y, C1.y, PV1.y
       z: MULADD R127.z, R1.y, C1.z, PV1.z
       w: MULADD R127.w, R1.y, C1.w, PV1.w
    3  x: MULADD R5.x,   R1.x, C0.x, PV2.x
       y: MULADD R5.y,   R1.x, C0.y, PV2.y
       z: MULADD R5.z,   R1.x, C0.z, PV2.z
       w: MULADD R5.w,   R1.x, C0.w, PV2.w
    4  x: DOT4   R6.x,   R1.x, C4.x
       y: DOT4   ____,   R1.y, C4.y
       z: DOT4   ____,   R1.z, C4.z
       w: DOT4   ____,   1.0f, C4.w
    5  x: MUL    R6.x,   PV4.x, C5.x
    6  t: EXP_IEEE R6.x, PV5.x CLAMP
02 EXP_DONE: POS0, R5
03 EXP: PARAM0, R2 NO_BARRIER
04 EXP: PARAM1, R3.xy01 NO_BARRIER
05 EXP: PARAM2, R4.xy01 NO_BARRIER
06 EXP_DONE: PARAM3, R6.x001 NO_BARRIER
END_OF_PROGRAM
