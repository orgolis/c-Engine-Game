#include "Renderer.h"
#include <GLFW/glfw3.h>
#include <glad/glad.h>


bool Renderer::Init() {
if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) return false;
glViewport(0, 0, 1280, 720);
return true;
}


void Renderer::Clear() {
glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
glClear(GL_COLOR_BUFFER_BIT);
}


void Renderer::Swap() {
glfwSwapBuffers(glfwGetCurrentContext());
}


void Renderer::Shutdown() {}