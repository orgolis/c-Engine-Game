#pragma once
#include "Window.h"
#include "Renderer.h"


class Engine {
public:
bool Init();
void Run();
void Shutdown();
private:
Window window;
Renderer renderer;
bool running = true;
};