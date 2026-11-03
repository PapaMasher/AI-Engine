#include "Slate/Application.h"
#include "Slate/Core/Log.h"
#include "Slate/Core/CrashHandler.h"
#include "Slate/Core/FileSystem.h"
#include "Slate/Platform/Input.h"
#include "Slate/Editor/AutomationServer.h"
#include "Slate/Editor/GameExporter.h"
#include "Slate/Core/CommandLine.h"

#include <iostream>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <glm/glm.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace {

using namespace Slate;

class SlateEditor : public Application
{
public:
    explicit SlateEditor(std::string scenePath)
        : Application({ "Slate Editor", 1600, 900 }, true)
        , m_StartupScene(std::move(scenePath))
    {
        GetContext().InitImGui();
    }

protected:
    void OnStart() override
    {
        SLATE_INFO("Slate editor started; asset root: {}", FileSystem::GetAssetRoot().string());

        m_ScenePath = m_StartupScene;

        if (!GetScene().Load(m_ScenePath))
        {
            SLATE_WARN("Could not load '{}', starting with an empty scene", m_ScenePath);
            GetScene().SetName("Untitled");
        }

        uint16_t automationPort = 45800;
        if (const char* envPort = std::getenv("SLATE_AUTOMATION_PORT"))
            automationPort = static_cast<uint16_t>(std::strtoul(envPort, nullptr, 10));

        m_Automation.Start(automationPort, [this](const nlohmann::json& request)
        {
            return HandleAutomation(request);
        });
    }

    void OnShutdown() override
    {
        m_Automation.Stop();
    }

    void OnUpdate(double deltaTime) override
    {
        UpdateCamera(deltaTime);

        if (Input::IsKeyPressed(Key::F5))
            SetPlaying(!IsPlaying());

        m_Automation.Poll();
    }

