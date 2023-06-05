#ifndef CHEEMSIT_GUI_VK_VULKAN_ENGINE_HPP
#define CHEEMSIT_GUI_VK_VULKAN_ENGINE_HPP

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#define VK_USE_PLATFORM_XCB_KHR
#include <vulkan/vulkan.hpp>

#include <cstdint>
#include <string_view>
#include <cstdint>
#include <bit>
#include <functional>
#include <ranges>

#include "shader/shader_include.hpp"
#include "vulkan_memory_allocator.hpp"
#include "slotmap.hpp"
#include "vector_types.hpp"
#include "vulkan_utility.hpp"
#include "vulkan_descriptor.hpp"
#include "name.hpp"
#include "vulkan_pipeline.hpp"
#include "camera.hpp"
#include "entity_manager.hpp"
#include "world.hpp"
#include "imgui.h"

class x11_window;
struct GLFWwindow;
class vulkan_engine_t;
class model_t;
class texture_t;
class world_t;

template<bool thread_safe>
class function_queue_t
{
public:
    struct callable_t
    {
        callable_t(void(*func_)(uint64_t), uint64_t arg_) : func(func_), arg(arg_) {}
        void(*func)(uint64_t);
        uint64_t arg;
    };

    function_queue_t()
    {
        if constexpr(thread_safe)
        {
            pthread_mutexattr_t attr;
            pthread_mutexattr_init(&attr);
            pthread_mutex_init(&mutex, &attr);
            pthread_mutexattr_destroy(&attr);
        }
    }

    template<typename T> requires(sizeof(T) == sizeof(uint64_t))
    void append(T arg, void(*func)(std::type_identity_t<T>))
    {
        lock();
        queue.emplace_back(std::bit_cast<void(*)(uint64_t)>(func), std::bit_cast<uint64_t>(arg));
        unlock();
    }

    void append(void(*func)())
    {
        lock();
        queue.emplace_back(std::bit_cast<void(*)(uint64_t)>(func), uint64_t{0});
        unlock();
    }

    void flush()
    {
        lock();
        for(auto it = queue.begin(); it != queue.end(); ++it)
        {
            it->func(it->arg);
        }
        queue.clear();
        unlock();
    }

    void flush_reverse()
    {
        lock();
        for(auto it = queue.rbegin(); it != queue.rend(); ++it)
        {
            it->func(it->arg);
        }
        queue.clear();
        unlock();
    }

    void lock()
    {
        if constexpr(thread_safe)
        {
            pthread_mutex_lock(&mutex);
        }
    }

    void unlock()
    {
        if constexpr(thread_safe)
        {
            pthread_mutex_unlock(&mutex);
        }
    }

    std::vector<callable_t> queue;

    struct empty_t{};
    std::conditional_t<thread_safe, pthread_mutex_t, empty_t> mutex;
};

struct directional_light_data_t
{
    directionallight_t light;
    glm::mat4x4 perspective;
};

struct frame_data_t
{
    function_queue_t<true> pre_render;
    function_queue_t<true> next_render; //functions submitted to this queue will be executed not at this frame, but the next time this frame draws

    vk::CommandBuffer cmd;
    vk::CommandBuffer recording;
    vk::CommandBuffer pending;

    vk::CommandBuffer shadowpass_cmd;

    vk::Fence in_flight;
    vk::Semaphore image_available;
    vk::Semaphore draw_finished;

    allocated_buffer_t entity_transform_buffer;
    uint64_t entity_transforms_allocated;

    allocated_buffer_t directional_light_buffer;

    allocated_buffer_t pointlight_buffer;
    allocated_buffer_t pointlight_projection_buffer;

    vk::DescriptorSet world_set; //contains entity transforms and pointlights
    vk::DescriptorSet pointlight_shadow_set; //contains pointlight shadow cubemaps
    vk::DescriptorSet pointlight_projection_set; //contains cube faces

    vk::DescriptorSet directional_shadow_set; //contains shadow maps
    vk::DescriptorSet directional_light_projection_set;
};

struct entity_batch_t
{
    slothandle_t<model_t> model;
    slothandle_t<texture_t> texture;
    slothandle_t<material_t> material;
    std::vector<uint32_t> indices;
};

class vulkan_engine_t
{
public:
    inline static constexpr uint32_t FRAMES_IN_FLIGHT = 3;

    vulkan_engine_t(GLFWwindow* window);

    template<typename T>
    void queue_destruction(T* object);

    void submit_error(std::string message) const;

    bool device_supports_extensions(vk::PhysicalDevice pdevice);
    bool device_supports_queues(vk::PhysicalDevice pdevice); //also sets indices
    bool device_supports_swapchain(vk::PhysicalDevice pdevice); //also sets swapchain details
    bool device_supports_features(vk::PhysicalDevice pdevice);

