#include "imgui_windows.hpp"
#include "vk-render.hpp"
#include "camera.hpp"
#include "time.hpp"
#include "glfw_window.hpp"
#include "entity_manager.hpp"
#include "world.hpp"

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/backends/imgui_impl_vulkan.h"
#include "imgui/backends/imgui_impl_glfw.h"

#include "implot/implot.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cmath>
#include <memory>
#include <map>
#include <fmt/format.h>
#include <unordered_map>
#include <sys/stat.h>

constinit std::array windows
{
    std::make_pair(ui::display_stats, true),
    std::make_pair(ui::display_options, true),
    std::make_pair(ui::display_camera, true),
    std::make_pair(ui::display_world, true),
    std::make_pair(ui::display_console, true)
};

namespace ImGui
{
    bool SliderDouble(const char* label, double* v, const double& min, const double& max, const char* format = nullptr, ImGuiSliderFlags flags = 0)
    {
        flags |= ImGuiInputTextFlags_CharsScientific;
        return SliderScalar(label, ImGuiDataType_Double, v, &min, &max, format, flags);
        //address of parameter... but ImGui does it
    }
}

void ui::iterate_windows()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    for(auto& value : windows)
    {
        if(value.second)
        {
            value.first(&value.second);
        }
    }

    ImGui::Render();
}

bool ui::set_open(void(*fn)(bool*), bool open)
{
    for(auto& value : windows)
    {
        if(value.first == fn)
        {
            value.second = open;
            return true;
        }
    }
    return false;
}

void ui::set_all(bool open)
{
    for(auto& value : windows)
    {
        value.second = open;
    }
}

namespace ui
{
    enum world_object_mask : uint8_t
    {
        location = 0b1,
        rotation = 0b10,
        scale = 0b100,
    };
}

namespace imgui = ImGui;

#define ROT_AS_EULER 0

static void display_location(glm::vec3& location)
{
    ImGui::DragFloat3("location", &location.x, 0.5f);
}

static void display_rotation(glm::quat& rotation, bool as_euler = false)
{
    if(as_euler)
    {
        glm::vec3 euler = glm::eulerAngles(rotation);
        euler = glm::degrees(euler);

        bool edited = ImGui::DragFloat3("rotation", &euler.x, 0.5f);

        if(edited)
        {
            euler = glm::radians(euler);
            rotation = glm::quat{euler};
        }
    }
    else
    {
        bool edited = ImGui::DragFloat4("rotation", &rotation.w, 0.01f, -1.0, 1.0);
        if(edited)
        {
            rotation = glm::normalize(rotation);
        }
    }
}

static void display_scale(glm::vec3& scale)
{
    glm::vec3 old_scale = scale;
    bool edited = ImGui::DragFloat3("scale", &scale.x, 0.1f, 0.f, 1000.f, "%.4f", ImGuiSliderFlags_Logarithmic);

    static std::unordered_map<glm::vec3*, bool> wants_uniform_scale{};
    bool* uniform = &wants_uniform_scale[&scale];

    ImGui::Checkbox("uniform scale", uniform);

    if(*uniform)
    {
        if(edited)
        {
            if(old_scale.x != scale.x)
            {
                scale = glm::vec3{scale.x};
            }
            else if(old_scale.y != scale.y)
            {
                scale = glm::vec3{scale.y};
            }
            else if(old_scale.z != scale.z)
            {
                scale = glm::vec3{scale.z};
            }
        }
        else
        {
            scale = glm::vec3{scale.x};
        }
    }
}

static void display_pointlight_output(pointlight_t& light)
{
    ImGuiColorEditFlags flags = ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_NoSidePreview;

    ImGui::ColorEdit3("color", &light.color.r, flags);
    ImGui::DragFloat("power", &light.strength, 1.0f, 0.0f, FLT_MAX);
}

static void display_planelight_output(directionallight_t& light)
{
    ImGuiColorEditFlags flags = ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_NoSidePreview;

    ImGui::ColorEdit3("color", &light.color.r, flags);
    ImGui::DragFloat("power", &light.strength, 1.0f, 0.0f, FLT_MAX);
}

