#include <glad/glad.h>
#include <cstdlib>
#include <cstring>

// --- OpenGL 4.5 Core Profile Function Pointer Definitions ---

// Shader operations
PFNGLCREATESHADERPROC glCreateShader = nullptr;
PFNGLSHADERSOURCEPROC glShaderSource = nullptr;
PFNGLCOMPILESHADERPROC glCompileShader = nullptr;
PFNGLGETSHADERIVPROC glGetShaderiv = nullptr;
PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog = nullptr;
PFNGLDELETESHADERPROC glDeleteShader = nullptr;

PFNGLCREATEPROGRAMPROC glCreateProgram = nullptr;
PFNGLATTACHSHADERPROC glAttachShader = nullptr;
PFNGLLINKPROGRAMPROC glLinkProgram = nullptr;
PFNGLGETPROGRAMIVPROC glGetProgramiv = nullptr;
PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog = nullptr;
PFNGLUSEPROGRAMPROC glUseProgram = nullptr;
PFNGLDELETEPROGRAMPROC glDeleteProgram = nullptr;
PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation = nullptr;
PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv = nullptr;

// Buffer operations
PFNGLGENBUFFERSPROC glGenBuffers = nullptr;
PFNGLBINDBUFFERPROC glBindBuffer = nullptr;
PFNGLBUFFERDATAPROC glBufferData = nullptr;
PFNGLBUFFERSUBDATAPROC glBufferSubData = nullptr;
PFNGLMAPBUFFERPROC glMapBuffer = nullptr;
PFNGLUNMAPBUFFERPROC glUnmapBuffer = nullptr;
PFNGLDELETEBUFFERSPROC glDeleteBuffers = nullptr;

// Texture operations
PFNGLGENTEXTURESPROC glGenTextures = nullptr;
PFNGLBINDTEXTUREPROC glBindTexture = nullptr;
PFNGLTEXIMAGE2DPROC glTexImage2D = nullptr;
PFNGLTEXPARAMETERIPROC glTexParameteri = nullptr;
PFNGLACTIVETEXTUREPROC glActiveTexture = nullptr;
PFNGLDELETETEXTURESPROC glDeleteTextures = nullptr;

// Vertex Array operations
PFNGLGENVERTEXARRAYSPROC glGenVertexArrays = nullptr;
PFNGLBINDVERTEXARRAYPROC glBindVertexArray = nullptr;
PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer = nullptr;
PFNGLVERTEXATTRIBIPOINTERPROC glVertexAttribIPointer = nullptr;
PFNGLVERTEXATTRIBLPOINTERPROC glVertexAttribLPointer = nullptr;
PFNGLBINDVERTEXBUFFERPROC glBindVertexBuffer = nullptr;
PFNGLVERTEXATTRIBBINDINGPROC glVertexAttribBinding = nullptr;
PFNGLVERTEXBINDINGDIVISORPROC glVertexBindingDivisor = nullptr;
PFNGLVERTEXATTRIBDIVISORPROC glVertexAttribDivisor = nullptr;
PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays = nullptr;
PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray = nullptr;
PFNGLDISABLEVERTEXATTRIBARRAYPROC glDisableVertexAttribArray = nullptr;

// Draw operations
PFNGLDRAWELEMENTSPROC glDrawElements = nullptr;
PFNGLDRAWARRAYSPROC glDrawArrays = nullptr;
PFNGLDRAWELEMENTSBINDINGPOINTSINDIRECTPROC glDrawElementsIndirect = nullptr;
PFNGLDRAWARRAYSINSTANCEDPROC glDrawArraysInstanced = nullptr;
PFNGLDRAWELEMENTSINSTANCEDPROC glDrawElementsInstanced = nullptr;
PFNGLDRAWARRAYSINDIRECTPROC glDrawArraysIndirect = nullptr;

