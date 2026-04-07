#pragma once

/**
 * @file graphics.h
 * @brief Central Graphics Abstraction Layer Header
 * 
 * This file includes all graphics-related abstractions and provides a unified
 * interface for rendering, shading, and graphics resource management.
 * 
 * The Graphics abstraction layer is designed to:
 * - Support multiple graphics APIs (OpenGL, Vulkan, DirectX12, Metal)
 * - Provide high-level rendering interfaces without exposing API details
 * - Enable efficient resource management and state tracking
 * - Support both immediate-mode and deferred rendering
 * 
 * Architecture:
 * - RenderDevice: Low-level graphics API abstraction
 * - Renderer: High-level rendering interface and frame management
 * - RenderPass: Abstract rendering stage with its own render target
 * - Mesh: Vertex/index buffer management and drawing
 * - Material: Shader program + uniform parameters and texture bindings
 * - Framebuffer: Off-screen render target abstraction
 * - Texture: GPU texture resource abstraction
 * - ShaderProgram: Shader compilation and linking
 */

// Core graphics abstractions
#include "render_device.h"
#include "renderer.h"
#include "mesh.h"
#include "shader.h"

// Texture and material systems
#include "texture.h"
#include "material.h"
#include "pbr.h"
#include "framebuffer.h"

// Graphics utilities
#include "graphics_utils.h"

// Geometry system
#include "geometry.h"

// Lighting system
#include "light.h"
#include "shadow.h"
#include "ibl.h"
#include "lighting.h"

// Deferred rendering
#include "g_buffer.h"
#include "deferred_renderer.h"

// Post-processing
#include "post_processing.h"

// Frustum culling
#include "frustum.h"

// Batching system
#include "batch.h"

// Performance profiling
#include "profiler.h"

// Deferred rendering system
#include "g_buffer.h"
#include "deferred_renderer.h"

namespace schizo::graphics {
    // Re-export commonly used types from renderer namespace for convenience
    using RenderAPI = schizo::renderer::RenderAPI;
    using RenderDevice = schizo::renderer::RenderDevice;
    using Renderer = schizo::renderer::Renderer;
    using Mesh = schizo::renderer::Mesh;
    using ShaderProgram = schizo::renderer::ShaderProgram;
    using RenderStats = schizo::renderer::RenderStats;
    
    using Camera = schizo::renderer::Camera;
    using Light = schizo::renderer::Light;
    using Transform = schizo::renderer::Transform;
    
    // New texture and material types
    using Texture = schizo::renderer::Texture;
    using Texture2D = schizo::renderer::Texture2D;
    using TextureCube = schizo::renderer::TextureCube;
    using Material = schizo::renderer::Material;
    using Framebuffer = schizo::renderer::Framebuffer;
    
} // namespace schizo::graphics

#endif // GRAPHICS_H