void ui::display_stats(bool* popen)
{
    if(ImGui::Begin("stats", popen, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysAutoResize))
    {
        double fps = 1.0 / (program_time.fp_delta / program_time.dilation);
        double average_fps = (1.0 / timespec2double(program_time.total)) * double(program_time.frame_count);

        int32_t width; int32_t height;
        glfwGetWindowSize(gWindow, &width, &height);

        ImGui::Text("%i x %i", width, height);
        ImGui::Text("fps %.1f", fps);
        ImGui::Text("average fps %.1f", average_fps);
        ImGui::Text("frame %lu", program_time.frame_count);
        ImGui::Text("time %ld.%.9ld", program_time.total.tv_sec, program_time.total.tv_nsec);
        ImGui::Text("delta %ld.%.9ld", program_time.delta.tv_sec, program_time.delta.tv_nsec);
    }

    ImGui::End();
}

extern bool reload_shaders();

void ui::display_options(bool* popen)
{
    if(ImGui::Begin("options", popen, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::InputDouble("time dilation", &program_time.dilation, 0.05, 1.0);

        double max_fps = 1.0 / timespec2double(program_time.min_frametime);
        if(ImGui::InputDouble("max fps", &max_fps, 1.0, 10.0))
        {
            program_time.min_frametime = double2timespec(1.0 / max_fps);
        }

        static int32_t reload_status = 0;
        static double status_time = 0.0;
        ImVec4 button_color;

        if(reload_status != 0)
        {
            status_time += program_time.fp_delta;

            if(status_time >= 3.0)
            {
                status_time = 0.0;
                reload_status = 0;
            }
        }

        switch(reload_status)
        {
            case -1: button_color = ImVec4(1.0, 0.0, 0.0, 1.0); break;
            case 0: button_color = ImVec4(0.0, 0.0, 1.0, 0.5); break;
            case 1: button_color = ImVec4(0.0, 1.0, 0.0, 1.0); break;
        }

        ImGui::PushStyleColor(ImGuiCol_Button, button_color);
        if(ImGui::Button("reload shaders"))
        {
            reload_status = reload_shaders() ? 1 : -1;
        }
        ImGui::PopStyleColor();
    }

    ImGui::End();
}

static void display_matrix(glm::mat4x4 matrix)
{
    constexpr char format[] = "%.3f";
    ImGui::InputFloat4("row 0", &matrix[0][0], format, ImGuiInputTextFlags_ReadOnly);
    ImGui::InputFloat4("row 1", &matrix[1][0], format, ImGuiInputTextFlags_ReadOnly);
    ImGui::InputFloat4("row 2", &matrix[2][0], format, ImGuiInputTextFlags_ReadOnly);
    ImGui::InputFloat4("row 3", &matrix[3][0], format, ImGuiInputTextFlags_ReadOnly);
}

void ui::display_camera(bool* popen)
{
    if(ImGui::Begin("camera [perspective]", popen, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::SliderFloat("field of view", &(gWorld->camera.FOVy), 0.0, std::numbers::pi);
        ImGui::InputFloat("z near", &(gWorld->camera.z_near), 0.01, 1.0);
        ImGui::InputFloat("z far", &(gWorld->camera.z_far), 0.1, 10.0);
        ImGui::InputFloat("move speed", &(gWorld->camera.move_speed), 0.1, 1.0);
        ImGui::InputFloat("look speed", &(gWorld->camera.look_speed), 0.0001);

        display_location(gWorld->camera.location);
        display_rotation(gWorld->camera.rotation);

        ImGui::Checkbox("lock roll", &(gWorld->camera.lock_roll));

        ImGui::SameLine();

        static bool bdisplay_matrix = false;
        ImGui::Checkbox("matrix form", &bdisplay_matrix);

        if(bdisplay_matrix)
        {
            ImGui::Text("view");
            ImGui::PushID("view");
            display_matrix(gWorld->camera.view_matrix());
            ImGui::PopID();

            ImGui::Text("projection");
            ImGui::PushID("projection");
            display_matrix(gWorld->camera.projection_matrix());
            ImGui::PopID();

            ImGui::Text("projection * view");
            ImGui::PushID("projection * view");
            display_matrix(gWorld->camera.projection_matrix() * gWorld->camera.view_matrix());
            ImGui::PopID();
        }
    }

    ImGui::End();
}

static void display_name(name_t& name)
{
    char buf[64];
    if(ImGui::InputTextWithHint("name", name.data(), buf, 64, ImGuiInputTextFlags_EnterReturnsTrue))
    {
        name = std::string_view(buf);
    }
}

static void display_models_combo(entity_t& entity)
{
    if(ImGui::BeginCombo("model", entity.model->name.data()))
    {
        size_t selected_index = get_world().models.get_index(entity.model);

        for(size_t index = 0; index < get_world().models.size(); ++index)
        {
            const bool is_selected = (index == selected_index);
            if(ImGui::Selectable(get_world().models[index].name.data(), is_selected))
            {
                selected_index = index;
                entity.model = get_world().models.get_handle(selected_index);
            }
        }

        ImGui::EndCombo();
    }
}

static void display_materials_combo(entity_t& entity)
{
    if(ImGui::BeginCombo("material", entity.material->name.data()))
    {
        size_t selected_index = get_world().materials.get_index(entity.material);

        for(size_t index = 0; index < get_world().materials.size(); ++index)
        {
            const bool is_selected = (index == selected_index);
            if(ImGui::Selectable(get_world().materials[index].name.data(), is_selected))
            {
                selected_index = index;
                entity.material = get_world().materials.get_handle(selected_index);
            }
        }

        ImGui::EndCombo();
    }
}

static void display_textures_combo(entity_t& entity)
{
    if(ImGui::BeginCombo("texture", entity.texture->name.data()))
    {
        size_t selected_index = get_world().textures.get_index(entity.texture);

        for(size_t index = 0; index < get_world().textures.size(); ++index)
        {
            const bool is_selected = (index == selected_index);
            if(ImGui::Selectable(get_world().textures[index].name.data(), is_selected))
            {
                selected_index = index;
                entity.texture = get_world().textures.get_handle(selected_index);
            }
        }

        ImGui::EndCombo();
    }
}

static void right_align(std::string text, float additional = 0.0f)
{
    float width_needed = ImGui::CalcTextSize(text.c_str()).x + ImGui::GetStyle().FramePadding.x + additional;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - width_needed);
}

static void display_world_options()
{
    if(ImGui::CollapsingHeader("options"))
    {
        global_device_data_t::scene_t& scene_data = get_world().scene_data;

        ImGuiColorEditFlags flags = ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_NoSidePreview;

        ImGui::ColorEdit3("ambiance color", &scene_data.ambiance_color.r, flags);
        ImGui::DragFloat("ambiance power", &scene_data.ambiance_strength, 0.001f, 0.0f, FLT_MAX);

        ImGui::ColorEdit3("sky color", &scene_data.sky_color.r, flags);
    }
}

static void display_planelights()
{
    ImVec2 size = ImGui::GetContentRegionAvail();
    size.y /= 4.0;

    if(ImGui::BeginChild("planelights", size, true,  ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysVerticalScrollbar))
    {
        static std::vector<slothandle_t<directionallight_t>> selected_lights{};
        slothandle<directionallight_t> added_light = nullptr;
        slothandle<directionallight_t> clicked_light = nullptr;

        std::string lights_name = fmt::format("planelights ( {} )", get_world().lightmanager.directional_lights.size());

        right_align(fmt::format("{}{}", "add   ", lights_name));
        if(ImGui::Button("add"))
        {
            added_light = get_world().lightmanager.spawn_directional_light(directionallight_t{axis::down, 0, {1,1,1}, 10.f});
        }
        ImGui::SameLine();

        ImGui::Text("%s", lights_name.c_str());

        for(uint64_t light_index = 0; light_index < get_world().lightmanager.directional_lights.size(); ++light_index)
        {
            slothandle<directionallight_t> light = get_world().lightmanager.directional_lights.get_handle(light_index);

            ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;

            if(added_light == light)
            {
                node_flags |= ImGuiTreeNodeFlags_Selected;
                ImGui::SetNextItemOpen(true);
            }
            else
            {
                for(slothandle<directionallight_t> selected : selected_lights)
                {
                    if(selected == light)
                    {
                        node_flags |= ImGuiTreeNodeFlags_Selected;
                        break;
                    }
                }
            }

            std::string node_name = fmt::format("planelight [{}]", light_index);
            right_align(node_name, 25);

            bool node_open = ImGui::TreeNodeEx((void*)(light_index), node_flags, "%s", node_name.c_str());

            if(added_light == light)
            {
                ImGui::ScrollToItem();
            }

            if(ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            {
                clicked_light = light;
            }

            if(node_open)
            {
                display_planelight_output(*light);
                ImGui::TreePop();
            }
        }

        if(clicked_light)
        {
            if(!ImGui::GetIO().KeyCtrl)
            {
                selected_lights.clear();
            }

            selected_lights.emplace_back(clicked_light);
        }

        if(!selected_lights.empty() && glfwGetKey(gWindow, GLFW_KEY_DELETE) == GLFW_PRESS)
        {
            for(slothandle<directionallight_t> light : selected_lights)
            {
                get_world().lightmanager.destroy_directional_light(light);
            }
            selected_lights.clear();
        }
    }
    ImGui::EndChild();
}

static void display_pointlights()
{
    ImVec2 size = ImGui::GetContentRegionAvail();
    size.y /= 2.0;

    if(ImGui::BeginChild("pointlights", size, true,  ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysVerticalScrollbar))
    {
        static std::vector<slothandle_t<pointlight_t>> selected_lights{};
        slothandle<pointlight_t> added_light = nullptr;
        slothandle<pointlight_t> clicked_light = nullptr;

        std::string lights_name = fmt::format("lights ( {} )", get_world().lightmanager.pointlights.size());

        right_align(fmt::format("{}{}", "add   ", lights_name));
        if(ImGui::Button("add"))
        {
            added_light = get_world().lightmanager.spawn_pointlight(get_world().camera.location, {1.0, 1.0, 1.0}, 500.f);
        }
        ImGui::SameLine();

        ImGui::Text("%s", lights_name.c_str());

        for(uint64_t light_index = 0; light_index < get_world().lightmanager.pointlights.size(); ++light_index)
        {
            slothandle<pointlight_t> light = get_world().lightmanager.pointlights.get_handle(light_index);

            ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;

            if(added_light == light)
            {
                node_flags |= ImGuiTreeNodeFlags_Selected;
                ImGui::SetNextItemOpen(true);
            }
            else
            {
                for(slothandle<pointlight_t> selected : selected_lights)
                {
                    if(selected == light)
                    {
                        node_flags |= ImGuiTreeNodeFlags_Selected;
                        break;
                    }
                }
            }

            std::string node_name = fmt::format("pointlight [{}]", light_index);
            right_align(node_name, 25);

            bool node_open = ImGui::TreeNodeEx((void*)(light_index), node_flags, "%s", node_name.c_str());

            if(added_light == light)
            {
                ImGui::ScrollToItem();
            }

            if(ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            {
                clicked_light = light;
            }

            if(node_open)
            {
                display_location(light->location);
                display_pointlight_output(*light);
                ImGui::TreePop();
            }
        }

        if(clicked_light)
        {
            if(!ImGui::GetIO().KeyCtrl)
            {
                selected_lights.clear();
            }

            selected_lights.emplace_back(clicked_light);
        }

        if(!selected_lights.empty() && glfwGetKey(gWindow, GLFW_KEY_DELETE) == GLFW_PRESS)
        {
            for(slothandle<pointlight_t> light : selected_lights)
            {
                get_world().lightmanager.destroy_pointlight(light);
            }
            selected_lights.clear();
        }
    }
    ImGui::EndChild();
}

static void display_entities()
{
    ImVec2 size = ImGui::GetContentRegionAvail();

    if(ImGui::BeginChild("entities", size, true,  ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysVerticalScrollbar))
    {
        static std::vector<slothandle_t<entity_t>> selected_entities{};
        slothandle<entity_t> added_entity = nullptr;
        slothandle<entity_t> clicked_entity = nullptr;

        std::string entities_name = fmt::format("entities ( {} )", get_world().entities.size());

        right_align(fmt::format("{}{}", "add   ", entities_name));
        if(ImGui::Button("add"))
        {
            added_entity = get_world().spawn_entity(entity_name_constructor{"new entity", "cube", "cube", "default lit textured"});
            added_entity->transform().location = get_world().camera.location;
        }
        ImGui::SameLine();

        ImGui::Text("%s", entities_name.c_str());

        for(size_t entity_index = 0; entity_index < get_world().entities.size(); ++entity_index)
        {
            slothandle<entity_t> entity = get_world().entities.get_handle(entity_index);

            ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;

            if(added_entity == entity)
            {
                node_flags |= ImGuiTreeNodeFlags_Selected;
                ImGui::SetNextItemOpen(true);
            }
            else
            {
                for(slothandle<entity_t> selected : selected_entities)
                {
                    if(selected == entity)
                    {
                        node_flags |= ImGuiTreeNodeFlags_Selected;
                        break;
                    }
                }
            }

            std::string node_name = fmt::format("{} [{}]", entity->name.str(), entity_index);
            right_align(node_name, 25);

            bool node_open = ImGui::TreeNodeEx((void*)(entity_index), node_flags, "%s", node_name.c_str());

            if(added_entity == entity)
            {
                ImGui::ScrollToItem();
            }

            if(ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            {
                clicked_entity = entity;
            }

            if(node_open)
            {
                display_name(entity->name);
                display_models_combo(*entity);
                display_textures_combo(*entity);
                display_materials_combo(*entity);

                display_location(entity->transform().location);
                display_rotation(entity->transform().rotation);
                display_scale(entity->transform().scale);

                ImGui::TreePop();
            }
        }

        if(clicked_entity)
        {
            if(!ImGui::GetIO().KeyCtrl)
            {
                selected_entities.clear();
            }

            selected_entities.emplace_back(clicked_entity);
        }

        if(!selected_entities.empty() && glfwGetKey(gWindow, GLFW_KEY_DELETE) == GLFW_PRESS)
        {
            for(slothandle<entity_t> entity : selected_entities)
            {
                get_world().destroy_entity(entity);
            }
            selected_entities.clear();
        }

    }
    ImGui::EndChild();
}

void ui::display_world(bool* popen)
{
    if(ImGui::Begin("world", popen,  ImGuiWindowFlags_NoBackground))
    {
        display_world_options();
        display_planelights();
        display_pointlights();
        display_entities();
    }
    ImGui::End();
}

void ui::display_console(bool* popen)
{
    if(ImGui::Begin("console", popen, 0))
    {
        if(!log_string.empty())
        {
            ImGui::TextWrapped("%s", log_string.c_str());

            static bool follow = true;
            bool manually_scrolled = GImGui->ActiveId == GImGui->CurrentWindow->GetID("#SCROLLY") || ImGui::GetIO().MouseWheel != 0.0f;
            bool scrolled_to_bottom = ImGui::GetScrollY() == ImGui::GetScrollMaxY();

            if(manually_scrolled)
            {
                if(scrolled_to_bottom)
                {
                    follow = true;
                }
                else
                {
                    follow = false;
                }
            }

            if(follow)
            {
                ImGui::SetScrollY(ImGui::GetScrollMaxY());
            }
        }
    }
    ImGui::End();
}


