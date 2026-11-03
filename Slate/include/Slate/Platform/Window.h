#pragma once

#include "Slate/Core/Base.h"

struct GLFWwindow;

namespace Slate {

struct WindowProps
{
    std::string Title = "Slate";
    uint32_t Width = 1600;
    uint32_t Height = 900;
    bool VSync = true;
    bool Visible = true;
};

// Platform window backed by GLFW. The window owns no graphics context: Vulkan
// surfaces are created by VulkanContext from the native handle.
class Window
{
public:
    explicit Window(const WindowProps& props = {});
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    void PollEvents();
    bool ShouldClose() const;
    void Close();

    uint32_t GetWidth() const { return m_Data.Width; }
    uint32_t GetHeight() const { return m_Data.Height; }
    float GetAspectRatio() const { return static_cast<float>(m_Data.Width) / static_cast<float>(m_Data.Height); }

    void SetTitle(const std::string& title);
    void SetVSync(bool enabled);
    bool IsVSync() const { return m_Data.VSync; }

    // Cursor handling for editor camera navigation.
    void SetCursorMode(bool captured);
    bool IsCursorCaptured() const { return m_Data.CursorCaptured; }

    GLFWwindow* GetNativeWindow() const { return m_Window; }

    using ResizeCallback = std::function<void(uint32_t width, uint32_t height)>;
    void SetResizeCallback(ResizeCallback callback) { m_ResizeCallback = std::move(callback); }

    bool WasResized() const { return m_Data.Resized; }
    void ClearResizedFlag() { m_Data.Resized = false; }

private:
    void Init(const WindowProps& props);
    void Shutdown();

    GLFWwindow* m_Window = nullptr;
    ResizeCallback m_ResizeCallback;

    struct WindowData
    {
        std::string Title;
        uint32_t Width = 0;
        uint32_t Height = 0;
        bool VSync = true;
        bool CursorCaptured = false;
        bool Resized = false;
    } m_Data;
};

} // namespace Slate
