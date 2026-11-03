#include <doctest/doctest.h>

#include "Slate/Platform/Input.h"

using namespace Slate;

TEST_CASE("Injected key state is visible to the input API")
{
    Input::ClearInjectedInput();

    CHECK_FALSE(Input::IsKeyDown(Key::Left));

    Input::SetKeyDown(Key::Left, true);
    CHECK(Input::IsKeyDown(Key::Left));
    CHECK(Input::IsKeyPressed(Key::Left));

    // Pressed is an edge; a second frame must not report it again.
    Input::Update();
    CHECK(Input::IsKeyDown(Key::Left));
    CHECK_FALSE(Input::IsKeyPressed(Key::Left));

    Input::SetKeyDown(Key::Left, false);
    CHECK_FALSE(Input::IsKeyDown(Key::Left));
    CHECK(Input::IsKeyReleased(Key::Left));

    Input::Update();
    CHECK_FALSE(Input::IsKeyReleased(Key::Left));
}

TEST_CASE("Injected mouse state is visible to the input API")
{
    Input::ClearInjectedInput();

    Input::SetMouseButtonDown(MouseButton::Left, true);
    CHECK(Input::IsMouseButtonDown(MouseButton::Left));
    CHECK(Input::IsMouseButtonPressed(MouseButton::Left));

    Input::ClearInjectedInput();
    CHECK_FALSE(Input::IsMouseButtonDown(MouseButton::Left));
}

TEST_CASE("Key names map to key codes")
{
    CHECK(KeyFromString("A") == Key::A);
    CHECK(KeyFromString("Space") == Key::Space);
    CHECK(KeyFromString("Left") == Key::Left);
    CHECK(KeyFromString("Up") == Key::Up);
    CHECK(KeyFromString("NotAKey") == Key::Space);
}
