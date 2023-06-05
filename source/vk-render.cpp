#include <ctime>
#include <memory>
#include <ratio>
#include <cmath>
#include <cstdio>
#include <fmt/core.h>
#include <memory>
#include <signal.h>
#include <pthread.h>

#include "glm/glm/glm.hpp"
#include "imgui_windows.hpp"
#include "camera.hpp"
#include "vk-render.hpp"
#include "tick.hpp"
#include "window.hpp"
#include "time.hpp"
#include "math.hpp"
#include "vulkan_engine.hpp"
#include "vulkan_model.hpp"
#include "glfw_window.hpp"
#include "slotmap.hpp"
#include "transform_component.hpp"
#include "world.hpp"
#include "bezier.hpp"
#include "vulkan_utility.hpp"
#include "log.hpp"
#include "vulkan_descriptor.hpp"
#include "name.hpp"
#include "vulkan_material.hpp"
#include "vulkan_pipeline.hpp"
#include "taskflow/taskflow/taskflow.hpp"

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/backends/imgui_impl_vulkan.h"
#include "imgui/backends/imgui_impl_glfw.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <fcntl.h>
#include <syscall.h>
#include <sys/stat.h>
#include <atomic>

__attribute__((noreturn)) void syscall_error(const char* str)
{
    perror(str);
    abort();
}

syscheck_t& syscheck_t::operator=(__syscall_slong_t in_val)
{
    val = in_val;
#ifndef NDEBUG
    if UNLIKELY(val == -1)
    {
        syscall_error(nullptr);
    }
#endif
    return *this;
}

std::vector<uint8_t> read_file_binary(std::string_view filepath)
{
    syscheck = open64(filepath.data(), O_RDONLY);
    int fd = syscheck;

    struct stat64 statbuf;
    syscheck = fstat64(fd, &statbuf);

    std::vector<uint8_t> file_data(statbuf.st_size);

    size_t nread = read(fd, file_data.data(), file_data.size());
    if(nread != statbuf.st_size)
    {
        syscall_error("failed to read");
    }

    syscheck = close(fd);

    return file_data;
}

std::vector<uint8_t> read_file_binary(std::string_view dir, std::string_view file)
{
    syscheck = open64(dir.data(), O_DIRECTORY | O_RDONLY);
    int dirfd = syscheck;

    std::vector<uint8_t> file_data = read_file_binary(dirfd, file);

    syscheck = close(dirfd);

    return file_data;
}

std::vector<uint8_t> read_file_binary(int dir, std::string_view file)
{
    syscheck = openat64(dir, file.data(), O_RDONLY);
    int fd = syscheck;

    struct stat64 statbuf;
    syscheck = fstat64(fd, &statbuf);

    std::vector<uint8_t> file_data(statbuf.st_size);

    size_t nread = read(fd, file_data.data(), file_data.size());
    if(nread != statbuf.st_size)
    {
        syscall_error("failed to read");
    }

    syscheck = close(fd);

    return file_data;
}

int glfw_main(int argc, char** argv);
int x11_main(int argc, char** argv);

int main(int argc, char** argv)
{
    return glfw_main(argc, argv);
}

template<typename T>
inline uint64_t size_bytes(const slotmap<T>& m)
{
    return m.size_bytes();
}

template<typename T>
inline uint64_t size_bytes(const std::vector<T>& v)
{
    return v.size() * sizeof(T);
}

void spawn_random_entity()
{
    double position_range = 1000.0;

    slothandle<entity_t> entity = get_world().spawn_entity(entity_random_constructor{});

    entity->transform().location = math::rand_pos(-position_range, position_range);

    if(entity->name == "pony")
    {
        entity->transform().scale = {0.02, 0.02, 0.02};
    }
    else if(entity->name == "gun")
    {
        entity->transform().scale = {3, 3, 3};
    }
    else if(entity->name == "fish")
    {
        entity->transform().scale = {0.5, 0.5, 0.5};
    }
}

constexpr float PI2 = std::numbers::pi * 2.0;

void spin_lights(void* data, double deltatime)
{
    static double time = 0.0;
    time += deltatime / 43.0;

    while(time > PI2)
    {
        time -= PI2;
    }
/*
    slothandle_t<entity_t> target = gWorld->find_entity("target", false);
    if(!target)
    {
        return;
    }
*/
    auto& pointlights = *static_cast<slotmap_t<pointlight_t>*>(data);

    float time_offset = 0.0;
    for(pointlight_t& light : pointlights)
    {
        time_offset += PI2 / pointlights.size();

        glm::quat rotation = glm::rotate(glm::identity<glm::quat>(), float(time), glm::normalize(glm::vec3(-0.4, 1, 0)));
        glm::vec3 location = glm::vec3(0, 25, 125) * rotation;
        light.location = location;
/*
        //glm::vec2 origin{target->transform().location.x, target->transform().location.z};
        glm::vec2 origin{0, 0};
        glm::vec2 pos_xy = math::walk_circle(origin, 125.0f, float(time) + time_offset);
        light.location.x = pos_xy.x;
        light.location.z = pos_xy.y;
        light.location.y = 50.0;
        //light.location.y = target->transform().location.y;
        */
    }
}

