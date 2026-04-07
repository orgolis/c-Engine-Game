#include <glad/glad.h>
#include <stdlib.h>
#include <string.h>

// --- OpenGL 4.5 Core Profile Function Pointer Definitions ---

// Shader operations
PFNGLCREATESHADERPROC glCreateShader = NULL;
PFNGLSHADERSOURCEPROC glShaderSource = NULL;
PFNGLCOMPILESHADERPROC glCompileShader = NULL;
PFNGLGETSHADERIVPROC glGetShaderiv = NULL;
PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog = NULL;
PFNGLDELETESHADERPROC glDeleteShader = NULL;

PFNGLCREATEPROGRAMPROC glCreateProgram = NULL;
PFNGLATTACHSHADERPROC glAttachShader = NULL;
PFNGLLINKPROGRAMPROC glLinkProgram = NULL;
PFNGLGETPROGRAMIVPROC glGetProgramiv = NULL;
PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog = NULL;
PFNGLUSEPROGRAMPROC glUseProgram = NULL;
PFNGLDELETEPROGRAMPROC glDeleteProgram = NULL;
PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation = NULL;
PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv = NULL;

// Buffer operations
PFNGLGENBUFFERSPROC glGenBuffers = NULL;
PFNGLBINDBUFFERPROC glBindBuffer = NULL;
PFNGLBUFFERDATAPROC glBufferData = NULL;
PFNGLBUFFERSUBDATAPROC glBufferSubData = NULL;
PFNGLMAPBUFFERPROC glMapBuffer = NULL;
PFNGLUNMAPBUFFERPROC glUnmapBuffer = NULL;
PFNGLDELETEBUFFERSPROC glDeleteBuffers = NULL;

// Texture operations
PFNGLGENTEXTURESPROC glGenTextures = NULL;
PFNGLBINDTEXTUREPROC glBindTexture = NULL;
PFNGLTEXIMAGE2DPROC glTexImage2D = NULL;
PFNGLTEXPARAMETERIPROC glTexParameteri = NULL;
PFNGLACTIVETEXTUREPROC glActiveTexture = NULL;
PFNGLDELETETEXTURESPROC glDeleteTextures = NULL;

// Vertex Array operations
PFNGLGENVERTEXARRAYSPROC glGenVertexArrays = NULL;
PFNGLBINDVERTEXARRAYPROC glBindVertexArray = NULL;
PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer = NULL;
PFNGLVERTEXATTRIBIPOINTERPROC glVertexAttribIPointer = NULL;
PFNGLVERTEXATTRIBLPOINTERPROC glVertexAttribLPointer = NULL;
PFNGLBINDVERTEXBUFFERPROC glBindVertexBuffer = NULL;
PFNGLVERTEXATTRIBBINDINGPROC glVertexAttribBinding = NULL;
PFNGLVERTEXBINDINGDIVISORPROC glVertexBindingDivisor = NULL;
PFNGLVERTEXATTRIBDIVISORPROC glVertexAttribDivisor = NULL;
PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays = NULL;
PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray = NULL;
PFNGLDISABLEVERTEXATTRIBARRAYPROC glDisableVertexAttribArray = NULL;

// Draw operations
PFNGLDRAWELEMENTSPROC glDrawElements = NULL;
PFNGLDRAWARRAYSPROC glDrawArrays = NULL;
PFNGLDRAWELEMENTSBINDINGPOINTSINDIRECTPROC glDrawElementsIndirect = NULL;
PFNGLDRAWARRAYSINSTANCEDPROC glDrawArraysInstanced = NULL;
PFNGLDRAWELEMENTSINSTANCEDPROC glDrawElementsInstanced = NULL;
PFNGLDRAWARRAYSINDIRECTPROC glDrawArraysIndirect = NULL;

// State management
PFNGLVIEWPORTPROC glViewport = NULL;
PFNGLCLEARPROC glClear = NULL;
PFNGLCLEARCOLORPROC glClearColor = NULL;
PFNGLDEPTHFUNCPROC glDepthFunc = NULL;
PFNGLDEPTHMASKPROC glDepthMask = NULL;
PFNGLBLENDFUNCPROC glBlendFunc = NULL;
PFNGLBLENDEQUATIONPROC glBlendEquation = NULL;
PFNGLENABLEPROC glEnable = NULL;
PFNGLDISABLEPROC glDisable = NULL;
PFNGLCULLFACEPROC glCullFace = NULL;
PFNGLFRONTFACEPROC glFrontFace = NULL;
PFNGLPOLYGONMODEPROC glPolygonMode = NULL;

// Framebuffer operations
PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer = NULL;
PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D = NULL;
PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus = NULL;
PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers = NULL;
PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers = NULL;

// Get state
PFNGLGETINTEGERVPROC glGetIntegerv = NULL;
PFNGLGETSTRINGPROC glGetString = NULL;
PFNGLGETSTRINGIPROC glGetStringi = NULL;

// Debug
PFNGLDEBUGGENMESSAGEINDEXPROC glDebugMessageControl = NULL;

/**
 * Helper macro to load a function pointer
 * Returns 1 if critical (required), -1 if optional (nice to have)
 */
#define LOAD_FUNCTION(ptr, name, required) \
    do { \
        (ptr) = (void*)loader(#name); \
        if ((required) && !(ptr)) { \
            return 0; \
        } \
    } while(0)

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

#undef LOAD_FUNCTION
