#include "gui.hpp"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <tchar.h>

// Data
static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

GUI::GUI(AudioEngine& engine) : m_engine(engine) {}
GUI::~GUI() { uninit(); }

bool GUI::init() {
    // Basic auto-select defaults
    if (!m_engine.getPlaybackDevices().empty()) {
        for(size_t i=0; i<m_engine.getPlaybackDevices().size(); ++i) {
            if (m_engine.getPlaybackDevices()[i].isDefault) {
                m_captureIdx = i;
                break;
            }
        }
        if (m_captureIdx == -1) m_captureIdx = 0;
    }
    return true;
}

void GUI::uninit() {
    // Handled in main
}

void GUI::applyEngineState() {
    if (m_isPlaying) {
        m_engine.start(m_captureIdx, m_pb1Idx, m_pb2Idx, m_isLoopback);
        m_engine.setDelayMs(0, m_delay1);
        m_engine.setDelayMs(1, m_delay2);
        m_engine.setFilterMode(0, static_cast<FilterMode>(m_filter1));
        m_engine.setFilterMode(1, static_cast<FilterMode>(m_filter2));
        m_engine.setVolume(0, m_vol1);
        m_engine.setVolume(1, m_vol2);
        m_engine.setMute(0, m_mute1);
        m_engine.setMute(1, m_mute2);
        m_engine.setPhaseInvert(0, m_invert1);
        m_engine.setPhaseInvert(1, m_invert2);
        m_engine.setCrossoverFreq(m_crossoverFreq);
    } else {
        m_engine.stop();
    }
}

static void ApplyCustomStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    style.WindowRounding = 12.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 6.0f;
    style.TabRounding = 6.0f;

    style.WindowPadding = ImVec2(24.0f, 24.0f);
    style.FramePadding = ImVec2(12.0f, 8.0f);
    style.ItemSpacing = ImVec2(12.0f, 16.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 8.0f);
    style.ScrollbarSize = 12.0f;
    
    // Backgrounds
    colors[ImGuiCol_WindowBg]           = ImVec4(0.06f, 0.06f, 0.08f, 1.00f);
    colors[ImGuiCol_ChildBg]            = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    colors[ImGuiCol_PopupBg]            = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    
    // Frames
    colors[ImGuiCol_FrameBg]            = ImVec4(0.16f, 0.16f, 0.20f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]     = ImVec4(0.24f, 0.24f, 0.28f, 1.00f);
    colors[ImGuiCol_FrameBgActive]      = ImVec4(0.32f, 0.32f, 0.38f, 1.00f);
    
    // Titles
    colors[ImGuiCol_TitleBg]            = ImVec4(0.06f, 0.06f, 0.08f, 1.00f);
    colors[ImGuiCol_TitleBgActive]      = ImVec4(0.06f, 0.06f, 0.08f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]   = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
    
    // Controls
    colors[ImGuiCol_CheckMark]          = ImVec4(0.65f, 0.45f, 0.95f, 1.00f);
    colors[ImGuiCol_SliderGrab]         = ImVec4(0.60f, 0.40f, 0.90f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]   = ImVec4(0.70f, 0.50f, 1.00f, 1.00f);
    
    // Buttons (Purple gradient feel)
    colors[ImGuiCol_Button]             = ImVec4(0.40f, 0.20f, 0.70f, 1.00f);
    colors[ImGuiCol_ButtonHovered]      = ImVec4(0.50f, 0.30f, 0.80f, 1.00f);
    colors[ImGuiCol_ButtonActive]       = ImVec4(0.60f, 0.40f, 0.90f, 1.00f);
    
    // Headers (Selections)
    colors[ImGuiCol_Header]             = ImVec4(0.40f, 0.20f, 0.70f, 1.00f);
    colors[ImGuiCol_HeaderHovered]      = ImVec4(0.50f, 0.30f, 0.80f, 1.00f);
    colors[ImGuiCol_HeaderActive]       = ImVec4(0.60f, 0.40f, 0.90f, 1.00f);

    // Separators
    colors[ImGuiCol_Separator]          = ImVec4(0.20f, 0.20f, 0.25f, 1.00f);
    colors[ImGuiCol_SeparatorHovered]   = ImVec4(0.40f, 0.20f, 0.70f, 1.00f);
    colors[ImGuiCol_SeparatorActive]    = ImVec4(0.60f, 0.40f, 0.90f, 1.00f);
    
    // Text
    colors[ImGuiCol_Text]               = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
    colors[ImGuiCol_TextDisabled]       = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
}

