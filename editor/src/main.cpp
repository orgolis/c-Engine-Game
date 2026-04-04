// GameWorldshaper Editor - Main entry point
// Phase 2: Testing window and renderer foundation

#include "window.h"
#include "renderer.h"
#include <spdlog/spdlog.h>
#include <iostream>

int main() {
    try {
        std::cout << "=== GameWorldshaper Editor ===" << std::endl;
        std::cout << "Phase 2: Window & Renderer Test" << std::endl;
        
        // Initialize logger first
        spdlog::set_level(spdlog::level::info);
        
        spdlog::info("=== GameWorldshaper Editor ===");
        spdlog::info("Phase 2: Window & Renderer Test");
        
        // Test window creation
        spdlog::info("Creating window...");
        std::cout << "Creating window..." << std::endl;
        
        schizo::window::WindowProperties props;
        props.width = 1280;
        props.height = 720;
        props.title = "GameWorldshaper - Editor";
        props.vsync = true;
        
        auto window = schizo::window::Window::Create(props);
        if (!window) {
            spdlog::error("Failed to create window!");
            std::cout << "ERROR: Failed to create window!" << std::endl;
            return 1;
        }
        
        spdlog::info("Window created successfully!");
        std::cout << "SUCCESS: Window created!" << std::endl;
        std::cout << "Window size: " << window->GetWidth() << "x" << window->GetHeight() << std::endl;
        std::cout << "Aspect ratio: " << window->GetAspectRatio() << std::endl;
        
        // Test renderer creation
        spdlog::info("Creating renderer...");
        std::cout << "Creating renderer..." << std::endl;
        
        auto renderer = schizo::renderer::Renderer::Create();
        if (!renderer) {
            spdlog::error("Failed to create renderer!");
            std::cout << "ERROR: Failed to create renderer!" << std::endl;
            return 1;
        }
        
        spdlog::info("Renderer created successfully!");
        std::cout << "SUCCESS: Renderer created!" << std::endl;
        
        spdlog::info("Initializing renderer...");
        std::cout << "Initializing renderer..." << std::endl;
        
        if (!renderer->Initialize()) {
            spdlog::error("Failed to initialize renderer!");
            std::cout << "ERROR: Failed to initialize renderer!" << std::endl;
            return 1;
        }
        
        spdlog::info("Renderer initialized successfully!");
        std::cout << "SUCCESS: Renderer initialized!" << std::endl;
        
        // Main loop
        spdlog::info("Entering main loop... (running for 5 seconds)");
        std::cout << "Entering main loop... press ESC or close window to exit" << std::endl;
        
        int frame_count = 0;
        const int max_frames = 300;  // Run for 300 frames (5 seconds at 60 FPS)
        
        while (window->Update() && frame_count < max_frames) {
            // Get camera for rendering
            schizo::renderer::Camera camera;
            camera.viewport_width = window->GetWidth();
            camera.viewport_height = window->GetHeight();
            camera.position = glm::vec3(0.0f, 1.0f, 5.0f);
            camera.direction = glm::vec3(0.0f, 0.0f, -1.0f);
            
            // Render frame
            renderer->BeginFrame(camera);
            // Frame is cleared to color
            renderer->EndFrame();
            
            // Swap buffers
            window->SwapBuffers();
            
            frame_count++;
            
            if (frame_count % 60 == 0) {
                auto stats = renderer->GetStats();
                spdlog::info("Frame {}: {} draw calls", frame_count, stats.draw_calls);
                std::cout << "Frame " << frame_count << ": " << stats.draw_calls << " draw calls" << std::endl;
            }
        }
        
        spdlog::info("Exiting after {} frames", frame_count);
        std::cout << "Test complete! Rendered " << frame_count << " frames" << std::endl;
        
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
}
