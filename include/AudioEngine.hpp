#pragma once

#ifndef _ALLOW_COMPILER_AND_STL_VERSION_MISMATCH
#define _ALLOW_COMPILER_AND_STL_VERSION_MISMATCH
#endif

#include "miniaudio.hpp"
#include <string>
#include <vector>
#include <mutex>
#include <atomic>

enum class FilterMode {
    FullRange = 0,
    Tweeter,
    Bass
};

enum class SyncToolMode {
    Off = 0,
    MicCalibration,
    PhaseNulling,
    Metronome
};

enum class CalibrationState {
    Idle = 0,
    PlaySpk1,
    WaitSpk1,
    PlaySpk2,
    WaitSpk2,
    Done
};

struct DeviceInfo {
    ma_device_id id;
    std::string name;
    bool isDefault;
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    bool init();
    void uninit();

    // Get available devices
    const std::vector<DeviceInfo>& getPlaybackDevices() const { return m_playbackDevices; }
    const std::vector<DeviceInfo>& getCaptureDevices() const { return m_captureDevices; }

    // Start streaming from capture to up to 2 playback devices
    // Pass -1 to disable a specific playback device
    bool start(int captureDeviceIdx, int pbDevice1Idx, int pbDevice2Idx, bool isCaptureLoopback);
    void stop();

    // Real-time parameter updates
    void setDelayMs(int deviceIndex, float delayMs); // deviceIndex: 0 for PB1, 1 for PB2
    void setFilterMode(int deviceIndex, FilterMode mode);
    void setVolume(int deviceIndex, float volume); // 0.0 to 1.0
    void setCrossoverFreq(float freqHz);
    void setMute(int deviceIndex, bool mute);
    void setPhaseInvert(int deviceIndex, bool invert);

    // Sync Tools
    void setSyncToolMode(SyncToolMode mode, int micDeviceIdx = -1);
    SyncToolMode getSyncToolMode() const { return m_syncMode; }
    CalibrationState getCalibrationState() const { return m_calibState; }
    bool getCalibrationResult(float& outDelay1, float& outDelay2);

    bool m_isCaptureLoopback = false;
    bool m_pb1IsLoopbackSource = false;
    bool m_pb2IsLoopbackSource = false;

private:
    ma_context m_context;
    bool m_contextInit = false;

    std::vector<DeviceInfo> m_playbackDevices;
    std::vector<DeviceInfo> m_captureDevices;

    ma_device m_captureDevice;
    ma_device m_playbackDevice1;
    ma_device m_playbackDevice2;
    
    bool m_captureActive = false;
    bool m_pb1Active = false;
    bool m_pb2Active = false;

    // Buffer and processing formats
    static const int SAMPLE_RATE = 48000;
    static const int CHANNELS = 2;

    struct PbContext {
        std::atomic<float> delayMs{0.0f};
        std::atomic<float> volume{1.0f};
        std::atomic<FilterMode> filterMode{FilterMode::FullRange};
        std::atomic<bool> mute{false};
        std::atomic<bool> phaseInvert{false};
        
        std::mutex hpfMutex;
        ma_hpf hpf;
        std::mutex lpfMutex;
        ma_lpf lpf;

        // Circular buffer for delay
        std::vector<float> delayBuffer;
        int writeIdx = 0;
        int readIdx = 0;
        int currentDelayFrames = 0;
    };

    PbContext m_pb1Ctx;
    PbContext m_pb2Ctx;
    std::atomic<float> m_crossoverFreq{1000.0f};

    // Sync Tool States
    std::atomic<SyncToolMode> m_syncMode{SyncToolMode::Off};
    std::atomic<CalibrationState> m_calibState{CalibrationState::Idle};
    
    // Generators
    std::atomic<double> m_phaseNullingPhase{0.0};
    std::atomic<int> m_metronomeFrame{0};
    
    // Auto-Calibration Data
    int m_calibMicIdx = -1;
    std::atomic<int> m_calibTimerFrames{0};
    std::atomic<int> m_spk1PeakFrame{-1};
    std::atomic<int> m_spk2PeakFrame{-1};
    float m_calibResultDelay1 = 0.0f;
    float m_calibResultDelay2 = 0.0f;

    void refreshDevices();
    
    static void captureCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);
    static void playback1Callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);
    static void playback2Callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);

    void processPlayback(ma_device* pDevice, PbContext& ctx, void* pOutput, ma_uint32 frameCount);
    void updateFilters(PbContext& ctx);
};
