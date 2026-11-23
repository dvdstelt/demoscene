// Minimal OpenGL 3.3 core loader (all entry points fetched through SDL_GL_GetProcAddress).
#pragma once
#include <cstddef>
#include <cstdint>

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
typedef double GLdouble;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;

#if defined(_WIN32) && !defined(_WIN64)
#define GLAPIENTRY __stdcall
#else
#define GLAPIENTRY
#endif

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_STENCIL_BUFFER_BIT 0x00000400
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_POINTS 0x0000
#define GL_LINES 0x0001
#define GL_LINE_STRIP 0x0003
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_NEVER 0x0200
#define GL_LESS 0x0201
#define GL_EQUAL 0x0202
#define GL_LEQUAL 0x0203
#define GL_GREATER 0x0204
#define GL_NOTEQUAL 0x0205
#define GL_GEQUAL 0x0206
#define GL_ALWAYS 0x0207
#define GL_ZERO 0
#define GL_ONE 1
#define GL_SRC_COLOR 0x0300
#define GL_ONE_MINUS_SRC_COLOR 0x0301
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_DST_COLOR 0x0306
#define GL_FRONT 0x0404
#define GL_BACK 0x0405
#define GL_FRONT_AND_BACK 0x0408
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_BLEND 0x0BE2
#define GL_STENCIL_TEST 0x0B90
#define GL_SCISSOR_TEST 0x0C11
#define GL_KEEP 0x1E00
#define GL_REPLACE 0x1E01
#define GL_INCR 0x1E02
#define GL_DECR 0x1E03
#define GL_INVERT 0x150A
#define GL_TEXTURE_BORDER_COLOR 0x1004
#define GL_LINE_LOOP 0x0002
#define GL_TRIANGLE_FAN 0x0006
#define GL_POLYGON_OFFSET_LINE 0x2A02
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_SAMPLE_ALPHA_TO_COVERAGE 0x809E
#define GL_CLAMP_TO_BORDER 0x812D
#define GL_MIN 0x8007
#define GL_RED_INTEGER 0x8D94
#define GL_R8UI 0x8232
#define GL_R32F 0x822E
#define GL_RG16F 0x822F
#define GL_RG32F 0x8230
#define GL_RG8 0x822B
#define GL_RGBA32F 0x8814
#define GL_R16 0x822A
#define GL_DEPTH24_STENCIL8 0x88F0
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#define GL_MAX_SAMPLES 0x8D57
#define GL_TEXTURE_2D_ARRAY 0x8C1A
#define GL_STREAM_DRAW 0x88E0
#define GL_ONE_MINUS_DST_COLOR 0x0307
#define GL_DST_ALPHA 0x0304
#define GL_ONE_MINUS_DST_ALPHA 0x0305
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_TEXTURE_2D 0x0DE1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_SHORT 0x1403
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_HALF_FLOAT 0x140B
#define GL_RED 0x1903
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_LINE 0x1B01
#define GL_FILL 0x1B02
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_REPEAT 0x2901
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_MIRRORED_REPEAT 0x8370
#define GL_TEXTURE_MAX_ANISOTROPY 0x84FE
#define GL_FUNC_ADD 0x8006
#define GL_MAX 0x8008
#define GL_FUNC_SUBTRACT 0x800A
#define GL_FUNC_REVERSE_SUBTRACT 0x800B
#define GL_R8 0x8229
#define GL_RG 0x8227
#define GL_R16F 0x822D
#define GL_RGBA8 0x8058
#define GL_RGBA16F 0x881A
#define GL_RGBA16 0x805B
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE2 0x84C2
#define GL_TEXTURE3 0x84C3
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_COLOR_ATTACHMENT1 0x8CE1
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_MULTISAMPLE 0x809D
#define GL_FRAMEBUFFER_SRGB 0x8DB9
#define GL_PROGRAM_POINT_SIZE 0x8642
#define GL_TEXTURE_2D_MULTISAMPLE 0x9100

