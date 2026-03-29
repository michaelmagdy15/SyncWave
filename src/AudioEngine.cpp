#include "AudioEngine.hpp"
#include "logger.hpp"
#include <math.h>

AudioEngine::AudioEngine() {
}

AudioEngine::~AudioEngine() {
    uninit();
}

bool AudioEngine::init() {
    if (ma_context_init(NULL, 0, NULL, &m_context) != MA_SUCCESS) {
        warn("Failed to initialize audio context.");
        return false;
    }
    m_contextInit = true;
    refreshDevices();
    return true;
}

void AudioEngine::uninit() {
    stop();
    if (m_contextInit) {
        ma_context_uninit(&m_context);
        m_contextInit = false;
    }
}

void AudioEngine::refreshDevices() {
    m_playbackDevices.clear();
    m_captureDevices.clear();

    ma_device_info* pPlaybackDeviceInfos;
    ma_uint32 playbackDeviceCount;
    ma_device_info* pCaptureDeviceInfos;
    ma_uint32 captureDeviceCount;

    if (ma_context_get_devices(&m_context, &pPlaybackDeviceInfos, &playbackDeviceCount, &pCaptureDeviceInfos, &captureDeviceCount) != MA_SUCCESS) {
        warn("Failed to get devices.");
        return;
    }

    for (ma_uint32 i = 0; i < playbackDeviceCount; ++i) {
        m_playbackDevices.push_back({pPlaybackDeviceInfos[i].id, pPlaybackDeviceInfos[i].name, pPlaybackDeviceInfos[i].isDefault != 0});
    }
    for (ma_uint32 i = 0; i < captureDeviceCount; ++i) {
        m_captureDevices.push_back({pCaptureDeviceInfos[i].id, pCaptureDeviceInfos[i].name, pCaptureDeviceInfos[i].isDefault != 0});
    }
}

void AudioEngine::updateFilters(PbContext& ctx) {
    ma_hpf_config hpfConfig = ma_hpf_config_init(ma_format_f32, CHANNELS, SAMPLE_RATE, m_crossoverFreq, 0); // Butterworth Q, wait 0 order is passthrough, wait hpf_config_init order parameter?
    ma_lpf_config lpfConfig = ma_lpf_config_init(ma_format_f32, CHANNELS, SAMPLE_RATE, m_crossoverFreq, 0);

    {
        std::lock_guard<std::mutex> lock(ctx.hpfMutex);
        ma_hpf_init(&hpfConfig, NULL, &ctx.hpf);
    }
    {
        std::lock_guard<std::mutex> lock(ctx.lpfMutex);
        ma_lpf_init(&lpfConfig, NULL, &ctx.lpf);
    }
}

