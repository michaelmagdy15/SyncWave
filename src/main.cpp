#include "common.hpp"
#include "AudioEngine.hpp"
#include "gui.hpp"
#include <iostream>

#include <windows.h>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    // We launch as a Windows GUI application to hide console, but you can still attach a console if needed.
    
    AudioEngine engine;
    if (!engine.init()) {
        MessageBoxW(nullptr, L"Failed to initialize Audio Engine", L"Error", MB_ICONERROR);
        return -1;
    }

    GUI gui(engine);
    if (!gui.init()) {
        MessageBoxW(nullptr, L"Failed to initialize GUI", L"Error", MB_ICONERROR);
        return -1;
    }

    // Blocking render loop
    gui.renderLoop();

    gui.uninit();
    engine.uninit();

    return 0;
}

// Fallback for standard main if compiled as console app
int main(int argc, char* argv[]) {
    return wWinMain(GetModuleHandle(nullptr), nullptr, nullptr, SW_SHOWDEFAULT);
}