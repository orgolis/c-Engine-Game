// GameWorldshaper Game - Main entry point
// Phase 2: Window & Renderer test with game loop

#include "window.h"
#include "renderer.h"
#include "mesh.h"
#include "shader.h"
#include <spdlog/spdlog.h>
#include <iostream>
#include <chrono>
#include <memory>

int main() {
    std::cout << "=== Project Schizo - Game ===" << std::endl;
    std::cout << "Phase 2: Game Loop Test" << std::endl;
    std::cout.flush();

    try {
        // Initialize logging
        spdlog::set_level(spdlog::level::info);
        spdlog::info("Logging initialized");
        
        // Create window
        std::cout << "Creating window..." << std::endl;
        std::cout.flush();
        
        schizo::window::WindowProperties props;
        props.width = 1280;
        props.height = 720;
        props.title = "Project Schizo";
        props.vsync = true;
        
        auto window = schizo::window::Window::Create(props);
        if (!window) {
            std::cerr << "ERROR: Failed to create window!" << std::endl;
            spdlog::error("Failed to create window!");
            return 1;
        }
        
        std::cout << "Window created: " << window->GetWidth() << "x" << window->GetHeight() << std::endl;
        std::cout.flush();
        
        // Create and initialize renderer
        std::cout << "Creating renderer..." << std::endl;
        std::cout.flush();
        
        auto renderer = schizo::renderer::Renderer::Create();
        if (!renderer) {
            std::cerr << "ERROR: Failed to create renderer!" << std::endl;
            return 1;
        }
        
        std::cout << "Initializing renderer..." << std::endl;
        std::cout.flush();
        
        if (!renderer->Initialize()) {
            std::cerr << "ERROR: Failed to initialize renderer!" << std::endl;
            spdlog::error("Failed to initialize renderer!");
            return 1;
        }
        
        std::cout << "Renderer initialized!" << std::endl;
        std::cout << "OpenGL: " << renderer->GetDevice()->GetDeviceInfo() << std::endl;
        std::cout.flush();
        
        // Create shader program
        std::cout << "Creating shader..." << std::endl;
        std::cout.flush();
        
        auto shader = schizo::renderer::ShaderProgram::CreateBasicShader(renderer->GetDevice());
        if (!shader) {
            std::cerr << "ERROR: Shader pointer is null!" << std::endl;
            return 1;
        }
        
        if (!shader->IsValid()) {
            std::cerr << "ERROR: Shader compilation failed: " << shader->GetLastError() << std::endl;
            spdlog::error("Shader error: {}", shader->GetLastError());
            return 1;
        }
        
        std::cout << "Shader created successfully!" << std::endl;
        std::cout.flush();
        
        // Create test meshes
        std::cout << "Creating meshes..." << std::endl;
        std::cout.flush();
        
        auto triangle = schizo::renderer::Mesh::CreateTriangle(renderer->GetDevice());
        if (!triangle) {
            std::cerr << "ERROR: Failed to create triangle!" << std::endl;
            return 1;
        }
        
        std::cout << "Triangle: " << triangle->GetVertexCount() << " vertices, " 
                  << triangle->GetIndexCount() << " indices" << std::endl;
        std::cout.flush();
        
        auto cube = schizo::renderer::Mesh::CreateCube(renderer->GetDevice());
        auto sphere = schizo::renderer::Mesh::CreateSphere(renderer->GetDevice(), 0.8f, 32, 16);
        
        if (!cube || !sphere) {
            std::cerr << "ERROR: Failed to create cube/sphere!" << std::endl;
            return 1;
        }
        
        std::cout << "All meshes created!" << std::endl;
        std::cout.flush();
        
        // Main game loop
        spdlog::info("Entering game loop...");
        std::cout << "\n=== Game Running ===\nPress ESC or close window to exit\n\n";
        
        int frame_count = 0;
        auto start_time = std::chrono::high_resolution_clock::now();
        
        while (window->Update()) {
            // Exit on ESC key
            if (window->IsKeyPressed(schizo::window::KeyCode::ESCAPE)) {
                spdlog::info("ESC pressed - exiting");
                break;
            }
            
            // Setup camera
            schizo::renderer::Camera camera;
            camera.viewport_width = window->GetWidth();
            camera.viewport_height = window->GetHeight();
            camera.position = glm::vec3(0.0f, 1.0f, 3.0f);
            camera.direction = glm::vec3(0.0f, 0.0f, -1.0f);
            
            // Render frame
            renderer->BeginFrame(camera);
            {
                // Setup projection matrix (perspective)
                float aspect = static_cast<float>(window->GetWidth()) / static_cast<float>(window->GetHeight());
                glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
                
                // Setup view matrix from camera
                glm::vec3 camera_up(0.0f, 1.0f, 0.0f);
                glm::mat4 view = glm::lookAt(camera.position, 
                                            camera.position + camera.direction, 
                                            camera_up);
                
                // Model matrix (identity for now)
                glm::mat4 model = glm::mat4(1.0f);
                
                // Use shader program for rendering
                shader->Use(renderer->GetDevice());
                
                // Set camera matrices as uniforms
                shader->SetUniformMat4(renderer->GetDevice(), "u_projection", projection);
                shader->SetUniformMat4(renderer->GetDevice(), "u_view", view);
                shader->SetUniformMat4(renderer->GetDevice(), "u_model", model);
                
                // Draw triangle in center
                triangle->Draw(renderer->GetDevice());
            }
            renderer->EndFrame();
            
            // Present to screen
            window->SwapBuffers();
            
            frame_count++;
            
            // Print stats every 60 frames (~1 second at 60 FPS)
            if (frame_count % 60 == 0) {
                auto stats = renderer->GetStats();
                auto elapsed = std::chrono::high_resolution_clock::now() - start_time;
                double elapsed_sec = std::chrono::duration<double>(elapsed).count();
                double fps = frame_count / elapsed_sec;
                
                spdlog::info("Frame {}: {:.1f} FPS, {} draw calls", 
                    frame_count, fps, stats.draw_calls);
                std::cout << "Frame " << frame_count << ": " << fps << " FPS\n";
            }
        }
        
        spdlog::info("Game loop ended after {} frames", frame_count);
        std::cout << "\nGame exited cleanly. Total frames: " << frame_count << "\n";
        
        return 0;
    }
    catch (const std::exception& e) {
        spdlog::error("Exception: {}", e.what());
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }
}
