#pragma once
#include <glfw/glfw3.h>

class Window {
    public: 
        bool Create(int width, int height, const char* title);
        void PollEvents();
        bool isOpen();
        void Destroy();
    private:
    GLFWwindow* handle = nullptr;
};