pthread_t render_thread = -1;

std::atomic<bool> window_has_closed = false;

bool main_data_available = false;
pthread_mutex_t main2render_copy_mx;
pthread_cond_t main2render_copy_cv;

bool render_finished = false;
pthread_mutex_t render_in_progress_mx;
pthread_cond_t render_finished_cv;

void copy_imgui_draw_data()
{
    ImDrawData& draw_data = gVulkan->imgui_data;

    if(!ImGui::GetDrawData())
    {
        draw_data.Clear();
        return;
    }

    draw_data = *ImGui::GetDrawData();

    static std::vector<ImDrawList*> draw_lists{};
    for(ImDrawList* list : draw_lists)
    {
        IM_FREE(list);
    }
    draw_lists.resize(draw_data.CmdListsCount);

    for(int32_t index = 0; index < draw_data.CmdListsCount; index++)
    {
        draw_lists[index] = draw_data.CmdLists[index]->CloneOutput();
    }

    draw_data.CmdLists = draw_lists.data();
}

void copy_world_data()
{
    render_thread_data_t& world_data = gVulkan->world_data;

    world_data.camera = gWorld->camera;
    world_data.scene = gWorld->scene_data;

    uint64_t entity_bytes_pad = pad_size2alignment(size_bytes(gWorld->entities), alignof(transform_t));
    uint64_t transform_bytes_pad = pad_size2alignment(size_bytes(gWorld->transforms), alignof(directionallight_t));
    uint64_t directional_light_bytes_pad = pad_size2alignment(size_bytes(gWorld->lightmanager.directional_lights), alignof(allocated_image_t));
    uint64_t directional_map_bytes_pad = pad_size2alignment(size_bytes(gWorld->lightmanager.directional_maps), alignof(pointlight_t));
    uint64_t pointlight_bytes_pad = pad_size2alignment(size_bytes(gWorld->lightmanager.pointlights), alignof(allocated_image_t));
    uint64_t cubemap_bytes_pad = size_bytes(gWorld->lightmanager.cubemaps);

    const uint64_t total_bytes = entity_bytes_pad + transform_bytes_pad + directional_light_bytes_pad + directional_map_bytes_pad + pointlight_bytes_pad + cubemap_bytes_pad;

    static uint8_t* allocation = nullptr;
    allocation = static_cast<uint8_t*>(realloc(allocation, total_bytes)); //todo free on exit
    uint8_t* offset_alloc = allocation;

    offset_alloc += 0;
    world_data.entities = std::span<entity_t>{(entity_t*)(offset_alloc + 0), gWorld->entities.size()};

    offset_alloc += entity_bytes_pad;
    world_data.transforms = std::span<transform_t>{(transform_t*)(offset_alloc), gWorld->transforms.size()};

    offset_alloc += transform_bytes_pad;
    world_data.directional_lights = std::span<directionallight_t>((directionallight_t*)(offset_alloc), gWorld->lightmanager.directional_lights.size());

    offset_alloc += directional_light_bytes_pad;
    world_data.directional_shadowmaps = std::span<allocated_image_t>((allocated_image_t*)(offset_alloc), gWorld->lightmanager.directional_maps.size());

    offset_alloc += directional_map_bytes_pad;
    world_data.pointlights = std::span<pointlight_t>{(pointlight_t*)(offset_alloc), gWorld->lightmanager.pointlights.size()};

    offset_alloc += pointlight_bytes_pad;
    world_data.cube_shadowmaps = std::span<allocated_image_t>{(allocated_image_t*)(offset_alloc), gWorld->lightmanager.cubemaps.size()};

    memcpy(world_data.entities.data(), gWorld->entities.data(), size_bytes(gWorld->entities));
    memcpy(world_data.transforms.data(), gWorld->transforms.data(), size_bytes(gWorld->transforms));
    memcpy(world_data.directional_lights.data(), gWorld->lightmanager.directional_lights.data(), size_bytes(gWorld->lightmanager.directional_lights));
    memcpy(world_data.directional_shadowmaps.data(), gWorld->lightmanager.directional_maps.data(), size_bytes(gWorld->lightmanager.directional_maps));
    memcpy(world_data.pointlights.data(), gWorld->lightmanager.pointlights.data(), size_bytes(gWorld->lightmanager.pointlights));
    memcpy(world_data.cube_shadowmaps.data(), gWorld->lightmanager.cubemaps.data(), size_bytes(gWorld->lightmanager.cubemaps));
}

