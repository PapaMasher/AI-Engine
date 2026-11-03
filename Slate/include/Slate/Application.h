#pragma once

#include "Slate/Core/Base.h"
#include "Slate/Core/Timer.h"
#include "Slate/Platform/Window.h"
#include "Slate/Platform/VulkanContext.h"
#include "Slate/Renderer/Renderer.h"
#include "Slate/Scene/Scene.h"
#include "Slate/Physics/PhysicsWorld.h"
#include "Slate/Scripting/ScriptEngine.h"
#include "Slate/Audio/AudioEngine.h"

namespace Slate {

// Base application: owns the window, Vulkan context and renderer, and runs the
// main loop. The editor and runtime executables subclass it to add their own
// frame logic and UI.
class Application
{
public:
    Application(const WindowProps& props, bool enableValidation);
    virtual ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void Run();
    void Close() { m_Running = false; }

    // Play mode: steps physics, scripts and audio.
    void SetPlaying(bool playing);
    bool IsPlaying() const { return m_Playing; }

    // Queues a screenshot to be captured after the next presented frame.
    void RequestScreenshot(const std::string& path);

    Window& GetWindow() { return m_Window; }
    VulkanContext& GetContext() { return m_Context; }
    Renderer& GetRenderer() { return m_Renderer; }
    Scene& GetScene() { return m_Scene; }
    PhysicsWorld& GetPhysics() { return m_Physics; }
    ScriptEngine& GetScripts() { return m_Scripts; }
    AudioEngine& GetAudio() { return m_Audio; }
    const FrameTimer& GetFrameTimer() const { return m_Timer; }

protected:
    virtual void OnStart() {}
    virtual void OnUpdate(double deltaTime) { (void)deltaTime; }
    virtual void OnImGui() {}
    virtual void OnShutdown() {}

private:
    void HandleResize();

    Window m_Window;
    VulkanContext m_Context;
    Renderer m_Renderer;
    Scene m_Scene;
    PhysicsWorld m_Physics;
    ScriptEngine m_Scripts;
    AudioEngine m_Audio;
    FrameTimer m_Timer;
    bool m_Running = true;
    bool m_Started = false;
    bool m_Playing = false;
    std::string m_PendingScreenshot;
};

} // namespace Slate