    allocated_buffer_t allocate_buffer(vk::BufferCreateInfo& bufferinfo, vma::AllocationCreateInfo& allocationinfo, std::string debug_name = "");
    void destroy_buffer(allocated_buffer_t allocated_buffer);

    void reallocate_buffer(allocated_buffer_t& buffer, const vk::BufferCreateInfo& bufferinfo, const vma::AllocationCreateInfo& allocationinfo, std::string debug_name = "");

    allocated_buffer_t allocate_staging_buffer(size_t buffer_size, std::string debug_name = "");
    allocated_buffer_t allocate_vertex_buffer(size_t buffer_size, std::string debug_name = "");
    allocated_buffer_t allocate_instance_buffer(size_t buffer_size, std::string debug_name = "");
    allocated_buffer_t allocate_index_buffer(size_t buffer_size, std::string debug_name = "");

    allocated_image_t allocate_image(vk::ImageCreateInfo& imageinfo, vma::AllocationCreateInfo& allocationinfo, std::string debug_name = "");
    allocated_image_t allocate_image(const vk::ImageCreateInfo& imageinfo, vk::ImageViewCreateInfo& viewinfo, const vma::AllocationCreateInfo& allocationinfo, std::string debug_name = "") const;
    void destroy_image(allocated_image_t image);

    allocated_image_t allocate_shadow_cubemap(uint32_t extent, std::string debug_name = "");
    texture_image_t allocate_texture_image(vk::Extent3D extent, std::string debug_name = "");
    void copy_buffer2texture(texture_image_t& texture, allocated_buffer_t staging_buffer, vk::Extent3D extent);
    void generate_mipmaps(texture_image_t& texture, vk::Extent3D extent);

    allocated_image_t allocate_directional_shadowmap(uint32_t width_height, std::string debug_name = "");

    vk::ImageView make_texture_view(vk::Image image, std::string debug_name = "");

    void copy_vertex_attribute_buffer(allocated_buffer_t dst, allocated_buffer_t src, size_t size);
    void copy_texture(allocated_image_t dst, allocated_buffer_t src, vk::Extent3D extent, void* data);

    void copy_index_buffer(allocated_buffer_t dst, allocated_buffer_t src, size_t size);

    void create_directional_shadow_sampler();
    void create_shadow_cubemap_sampler();

    void create_model_pipeline();
    void create_wireframe_pipeline();
    void create_line_pipeline();
    void create_directional_light_pipeline();
    void create_pointlight_pipeline();
    void create_pointlight_mesh_pipeline();
    void create_particle_pipeline();
    void create_particle_compute_pipeline();

    std::pair<vk::Viewport, vk::Rect2D> whole_render_area() const;

    void wait_all_frames();

    vk::Format find_image_format(vk::FormatFeatureFlags feature_flags, std::span<vk::Format> candidates) const;
    vk::Format find_buffer_format(vk::FormatFeatureFlags feature_flags, std::span<vk::Format> candidates) const;

    size_t pad_uniform_buffer_size(size_t size) const;

    void destroy_pipelines();

    void initialize();
    void shutdown();

    void create_instance();
    void create_debug_messenger();
    void create_surface();
    void pick_physical_device();
    void create_logical_device();
    void initialize_helpers();
    void create_allocator();
    void create_depth_image();
    void create_hdr_color_image();
    void create_swapchain(vk::SwapchainKHR old_swapchain = nullptr);
    void create_swapchain_views();
    void queue_swapchain_destruction();
    void create_frames();
    void create_buffers();
    void create_set_layouts();
    void allocate_frames();
    void load_files();
    void create_texture_sampler();
    void create_pipelines();
    void initialize_imgui();

    void recreate_swapchain();

    uint64_t frame_index() const;
    frame_data_t& active_frame();
    frame_data_t& next_frame();
    frame_data_t& prev_frame();

    void check_buffer_sizes(frame_data_t& frame);
    void upload_device_global_data();
    void upoad_transforms();
    void upload_directional_lights();
    void upload_pointlights();
    void upload_particle_control();
    void flush_uploads();

    void draw();

