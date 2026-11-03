#include "Slate/Platform/Input.h"

#include <GLFW/glfw3.h>

#include <string>
#include <unordered_map>

namespace Slate {

namespace {

void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    (void)window; (void)scancode; (void)mods;
    Input::OnKey(key, action);
}

void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
    (void)window; (void)mods;
    Input::OnMouseButton(button, action);
}

void CursorPositionCallback(GLFWwindow* window, double x, double y)
{
    (void)window;
    Input::OnCursorPosition(x, y);
}

void ScrollCallback(GLFWwindow* window, double xOffset, double yOffset)
{
    (void)window; (void)xOffset;
    Input::OnScroll(yOffset);
}

} // namespace

Key KeyFromString(const std::string& name)
{
    static const std::unordered_map<std::string, Key> keys = {
        { "A", Key::A }, { "B", Key::B }, { "C", Key::C }, { "D", Key::D },
        { "E", Key::E }, { "F", Key::F }, { "G", Key::G }, { "H", Key::H },
        { "I", Key::I }, { "J", Key::J }, { "K", Key::K }, { "L", Key::L },
        { "M", Key::M }, { "N", Key::N }, { "O", Key::O }, { "P", Key::P },
        { "Q", Key::Q }, { "R", Key::R }, { "S", Key::S }, { "T", Key::T },
        { "U", Key::U }, { "V", Key::V }, { "W", Key::W }, { "X", Key::X },
        { "Y", Key::Y }, { "Z", Key::Z },
        { "Space", Key::Space }, { "Enter", Key::Enter }, { "Escape", Key::Escape },
        { "LeftShift", Key::LeftShift }, { "LeftControl", Key::LeftControl },
        { "LeftAlt", Key::LeftAlt }, { "Up", Key::Up }, { "Down", Key::Down },
        { "Left", Key::Left }, { "Right", Key::Right }
    };

    const auto it = keys.find(name);
    return it != keys.end() ? it->second : Key::Space;
}

void Input::OnKey(int key, int action)
{
    if (key < 0 || key >= static_cast<int>(s_KeyDown.size()))
        return;

    if (action == GLFW_PRESS || action == GLFW_REPEAT)
    {
        if (!s_KeyDown[key])
            s_KeyPressed[key] = true;
        s_KeyDown[key] = true;
    }
    else if (action == GLFW_RELEASE)
    {
        s_KeyDown[key] = false;
        s_KeyReleased[key] = true;
    }
}

void Input::OnMouseButton(int button, int action)
{
    if (button < 0 || button >= static_cast<int>(s_MouseDown.size()))
        return;

    if (action == GLFW_PRESS)
    {
        s_MouseDown[button] = true;
        s_MousePressed[button] = true;
    }
    else if (action == GLFW_RELEASE)
    {
        s_MouseDown[button] = false;
        s_MouseReleased[button] = true;
    }
}

void Input::OnCursorPosition(double x, double y)
{
    s_MouseDelta.x += static_cast<float>(x) - s_MousePosition.x;
    s_MouseDelta.y += static_cast<float>(y) - s_MousePosition.y;
    s_MousePosition = { static_cast<float>(x), static_cast<float>(y) };
}

void Input::OnScroll(double yOffset)
{
    s_ScrollDelta += static_cast<float>(yOffset);
}

void Input::Init(GLFWwindow* window)
{
    s_Window = window;
    s_KeyDown.fill(false);
    s_KeyPressed.fill(false);
    s_KeyReleased.fill(false);
    s_MouseDown.fill(false);
    s_MousePressed.fill(false);
    s_MouseReleased.fill(false);

    glfwSetKeyCallback(window, KeyCallback);
    glfwSetMouseButtonCallback(window, MouseButtonCallback);
    glfwSetCursorPosCallback(window, CursorPositionCallback);
    glfwSetScrollCallback(window, ScrollCallback);

    double x = 0.0, y = 0.0;
    glfwGetCursorPos(window, &x, &y);
    s_MousePosition = { static_cast<float>(x), static_cast<float>(y) };
    s_MouseDelta = { 0.0f, 0.0f };
}

void Input::Shutdown()
{
    s_Window = nullptr;
}