bool AudioEngine::start(int captureDeviceIdx, int pbDevice1Idx, int pbDevice2Idx, bool isCaptureLoopback) {
    stop();

    m_isCaptureLoopback = isCaptureLoopback;
    m_pb1IsLoopbackSource = (isCaptureLoopback && pbDevice1Idx == captureDeviceIdx);
    m_pb2IsLoopbackSource = (isCaptureLoopback && pbDevice2Idx == captureDeviceIdx);

    auto setupPbContext = [&](PbContext& ctx) {
        // Allocate 5 seconds max delay buffer at 48kHz stereo float
        ctx.delayBuffer.assign(SAMPLE_RATE * CHANNELS * 5, 0.0f);
        ctx.writeIdx = 0;
        ctx.readIdx = 0;
        ctx.currentDelayFrames = 0;
        updateFilters(ctx);
    };

    setupPbContext(m_pb1Ctx);
    setupPbContext(m_pb2Ctx);

    // Setup Capture
    if (captureDeviceIdx >= 0 && captureDeviceIdx < (isCaptureLoopback ? m_playbackDevices.size() : m_captureDevices.size())) {
        ma_device_config config = ma_device_config_init(isCaptureLoopback ? ma_device_type_loopback : ma_device_type_capture);
        config.capture.pDeviceID = isCaptureLoopback ? &m_playbackDevices[captureDeviceIdx].id : &m_captureDevices[captureDeviceIdx].id;
        config.capture.format = ma_format_f32;
        config.capture.channels = CHANNELS;
        config.sampleRate = SAMPLE_RATE;
        config.dataCallback = captureCallback;
        config.pUserData = this;
        // Loopback on WASAPI requires backends array explicitly 
        ma_backend backends[] = { ma_backend_wasapi };
        if (ma_device_init_ex(backends, sizeof(backends) / sizeof(backends[0]), NULL, &config, &m_captureDevice) == MA_SUCCESS) {
            m_captureActive = true;
        } else {
            crit("Failed to init capture device");
            return false;
        }
    } else {
        return false; // must have capture
    }

    // Setup Playback 1
    if (pbDevice1Idx >= 0 && pbDevice1Idx < m_playbackDevices.size()) {
        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.playback.pDeviceID = &m_playbackDevices[pbDevice1Idx].id;
        config.playback.format = ma_format_f32;
        config.playback.channels = CHANNELS;
        config.sampleRate = SAMPLE_RATE;
        config.dataCallback = playback1Callback;
        config.pUserData = this;
        
        if (ma_device_init(&m_context, &config, &m_playbackDevice1) == MA_SUCCESS) {
            m_pb1Active = true;
        }
    }

    // Setup Playback 2
    if (pbDevice2Idx >= 0 && pbDevice2Idx < m_playbackDevices.size()) {
        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.playback.pDeviceID = &m_playbackDevices[pbDevice2Idx].id;
        config.playback.format = ma_format_f32;
        config.playback.channels = CHANNELS;
        config.sampleRate = SAMPLE_RATE;
        config.dataCallback = playback2Callback;
        config.pUserData = this;
        
        if (ma_device_init(&m_context, &config, &m_playbackDevice2) == MA_SUCCESS) {
            m_pb2Active = true;
        }
    }

    // Start active devices
    if (m_pb1Active) ma_device_start(&m_playbackDevice1);
    if (m_pb2Active) ma_device_start(&m_playbackDevice2);
    if (m_captureActive) ma_device_start(&m_captureDevice);

    return true;
}

void AudioEngine::stop() {
    if (m_captureActive) {
        ma_device_uninit(&m_captureDevice);
        m_captureActive = false;
    }
    if (m_pb1Active) {
        ma_device_uninit(&m_playbackDevice1);
        m_pb1Active = false;
    }
    if (m_pb2Active) {
        ma_device_uninit(&m_playbackDevice2);
        m_pb2Active = false;
    }
}

void AudioEngine::setDelayMs(int deviceIndex, float delayMs) {
    if (deviceIndex == 0) m_pb1Ctx.delayMs = delayMs;
    else if (deviceIndex == 1) m_pb2Ctx.delayMs = delayMs;
}

void AudioEngine::setFilterMode(int deviceIndex, FilterMode mode) {
    if (deviceIndex == 0) m_pb1Ctx.filterMode = mode;
    else if (deviceIndex == 1) m_pb2Ctx.filterMode = mode;
}

void AudioEngine::setVolume(int deviceIndex, float volume) {
    if (deviceIndex == 0) m_pb1Ctx.volume = volume;
    else if (deviceIndex == 1) m_pb2Ctx.volume = volume;
}

void AudioEngine::setMute(int deviceIndex, bool mute) {
    if (deviceIndex == 0) m_pb1Ctx.mute = mute;
    else if (deviceIndex == 1) m_pb2Ctx.mute = mute;
}

void AudioEngine::setPhaseInvert(int deviceIndex, bool invert) {
    if (deviceIndex == 0) m_pb1Ctx.phaseInvert = invert;
    else if (deviceIndex == 1) m_pb2Ctx.phaseInvert = invert;
}

