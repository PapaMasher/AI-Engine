#include "Slate/Platform/Window.h"
#include "Slate/Core/Log.h"
#include "Slate/Platform/Input.h"

#include <GLFW/glfw3.h>

namespace Slate {

namespace {

void GLFWErrorCallback(int error, const char* description)
{
    SLATE_ERROR("GLFW error ({}): {}", error, description);
}

} // namespace

Window::Window(const WindowProps& props)
{
    Init(props);
}

Window::~Window()
{
    Shutdown();
}

void Window::Init(const WindowProps& props)
{
    m_Data.Title = props.Title;
    m_Data.Width = props.Width;
    m_Data.Height = props.Height;
    m_Data.VSync = props.VSync;

    if (!glfwInit())
    {
        SLATE_CRITICAL("Failed to initialize GLFW");
        return;
    }

    glfwSetErrorCallback(GLFWErrorCallback);

    // Slate renders with Vulkan through NVRHI; no OpenGL context is created.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, props.Visible ? GLFW_TRUE : GLFW_FALSE);

    m_Window = glfwCreateWindow(static_cast<int>(props.Width), static_cast<int>(props.Height),
        props.Title.c_str(), nullptr, nullptr);
    if (!m_Window)
    {
        SLATE_CRITICAL("Failed to create GLFW window");
        glfwTerminate();
        return;
    }

    glfwSetWindowUserPointer(m_Window, this);

    glfwSetFramebufferSizeCallback(m_Window, [](GLFWwindow* window, int width, int height)
    {
        Window* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
        if (!self)
            return;

        self->m_Data.Width = static_cast<uint32_t>(width);
        self->m_Data.Height = static_cast<uint32_t>(height);
        self->m_Data.Resized = true;

        if (self->m_ResizeCallback)
            self->m_ResizeCallback(self->m_Data.Width, self->m_Data.Height);
    });

    Input::Init(m_Window);

    SLATE_INFO("Window created: {} ({}x{})", props.Title, props.Width, props.Height);
}

void Window::Shutdown()
{
    if (m_Window)
    {
        Input::Shutdown();
        glfwDestroyWindow(m_Window);
        m_Window = nullptr;
    }
    glfwTerminate();
}

void Window::PollEvents()
{
    Input::Update();
    glfwPollEvents();
}

bool Window::ShouldClose() const
{
    return m_Window ? glfwWindowShouldClose(m_Window) : true;
}

void Window::Close()
{
    if (m_Window)
        glfwSetWindowShouldClose(m_Window, GLFW_TRUE);
}

void Window::SetTitle(const std::string& title)
{
    m_Data.Title = title;
    if (m_Window)
        glfwSetWindowTitle(m_Window, title.c_str());
}

void Window::SetVSync(bool enabled)
{
    m_Data.VSync = enabled;
}

void Window::SetCursorMode(bool captured)
{
    if (!m_Window)
        return;

    m_Data.CursorCaptured = captured;
    glfwSetInputMode(m_Window, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

} // namespace Slate
