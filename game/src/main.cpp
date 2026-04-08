// GameWorldshaper Game - Main entry point
// Phase 2: Window & Renderer test with game loop

#include "window.h"
// #include "renderer.h"  // Disabled - renderer module disabled
// #include "mesh.h"
// #include "shader.h"
#include <spdlog/spdlog.h>
#include <iostream>
#include <chrono>
#include <memory>

int main() {
    std::cout << "=== Project Schizo - Game ===" << std::endl;
    std::cout << "Phase 2 (Modified): Window test (renderer disabled)" << std::endl;
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
        props.title = "Project Schizo (Renderer Disabled)";
        props.vsync = true;
        
        auto window = schizo::window::Window::Create(props);
        if (!window) {
            std::cerr << "ERROR: Failed to create window!" << std::endl;
            spdlog::error("Failed to create window!");
            return 1;
        }
        
        std::cout << "Window created: " << window->GetWidth() << "x" << window->GetHeight() << std::endl;
        std::cout.flush();
        
        // Main game loop - simple window update loop without rendering
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
            
            frame_count++;
            
            // Print stats every 60 frames (~1 second at 60 FPS)
            if (frame_count % 60 == 0) {
                auto elapsed = std::chrono::high_resolution_clock::now() - start_time;
                double elapsed_sec = std::chrono::duration<double>(elapsed).count();
                double fps = frame_count / elapsed_sec;
                
                spdlog::info("Frame {}: {:.1f} FPS", frame_count, fps);
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
