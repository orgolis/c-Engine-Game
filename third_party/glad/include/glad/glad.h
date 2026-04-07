#pragma once

// OpenGL 4.5 Core Profile - GLAD Loader

#include <cstddef>
#include <cstdint>

#ifdef _WIN32
    #define GLAD_API_CALL __stdcall
#else
    #define GLAD_API_CALL
#endif

typedef void* (*GLADloadproc)(const char *name);

// --- OpenGL Type Definitions (from GL.h) ---
typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef signed char GLbyte;
typedef short GLshort;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef float GLclampf;
typedef double GLdouble;
typedef double GLclampd;
typedef void GLvoid;
typedef int GLintptr;
typedef int GLsizeiptr;
typedef char GLchar;

// --- OpenGL Constants (Core Profile) ---
#define GL_FALSE                        0
#define GL_TRUE                         1

#define GL_VENDOR                       0x1F00
#define GL_RENDERER                     0x1F01
#define GL_VERSION                      0x1F02
#define GL_EXTENSIONS                   0x1F03

#define GL_COLOR_BUFFER_BIT             0x00004000
#define GL_DEPTH_BUFFER_BIT             0x00000100
#define GL_STENCIL_BUFFER_BIT           0x00000400

#define GL_VERTEX_SHADER                0x8B31
#define GL_FRAGMENT_SHADER              0x8B30
#define GL_GEOMETRY_SHADER              0x8DD9
#define GL_TESS_CONTROL_SHADER          0x8E88
#define GL_TESS_EVALUATION_SHADER       0x8E87
#define GL_COMPUTE_SHADER               0x91B9

#define GL_COMPILE_STATUS               0x8B81
#define GL_LINK_STATUS                  0x8B82
#define GL_INFO_LOG_LENGTH              0x8B84

#define GL_ARRAY_BUFFER                 0x8892
#define GL_ELEMENT_ARRAY_BUFFER         0x8893
#define GL_COPY_READ_BUFFER             0x8F36
#define GL_COPY_WRITE_BUFFER            0x8F37

#define GL_STATIC_DRAW                  0x88E4
#define GL_DYNAMIC_DRAW                 0x88E8

#define GL_TEXTURE_2D                   0x0DE1
#define GL_TEXTURE_CUBE_MAP             0x8513
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X  0x8515
#define GL_TEXTURE_MAG_FILTER           0x2800
#define GL_TEXTURE_MIN_FILTER           0x2801
#define GL_LINEAR                       0x2601
#define GL_NEAREST                      0x2600

#define GL_TEXTURE0                     0x84C0
#define GL_TEXTURE1                     0x84C1

#define GL_UNSIGNED_BYTE                0x1401

#define GL_DEPTH_TEST                   0x0B71
#define GL_BLEND                        0x0BE2
#define GL_CULL_FACE                    0x0B44
#define GL_FRONT                        0x0404
#define GL_BACK                         0x0405

#define GL_TRIANGLES                    0x0004
#define GL_TRIANGLE_STRIP               0x0005
#define GL_LINES                        0x0001
#define GL_LINE_STRIP                   0x0003

#define GL_READ_ONLY                    0x88B8
#define GL_READ_WRITE                   0x88BA

// --- OpenGL Function Pointer Types ---