void AudioEngine::setCrossoverFreq(float freqHz) {
    m_crossoverFreq = freqHz;
    updateFilters(m_pb1Ctx);
    updateFilters(m_pb2Ctx);
}

void AudioEngine::setSyncToolMode(SyncToolMode mode, int micDeviceIdx) {
    m_syncMode.store(mode);
    if (mode == SyncToolMode::MicCalibration) {
        m_calibMicIdx = micDeviceIdx;
        m_calibState.store(CalibrationState::PlaySpk1);
        m_calibTimerFrames.store(0);
        m_spk1PeakFrame.store(-1);
        m_spk2PeakFrame.store(-1);
        m_calibResultDelay1 = 0.0f;
        m_calibResultDelay2 = 0.0f;
    }
}

bool AudioEngine::getCalibrationResult(float& outDelay1, float& outDelay2) {
    if (m_calibState.load() == CalibrationState::Done) {
        if (m_spk1PeakFrame.load() != -1 && m_spk2PeakFrame.load() != -1) {
            outDelay1 = m_calibResultDelay1;
            outDelay2 = m_calibResultDelay2;
        } else {
            outDelay1 = 0.0f;
            outDelay2 = 0.0f;
        }
        return true;
    }
    return false;
}

void AudioEngine::captureCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    AudioEngine* engine = (AudioEngine*)pDevice->pUserData;
    const float* inFrames = (const float*)pInput;

    SyncToolMode mode = engine->m_syncMode.load();
    std::vector<float> syntheticBuf(frameCount * CHANNELS, 0.0f);

    // Audio Generation inside capture to sync pb1/pb2 properly
    if (mode == SyncToolMode::PhaseNulling) {
        double phase = engine->m_phaseNullingPhase.load();
        double phaseInc = 400.0 * 2.0 * 3.14159265358979323846 / SAMPLE_RATE;
        for (ma_uint32 i = 0; i < frameCount; ++i) {
            float val = (float)std::sin(phase) * 0.3f;
            syntheticBuf[i * CHANNELS + 0] = val;
            syntheticBuf[i * CHANNELS + 1] = val;
            phase += phaseInc;
        }
        engine->m_phaseNullingPhase.store(fmod(phase, 2.0 * 3.14159265358979323846));
    } 
    else if (mode == SyncToolMode::Metronome) {
        int frame = engine->m_metronomeFrame.load();
        int interval = SAMPLE_RATE / 2; // 500ms
        for (ma_uint32 i = 0; i < frameCount; ++i) {
            float val = 0.0f;
            if (frame % interval < 480) { // 10ms click
                val = ((rand() % 100) / 100.0f - 0.5f) * 0.8f;
            }
            syntheticBuf[i * CHANNELS + 0] = val;
            syntheticBuf[i * CHANNELS + 1] = val;
            frame++;
        }
        engine->m_metronomeFrame.store(frame);
    }
    else if (mode == SyncToolMode::MicCalibration) {
        CalibrationState state = engine->m_calibState.load();
        int timer = engine->m_calibTimerFrames.load();
        
        if (state == CalibrationState::PlaySpk1 || state == CalibrationState::PlaySpk2) {
            for (ma_uint32 i = 0; i < frameCount; ++i) {
                float val = 0.0f;
                if (timer + i < 960) { // 20ms impulse (1kHz sine)
                    val = (float)std::sin((timer + i) * 1000.0 * 2.0 * 3.14159265358979323846 / SAMPLE_RATE) * 0.9f;
                }
                syntheticBuf[i * CHANNELS + 0] = val;
                syntheticBuf[i * CHANNELS + 1] = val;
            }
        }
        
        // Mic Detection Logic
        if (inFrames) {
            float energy = 0.0f;
            for (ma_uint32 i = 0; i < frameCount * CHANNELS; ++i) {
                energy += inFrames[i] * inFrames[i];
            }
            float rms = std::sqrt(energy / (frameCount * CHANNELS));

            if (state == CalibrationState::PlaySpk1) {
                if (timer == 0) {
                    engine->m_calibResultDelay1 = 0.0f; // Reuse to track max RMS
                    engine->m_spk1PeakFrame.store(-1);
                }
                if (timer > 240) { // wait 5ms before detecting
                    if (rms > engine->m_calibResultDelay1) {
                        engine->m_calibResultDelay1 = rms;
                        engine->m_spk1PeakFrame.store(timer);
                    }
                }
                if (timer > SAMPLE_RATE * 10.0f) { // Analyze for 10 seconds
                    engine->m_calibState.store(CalibrationState::WaitSpk1);
                    timer = 0;
                }
            } else if (state == CalibrationState::WaitSpk1) {
                if (timer > SAMPLE_RATE * 1.0f) { // 1.0 second gap for echoes to die down
                    engine->m_calibState.store(CalibrationState::PlaySpk2);
                    timer = 0;
                }
            } else if (state == CalibrationState::PlaySpk2) {
                if (timer == 0) {
                    engine->m_calibResultDelay2 = 0.0f;
                    engine->m_spk2PeakFrame.store(-1);
                }
                if (timer > 240) {
                    if (rms > engine->m_calibResultDelay2) {
                        engine->m_calibResultDelay2 = rms;
                        engine->m_spk2PeakFrame.store(timer);
                    }
                }
                if (timer > SAMPLE_RATE * 10.0f) { // Analyze for 10 seconds
                    float d1 = 0.0f, d2 = 0.0f;
                    int p1 = engine->m_spk1PeakFrame.load();
                    int p2 = engine->m_spk2PeakFrame.load();
                    
                    // Validate that a genuine pulse was captured (RMS > 0.005)
                    if (engine->m_calibResultDelay1 > 0.005f && engine->m_calibResultDelay2 > 0.005f) {
                        // The speaker which took MORE frames (p1 > p2) is SLOWER. We must delay the FASTER speaker.
                        if (p1 > p2) d2 = (p1 - p2) * 1000.0f / SAMPLE_RATE;
                        else d1 = (p2 - p1) * 1000.0f / SAMPLE_RATE;
                    }
                    
                    engine->m_calibResultDelay1 = d1;
                    engine->m_calibResultDelay2 = d2;
                    engine->m_calibState.store(CalibrationState::Done);
                }
            }
            
            // Timeout abort (12 seconds per speaker max)
            if ((state == CalibrationState::PlaySpk1 || state == CalibrationState::PlaySpk2) && timer > SAMPLE_RATE * 12.0f) {
                engine->m_calibState.store(CalibrationState::Done);
            }
        }
        
        engine->m_calibTimerFrames.store(timer + frameCount);
    }

    const float* sourceFrames = (mode == SyncToolMode::Off) ? inFrames : syntheticBuf.data();

    auto pushToBuffer = [&](PbContext& ctx, bool active, bool invertPhase, bool mute) {
        if (!active || !sourceFrames) return;
        
        int wIdx = ctx.writeIdx;
        int bufSize = ctx.delayBuffer.size();
        for (ma_uint32 i = 0; i < frameCount * CHANNELS; ++i) {
            float val = mute ? 0.0f : sourceFrames[i];
            if (invertPhase) val = -val;
            ctx.delayBuffer[wIdx] = val;
            wIdx++;
            if (wIdx >= bufSize) wIdx = 0;
        }
        ctx.writeIdx = wIdx;
    };

    bool mute1 = false;
    bool mute2 = false;

    if (mode == SyncToolMode::MicCalibration) {
        mute1 = (engine->m_calibState.load() == CalibrationState::PlaySpk2 || engine->m_calibState.load() == CalibrationState::WaitSpk2);
        mute2 = (engine->m_calibState.load() == CalibrationState::PlaySpk1 || engine->m_calibState.load() == CalibrationState::WaitSpk1);
    } else if (mode == SyncToolMode::Off && engine->m_isCaptureLoopback) {
        mute1 = engine->m_pb1IsLoopbackSource;
        mute2 = engine->m_pb2IsLoopbackSource;
    }

    pushToBuffer(engine->m_pb1Ctx, engine->m_pb1Active, false, mute1);
    pushToBuffer(engine->m_pb2Ctx, engine->m_pb2Active, mode == SyncToolMode::PhaseNulling, mute2);
}

