#pragma once

#include "Slate/Core/Base.h"

#include <glm/glm.hpp>

struct GLFWwindow;

namespace Slate {

// Key codes mirror GLFW's key values so no translation table is required.
enum class Key : int
{
    Space = 32, Apostrophe = 39, Comma = 44, Minus = 45, Period = 46, Slash = 47,
    D0 = 48, D1 = 49, D2 = 50, D3 = 51, D4 = 52, D5 = 53, D6 = 54, D7 = 55, D8 = 56, D9 = 57,
    Semicolon = 59, Equal = 61,
    A = 65, B = 66, C = 67, D = 68, E = 69, F = 70, G = 71, H = 72, I = 73, J = 74,
    K = 75, L = 76, M = 77, N = 78, O = 79, P = 80, Q = 81, R = 82, S = 83, T = 84,
    U = 85, V = 86, W = 87, X = 88, Y = 89, Z = 90,
    LeftBracket = 91, Backslash = 92, RightBracket = 93, GraveAccent = 96,
    Escape = 256, Enter = 257, Tab = 258, Backspace = 259, Insert = 260, Delete = 261,
    Right = 262, Left = 263, Down = 264, Up = 265,
    PageUp = 266, PageDown = 267, Home = 268, End = 269,
    CapsLock = 280, ScrollLock = 281, NumLock = 282, PrintScreen = 283, Pause = 284,
    F1 = 290, F2 = 291, F3 = 292, F4 = 293, F5 = 294, F6 = 295, F7 = 296, F8 = 297,
    F9 = 298, F10 = 299, F11 = 300, F12 = 301,
    LeftShift = 340, LeftControl = 341, LeftAlt = 342, LeftSuper = 343,
    RightShift = 344, RightControl = 345, RightAlt = 346, RightSuper = 347,
    Menu = 348
};

enum class MouseButton : int
{
    Left = 0,
    Right = 1,
    Middle = 2,
    Button4 = 3,
    Button5 = 4
};

// Maps a key name ("A", "Space", "Left", ...) to a Key. Unknown names map to
// Space, matching the scripting API's forgiving behaviour.
Key KeyFromString(const std::string& name);

// Polled keyboard and mouse state. Updated once per frame by Window::PollEvents.
class Input
{
public:
    static void Init(GLFWwindow* window);
    static void Shutdown();
    static void Update();

    static bool IsKeyDown(Key key);
    static bool IsKeyPressed(Key key);
    static bool IsKeyReleased(Key key);

    static bool IsMouseButtonDown(MouseButton button);
    static bool IsMouseButtonPressed(MouseButton button);
    static bool IsMouseButtonReleased(MouseButton button);

    static glm::vec2 GetMousePosition();
    static glm::vec2 GetMouseDelta();
    static float GetScrollDelta();

    // Consumes accumulated scroll so it is only reported once per frame.
    static void ResetScrollDelta();

    // Callbacks invoked by the platform layer.
    static void OnKey(int key, int action);
    static void OnMouseButton(int button, int action);
    static void OnCursorPosition(double x, double y);
    static void OnScroll(double yOffset);

    // Injects input state for automation and tests. Injected keys stay down
    // until cleared, independent of the real keyboard.
    static void SetKeyDown(Key key, bool down);
    static void SetMouseButtonDown(MouseButton button, bool down);
    static void ClearInjectedInput();

private:
    static inline GLFWwindow* s_Window = nullptr;
    static inline std::array<bool, 512> s_KeyDown {};
    static inline std::array<bool, 512> s_KeyPressed {};
    static inline std::array<bool, 512> s_KeyReleased {};
    static inline std::array<bool, 512> s_InjectedKeyDown {};
    static inline std::array<bool, 512> s_InjectedKeyPressed {};
    static inline std::array<bool, 512> s_InjectedKeyReleased {};
    static inline std::array<bool, 8> s_MouseDown {};
    static inline std::array<bool, 8> s_MousePressed {};
    static inline std::array<bool, 8> s_MouseReleased {};
    static inline std::array<bool, 8> s_InjectedMouseDown {};
    static inline std::array<bool, 8> s_InjectedMousePressed {};
    static inline std::array<bool, 8> s_InjectedMouseReleased {};
    static inline glm::vec2 s_MousePosition { 0.0f };
    static inline glm::vec2 s_MouseDelta { 0.0f };
    static inline float s_ScrollDelta = 0.0f;
};

} // namespace Slate
