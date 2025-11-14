#include "Window.h"
#include <iostream>

bool Window::Create(int width, int height, const char* title) {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return false;
    }
    handle = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!handle) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return false;
    }
    glfwMakeContextCurrent(handle);
    return true;
}

void Window::PollEvents() { 
    glfwPollEvents();
}


bool Window::IsOpen() {
    return !glfwWindowShouldClose(handle);
}


void Window::Destroy() {
    glfwTerminate(); 
}