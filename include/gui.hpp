#pragma once

#include "AudioEngine.hpp"

class GUI {
public:
    GUI(AudioEngine& engine);
    ~GUI();

    bool init();
    void uninit();
    void renderLoop();

private:
    AudioEngine& m_engine;
    
    // UI State
    int m_captureIdx = -1;
    int m_pb1Idx = -1;
    int m_pb2Idx = -1;
    bool m_isLoopback = true;
    bool m_isCalibrating = false;

    float m_delay1 = 0.0f;
    float m_delay2 = 0.0f;
    
    int m_filter1 = 0;
    int m_filter2 = 0;

    float m_vol1 = 1.0f;
    float m_vol2 = 1.0f;
    
    bool m_mute1 = false;
    bool m_mute2 = false;
    bool m_invert1 = false;
    bool m_invert2 = false;

    float m_crossoverFreq = 1000.0f;

    bool m_isPlaying = false;

    // Sync Wizard UI State
    int m_syncToolSelected = 0; // 0=Off, 1=AutoMic, 2=PhaseNulling, 3=Metronome
    int m_calibMicIdx = -1;
    bool m_isSyncToolActive = false;

    void drawUI();
    void applyEngineState();
};