// State management
PFNGLVIEWPORTPROC glViewport = nullptr;
PFNGLCLEARPROC glClear = nullptr;
PFNGLCLEARCOLORPROC glClearColor = nullptr;
PFNGLDEPTHFUNCPROC glDepthFunc = nullptr;
PFNGLDEPTHMASKPROC glDepthMask = nullptr;
PFNGLBLENDFUNCPROC glBlendFunc = nullptr;
PFNGLBLENDEQUATIONPROC glBlendEquation = nullptr;
PFNGLENABLEPROC glEnable = nullptr;
PFNGLDISABLEPROC glDisable = nullptr;
PFNGLCULLFACEPROC glCullFace = nullptr;
PFNGLFRONTFACEPROC glFrontFace = nullptr;
PFNGLPOLYGONMODEPROC glPolygonMode = nullptr;

// Framebuffer operations
PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer = nullptr;
PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D = nullptr;
PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus = nullptr;
PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers = nullptr;
PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers = nullptr;

// Get state
PFNGLGETINTEGERVPROC glGetIntegerv = nullptr;
PFNGLGETSTRINGPROC glGetString = nullptr;
PFNGLGETSTRINGIPROC glGetStringi = nullptr;

// Debug
PFNGLDEBUGGENMESSAGEINDEXPROC glDebugMessageControl = nullptr;

/**
 * Helper macro to load a function pointer
 * Returns 1 if critical (required), -1 if optional (nice to have)
 */