void Input::Update()
{
    s_KeyPressed.fill(false);
    s_KeyReleased.fill(false);
    s_MousePressed.fill(false);
    s_MouseReleased.fill(false);
    s_InjectedKeyPressed.fill(false);
    s_InjectedKeyReleased.fill(false);
    s_InjectedMousePressed.fill(false);
    s_InjectedMouseReleased.fill(false);
    s_MouseDelta = { 0.0f, 0.0f };
    s_ScrollDelta = 0.0f;

    if (!s_Window)
        return;

    // Poll only the keys GLFW actually knows about (space through the last
    // named key). The callbacks above keep the state current; polling covers
    // focus changes that suppress callbacks.
    for (int key = GLFW_KEY_SPACE; key <= GLFW_KEY_LAST; ++key)
    {
        const int state = glfwGetKey(s_Window, key);
        if (state == GLFW_PRESS)
            s_KeyDown[key] = true;
        else if (state == GLFW_RELEASE)
            s_KeyDown[key] = false;
    }
}

bool Input::IsKeyDown(Key key)
{
    const int code = static_cast<int>(key);
    return code >= 0 && code < static_cast<int>(s_KeyDown.size())
        && (s_KeyDown[code] || s_InjectedKeyDown[code]);
}

bool Input::IsKeyPressed(Key key)
{
    const int code = static_cast<int>(key);
    return code >= 0 && code < static_cast<int>(s_KeyPressed.size())
        && (s_KeyPressed[code] || s_InjectedKeyPressed[code]);
}

bool Input::IsKeyReleased(Key key)
{
    const int code = static_cast<int>(key);
    return code >= 0 && code < static_cast<int>(s_KeyReleased.size())
        && (s_KeyReleased[code] || s_InjectedKeyReleased[code]);
}

bool Input::IsMouseButtonDown(MouseButton button)
{
    const int code = static_cast<int>(button);
    return code >= 0 && code < static_cast<int>(s_MouseDown.size())
        && (s_MouseDown[code] || s_InjectedMouseDown[code]);
}

bool Input::IsMouseButtonPressed(MouseButton button)
{
    const int code = static_cast<int>(button);
    return code >= 0 && code < static_cast<int>(s_MousePressed.size())
        && (s_MousePressed[code] || s_InjectedMousePressed[code]);
}

bool Input::IsMouseButtonReleased(MouseButton button)
{
    const int code = static_cast<int>(button);
    return code >= 0 && code < static_cast<int>(s_MouseReleased.size())
        && (s_MouseReleased[code] || s_InjectedMouseReleased[code]);
}

void Input::SetKeyDown(Key key, bool down)
{
    const int code = static_cast<int>(key);
    if (code < 0 || code >= static_cast<int>(s_InjectedKeyDown.size()))
        return;

    if (down && !s_InjectedKeyDown[code])
        s_InjectedKeyPressed[code] = true;
    else if (!down && s_InjectedKeyDown[code])
        s_InjectedKeyReleased[code] = true;

    s_InjectedKeyDown[code] = down;
}

void Input::SetMouseButtonDown(MouseButton button, bool down)
{
    const int code = static_cast<int>(button);
    if (code < 0 || code >= static_cast<int>(s_InjectedMouseDown.size()))
        return;

    if (down && !s_InjectedMouseDown[code])
        s_InjectedMousePressed[code] = true;
    else if (!down && s_InjectedMouseDown[code])
        s_InjectedMouseReleased[code] = true;

    s_InjectedMouseDown[code] = down;
}

void Input::ClearInjectedInput()
{
    s_InjectedKeyDown.fill(false);
    s_InjectedMouseDown.fill(false);
    s_InjectedKeyPressed.fill(false);
    s_InjectedKeyReleased.fill(false);
    s_InjectedMousePressed.fill(false);
    s_InjectedMouseReleased.fill(false);
}

glm::vec2 Input::GetMousePosition() { return s_MousePosition; }
glm::vec2 Input::GetMouseDelta() { return s_MouseDelta; }
float Input::GetScrollDelta() { return s_ScrollDelta; }
void Input::ResetScrollDelta() { s_ScrollDelta = 0.0f; }

} // namespace Slate