void GUI::drawUI() {
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);
    
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    
    if (ImGui::Begin("SyncWave Control Panel", nullptr, flags)) {
        // App Header
        ImGui::BeginGroup();
        ImGui::SetWindowFontScale(2.5f);
        ImGui::TextColored(ImVec4(0.7f, 0.5f, 1.0f, 1.0f), "SyncWave");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::TextDisabled("DUAL AUDIO ENHANCER");
        ImGui::EndGroup();

        ImGui::SameLine(ImGui::GetWindowWidth() - 250);
        
        ImGui::BeginGroup();
        if (m_isPlaying) {
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, 1.0f), "● STREAMING ACTIVE");
        } else {
            ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "○ STOPPED");
        }

        // START/STOP Button
        ImVec4 playColor = m_isPlaying ? ImVec4(0.8f, 0.2f, 0.2f, 1.0f) : ImVec4(0.2f, 0.7f, 0.3f, 1.0f);
        ImVec4 playHover = m_isPlaying ? ImVec4(0.9f, 0.3f, 0.3f, 1.0f) : ImVec4(0.3f, 0.8f, 0.4f, 1.0f);
        ImVec4 playActive= m_isPlaying ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f) : ImVec4(0.4f, 0.9f, 0.5f, 1.0f);

        ImGui::PushStyleColor(ImGuiCol_Button, playColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, playHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, playActive);
        
        bool needRestart = false;
        bool needParamUpdate = false;

        ImGui::Dummy(ImVec2(0, 5));
        ImGui::SetWindowFontScale(1.3f);
        if (ImGui::Button(m_isPlaying ? "STOP AUDIO" : "START AUDIO", ImVec2(200, 50))) {
            m_isPlaying = !m_isPlaying;
            needRestart = m_isPlaying;
        }
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor(3);
        ImGui::EndGroup();
        
        ImGui::Dummy(ImVec2(0, 10));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 10));

        // Input Source Card
        ImGui::SetWindowFontScale(1.2f);
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "Input Source");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Dummy(ImVec2(0, 5));

        if (ImGui::BeginChild("SourceCard", ImVec2(0, 110), true, 0)) {
            const auto& capDevices = m_isLoopback ? m_engine.getPlaybackDevices() : m_engine.getCaptureDevices();
            if (ImGui::Checkbox("Capture from Playback (Loopback)", &m_isLoopback)) {
                m_captureIdx = 0;
                needRestart = true;
            }

            ImGui::SetNextItemWidth(-FLT_MIN);
            std::string capPreview = (m_captureIdx >= 0 && m_captureIdx < capDevices.size()) ? capDevices[m_captureIdx].name : "Select Capture/Loopback";
            if (ImGui::BeginCombo("Device##Cap", capPreview.c_str())) {
                for (int i = 0; i < capDevices.size(); i++) {
                    if (ImGui::Selectable(capDevices[i].name.c_str(), m_captureIdx == i)) {
                        m_captureIdx = i;
                        needRestart = true;
                    }
                }
                ImGui::EndCombo();
            }
        }
        ImGui::EndChild();

        ImGui::Dummy(ImVec2(0, 10));

        // Output Devices section
        auto renderDeviceControl = [&](const char* title, int& pbIdx, float& delay, int& filter, float& vol, bool& mute, bool& invert) {
            ImGui::PushID(title);
            if (ImGui::BeginChild("DevCard", ImVec2(0, 270), true, 0)) {
                ImGui::SetWindowFontScale(1.2f);
                ImGui::TextColored(ImVec4(0.7f, 0.5f, 1.0f, 1.0f), "%s", title);
                ImGui::SetWindowFontScale(1.0f);
                ImGui::Dummy(ImVec2(0, 8));

                const auto& pbDevices = m_engine.getPlaybackDevices();
                ImGui::SetNextItemWidth(-FLT_MIN);
                std::string preview = (pbIdx >= 0 && pbIdx < pbDevices.size()) ? pbDevices[pbIdx].name : "Select Device...";
                if (ImGui::BeginCombo("##Dev", preview.c_str())) {
                    if (ImGui::Selectable("None", pbIdx == -1)) { pbIdx = -1; needRestart = true; }
                    for (int i = 0; i < pbDevices.size(); i++) {
                        if (ImGui::Selectable(pbDevices[i].name.c_str(), pbIdx == i)) { pbIdx = i; needRestart = true; }
                    }
                    ImGui::EndCombo();
                }

                if (pbIdx != -1) {
                    ImGui::Dummy(ImVec2(0, 5));
                    
                    bool isLoopbackSource = (m_isLoopback && pbIdx == m_captureIdx);
                    if (isLoopbackSource) {
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Loopback Source Device");
                        ImGui::TextDisabled("Internal loopback is muted to prevent\naudio doubling.");
                        ImGui::Dummy(ImVec2(0, 5));
                        ImGui::TextDisabled("Note: Adjusting Delay or Filter here\nwon't affect the system's native stream.");
                    }
                    
                    if (isLoopbackSource) ImGui::BeginDisabled();
                    
                    ImGui::TextDisabled("Volume");
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::SliderFloat("##Vol", &vol, 0.0f, 1.0f, "%.2f")) needParamUpdate = true;
                    
                    ImGui::Dummy(ImVec2(0, 5));
                    if (ImGui::Checkbox("Mute", &mute)) needParamUpdate = true;
                    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.4f);
                    if (ImGui::Checkbox("Invert Phase", &invert)) needParamUpdate = true;
                    ImGui::Dummy(ImVec2(0, 5));
                    
                    ImGui::TextDisabled("Delay");
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::SliderFloat("##Del", &delay, 0.0f, 1000.0f, "%.1f ms")) needParamUpdate = true;
                    
                    ImGui::TextDisabled("Filter Mode");
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    const char* filters[] = { "Full Range", "Tweeter (High-Pass)", "Bass (Low-Pass)" };
                    if (ImGui::Combo("##Flt", &filter, filters, 3)) needParamUpdate = true;
                    
                    if (isLoopbackSource) ImGui::EndDisabled();
                } else {
                    ImGui::Dummy(ImVec2(0, 30));
                    ImGui::TextDisabled("Select an output device to configure.");
                }
            }
            ImGui::EndChild();
            ImGui::PopID();
        };

        if (ImGui::BeginTable("OutputsTable", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            renderDeviceControl("Output 1 (Primary)", m_pb1Idx, m_delay1, m_filter1, m_vol1, m_mute1, m_invert1);
            ImGui::TableSetColumnIndex(1);
            renderDeviceControl("Output 2 (Secondary)", m_pb2Idx, m_delay2, m_filter2, m_vol2, m_mute2, m_invert2);
            ImGui::EndTable();
        }
        
        if (m_filter1 != 0 || m_filter2 != 0) {
            ImGui::Dummy(ImVec2(0, 10));
            if (ImGui::BeginChild("CrossoverCard", ImVec2(0, 100), true, 0)) {
                ImGui::SetWindowFontScale(1.1f);
                ImGui::Text("Crossover Frequency");
                ImGui::SetWindowFontScale(1.0f);
                ImGui::TextDisabled("Controls the boundary between Tweeter and Bass filters.");
                
                ImGui::Dummy(ImVec2(0, 5));
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::SliderFloat("##XOver", &m_crossoverFreq, 50.0f, 10000.0f, "%.0f Hz", ImGuiSliderFlags_Logarithmic)) {
                    needParamUpdate = true;
                }
            }
            ImGui::EndChild();
        }
        
        ImGui::Dummy(ImVec2(0, 10));
        if (ImGui::BeginChild("SyncToolsCard", ImVec2(0, 180), true, 0)) {
            ImGui::SetWindowFontScale(1.3f);
            ImGui::TextColored(ImVec4(0.40f, 0.90f, 0.60f, 1.00f), "Sync Tools");
            ImGui::SetWindowFontScale(1.0f);
            
            int oldTool = m_syncToolSelected;
            float availWidth = ImGui::GetContentRegionAvail().x;
            if (availWidth > 400.0f) {
                ImGui::SameLine(availWidth - 350.0f);
            } else {
                ImGui::Dummy(ImVec2(0, 5));
            }
            ImGui::RadioButton("Off", &m_syncToolSelected, 0); ImGui::SameLine();
            ImGui::RadioButton("Auto-Mic", &m_syncToolSelected, 1); ImGui::SameLine();
            ImGui::RadioButton("Nulling", &m_syncToolSelected, 2); ImGui::SameLine();
            ImGui::RadioButton("Metronome", &m_syncToolSelected, 3);
            if (oldTool != m_syncToolSelected) {
                m_engine.setSyncToolMode(SyncToolMode::Off);
            }
            
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 5));
            
            if (m_syncToolSelected == 0) {
                ImGui::TextDisabled("Select a tool above to help precisely align the dual speaker output.");
            } else if (m_syncToolSelected == 1) { // Auto-Mic
                ImGui::Text("Acoustic Auto-Sync uses a selected microphone to measure room latency.");
                const auto& capDevices = m_engine.getCaptureDevices();
                std::string micPreview = (m_calibMicIdx >= 0 && m_calibMicIdx < capDevices.size()) ? capDevices[m_calibMicIdx].name : "Select Calibration Mic...";
                ImGui::SetNextItemWidth(400);
                if (ImGui::BeginCombo("##Mic", micPreview.c_str())) {
                    for (int i = 0; i < capDevices.size(); i++) {
                        if (ImGui::Selectable(capDevices[i].name.c_str(), m_calibMicIdx == i)) m_calibMicIdx = i;
                    }
                    ImGui::EndCombo();
                }
                
                ImGui::Dummy(ImVec2(0, 5));
                CalibrationState state = m_engine.getCalibrationState();
                if (m_isCalibrating && state == CalibrationState::Done) {
                    m_isCalibrating = false;
                    needRestart = true; // Reverts to the user's previously selected Capture Loopback device
                }

                if (state != CalibrationState::Idle && state != CalibrationState::Done) {
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Calibrating... Please remain quiet.");
                } else {
                    if (ImGui::Button("Start Auto-Measure", ImVec2(200, 30))) {
                        if (m_calibMicIdx >= 0) {
                            m_isPlaying = true;
                            m_isCalibrating = true;
                            // Re-start the audio engine expressly using the Microphone instead of Loopback
                            m_engine.start(m_calibMicIdx, m_pb1Idx, m_pb2Idx, false); 
                            m_engine.setSyncToolMode(SyncToolMode::MicCalibration, m_calibMicIdx);
                        }
                    }
                    if (m_calibMicIdx < 0) ImGui::SameLine(), ImGui::TextDisabled("Select a mic first.");
                    
                    float d1=0, d2=0;
                    if (m_engine.getCalibrationResult(d1, d2)) {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), " Measured: PB1: %.1fms, PB2: %.1fms", d1, d2);
                        ImGui::SameLine();
                        if (ImGui::Button("Apply Result")) {
                            m_delay1 = d1;
                            m_delay2 = d2;
                            needParamUpdate = true;
                            m_engine.setSyncToolMode(SyncToolMode::Off);
                            m_syncToolSelected = 0;
                        }
                    }
                }
            } else if (m_syncToolSelected == 2) { // Phase Nulling
                ImGui::Text("Phase Inversion plays a 400Hz tone through both speakers (Output 2 is inverted).");
                ImGui::TextDisabled("Adjust either delay slider above until the sound perfectly cancels out (quietest point).");
                ImGui::Dummy(ImVec2(0, 5));
                if (m_isPlaying) {
                    bool active = (m_engine.getSyncToolMode() == SyncToolMode::PhaseNulling);
                    if (ImGui::Checkbox("Play 400Hz Nulling Tone", &active)) {
                        m_engine.setSyncToolMode(active ? SyncToolMode::PhaseNulling : SyncToolMode::Off);
                    }
                } else {
                    ImGui::TextDisabled("Start audio streaming to begin.");
                }
            } else if (m_syncToolSelected == 3) { // Metronome
                ImGui::Text("Metronome plays a rhythmic click sequence.");
                ImGui::TextDisabled("Adjust the delay sliders until the double-clicks merge perfectly into one.");
                ImGui::Dummy(ImVec2(0, 5));
                if (m_isPlaying) {
                    bool active = (m_engine.getSyncToolMode() == SyncToolMode::Metronome);
                    if (ImGui::Checkbox("Play Metronome Track", &active)) {
                        m_engine.setSyncToolMode(active ? SyncToolMode::Metronome : SyncToolMode::Off);
                    }
                } else {
                    ImGui::TextDisabled("Start audio streaming to begin.");
                }
            }
        }
        ImGui::EndChild();

        
        if (needRestart && m_isPlaying) {
            applyEngineState();
        } else if (needParamUpdate && m_isPlaying) {
            m_engine.setDelayMs(0, m_delay1);
            m_engine.setDelayMs(1, m_delay2);
            m_engine.setFilterMode(0, static_cast<FilterMode>(m_filter1));
            m_engine.setFilterMode(1, static_cast<FilterMode>(m_filter2));
            m_engine.setVolume(0, m_vol1);
            m_engine.setVolume(1, m_vol2);
            m_engine.setMute(0, m_mute1);
            m_engine.setMute(1, m_mute2);
            m_engine.setPhaseInvert(0, m_invert1);
            m_engine.setPhaseInvert(1, m_invert2);
            m_engine.setCrossoverFreq(m_crossoverFreq);
        }

    }
    ImGui::End();
}

void GUI::renderLoop() {
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"SyncWaveClass", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"SyncWave", WS_OVERLAPPEDWINDOW, 100, 100, 800, 450, nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return;
    }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ApplyCustomStyle();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    bool done = false;
    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done) break;

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        drawUI();

        ImGui::Render();
        const float clear_color_with_alpha[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        
        g_pSwapChain->Present(1, 0); // VSYNC
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
}

// Win32 and DX11 boilerplate below
bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK) return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;

    switch (msg) {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam);
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
