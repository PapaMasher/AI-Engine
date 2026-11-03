#include "Slate/Application.h"
#include "Slate/Core/Log.h"

#include <imgui.h>

#include <cstdlib>

namespace Slate {

Application::Application(const WindowProps& props, bool enableValidation)
    : m_Window(props)
    , m_Context(m_Window, enableValidation)
{
    m_Renderer.Init(m_Context);

    m_Physics.Init();
    m_Scripts.Init(m_Scene, &m_Physics);
    m_Audio.Init();
}

Application::~Application()
{
    SetPlaying(false);
    m_Scripts.Shutdown();
    m_Audio.Shutdown();
    m_Physics.Shutdown();
    m_Context.WaitIdle();
    m_Renderer.Shutdown();
}

void Application::SetPlaying(bool playing)
{
    if (playing == m_Playing)
        return;

    m_Playing = playing;

    if (m_Playing)
    {
        m_Physics.Clear();
        m_Scripts.Start();

        m_Scene.GetRegistry().Each<AudioSourceComponent, TransformComponent>(
            [&](Entity entity, AudioSourceComponent& source, TransformComponent& transform)
            {
                if (source.PlayOnStart && !source.ClipPath.empty())
                {
                    m_Audio.PlaySound(source.ClipPath, source.Volume, source.Loop, source.Spatial,
                        glm::vec3(transform.WorldMatrix[3]));
                }
            });

        SLATE_INFO("Play mode started");
    }
    else
    {
        m_Scripts.Stop();
        m_Audio.StopAll();
        m_Physics.Clear();
        SLATE_INFO("Play mode stopped");
    }
}

void Application::RequestScreenshot(const std::string& path)
{
    m_PendingScreenshot = path;
}

void Application::HandleResize()
{
    if (!m_Window.WasResized())
        return;

    m_Window.ClearResizedFlag();

    if (m_Window.GetWidth() == 0 || m_Window.GetHeight() == 0)
        return;

    m_Context.RecreateSwapchain();
    m_Renderer.Resize(m_Context.GetSwapchainExtent().width, m_Context.GetSwapchainExtent().height);
}

void Application::Run()
{
    if (!m_Started)
    {
        OnStart();
        m_Started = true;
    }

    uint64_t maxFrames = 0;
    if (const char* env = std::getenv("SLATE_MAX_FRAMES"))
        maxFrames = std::strtoull(env, nullptr, 10);
    uint64_t frameCount = 0;

    while (m_Running && !m_Window.ShouldClose())
    {
        if (maxFrames > 0 && frameCount >= maxFrames)
            break;
        ++frameCount;

        m_Window.PollEvents();
        m_Timer.Tick();

        HandleResize();

        if (m_Context.GetSwapchainExtent().width == 0 || m_Context.GetSwapchainExtent().height == 0)
            continue;

        const double deltaTime = m_Timer.GetDeltaTime();

        OnUpdate(deltaTime);

        if (m_Playing)
        {
            m_Physics.Step(m_Scene, deltaTime);
            m_Scripts.Update(deltaTime);

            const CameraData camera = m_Scene.GetCameraData(
                static_cast<float>(m_Context.GetSwapchainExtent().width)
                / static_cast<float>(m_Context.GetSwapchainExtent().height));
            const glm::mat4 view = camera.View;
            const glm::vec3 forward = -glm::normalize(glm::vec3(view[2]));
            const glm::vec3 up = glm::normalize(glm::vec3(view[1]));
            m_Audio.SetListener(camera.Position, forward, up);
            m_Audio.Update();
        }

        m_Context.BeginFrame();
        if (frameCount < 3) SLATE_INFO("stage: begin frame");

        if (m_Context.IsSwapchainOutOfDate())
        {
            m_Context.RecreateSwapchain();
            m_Renderer.Resize(m_Context.GetSwapchainExtent().width, m_Context.GetSwapchainExtent().height);
            continue;
        }

        m_Renderer.Render(m_Scene);
        if (frameCount < 3) SLATE_INFO("stage: scene submitted");

        m_Context.Composite();
        if (frameCount < 3) SLATE_INFO("stage: composite recorded");

        if (m_Context.IsImGuiInitialized())
        {
            m_Context.BeginImGuiFrame();
            OnImGui();
            m_Context.RenderImGui();
            if (frameCount < 3) SLATE_INFO("stage: imgui recorded");
        }

        m_Context.EndFrame();
        if (frameCount < 3) SLATE_INFO("stage: frame submitted");

        if (!m_PendingScreenshot.empty())
        {
            if (m_Context.CaptureScreenshot(m_PendingScreenshot))
                SLATE_INFO("Screenshot written to '{}'", m_PendingScreenshot);
            m_PendingScreenshot.clear();
        }

        if (maxFrames > 0 && frameCount == maxFrames)
        {
            if (const char* screenshotPath = std::getenv("SLATE_SCREENSHOT_PATH"))
            {
                if (m_Context.CaptureScreenshot(screenshotPath))
                    SLATE_INFO("Screenshot written to '{}'", screenshotPath);
            }
        }

        if (m_Window.WasResized())
            HandleResize();
    }

    SetPlaying(false);
    OnShutdown();
    m_Context.WaitIdle();
}

} // namespace Slate
