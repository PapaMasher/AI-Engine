#pragma once

#include "Slate/Core/Base.h"

#include <glm/glm.hpp>

namespace Slate {

// miniaudio-backed playback engine with optional 3D spatialization.
class AudioEngine
{
public:
    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    void Init();
    void Shutdown();
    bool IsInitialized() const { return m_Impl != nullptr; }

    void PlaySound(const std::string& path, float volume = 1.0f, bool loop = false,
        bool spatial = false, const glm::vec3& position = glm::vec3(0.0f));

    void StopAll();

    // Updates the listener used for spatialized sounds.
    void SetListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up);

    // Releases finished sounds; call once per frame.
    void Update();

    size_t GetActiveSoundCount() const;

private:
    struct Impl;
    Scope<Impl> m_Impl;
};

} // namespace Slate