#define LOAD_FUNCTION(ptr, name, required) \
    do { \
        (ptr) = reinterpret_cast<decltype(ptr)>(loader(#name)); \
        if ((required) && !(ptr)) { \
            return 0; \
        } \
    } while(0)

#pragma warning(push)
#pragma warning(disable:4127)
int gladLoadGL(GLADloadproc loader) {
    if (!loader) {
        return 0;
    }

    // Critical shader functions (required)
    LOAD_FUNCTION(glCreateShader, glCreateShader, 1);
    LOAD_FUNCTION(glShaderSource, glShaderSource, 1);
    LOAD_FUNCTION(glCompileShader, glCompileShader, 1);
    LOAD_FUNCTION(glGetShaderiv, glGetShaderiv, 1);
    LOAD_FUNCTION(glGetShaderInfoLog, glGetShaderInfoLog, 1);
    LOAD_FUNCTION(glDeleteShader, glDeleteShader, 1);

    LOAD_FUNCTION(glCreateProgram, glCreateProgram, 1);
    LOAD_FUNCTION(glAttachShader, glAttachShader, 1);
    LOAD_FUNCTION(glLinkProgram, glLinkProgram, 1);
    LOAD_FUNCTION(glGetProgramiv, glGetProgramiv, 1);
    LOAD_FUNCTION(glGetProgramInfoLog, glGetProgramInfoLog, 1);
    LOAD_FUNCTION(glUseProgram, glUseProgram, 1);
    LOAD_FUNCTION(glDeleteProgram, glDeleteProgram, 1);
    LOAD_FUNCTION(glGetUniformLocation, glGetUniformLocation, 1);
    LOAD_FUNCTION(glUniformMatrix4fv, glUniformMatrix4fv, 1);

    // Critical buffer functions
    LOAD_FUNCTION(glGenBuffers, glGenBuffers, 1);
    LOAD_FUNCTION(glBindBuffer, glBindBuffer, 1);
    LOAD_FUNCTION(glBufferData, glBufferData, 1);
    LOAD_FUNCTION(glBufferSubData, glBufferSubData, 1);
    LOAD_FUNCTION(glDeleteBuffers, glDeleteBuffers, 1);
    LOAD_FUNCTION(glMapBuffer, glMapBuffer, 1);
    LOAD_FUNCTION(glUnmapBuffer, glUnmapBuffer, 1);

    // Critical texture functions
    LOAD_FUNCTION(glGenTextures, glGenTextures, 1);
    LOAD_FUNCTION(glBindTexture, glBindTexture, 1);
    LOAD_FUNCTION(glTexImage2D, glTexImage2D, 1);
    LOAD_FUNCTION(glTexParameteri, glTexParameteri, 1);
    LOAD_FUNCTION(glActiveTexture, glActiveTexture, 1);
    LOAD_FUNCTION(glDeleteTextures, glDeleteTextures, 1);

    // Critical VAO functions
    LOAD_FUNCTION(glGenVertexArrays, glGenVertexArrays, 1);
    LOAD_FUNCTION(glBindVertexArray, glBindVertexArray, 1);
    LOAD_FUNCTION(glVertexAttribPointer, glVertexAttribPointer, 1);
    LOAD_FUNCTION(glEnableVertexAttribArray, glEnableVertexAttribArray, 1);
    LOAD_FUNCTION(glDisableVertexAttribArray, glDisableVertexAttribArray, 1);
    LOAD_FUNCTION(glDeleteVertexArrays, glDeleteVertexArrays, 1);

    // Optional VAO functions
    LOAD_FUNCTION(glVertexAttribIPointer, glVertexAttribIPointer, 0);
    LOAD_FUNCTION(glVertexAttribLPointer, glVertexAttribLPointer, 0);
    LOAD_FUNCTION(glBindVertexBuffer, glBindVertexBuffer, 0);
    LOAD_FUNCTION(glVertexAttribBinding, glVertexAttribBinding, 0);
    LOAD_FUNCTION(glVertexBindingDivisor, glVertexBindingDivisor, 0);
    LOAD_FUNCTION(glVertexAttribDivisor, glVertexAttribDivisor, 0);

    // Critical draw functions
    LOAD_FUNCTION(glDrawElements, glDrawElements, 1);
    LOAD_FUNCTION(glDrawArrays, glDrawArrays, 1);

    // Optional draw functions
    LOAD_FUNCTION(glDrawElementsIndirect, glDrawElementsIndirect, 0);
    LOAD_FUNCTION(glDrawArraysInstanced, glDrawArraysInstanced, 0);
    LOAD_FUNCTION(glDrawElementsInstanced, glDrawElementsInstanced, 0);
    LOAD_FUNCTION(glDrawArraysIndirect, glDrawArraysIndirect, 0);

    // State management
    LOAD_FUNCTION(glViewport, glViewport, 1);
    LOAD_FUNCTION(glClear, glClear, 1);
    LOAD_FUNCTION(glClearColor, glClearColor, 1);
    LOAD_FUNCTION(glDepthFunc, glDepthFunc, 0);
    LOAD_FUNCTION(glDepthMask, glDepthMask, 0);
    LOAD_FUNCTION(glBlendFunc, glBlendFunc, 0);
    LOAD_FUNCTION(glBlendEquation, glBlendEquation, 0);
    LOAD_FUNCTION(glEnable, glEnable, 1);
    LOAD_FUNCTION(glDisable, glDisable, 1);
    LOAD_FUNCTION(glCullFace, glCullFace, 0);
    LOAD_FUNCTION(glFrontFace, glFrontFace, 0);
    LOAD_FUNCTION(glPolygonMode, glPolygonMode, 0);

    // Get state
    LOAD_FUNCTION(glGetIntegerv, glGetIntegerv, 1);
    LOAD_FUNCTION(glGetString, glGetString, 1);
    LOAD_FUNCTION(glGetStringi, glGetStringi, 0);

    // Optional framebuffer functions
    LOAD_FUNCTION(glBindFramebuffer, glBindFramebuffer, 0);
    LOAD_FUNCTION(glFramebufferTexture2D, glFramebufferTexture2D, 0);
    LOAD_FUNCTION(glCheckFramebufferStatus, glCheckFramebufferStatus, 0);
    LOAD_FUNCTION(glGenFramebuffers, glGenFramebuffers, 0);
    LOAD_FUNCTION(glDeleteFramebuffers, glDeleteFramebuffers, 0);

    // Optional debug
    LOAD_FUNCTION(glDebugMessageControl, glDebugMessageControl, 0);

    return 1;  // Success
}
#pragma warning(pop)