#define GL_FUNCS(X)                                                                                            \
    X(void, Finish, (void))                                                                                    \
    X(void, Clear, (GLbitfield))                                                                               \
    X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat))                                                  \
    X(void, Viewport, (GLint, GLint, GLsizei, GLsizei))                                                        \
    X(void, Scissor, (GLint, GLint, GLsizei, GLsizei))                                                         \
    X(void, Enable, (GLenum))                                                                                  \
    X(void, Disable, (GLenum))                                                                                 \
    X(void, BlendFunc, (GLenum, GLenum))                                                                       \
    X(void, BlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum))                                               \
    X(void, BlendEquation, (GLenum))                                                                           \
    X(void, DepthFunc, (GLenum))                                                                               \
    X(void, DepthMask, (GLboolean))                                                                            \
    X(void, ColorMask, (GLboolean, GLboolean, GLboolean, GLboolean))                                           \
    X(void, CullFace, (GLenum))                                                                                \
    X(void, LineWidth, (GLfloat))                                                                              \
    X(void, PolygonMode, (GLenum, GLenum))                                                                     \
    X(void, PixelStorei, (GLenum, GLint))                                                                      \
    X(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*))                               \
    X(GLenum, GetError, (void))                                                                                \
    X(void, GenTextures, (GLsizei, GLuint*))                                                                   \
    X(void, DeleteTextures, (GLsizei, const GLuint*))                                                          \
    X(void, BindTexture, (GLenum, GLuint))                                                                     \
    X(void, ActiveTexture, (GLenum))                                                                           \
    X(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*))          \
    X(void, TexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*))       \
    X(void, TexParameteri, (GLenum, GLenum, GLint))                                                            \
    X(void, TexParameterf, (GLenum, GLenum, GLfloat))                                                          \
    X(void, GenerateMipmap, (GLenum))                                                                          \
    X(GLuint, CreateShader, (GLenum))                                                                          \
    X(void, ShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*))                               \
    X(void, CompileShader, (GLuint))                                                                           \
    X(void, GetShaderiv, (GLuint, GLenum, GLint*))                                                             \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))                                            \
    X(void, DeleteShader, (GLuint))                                                                            \
    X(GLuint, CreateProgram, (void))                                                                           \
    X(void, AttachShader, (GLuint, GLuint))                                                                    \
    X(void, LinkProgram, (GLuint))                                                                             \
    X(void, GetProgramiv, (GLuint, GLenum, GLint*))                                                            \
    X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))                                           \
    X(void, UseProgram, (GLuint))                                                                              \
    X(GLint, GetUniformLocation, (GLuint, const GLchar*))                                                      \
    X(void, Uniform1i, (GLint, GLint))                                                                         \
    X(void, Uniform2i, (GLint, GLint, GLint))                                                                  \
    X(void, Uniform1f, (GLint, GLfloat))                                                                       \
    X(void, Uniform2f, (GLint, GLfloat, GLfloat))                                                              \
    X(void, Uniform3f, (GLint, GLfloat, GLfloat, GLfloat))                                                     \
    X(void, Uniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat))                                            \
    X(void, Uniform1fv, (GLint, GLsizei, const GLfloat*))                                                      \
    X(void, Uniform3fv, (GLint, GLsizei, const GLfloat*))                                                      \
    X(void, Uniform4fv, (GLint, GLsizei, const GLfloat*))                                                      \
    X(void, UniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat*))                                     \
    X(void, UniformMatrix3fv, (GLint, GLsizei, GLboolean, const GLfloat*))                                     \
    X(void, GenVertexArrays, (GLsizei, GLuint*))                                                               \
    X(void, BindVertexArray, (GLuint))                                                                         \
    X(void, GenBuffers, (GLsizei, GLuint*))                                                                    \
    X(void, BindBuffer, (GLenum, GLuint))                                                                      \
    X(void, BufferData, (GLenum, GLsizeiptr, const void*, GLenum))                                             \
    X(void, BufferSubData, (GLenum, GLintptr, GLsizeiptr, const void*))                                        \
    X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*))                     \
    X(void, VertexAttribDivisor, (GLuint, GLuint))                                                             \
    X(void, EnableVertexAttribArray, (GLuint))                                                                 \
    X(void, DrawArrays, (GLenum, GLint, GLsizei))                                                              \
    X(void, DrawArraysInstanced, (GLenum, GLint, GLsizei, GLsizei))                                            \
    X(void, DrawElements, (GLenum, GLsizei, GLenum, const void*))                                              \
    X(void, GenFramebuffers, (GLsizei, GLuint*))                                                               \
    X(void, DeleteFramebuffers, (GLsizei, const GLuint*))                                                      \
    X(void, BindFramebuffer, (GLenum, GLuint))                                                                 \
    X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))                                     \
    X(GLenum, CheckFramebufferStatus, (GLenum))                                                                \
    X(void, GenRenderbuffers, (GLsizei, GLuint*))                                                              \
    X(void, DeleteRenderbuffers, (GLsizei, const GLuint*))                                                     \
    X(void, BindRenderbuffer, (GLenum, GLuint))                                                                \
    X(void, RenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei))                                           \
    X(void, RenderbufferStorageMultisample, (GLenum, GLsizei, GLenum, GLsizei, GLsizei))                       \
    X(void, FramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint))                                         \
    X(void, ClearDepth, (GLdouble))                                                                            \
    X(void, ClearStencil, (GLint))                                                                             \
    X(void, StencilFunc, (GLenum, GLint, GLuint))                                                              \
    X(void, StencilOp, (GLenum, GLenum, GLenum))                                                               \
    X(void, StencilMask, (GLuint))                                                                             \
    X(void, PolygonOffset, (GLfloat, GLfloat))                                                                 \
    X(void, GetIntegerv, (GLenum, GLint*))                                                                     \
    X(void, TexParameterfv, (GLenum, GLenum, const GLfloat*))                                                  \
    X(void, Uniform2fv, (GLint, GLsizei, const GLfloat*))                                                      \
    X(void, Uniform1iv, (GLint, GLsizei, const GLint*))                                                        \
    X(void, TexImage3D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)) \
    X(void, TexSubImage3D, (GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const void*)) \
    X(void, DrawElementsInstanced, (GLenum, GLsizei, GLenum, const void*, GLsizei))                            \
    X(void, DeleteBuffers, (GLsizei, const GLuint*))                                                           \
    X(void, DeleteVertexArrays, (GLsizei, const GLuint*))                                                      \
    X(void, DeleteProgram, (GLuint))                                                                           \
    X(void, VertexAttribIPointer, (GLuint, GLint, GLenum, GLsizei, const void*))                               \
    X(void, BlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum)) \
    X(void, DrawBuffers, (GLsizei, const GLenum*))                                                             \
    X(void, ReadBuffer, (GLenum))

namespace gl {
#define GL_DECLARE(ret, name, args) extern ret(GLAPIENTRY* name) args;
GL_FUNCS(GL_DECLARE)
#undef GL_DECLARE
bool load();  // call after the context is current
}  // namespace gl