void main_thread_routine()
{
    tf::Taskflow taskflow{};

    taskflow.emplace(&::copy_imgui_draw_data).name("copy imgui data");
    taskflow.emplace(&::copy_world_data).name("copy world data");

    while(!(window_has_closed = glfwWindowShouldClose(gWindow)))
    {
        timespec worktime_start = timespec_time_now();
        program_time.frame_start = worktime_start;

        pthread_mutex_lock(&main2render_copy_mx);

        tf_executor->run(taskflow).wait();

        main_data_available = true;
        pthread_cond_signal(&main2render_copy_cv);
        pthread_mutex_unlock(&main2render_copy_mx);

        glfwPollEvents();
        gWorld->camera.tick(program_time.fp_delta);
        tick::dispatch(program_time.fp_delta);
        ui::iterate_windows();

        pthread_mutex_lock(&render_in_progress_mx);
        if(!render_finished)
        {
            pthread_cond_wait(&render_finished_cv, &render_in_progress_mx);
        }

        render_finished = false;
        pthread_mutex_unlock(&render_in_progress_mx);

        program_time.frame_count += 1;

        timespec worktime_end = timespec_time_now();
        timespec worktime_delta = worktime_end - worktime_start;

        timespec undershoot = program_time.min_frametime - worktime_delta;
        if(undershoot.tv_sec >= 0 && undershoot.tv_nsec > 0)
        {
            time_sleep(undershoot);
        }

        program_time.frame_end = timespec_time_now();
        program_time.delta = program_time.frame_end - program_time.frame_start;
        program_time.delta *= program_time.dilation;
        program_time.total +=  program_time.delta;
        program_time.fp_delta = timespec2double(program_time.delta);
    }
}

void* render_thread_routine(void*)
{
    while(!window_has_closed)
    {
        pthread_mutex_lock(&main2render_copy_mx);
        if(!main_data_available)
        {
            pthread_cond_wait(&main2render_copy_cv, &main2render_copy_mx);
        }

        main_data_available = false;
        pthread_mutex_unlock(&main2render_copy_mx);

        pthread_mutex_lock(&render_in_progress_mx);

        gVulkan->draw();

        render_finished = true;
        pthread_cond_signal(&render_finished_cv);
        pthread_mutex_unlock(&render_in_progress_mx);
    }

    pthread_exit(nullptr);
}

void launch_render_thread()
{
    LogTemp("launching render thread");

    pthread_mutexattr_t mx_attr;
    pthread_mutexattr_init(&mx_attr);

    pthread_mutex_init(&main2render_copy_mx, &mx_attr);
    pthread_mutex_init(&render_in_progress_mx, &mx_attr);

    pthread_condattr_t cv_attr;
    pthread_condattr_init(&cv_attr);

    pthread_cond_init(&main2render_copy_cv, &cv_attr);
    pthread_cond_init(&render_finished_cv, &cv_attr);

    pthread_attr_t thread_attr;
    pthread_attr_init(&thread_attr);
    pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED);

    pthread_create(&render_thread, &thread_attr, render_thread_routine, nullptr);

    pthread_setname_np(pthread_self(), "main thread");
    pthread_setname_np(render_thread, "render thread");
}

int glfw_main(int argc, char** argv)
{
    tf::Executor executor{};
    tf_executor = &executor;

    GLFWwindow* window = create_window();
    gWindow = window;

    world_t world{};
    gWorld = &world;

    vulkan_engine_t vulkan{window};
    gVulkan = &vulkan;
    vulkan.initialize();

    world.generate_world(0);

    tick::add(spin_lights, &world.lightmanager.pointlights);

    launch_render_thread();
    main_thread_routine();

    vulkan.shutdown();
    close_window(window);

    return 0;
}

bool reload_shaders()
{
    LogVulkan("reloading shaders");

    pthread_mutex_lock(&render_in_progress_mx);
    if(!render_finished)
    {
        pthread_cond_wait(&render_finished_cv, &render_in_progress_mx);
    }
    pthread_mutex_unlock(&render_in_progress_mx);

    if(system("cd ../shader/ && make -j compile_shaders") != 0)
    {
        LogVulkan("failed to compile shaders");
        return false;
    }

    gVulkan->device.waitIdle();
    gVulkan->shader_cache.reset();
    gVulkan->destroy_pipelines();
    gVulkan->create_pipelines();
    return true;
}

int x11_main(int argc, char** argv)
{
    /*
    x11_window xwindow{"cheemsit-x11", 1200, 1200, true};
    xwindow.open();

    vulkan_engine_t vulkan{};
    vulkan.xwindow = &xwindow;

    vulkan.create_instance();
    vulkan.create_debug_messenger();
    vulkan.create_surface();
    vulkan.pick_physical_device();
    vulkan.create_logical_device();
    vulkan.create_swapchain();
    vulkan.create_swapchain_views();
    vulkan.create_frames();
    vulkan.create_renderpass();
    vulkan.create_swapchain_framebuffers();
    vulkan.create_pipelines();
    vulkan.create_allocator();
    vulkan.load_models();

    while(xwindow.is_open())
    {
        program_time.frame_start = timespec_time_now();

        xwindow.poll_events();

        vulkan.draw(program_time.fp_delta);

        if(math::randrange(0l, 10000l) == 0)
        {
            vulkan.options.wireframe = !vulkan.options.wireframe;
        }

        //framcount is incremented in the vulkan engine
        program_time.frame_end = timespec_time_now();
        program_time.delta = program_time.frame_end - program_time.frame_start;
        program_time.delta *= program_time.dilation;
        program_time.total +=  program_time.delta;
        program_time.fp_delta = timespec2double(program_time.delta);
    }

    vulkan.shutdown();
*/
    return 0;
}