    std::vector<entity_batch_t> make_entity_batches();
    uint32_t acquire_swapchain_image(frame_data_t& frame);
    void prepare_frame(frame_data_t& frame);
    void begin_swapchain_render(frame_data_t& frame, uint32_t swapchain_image);
    void end_swapchain_render(frame_data_t& frame, uint32_t swapchain_image);
    void directional_light_pass(frame_data_t& frame, std::span<entity_batch_t> batches, uint32_t light_index);
    void pointlight_shadow_pass(frame_data_t& frame, std::span<entity_batch_t> batches, uint32_t pointlight_index);
    void pointlight_mesh_pass(frame_data_t& frame);
    void compute_pass(frame_data_t& frame);
    void entity_pass(frame_data_t& frame, std::span<entity_batch_t> batches);
    void particle_pass(frame_data_t& frame);
    void ui_pass(frame_data_t& frame);
    void submit_commands(frame_data_t& frame);
    void present_swapchain_image(frame_data_t& frame, uint32_t swapchain_image);

    struct queue_indices_t
    {
        uint32_t graphics;
        uint32_t present;
        uint32_t transfer;
        uint32_t compute;
    };

    struct queue_handles_t
    {
        vk::Queue graphics;
        vk::Queue present;
        vk::Queue transfer;
        vk::Queue compute;
    };

    GLFWwindow* glfwwindow = nullptr;

    std::mutex recording_mx;

    std::vector<const char*> validation_layers;
    std::vector<const char*> instance_extensions;
    std::vector<const char*> device_extensions;

    vk::PhysicalDeviceProperties2 gpu_properties;
    vk::PhysicalDeviceFeatures2 gpu_features;
    vk::PhysicalDeviceVulkan13Features gpu_vk13features;

    function_queue_t<false> destruction_que;

    std::array<frame_data_t, FRAMES_IN_FLIGHT> frames;

    vk::Instance instance;
    vk::DebugUtilsMessengerEXT debug_messenger;
    vma::Allocator allocator;
    vk::SurfaceKHR surface;
    vk::PhysicalDevice physical_device;
    vk::Device device;

    vk::SwapchainKHR swapchain;
    std::vector<vk::Image> swapchain_images;
    std::vector<vk::ImageView> swapchain_image_views;

    allocated_image_t swapchain_depth_image;
    allocated_image_t hdr_color_image;
    vk::Format hdr_color_format;

    queue_indices_t queue_indices;
    queue_handles_t queues;

    vk::SurfaceFormatKHR surface_format;
    vk::PresentModeKHR present_mode;
    vk::SharingMode sharing_mode;
    vk::Extent2D image_extent;
    uint32_t min_image_count;

    vk::Format depth_format;
    vk::Format cube_shadow_format;
    vk::Format directional_shadow_format;

    vk::CommandPool graphics_command_pool;

    vk::DescriptorSetLayout global_set_layout;
    vk::DescriptorSet global_descriptor_set;
    allocated_buffer_t global_buffer;

    vk::DescriptorSetLayout world_set_layout; //set 1 contains entities and lights
    vk::DescriptorSetLayout pointlight_shadow_set_layout; //set 2, contains cube shadowmap images
    vk::DescriptorSetLayout texture_set_layout; // set 3, contains textures

    vk::DescriptorSetLayout pointlight_projection_layout; //set 0
    vk::DescriptorSetLayout directional_light_projection_layout;

    vk::DescriptorSetLayout directional_shadow_layout;

    vk::Sampler texture_sampler;
    vk::Sampler directional_shadow_sampler;
    vk::Sampler cube_shadow_sampler;

    vk::PipelineLayout directional_light_pipelinelayout;
    vk::Pipeline directional_light_pipeline;

    vk::PipelineLayout pointlight_pipelinelayout;
    vk::Pipeline pointlight_pipeline;

    vk::PipelineLayout line_pipelinelayout;
    vk::Pipeline line_pipeline;

    vk::PipelineLayout pointlight_mesh_pipelinelayout;
    vk::Pipeline pointlight_mesh_pipeline;

    vk::PipelineLayout animate_particle_layout;
    vk::Pipeline animate_particle_pipeline;

    descriptor_allocator_t descriptor_allocator;
    descriptor_layout_cache_t descriptor_cache;
    descriptor_builder_t descriptor_builder;

    pipeline_layout_cache_t pipeline_cache;
    shader_module_cache_t shader_cache;
    graphics_pipeline_builder_t pipeline_builder;
    vk::CommandPool shadowpass_cmdpool;

    render_thread_data_t world_data;
    ImDrawData imgui_data;

    vk::DescriptorPool ImGUI_pool;

    particle_emitter_t particle_emitter;

    vk::DescriptorSetLayout particle_setlayout;
    vk::DescriptorSet particle_set;

    allocated_buffer_t particle_control_buffer;
};

inline vulkan_engine_t* gVulkan = nullptr;
inline vulkan_engine_t& get_vulkan()
{
    return *gVulkan;
}


#endif //CHEEMSIT_GUI_VK_VULKAN_ENGINE_HPP
