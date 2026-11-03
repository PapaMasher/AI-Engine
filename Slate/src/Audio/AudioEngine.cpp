#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include "Slate/Audio/AudioEngine.h"
#include "Slate/Core/FileSystem.h"
#include "Slate/Core/Log.h"

#include <vector>

namespace Slate {

struct AudioEngine::Impl
{
    ma_engine Engine {};
    std::vector<ma_sound*> Sounds;
    bool Initialized = false;
};

AudioEngine::AudioEngine() = default;
AudioEngine::~AudioEngine() { Shutdown(); }

void AudioEngine::Init()
{
    if (m_Impl)
        return;

    m_Impl = MakeScope<Impl>();

    ma_engine_config config = ma_engine_config_init();
    if (ma_engine_init(&config, &m_Impl->Engine) != MA_SUCCESS)
    {
        SLATE_WARN("Audio device could not be initialized; running without sound");
        m_Impl.reset();
        return;
    }

    m_Impl->Initialized = true;
    SLATE_INFO("Audio engine initialized");
}

void AudioEngine::Shutdown()
{
    if (!m_Impl)
        return;

    StopAll();

    if (m_Impl->Initialized)
        ma_engine_uninit(&m_Impl->Engine);

    m_Impl.reset();
}

void AudioEngine::PlaySound(const std::string& path, float volume, bool loop, bool spatial,
    const glm::vec3& position)
{
    if (!m_Impl || !m_Impl->Initialized)
        return;

    const std::filesystem::path resolved = FileSystem::ResolveAssetPath(path);
    if (!FileSystem::Exists(resolved))
    {
        SLATE_WARN("Sound '{}' not found", resolved.string());
        return;
    }

    auto* sound = new ma_sound();
    if (ma_sound_init_from_file(&m_Impl->Engine, resolved.string().c_str(), MA_SOUND_FLAG_DECODE,
            nullptr, nullptr, sound) != MA_SUCCESS)
    {
        SLATE_WARN("Failed to load sound '{}'", resolved.string());
        delete sound;
        return;
    }

    ma_sound_set_volume(sound, volume);
    ma_sound_set_looping(sound, loop ? MA_TRUE : MA_FALSE);

    if (spatial)
    {
        ma_sound_set_spatialization_enabled(sound, MA_TRUE);
        ma_sound_set_position(sound, position.x, position.y, position.z);
        ma_sound_set_min_distance(sound, 0.5f);
        ma_sound_set_max_distance(sound, 100.0f);
        ma_sound_set_rolloff(sound, 1.0f);
    }
    else
    {
        ma_sound_set_spatialization_enabled(sound, MA_FALSE);
    }

    ma_sound_start(sound);
    m_Impl->Sounds.push_back(sound);
}

void AudioEngine::StopAll()
{
    if (!m_Impl)
        return;

    for (ma_sound* sound : m_Impl->Sounds)
    {
        ma_sound_stop(sound);
        ma_sound_uninit(sound);
        delete sound;
    }
    m_Impl->Sounds.clear();
}

void AudioEngine::SetListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up)
{
    if (!m_Impl || !m_Impl->Initialized)
        return;

    ma_engine_listener_set_position(&m_Impl->Engine, 0, position.x, position.y, position.z);
    ma_engine_listener_set_direction(&m_Impl->Engine, 0, forward.x, forward.y, forward.z);
    ma_engine_listener_set_world_up(&m_Impl->Engine, 0, up.x, up.y, up.z);
}

void AudioEngine::Update()
{
    if (!m_Impl)
        return;

    for (auto it = m_Impl->Sounds.begin(); it != m_Impl->Sounds.end();)
    {
        ma_sound* sound = *it;
        if (ma_sound_at_end(sound))
        {
            ma_sound_uninit(sound);
            delete sound;
            it = m_Impl->Sounds.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

size_t AudioEngine::GetActiveSoundCount() const
{
    return m_Impl ? m_Impl->Sounds.size() : 0;
}

} // namespace Slate