typedef GLuint (GLAD_API_CALL *PFNGLCREATESHADERPROC)(GLenum type);
typedef void (GLAD_API_CALL *PFNGLSHADERSOURCEPROC)(GLuint shader, GLsizei count, const GLchar *const*string, const GLint *length);
typedef void (GLAD_API_CALL *PFNGLCOMPILESHADERPROC)(GLuint shader);
typedef void (GLAD_API_CALL *PFNGLGETSHADERIVPROC)(GLuint shader, GLenum pname, GLint *params);
typedef void (GLAD_API_CALL *PFNGLGETSHADERINFOLOGPROC)(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
typedef void (GLAD_API_CALL *PFNGLDELETESHADERPROC)(GLuint shader);

typedef GLuint (GLAD_API_CALL *PFNGLCREATEPROGRAMPROC)(void);
typedef void (GLAD_API_CALL *PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);
typedef void (GLAD_API_CALL *PFNGLLINKPROGRAMPROC)(GLuint program);
typedef void (GLAD_API_CALL *PFNGLGETPROGRAMIVPROC)(GLuint program, GLenum pname, GLint *params);
typedef void (GLAD_API_CALL *PFNGLGETPROGRAMINFOLOGPROC)(GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
typedef void (GLAD_API_CALL *PFNGLUSEPROGRAMPROC)(GLuint program);
typedef void (GLAD_API_CALL *PFNGLDELETEPROGRAMPROC)(GLuint program);
typedef GLint (GLAD_API_CALL *PFNGLGETUNIFORMLOCATIONPROC)(GLuint program, const GLchar *name);
typedef void (GLAD_API_CALL *PFNGLUNIFORMMATRIX4FVPROC)(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);

typedef void (GLAD_API_CALL *PFNGLGENBUFFERSPROC)(GLsizei n, GLuint *buffers);
typedef void (GLAD_API_CALL *PFNGLBINDBUFFERPROC)(GLenum target, GLuint buffer);
typedef void (GLAD_API_CALL *PFNGLBUFFERDATAPROC)(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
typedef void (GLAD_API_CALL *PFNGLBUFFERSUBDATAPROC)(GLenum target, GLintptr offset, GLsizeiptr size, const void *data);
typedef void* (GLAD_API_CALL *PFNGLMAPBUFFERPROC)(GLenum target, GLenum access);
typedef GLboolean (GLAD_API_CALL *PFNGLUNMAPBUFFERPROC)(GLenum target);
typedef void (GLAD_API_CALL *PFNGLDELETEBUFFERSPROC)(GLsizei n, const GLuint *buffers);

typedef void (GLAD_API_CALL *PFNGLGENTEXTURESPROC)(GLsizei n, GLuint *textures);
typedef void (GLAD_API_CALL *PFNGLBINDTEXTUREPROC)(GLenum target, GLuint texture);
typedef void (GLAD_API_CALL *PFNGLTEXIMAGE2DPROC)(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels);
typedef void (GLAD_API_CALL *PFNGLTEXPARAMETERIPROC)(GLenum target, GLenum pname, GLint param);
typedef void (GLAD_API_CALL *PFNGLACTIVETEXTUREPROC)(GLenum texture);
typedef void (GLAD_API_CALL *PFNGLDELETETEXTURESPROC)(GLsizei n, const GLuint *textures);

typedef void (GLAD_API_CALL *PFNGLGENVERTEXARRAYSPROC)(GLsizei n, GLuint *arrays);
typedef void (GLAD_API_CALL *PFNGLBINDVERTEXARRAYPROC)(GLuint array);
typedef void (GLAD_API_CALL *PFNGLVERTEXATTRIBPOINTERPROC)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);
typedef void (GLAD_API_CALL *PFNGLVERTEXATTRIBIPOINTERPROC)(GLuint index, GLint size, GLenum type, GLsizei stride, const void *pointer);
typedef void (GLAD_API_CALL *PFNGLVERTEXATTRIBLPOINTERPROC)(GLuint index, GLint size, GLenum type, GLsizei stride, const void *pointer);
typedef void (GLAD_API_CALL *PFNGLBINDVERTEXBUFFERPROC)(GLuint bindingindex, GLuint buffer, GLintptr offset, GLsizei stride);
typedef void (GLAD_API_CALL *PFNGLVERTEXATTRIBBINDINGPROC)(GLuint attribindex, GLuint bindingindex);
typedef void (GLAD_API_CALL *PFNGLVERTEXBINDINGDIVISORPROC)(GLuint bindingindex, GLuint divisor);
typedef void (GLAD_API_CALL *PFNGLVERTEXATTRIBDIVISORPROC)(GLuint index, GLuint divisor);
typedef void (GLAD_API_CALL *PFNGLDELETEVERTEXARRAYSPROC)(GLsizei n, const GLuint *arrays);
typedef void (GLAD_API_CALL *PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint index);
typedef void (GLAD_API_CALL *PFNGLDISABLEVERTEXATTRIBARRAYPROC)(GLuint index);

typedef void (GLAD_API_CALL *PFNGLDRAWELEMENTSPROC)(GLenum mode, GLsizei count, GLenum type, const void *indices);
typedef void (GLAD_API_CALL *PFNGLDRAWARRAYSPROC)(GLenum mode, GLint first, GLsizei count);
typedef void (GLAD_API_CALL *PFNGLDRAWELEMENTSBINDINGPOINTSINDIRECTPROC)(GLenum mode, GLenum type, const void *indirect);
typedef void (GLAD_API_CALL *PFNGLDRAWARRAYSINSTANCEDPROC)(GLenum mode, GLint first, GLsizei count, GLsizei instancecount);
typedef void (GLAD_API_CALL *PFNGLDRAWELEMENTSINSTANCEDPROC)(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei instancecount);
typedef void (GLAD_API_CALL *PFNGLDRAWARRAYSINDIRECTPROC)(GLenum mode, const void *indirect);

typedef void (GLAD_API_CALL *PFNGLVIEWPORTPROC)(GLint x, GLint y, GLsizei width, GLsizei height);
typedef void (GLAD_API_CALL *PFNGLCLEARPROC)(GLbitfield mask);
typedef void (GLAD_API_CALL *PFNGLCLEARCOLORPROC)(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
typedef void (GLAD_API_CALL *PFNGLDEPTHFUNCPROC)(GLenum func);
typedef void (GLAD_API_CALL *PFNGLDEPTHMASKPROC)(GLboolean flag);
typedef void (GLAD_API_CALL *PFNGLBLENDFUNCPROC)(GLenum sfactor, GLenum dfactor);
typedef void (GLAD_API_CALL *PFNGLBLENDEQUATIONPROC)(GLenum mode);
typedef void (GLAD_API_CALL *PFNGLENABLEPROC)(GLenum cap);
typedef void (GLAD_API_CALL *PFNGLDISABLEPROC)(GLenum cap);
typedef void (GLAD_API_CALL *PFNGLCULLFACEPROC)(GLenum mode);
typedef void (GLAD_API_CALL *PFNGLFRONTFACEPROC)(GLenum mode);
typedef void (GLAD_API_CALL *PFNGLPOLYGONMODEPROC)(GLenum face, GLenum mode);

typedef void (GLAD_API_CALL *PFNGLBINDFRAMEBUFFERPROC)(GLenum target, GLuint framebuffer);
typedef void (GLAD_API_CALL *PFNGLFRAMEBUFFERTEXTURE2DPROC)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
typedef GLenum (GLAD_API_CALL *PFNGLCHECKFRAMEBUFFERSTATUSPROC)(GLenum target);
typedef void (GLAD_API_CALL *PFNGLGENFRAMEBUFFERSPROC)(GLsizei n, GLuint *framebuffers);
typedef void (GLAD_API_CALL *PFNGLDELETEFRAMEBUFFERSPROC)(GLsizei n, const GLuint *framebuffers);

typedef void (GLAD_API_CALL *PFNGLGETINTEGERVPROC)(GLenum pname, GLint *data);
typedef const GLubyte* (GLAD_API_CALL *PFNGLGETSTRINGPROC)(GLenum name);
typedef const GLubyte* (GLAD_API_CALL *PFNGLGETSTRINGIPROC)(GLenum name, GLuint index);

typedef void (GLAD_API_CALL *PFNGLDEBUGGENMESSAGEINDEXPROC)(GLenum source, GLenum type, GLenum severity, GLsizei count, const GLuint *ids, GLboolean enabled);

// --- OpenGL Function Pointer Declarations ---
// These are defined and initialized by glad.c at runtime

// Shader operations
extern PFNGLCREATESHADERPROC glCreateShader;
extern PFNGLSHADERSOURCEPROC glShaderSource;
extern PFNGLCOMPILESHADERPROC glCompileShader;
extern PFNGLGETSHADERIVPROC glGetShaderiv;
extern PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog;
extern PFNGLDELETESHADERPROC glDeleteShader;

extern PFNGLCREATEPROGRAMPROC glCreateProgram;
extern PFNGLATTACHSHADERPROC glAttachShader;
extern PFNGLLINKPROGRAMPROC glLinkProgram;
extern PFNGLGETPROGRAMIVPROC glGetProgramiv;
extern PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog;
extern PFNGLUSEPROGRAMPROC glUseProgram;
extern PFNGLDELETEPROGRAMPROC glDeleteProgram;
extern PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation;
extern PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv;

// Buffer operations
extern PFNGLGENBUFFERSPROC glGenBuffers;
extern PFNGLBINDBUFFERPROC glBindBuffer;
extern PFNGLBUFFERDATAPROC glBufferData;
extern PFNGLBUFFERSUBDATAPROC glBufferSubData;
extern PFNGLMAPBUFFERPROC glMapBuffer;
extern PFNGLUNMAPBUFFERPROC glUnmapBuffer;
extern PFNGLDELETEBUFFERSPROC glDeleteBuffers;

// Texture operations
extern PFNGLGENTEXTURESPROC glGenTextures;
extern PFNGLBINDTEXTUREPROC glBindTexture;
extern PFNGLTEXIMAGE2DPROC glTexImage2D;
extern PFNGLTEXPARAMETERIPROC glTexParameteri;
extern PFNGLACTIVETEXTUREPROC glActiveTexture;
extern PFNGLDELETETEXTURESPROC glDeleteTextures;

// Vertex Array operations
extern PFNGLGENVERTEXARRAYSPROC glGenVertexArrays;
extern PFNGLBINDVERTEXARRAYPROC glBindVertexArray;
extern PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer;
extern PFNGLVERTEXATTRIBIPOINTERPROC glVertexAttribIPointer;
extern PFNGLVERTEXATTRIBLPOINTERPROC glVertexAttribLPointer;
extern PFNGLBINDVERTEXBUFFERPROC glBindVertexBuffer;
extern PFNGLVERTEXATTRIBBINDINGPROC glVertexAttribBinding;
extern PFNGLVERTEXBINDINGDIVISORPROC glVertexBindingDivisor;
extern PFNGLVERTEXATTRIBDIVISORPROC glVertexAttribDivisor;
extern PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray;
extern PFNGLDISABLEVERTEXATTRIBARRAYPROC glDisableVertexAttribArray;

// Draw operations
extern PFNGLDRAWELEMENTSPROC glDrawElements;
extern PFNGLDRAWARRAYSPROC glDrawArrays;
extern PFNGLDRAWELEMENTSBINDINGPOINTSINDIRECTPROC glDrawElementsIndirect;
extern PFNGLDRAWARRAYSINSTANCEDPROC glDrawArraysInstanced;
extern PFNGLDRAWELEMENTSINSTANCEDPROC glDrawElementsInstanced;
extern PFNGLDRAWARRAYSINDIRECTPROC glDrawArraysIndirect;

// State management
extern PFNGLVIEWPORTPROC glViewport;
extern PFNGLCLEARPROC glClear;
extern PFNGLCLEARCOLORPROC glClearColor;
extern PFNGLDEPTHFUNCPROC glDepthFunc;
extern PFNGLDEPTHMASKPROC glDepthMask;
extern PFNGLBLENDFUNCPROC glBlendFunc;
extern PFNGLBLENDEQUATIONPROC glBlendEquation;
extern PFNGLENABLEPROC glEnable;
extern PFNGLDISABLEPROC glDisable;
extern PFNGLCULLFACEPROC glCullFace;
extern PFNGLFRONTFACEPROC glFrontFace;
extern PFNGLPOLYGONMODEPROC glPolygonMode;

// Framebuffer operations
extern PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer;
extern PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D;
extern PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus;
extern PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers;
extern PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers;

// Get state
extern PFNGLGETINTEGERVPROC glGetIntegerv;
extern PFNGLGETSTRINGPROC glGetString;
extern PFNGLGETSTRINGIPROC glGetStringi;

// Debug
extern PFNGLDEBUGGENMESSAGEINDEXPROC glDebugMessageControl;

/**
 * Initialize GLAD - load all OpenGL function pointers
 * @param loader Function pointer to use for loading (e.g., glfwGetProcAddress)
 * @return Non-zero if successful, zero if any critical functions failed to load
 */
int gladLoadGL(GLADloadproc loader);