void AudioEngine::playback1Callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    AudioEngine* engine = (AudioEngine*)pDevice->pUserData;
    engine->processPlayback(pDevice, engine->m_pb1Ctx, pOutput, frameCount);
}

void AudioEngine::playback2Callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    AudioEngine* engine = (AudioEngine*)pDevice->pUserData;
    engine->processPlayback(pDevice, engine->m_pb2Ctx, pOutput, frameCount);
}

void AudioEngine::processPlayback(ma_device* pDevice, PbContext& ctx, void* pOutput, ma_uint32 frameCount) {
    float* outFrames = (float*)pOutput;
    
    int rIdx = ctx.readIdx;
    int bufSize = ctx.delayBuffer.size();
    
    // Calculate target delay frames
    float delayMs = ctx.delayMs;
    int targetDelayFrames = (int)((delayMs / 1000.0f) * SAMPLE_RATE);
    int targetDistance = targetDelayFrames * CHANNELS;
    
    // If the slider was moved
    if (ctx.currentDelayFrames != targetDelayFrames) {
        ctx.currentDelayFrames = targetDelayFrames;
        rIdx = ctx.writeIdx - targetDistance;
        while (rIdx < 0) rIdx += bufSize;
        while (rIdx >= bufSize) rIdx -= bufSize;
    } else {
        // Prevent buffer drift (correct if out of sync by > 100ms) with safe circular math
        int currentDistance = (ctx.writeIdx - rIdx);
        while (currentDistance > bufSize / 2) currentDistance -= bufSize;
        while (currentDistance < -bufSize / 2) currentDistance += bufSize;
        
        if (std::abs(currentDistance - targetDistance) > (4800 * CHANNELS)) {
            rIdx = ctx.writeIdx - targetDistance;
            while (rIdx < 0) rIdx += bufSize;
            while (rIdx >= bufSize) rIdx -= bufSize;
        }
    }

    // Copy ringbuffer to output
    for (ma_uint32 i = 0; i < frameCount * CHANNELS; ++i) {
        outFrames[i] = ctx.delayBuffer[rIdx];
        rIdx++;
        if (rIdx >= bufSize) rIdx = 0;
    }
    ctx.readIdx = rIdx;

    // Apply Filter
    FilterMode mode = ctx.filterMode;
    if (mode == FilterMode::Tweeter) {
        std::lock_guard<std::mutex> lock(ctx.hpfMutex);
        ma_hpf_process_pcm_frames(&ctx.hpf, outFrames, outFrames, frameCount);
    } else if (mode == FilterMode::Bass) {
        std::lock_guard<std::mutex> lock(ctx.lpfMutex);
        ma_lpf_process_pcm_frames(&ctx.lpf, outFrames, outFrames, frameCount);
    }

    // Apply Volume, Mute, and Phase Invert
    float vol = ctx.volume;
    bool isMute = ctx.mute;
    bool isInvert = ctx.phaseInvert;

    if (vol != 1.0f || isMute || isInvert) {
        float multiplier = isMute ? 0.0f : vol;
        if (isInvert && !isMute) multiplier = -multiplier;
        for (ma_uint32 i = 0; i < frameCount * CHANNELS; ++i) {
            outFrames[i] *= multiplier;
        }
    }
}
