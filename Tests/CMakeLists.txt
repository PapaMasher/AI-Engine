add_executable(SlateTests
    TestMain.cpp
    CoreTests.cpp
    AssetTests.cpp
    ECSTests.cpp
    SceneTests.cpp
    PhysicsTests.cpp
    ScriptingTests.cpp)

target_link_libraries(SlateTests PRIVATE Slate doctest)

add_executable(SlateIntegrationTests IntegrationTests.cpp)
target_link_libraries(SlateIntegrationTests PRIVATE Slate doctest)
target_include_directories(SlateIntegrationTests PRIVATE ${CMAKE_SOURCE_DIR}/third_party/stb)

add_test(NAME SlateTests COMMAND SlateTests)
add_test(NAME SlateIntegrationTests COMMAND SlateIntegrationTests)