    void OnImGui() override
    {
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

        DrawMenuBar();
        DrawHierarchy();
        DrawInspector();
        DrawStatistics();
        DrawGizmo();
    }

private:
    void DrawMenuBar()
    {
        if (!ImGui::BeginMainMenuBar())
            return;

        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("New Scene"))
            {
                GetScene().SetName("Untitled");
                m_Selected = Entity();
            }
            if (ImGui::MenuItem("Open Test Scene"))
            {
                GetScene().Load("Scenes/TestScene.json");
                m_Selected = Entity();
            }
            if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
                GetScene().Save(m_ScenePath);
            if (ImGui::MenuItem("Save Scene As..."))
                GetScene().Save("Scenes/SavedScene.json");
            if (ImGui::MenuItem("Export Game..."))
            {
                ExportOptions options;
                options.Name = GetScene().GetName();
                options.ScenePath = m_ScenePath;
                std::string error;
                if (!ExportGame(GetScene(), options, error))
                    SLATE_ERROR("Export failed: {}", error);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit"))
                Close();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Entity"))
        {
            if (ImGui::MenuItem("Create Empty"))
                m_Selected = GetScene().CreateEntity("Entity");
            if (ImGui::MenuItem("Create Camera"))
            {
                m_Selected = GetScene().CreateEntity("Camera");
                m_Selected.AddComponent<CameraComponent>();
            }
            if (ImGui::MenuItem("Create Directional Light"))
            {
                m_Selected = GetScene().CreateEntity("Directional Light");
                m_Selected.AddComponent<DirectionalLightComponent>();
            }
            if (ImGui::MenuItem("Create Point Light"))
            {
                m_Selected = GetScene().CreateEntity("Point Light");
                m_Selected.AddComponent<PointLightComponent>();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete Selected", nullptr, false, m_Selected.IsValid()))
            {
                GetScene().DestroyEntity(m_Selected);
                m_Selected = Entity();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View"))
        {
            ImGui::MenuItem("Demo Window", nullptr, &m_ShowDemo);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Automation"))
        {
            ImGui::Text("Status: %s", m_Automation.IsRunning() ? "listening" : "off");
            ImGui::Text("Port: %u", m_Automation.GetPort());
            ImGui::TextDisabled("Newline-delimited JSON over TCP");
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Play"))
        {
            if (ImGui::MenuItem(IsPlaying() ? "Stop" : "Play", "F5"))
                SetPlaying(!IsPlaying());
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    void DrawHierarchy()
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(
            ImVec2(viewport->WorkPos.x + 10.0f, viewport->WorkPos.y + 34.0f),
            ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(320.0f, 420.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Hierarchy");

        if (ImGui::Button("Create Entity"))
            m_Selected = GetScene().CreateEntity("Entity");
        ImGui::SameLine();
        if (ImGui::Button("Delete") && m_Selected.IsValid())
        {
            GetScene().DestroyEntity(m_Selected);
            m_Selected = Entity();
        }

        ImGui::Separator();

        GetScene().GetRegistry().Each<TagComponent>([&](Entity entity, TagComponent& tag)
        {
            ImGui::PushID(static_cast<int>(entity.GetID()));
            const bool selected = (m_Selected == entity);
            if (ImGui::Selectable(tag.Name.c_str(), selected))
                m_Selected = entity;
            ImGui::PopID();
        });

        ImGui::End();
    }

    void DrawInspector()
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(
            ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 360.0f, viewport->WorkPos.y + 34.0f),
            ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(350.0f, 520.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Inspector");

        if (!m_Selected.IsValid())
        {
            ImGui::TextDisabled("No entity selected");
            ImGui::End();
            return;
        }

        if (m_Selected.HasComponent<TagComponent>())
        {
            auto& tag = m_Selected.GetComponent<TagComponent>();
            char nameBuffer[256];
            std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", tag.Name.c_str());
            if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer)))
                tag.Name = nameBuffer;
        }

        ImGui::Separator();

        if (m_Selected.HasComponent<TransformComponent>())
        {
            auto& transform = m_Selected.GetComponent<TransformComponent>();
            ImGui::Text("Transform");
            ImGui::DragFloat3("Translation", &transform.Translation.x, 0.05f);
            ImGui::DragFloat3("Rotation", &transform.Rotation.x, 0.5f);
            ImGui::DragFloat3("Scale", &transform.Scale.x, 0.05f, 0.001f, 1000.0f);
            ImGui::Separator();
        }

        if (m_Selected.HasComponent<MeshRendererComponent>())
        {
            auto& renderer = m_Selected.GetComponent<MeshRendererComponent>();
            ImGui::Text("Mesh Renderer");

            char modelBuffer[512];
            std::snprintf(modelBuffer, sizeof(modelBuffer), "%s", renderer.ModelPath.c_str());
            if (ImGui::InputText("Model", modelBuffer, sizeof(modelBuffer)))
                renderer.ModelPath = modelBuffer;

            ImGui::ColorEdit4("Tint", &renderer.Tint.x);
            ImGui::Checkbox("Visible", &renderer.Visible);
            ImGui::Checkbox("Cast Shadows", &renderer.CastShadows);
            ImGui::Separator();
        }

        if (m_Selected.HasComponent<CameraComponent>())
        {
            auto& camera = m_Selected.GetComponent<CameraComponent>();
            ImGui::Text("Camera");
            ImGui::DragFloat("Field of View", &camera.FieldOfView, 0.5f, 10.0f, 170.0f);
            ImGui::DragFloat("Near Clip", &camera.NearClip, 0.01f, 0.001f, 10.0f);
            ImGui::DragFloat("Far Clip", &camera.FarClip, 1.0f, 1.0f, 10000.0f);
            ImGui::Checkbox("Primary", &camera.Primary);
            ImGui::Separator();
        }

        if (m_Selected.HasComponent<DirectionalLightComponent>())
        {
            auto& light = m_Selected.GetComponent<DirectionalLightComponent>();
            ImGui::Text("Directional Light");
            ImGui::DragFloat3("Direction", &light.Direction.x, 0.02f);
            ImGui::ColorEdit3("Color", &light.Color.x);
            ImGui::DragFloat("Intensity", &light.Intensity, 0.05f, 0.0f, 100.0f);
            ImGui::Checkbox("Cast Shadows", &light.CastShadows);
            ImGui::Separator();
        }

        if (m_Selected.HasComponent<PointLightComponent>())
        {
            auto& light = m_Selected.GetComponent<PointLightComponent>();
            ImGui::Text("Point Light");
            ImGui::ColorEdit3("Color", &light.Color.x);
            ImGui::DragFloat("Intensity", &light.Intensity, 0.05f, 0.0f, 100.0f);
            ImGui::DragFloat("Range", &light.Range, 0.1f, 0.1f, 1000.0f);
            ImGui::Separator();
        }

        if (m_Selected.HasComponent<RigidBodyComponent>())
        {
            auto& body = m_Selected.GetComponent<RigidBodyComponent>();
            ImGui::Text("Rigid Body");
            const char* types[] = { "Static", "Dynamic", "Kinematic" };
            int current = static_cast<int>(body.Type);
            if (ImGui::Combo("Type", &current, types, IM_ARRAYSIZE(types)))
                body.Type = static_cast<BodyType>(current);
            ImGui::DragFloat("Mass", &body.Mass, 0.05f, 0.01f, 10000.0f);
            ImGui::DragFloat("Friction", &body.Friction, 0.01f, 0.0f, 2.0f);
            ImGui::DragFloat("Restitution", &body.Restitution, 0.01f, 0.0f, 1.0f);
            ImGui::Checkbox("Use Gravity", &body.UseGravity);
            ImGui::Separator();
        }

        if (m_Selected.HasComponent<BoxColliderComponent>())
        {
            auto& collider = m_Selected.GetComponent<BoxColliderComponent>();
            ImGui::Text("Box Collider");
            ImGui::DragFloat3("Half Extents", &collider.HalfExtents.x, 0.01f, 0.001f, 1000.0f);
            ImGui::Separator();
        }

        if (m_Selected.HasComponent<SphereColliderComponent>())
        {
            auto& collider = m_Selected.GetComponent<SphereColliderComponent>();
            ImGui::Text("Sphere Collider");
            ImGui::DragFloat("Radius", &collider.Radius, 0.01f, 0.001f, 1000.0f);
            ImGui::Separator();
        }

        if (m_Selected.HasComponent<CapsuleColliderComponent>())
        {
            auto& collider = m_Selected.GetComponent<CapsuleColliderComponent>();
            ImGui::Text("Capsule Collider");
            ImGui::DragFloat("Radius", &collider.Radius, 0.01f, 0.001f, 100.0f);
            ImGui::DragFloat("Half Height", &collider.HalfHeight, 0.01f, 0.001f, 100.0f);
            ImGui::Separator();
        }

        if (m_Selected.HasComponent<ScriptComponent>())
        {
            auto& script = m_Selected.GetComponent<ScriptComponent>();
            ImGui::Text("Script");
            char pathBuffer[512];
            std::snprintf(pathBuffer, sizeof(pathBuffer), "%s", script.ScriptPath.c_str());
            if (ImGui::InputText("Script", pathBuffer, sizeof(pathBuffer)))
                script.ScriptPath = pathBuffer;
            ImGui::Separator();
        }

        if (m_Selected.HasComponent<AudioSourceComponent>())
        {
            auto& audio = m_Selected.GetComponent<AudioSourceComponent>();
            ImGui::Text("Audio Source");
            char clipBuffer[512];
            std::snprintf(clipBuffer, sizeof(clipBuffer), "%s", audio.ClipPath.c_str());
            if (ImGui::InputText("Clip", clipBuffer, sizeof(clipBuffer)))
                audio.ClipPath = clipBuffer;
            ImGui::DragFloat("Volume", &audio.Volume, 0.01f, 0.0f, 1.0f);
            ImGui::Checkbox("Loop", &audio.Loop);
            ImGui::Checkbox("Play On Start", &audio.PlayOnStart);
            ImGui::Checkbox("Spatial", &audio.Spatial);
            ImGui::Separator();
        }

        if (ImGui::Button("Add Component..."))
            ImGui::OpenPopup("AddComponentPopup");

        if (ImGui::BeginPopup("AddComponentPopup"))
        {
            if (!m_Selected.HasComponent<MeshRendererComponent>() && ImGui::MenuItem("Mesh Renderer"))
                m_Selected.AddComponent<MeshRendererComponent>();
            if (!m_Selected.HasComponent<CameraComponent>() && ImGui::MenuItem("Camera"))
                m_Selected.AddComponent<CameraComponent>();
            if (!m_Selected.HasComponent<DirectionalLightComponent>() && ImGui::MenuItem("Directional Light"))
                m_Selected.AddComponent<DirectionalLightComponent>();
            if (!m_Selected.HasComponent<PointLightComponent>() && ImGui::MenuItem("Point Light"))
                m_Selected.AddComponent<PointLightComponent>();
            if (!m_Selected.HasComponent<RigidBodyComponent>() && ImGui::MenuItem("Rigid Body"))
                m_Selected.AddComponent<RigidBodyComponent>();
            if (!m_Selected.HasComponent<BoxColliderComponent>() && ImGui::MenuItem("Box Collider"))
                m_Selected.AddComponent<BoxColliderComponent>();
            if (!m_Selected.HasComponent<SphereColliderComponent>() && ImGui::MenuItem("Sphere Collider"))
                m_Selected.AddComponent<SphereColliderComponent>();
            if (!m_Selected.HasComponent<CapsuleColliderComponent>() && ImGui::MenuItem("Capsule Collider"))
                m_Selected.AddComponent<CapsuleColliderComponent>();
            if (!m_Selected.HasComponent<ScriptComponent>() && ImGui::MenuItem("Script"))
                m_Selected.AddComponent<ScriptComponent>();
            if (!m_Selected.HasComponent<AudioSourceComponent>() && ImGui::MenuItem("Audio Source"))
                m_Selected.AddComponent<AudioSourceComponent>();
            ImGui::EndPopup();
        }

        ImGui::End();
    }


    void DrawGizmo()
    {
        if (!m_Selected.IsValid() || IsPlaying())
            return;
        if (!m_Selected.HasComponent<TransformComponent>())
            return;

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 viewportPos = viewport->Pos;
        const ImVec2 viewportSize = viewport->Size;
        if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
            return;

        const CameraData camera = GetScene().GetCameraData(viewportSize.x / viewportSize.y);
        auto& transform = m_Selected.GetComponent<TransformComponent>();
        const glm::vec3 position = glm::vec3(transform.WorldMatrix[3]);

        auto project = [&](const glm::vec3& world, ImVec2& out) -> bool
        {
            const glm::vec4 clip = camera.ViewProjection * glm::vec4(world, 1.0f);
            if (clip.w <= 0.0f)
                return false;
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            out = ImVec2(viewportPos.x + (ndc.x * 0.5f + 0.5f) * viewportSize.x,
                viewportPos.y + (0.5f - ndc.y * 0.5f) * viewportSize.y);
            return true;
        };

        ImVec2 originScreen;
        if (!project(position, originScreen))
            return;

        const glm::vec3 axes[3] = { glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1) };
        const ImU32 colors[3] = { IM_COL32(230, 70, 70, 255), IM_COL32(70, 220, 70, 255),
            IM_COL32(80, 130, 255, 255) };
        const char* labels[3] = { "X", "Y", "Z" };

        // Keep the gizmo a constant, comfortable size on screen regardless of
        // camera distance: find how many pixels one world unit spans and scale
        // the handles so they are roughly 90 pixels long.
        float maxProjectedLength = 1.0f;
        for (int axis = 0; axis < 3; ++axis)
        {
            ImVec2 unitHandle;
            if (!project(position + axes[axis], unitHandle))
                continue;

            const float dx = unitHandle.x - originScreen.x;
            const float dy = unitHandle.y - originScreen.y;
            maxProjectedLength = std::max(maxProjectedLength, std::sqrt(dx * dx + dy * dy));
        }

        const float gizmoScale = 90.0f / maxProjectedLength;

        ImDrawList* drawList = ImGui::GetBackgroundDrawList();
        drawList->AddCircleFilled(originScreen, 5.0f, IM_COL32(255, 255, 255, 220));

        ImVec2 handles[3];
        bool handleValid[3] = { false, false, false };

        for (int axis = 0; axis < 3; ++axis)
        {
            handleValid[axis] = project(position + axes[axis] * gizmoScale, handles[axis]);
            if (!handleValid[axis])
                continue;

            drawList->AddLine(originScreen, handles[axis], colors[axis], 3.0f);
            drawList->AddCircleFilled(handles[axis], 7.0f, colors[axis]);
            drawList->AddText(ImVec2(handles[axis].x + 10.0f, handles[axis].y - 9.0f),
                colors[axis], labels[axis]);
        }

        const ImVec2 mouse = ImGui::GetIO().MousePos;

        if (!m_GizmoDragging && ImGui::IsMouseClicked(0))
        {
            for (int axis = 0; axis < 3; ++axis)
            {
                if (!handleValid[axis])
                    continue;

                const float dx = mouse.x - handles[axis].x;
                const float dy = mouse.y - handles[axis].y;
                if (dx * dx + dy * dy > 169.0f)
                    continue;

                m_GizmoAxis = axis;
                m_GizmoDragging = true;
                m_GizmoStartMouse = mouse;
                m_GizmoStartTranslation = transform.Translation;
                m_GizmoScale = gizmoScale;
                m_GizmoScreenAxis = ImVec2(handles[axis].x - originScreen.x,
                    handles[axis].y - originScreen.y);
                break;
            }
        }

        if (!m_GizmoDragging)
            return;

        if (!ImGui::IsMouseDown(0))
        {
            m_GizmoDragging = false;
            return;
        }

        const float lengthSquared = m_GizmoScreenAxis.x * m_GizmoScreenAxis.x
            + m_GizmoScreenAxis.y * m_GizmoScreenAxis.y;
        if (lengthSquared < 1.0f)
            return;

        const ImVec2 delta(mouse.x - m_GizmoStartMouse.x, mouse.y - m_GizmoStartMouse.y);
        const float amount = (delta.x * m_GizmoScreenAxis.x + delta.y * m_GizmoScreenAxis.y)
            / lengthSquared * m_GizmoScale;

        glm::vec3 direction(0.0f);
        direction[m_GizmoAxis] = 1.0f;
        transform.Translation = m_GizmoStartTranslation + direction * amount;
    }

    void DrawStatistics()
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(
            ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 360.0f,
                viewport->WorkPos.y + viewport->WorkSize.y - 300.0f),
            ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(350.0f, 280.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Statistics");

        const auto& stats = GetRenderer().GetStatistics();
        ImGui::Text("FPS: %.1f", GetFrameTimer().GetFramesPerSecond());
        ImGui::Text("Frame time: %.2f ms", GetFrameTimer().GetDeltaTime() * 1000.0);
        ImGui::Text("Draw calls: %u", stats.DrawCalls);
        ImGui::Text("Triangles: %u", stats.Triangles);
        ImGui::Text("Entities: %zu", GetScene().GetEntityCount());
        ImGui::Text("Viewport: %u x %u", GetContext().GetSwapchainExtent().width,
            GetContext().GetSwapchainExtent().height);
        ImGui::Text("GPU: %s", GetContext().GetDeviceProperties().deviceName);
        ImGui::Text("IBL: %s", GetRenderer().IsIBLReady() ? "ready" : "pending");
        ImGui::Text("Physics bodies: %zu", GetPhysics().GetBodyCount());
        ImGui::Text("Script instances: %zu", GetScripts().GetInstanceCount());
        ImGui::Text("Active sounds: %zu", GetAudio().GetActiveSoundCount());
        ImGui::Text("Playing: %s", IsPlaying() ? "yes" : "no");
        ImGui::Separator();

        bool bloomEnabled = GetRenderer().IsBloomEnabled();
        if (ImGui::Checkbox("Bloom", &bloomEnabled))
            GetRenderer().SetBloomEnabled(bloomEnabled);

        float bloomThreshold = GetRenderer().GetBloomThreshold();
        if (ImGui::DragFloat("Bloom Threshold", &bloomThreshold, 0.02f, 0.0f, 8.0f))
            GetRenderer().SetBloomThreshold(bloomThreshold);

        float bloomIntensity = GetRenderer().GetBloomIntensity();
        if (ImGui::DragFloat("Bloom Intensity", &bloomIntensity, 0.005f, 0.0f, 1.0f))
            GetRenderer().SetBloomIntensity(bloomIntensity);

        ImGui::End();

        if (m_ShowDemo)
            ImGui::ShowDemoWindow(&m_ShowDemo);
    }

    void UpdateCamera(double deltaTime)
    {
        Entity camera = GetScene().GetPrimaryCameraEntity();
        if (!camera.IsValid() || !camera.HasComponent<TransformComponent>())
            return;

        auto& transform = camera.GetComponent<TransformComponent>();

        if (Input::IsMouseButtonDown(MouseButton::Right))
        {
            const glm::vec2 delta = Input::GetMouseDelta();
            transform.Rotation.y -= delta.x * 0.15f;
            transform.Rotation.x = std::clamp(transform.Rotation.x - delta.y * 0.15f, -89.0f, 89.0f);
        }

        const glm::mat4 local = transform.GetLocalMatrix();
        const glm::vec3 forward = -glm::normalize(glm::vec3(local[2]));
        const glm::vec3 right = glm::normalize(glm::vec3(local[0]));

        float speed = 5.0f * static_cast<float>(deltaTime);
        if (Input::IsKeyDown(Key::LeftShift))
            speed *= 3.0f;

        if (Input::IsKeyDown(Key::W))
            transform.Translation += forward * speed;
        if (Input::IsKeyDown(Key::S))
            transform.Translation -= forward * speed;
        if (Input::IsKeyDown(Key::D))
            transform.Translation += right * speed;
        if (Input::IsKeyDown(Key::A))
            transform.Translation -= right * speed;
        if (Input::IsKeyDown(Key::E))
            transform.Translation.y += speed;
        if (Input::IsKeyDown(Key::Q))
            transform.Translation.y -= speed;
    }

    Entity ResolveEntity(const nlohmann::json& request)
    {
        if (request.contains("uuid") && request["uuid"].is_string())
        {
            const Slate::UUID id(std::stoull(request["uuid"].get<std::string>(), nullptr, 16));
            return GetScene().FindEntityByUUID(id);
        }

        if (request.contains("name") && request["name"].is_string())
        {
            const std::string name = request["name"].get<std::string>();
            Entity found;
            GetScene().GetRegistry().Each<TagComponent>([&](Entity entity, TagComponent& tag)
            {
                if (!found.IsValid() && tag.Name == name)
                    found = entity;
            });
            return found;
        }

        return Entity();
    }

    static glm::vec3 ReadVec3(const nlohmann::json& value, const glm::vec3& fallback)
    {
        if (!value.is_array() || value.size() < 3)
            return fallback;
        return glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
    }

    nlohmann::json HandleAutomation(const nlohmann::json& request)
    {
        const std::string command = request.value("cmd", "");
        nlohmann::json response { { "ok", true }, { "cmd", command } };

        if (command == "ping")
        {
            response["engine"] = "Slate";
            return response;
        }

        if (command == "stats")
        {
            const auto& stats = GetRenderer().GetStatistics();
            response["fps"] = GetFrameTimer().GetFramesPerSecond();
            response["entities"] = GetScene().GetEntityCount();
            response["drawCalls"] = stats.DrawCalls;
            response["triangles"] = stats.Triangles;
            response["physicsBodies"] = GetPhysics().GetBodyCount();
            response["scriptInstances"] = GetScripts().GetInstanceCount();
            response["playing"] = IsPlaying();
            return response;
        }

        if (command == "list_entities")
        {
            nlohmann::json entities = nlohmann::json::array();
            GetScene().GetRegistry().Each<TagComponent>([&](Entity entity, TagComponent& tag)
            {
                entities.push_back({ { "name", tag.Name }, { "uuid", GetScene().GetEntityUUID(entity).ToString() } });
            });
            response["entities"] = entities;
            return response;
        }

        if (command == "select")
        {
            Entity selected = ResolveEntity(request);
            if (!selected.IsValid())
                return { { "ok", false }, { "error", "entity not found" } };
            m_Selected = selected;
            return response;
        }

        if (command == "list_transforms")
        {
            nlohmann::json entities = nlohmann::json::array();
            GetScene().GetRegistry().Each<TransformComponent>([&](Entity entity, TransformComponent& transform)
            {
                std::string name;
                if (entity.HasComponent<TagComponent>())
                    name = entity.GetComponent<TagComponent>().Name;

                entities.push_back({
                    { "name", name },
                    { "uuid", GetScene().GetEntityUUID(entity).ToString() },
                    { "translation", { transform.Translation.x, transform.Translation.y, transform.Translation.z } }
                });
            });
            response["entities"] = entities;
            return response;
        }

        if (command == "create_entity")
        {
            const std::string name = request.value("name", "Entity");
            Entity entity = GetScene().CreateEntity(name);
            m_Selected = entity;
            response["uuid"] = GetScene().GetEntityUUID(entity).ToString();
            return response;
        }

        Entity entity = ResolveEntity(request);

        if (command == "delete_entity")
        {
            if (!entity.IsValid())
                return { { "ok", false }, { "error", "entity not found" } };
            GetScene().DestroyEntity(entity);
            m_Selected = Entity();
            return response;
        }

        const bool needsEntity = command != "load_scene" && command != "save_scene"
            && command != "screenshot" && command != "play" && command != "stop"
            && command != "export" && command != "quit"
            && command != "key" && command != "mouse" && command != "clear_input";

        if (needsEntity && !entity.IsValid())
            return { { "ok", false }, { "error", "entity not found" } };

        if (command == "set_transform")
        {
            auto& transform = entity.GetComponent<TransformComponent>();
            if (request.contains("translation"))
                transform.Translation = ReadVec3(request["translation"], transform.Translation);
            if (request.contains("rotation"))
                transform.Rotation = ReadVec3(request["rotation"], transform.Rotation);
            if (request.contains("scale"))
                transform.Scale = ReadVec3(request["scale"], transform.Scale);
            return response;
        }

        if (command == "get_transform")
        {
            const auto& transform = entity.GetComponent<TransformComponent>();
            response["translation"] = { transform.Translation.x, transform.Translation.y, transform.Translation.z };
            response["rotation"] = { transform.Rotation.x, transform.Rotation.y, transform.Rotation.z };
            response["scale"] = { transform.Scale.x, transform.Scale.y, transform.Scale.z };
            return response;
        }

        if (command == "set_mesh")
        {
            if (!entity.HasComponent<MeshRendererComponent>())
                entity.AddComponent<MeshRendererComponent>();
            entity.GetComponent<MeshRendererComponent>().ModelPath = request.value("model", "");
            return response;
        }

        if (command == "add_component")
        {
            const std::string component = request.value("component", "");
            if (component == "meshrenderer" && !entity.HasComponent<MeshRendererComponent>())
                entity.AddComponent<MeshRendererComponent>();
            else if (component == "camera" && !entity.HasComponent<CameraComponent>())
                entity.AddComponent<CameraComponent>();
            else if (component == "directionallight" && !entity.HasComponent<DirectionalLightComponent>())
                entity.AddComponent<DirectionalLightComponent>();
            else if (component == "pointlight" && !entity.HasComponent<PointLightComponent>())
                entity.AddComponent<PointLightComponent>();
            else if (component == "rigidbody" && !entity.HasComponent<RigidBodyComponent>())
            {
                auto& body = entity.AddComponent<RigidBodyComponent>();
                body.Type = static_cast<BodyType>(request.value("type", 0));
                body.Mass = request.value("mass", 1.0f);
            }
            else if (component == "boxcollider" && !entity.HasComponent<BoxColliderComponent>())
            {
                auto& collider = entity.AddComponent<BoxColliderComponent>();
                if (request.contains("halfExtents"))
                    collider.HalfExtents = ReadVec3(request["halfExtents"], collider.HalfExtents);
            }
            else if (component == "spherecollider" && !entity.HasComponent<SphereColliderComponent>())
                entity.AddComponent<SphereColliderComponent>().Radius = request.value("radius", 0.5f);
            else if (component == "capsulecollider" && !entity.HasComponent<CapsuleColliderComponent>())
            {
                auto& capsule = entity.AddComponent<CapsuleColliderComponent>();
                capsule.Radius = request.value("radius", 0.4f);
                capsule.HalfHeight = request.value("halfHeight", 0.6f);
            }
            else if (component == "script" && !entity.HasComponent<ScriptComponent>())
                entity.AddComponent<ScriptComponent>().ScriptPath = request.value("path", "");
            else if (component == "audio" && !entity.HasComponent<AudioSourceComponent>())
                entity.AddComponent<AudioSourceComponent>().ClipPath = request.value("clip", "");
            return response;
        }

        if (command == "load_scene")
        {
            const std::string path = request.value("path", "Scenes/TestScene.json");
            if (!GetScene().Load(path))
                return { { "ok", false }, { "error", "failed to load scene" } };
            m_ScenePath = path;
            m_Selected = Entity();
            return response;
        }

        if (command == "save_scene")
        {
            const std::string path = request.value("path", m_ScenePath);
            if (!GetScene().Save(path))
                return { { "ok", false }, { "error", "failed to save scene" } };
            m_ScenePath = path;
            return response;
        }

        if (command == "screenshot")
        {
            const std::string path = request.value("path", "screenshots/editor.png");
            RequestScreenshot(path);
            response["path"] = path;
            return response;
        }

        if (command == "key")
        {
            const std::string name = request.value("key", "");
            const bool down = request.value("down", true);
            Input::SetKeyDown(KeyFromString(name), down);
            response["key"] = name;
            response["down"] = down;
            return response;
        }

        if (command == "mouse")
        {
            const std::string button = request.value("button", "Left");
            const bool down = request.value("down", true);
            if (button == "Right")
                Input::SetMouseButtonDown(MouseButton::Right, down);
            else if (button == "Middle")
                Input::SetMouseButtonDown(MouseButton::Middle, down);
            else
                Input::SetMouseButtonDown(MouseButton::Left, down);
            return response;
        }

        if (command == "clear_input")
        {
            Input::ClearInjectedInput();
            return response;
        }

        if (command == "play")
        {
            SetPlaying(true);
            return response;
        }

        if (command == "stop")
        {
            SetPlaying(false);
            return response;
        }

        if (command == "export")
        {
            ExportOptions options;
            options.Name = request.value("name", GetScene().GetName());
            options.ScenePath = request.value("scene", m_ScenePath);
            options.OutputDirectory = request.value("output", "Export");
            std::string error;
            if (!ExportGame(GetScene(), options, error))
                return { { "ok", false }, { "error", error } };
            response["output"] = (std::filesystem::path(options.OutputDirectory) / options.Name).string();
            return response;
        }

        if (command == "quit")
        {
            Close();
            return response;
        }

        return { { "ok", false }, { "error", "unknown command" } };
    }

    Entity m_Selected;
    std::string m_StartupScene;
    std::string m_ScenePath;
    bool m_ShowDemo = false;
    bool m_GizmoDragging = false;
    int m_GizmoAxis = 0;
    ImVec2 m_GizmoStartMouse { 0.0f, 0.0f };
    ImVec2 m_GizmoScreenAxis { 0.0f, 0.0f };
    float m_GizmoScale = 1.0f;
    glm::vec3 m_GizmoStartTranslation { 0.0f };
    AutomationServer m_Automation;
};

} // namespace

int main(int argc, char** argv)
{
    const Slate::CommandLineOptions options = Slate::ParseCommandLine(argc, argv);
    if (options.ShowHelp)
    {
        Slate::PrintUsage(std::cout, "Slate-Editor", "Slate engine editor.");
        return 0;
    }

    if (!options.AssetRoot.empty())
        Slate::FileSystem::SetAssetRoot(options.AssetRoot);

    std::string scenePath = options.ScenePath;
    if (scenePath.empty())
    {
        if (const char* environmentScene = std::getenv("SLATE_SCENE");
            environmentScene != nullptr && *environmentScene != '\0')
        {
            scenePath = environmentScene;
        }
        else
        {
            scenePath = "Scenes/TestScene.json";
        }
    }

    Slate::Log::Init("Slate-Editor.log");
    Slate::InstallCrashHandler();
    SLATE_INFO("Slate Editor");

    {
        SlateEditor editor(std::move(scenePath));
        editor.Run();
    }

    Slate::Log::Shutdown();
    return 0;
}
