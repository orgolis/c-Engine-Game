#include "Engine.h"

bool Engine::Init() {
if (!window.Create(1280, 720, "My Engine")) return false;
if (!renderer.Init()) return false;
return true;
}


void Engine::Run() {
while (running && window.IsOpen()) {
window.PollEvents();
renderer.Clear();
renderer.Swap();
}
}


void Engine::Shutdown() {
renderer.Shutdown();
window.Destroy();
}