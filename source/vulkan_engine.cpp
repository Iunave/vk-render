#include "vulkan_engine.hpp"
#include "window.hpp"
#include <vulkan/vk_platform.h>
#include <fmt/format.h>
#include <fmt/color.h>
#include <set>
#include <numbers>
#include <ratio>
#include <utility>
#include "math.hpp"
#include "camera.hpp"
#include "time.hpp"
#include "world.hpp"
#include "vulkan_model.hpp"
#include "log.hpp"
#include "implot/implot.h"
#include "taskflow/taskflow/taskflow.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#define GLFW_EXPOSE_NATIVE_XCB //doesnt exist??
#include <GLFW/glfw3native.h>

#include "imgui.h"
#include "imgui/backends/imgui_impl_vulkan.h"
#include "imgui/backends/imgui_impl_glfw.h"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#define CHECK_QUEUE_DUPLICATE(ptr)\
for(auto callable : destruction_que.queue)\
{\
    if(callable.arg == std::bit_cast<uint64_t>(ptr))\
    {\
        return;\
    }\
}

template<typename T>
inline void vulkan_engine_t::queue_destruction(T* object)
{
    CHECK_QUEUE_DUPLICATE(object)
    destruction_que.append(object, [](T* object_)
    {
        gVulkan->device.destroy(*object_);
    });
}

template<>
inline void vulkan_engine_t::queue_destruction<allocated_buffer_t>(allocated_buffer_t* buffer)
{
    CHECK_QUEUE_DUPLICATE(buffer)
    destruction_que.append(buffer, [](allocated_buffer_t* buffer_)
    {
        gVulkan->destroy_buffer(*buffer_);
    });
}

template<>
inline void vulkan_engine_t::queue_destruction<allocated_image_t>(allocated_image_t* image)
{
    CHECK_QUEUE_DUPLICATE(image)
    destruction_que.append(image, [](allocated_image_t* image_)
    {
        gVulkan->destroy_image(*image_);
    });
}
#undef CHECK_QUEUE_DUPLICATE

static vk::Bool32 vulkan_debug_callback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity, vk::DebugUtilsMessageTypeFlagsEXT type, vk::DebugUtilsMessengerCallbackDataEXT* callback_data, void* user_data)
{
    fmt::color color;
    switch(severity)
    {
        case vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose:
            color = fmt::color::white_smoke;
            break;
        case vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo:
            color = fmt::color::white;
            break;
        case vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning:
            color = fmt::color::yellow;
            break;
        case vk::DebugUtilsMessageSeverityFlagBitsEXT::eError:
            color = fmt::color::red;
            break;
    }

    fmt::print("\n");

    fmt::print(fmt::fg(color), "{} {}\n", vk::to_string(severity), vk::to_string(type));
    fmt::print(fmt::fg(color), "Message ID Number {}, Message ID String : {}\n", callback_data->messageIdNumber, callback_data->pMessageIdName);
    fmt::print(fmt::fg(color), "{}\n", callback_data->pMessage);

    fmt::print(fmt::fg(color), "\nObjects - {}\n", callback_data->objectCount);

    if(callback_data->objectCount > 0)
    {
        for(uint32_t object = 0; object < callback_data->objectCount; ++object)
        {
            auto& objectref = callback_data->pObjects[object];

            fmt::print(fmt::fg(color), "Object[{}] - Type - {}, Handle - {}, Name - {}\n",
                       object,
                       vk::to_string(objectref.objectType),
                       objectref.objectHandle,
                       objectref.pObjectName ? objectref.pObjectName : "?");
        }
    }

    if(callback_data->cmdBufLabelCount > 0)
    {
        fmt::print(fmt::fg(color), "\nCommand Buffer Labels - {}\n", callback_data->cmdBufLabelCount);

        for(uint32_t label = 0; label < callback_data->cmdBufLabelCount; ++label)
        {
            auto& labelref = callback_data->pCmdBufLabels[label];
            fmt::print(fmt::fg(color), "Label[{}] - {}\n", label, labelref.pLabelName);
        }
    }

    if(callback_data->queueLabelCount > 0)
    {
        fmt::print(fmt::fg(color), "\nQueue Labels - {}\n", callback_data->queueLabelCount);

        for(uint32_t label = 0; label < callback_data->queueLabelCount; ++label)
        {
            auto& labelref = callback_data->pQueueLabels[label];
            fmt::print(fmt::fg(color), "Label[{}] - {}\n", label, labelref.pLabelName);
        }
    }

    fmt::print("\n");

    if(severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError)
    {
        abort();
    }

    return false;
}

void vulkan_engine_t::submit_error(std::string message) const
{
    static std::string message_save;
    message_save = std::move(message);

    static vk::DebugUtilsMessengerCallbackDataEXT callback_data{};
    callback_data.pMessage = message_save.data();
    callback_data.pMessageIdName = "none";

    instance.submitDebugUtilsMessageEXT(vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
                                        vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation,
                                        &callback_data);
}

static vk::DebugUtilsMessengerCreateInfoEXT make_messenger_create_info()
{
    using enum vk::DebugUtilsMessageSeverityFlagBitsEXT;
    using enum vk::DebugUtilsMessageTypeFlagBitsEXT;

    vk::DebugUtilsMessengerCreateInfoEXT messenger_info{};
    messenger_info.messageSeverity = eWarning | eError;
    messenger_info.messageType = eValidation | eGeneral | ePerformance | eDeviceAddressBinding;

    messenger_info.pfnUserCallback = reinterpret_cast<PFN_vkDebugUtilsMessengerCallbackEXT>(&::vulkan_debug_callback);
    return messenger_info;
}

bool vulkan_engine_t::device_supports_extensions(vk::PhysicalDevice pdevice)
{
    for(const char* logical_device_extension : device_extensions)
    {
        bool extension_supported = false;
        for(vk::ExtensionProperties& extension_property : pdevice.enumerateDeviceExtensionProperties())
        {
            if(strcmp(logical_device_extension, extension_property.extensionName) == 0)
            {
                extension_supported = true;
                break;
            }
        }

        if(!extension_supported)
        {
            return false;
        }
    }

    return true;
}

bool vulkan_engine_t::device_supports_queues(vk::PhysicalDevice pdevice) //also sets indices
{
    std::vector<vk::QueueFamilyProperties> queue_properties = pdevice.getQueueFamilyProperties();

    memset(&queue_indices, -1, sizeof(queue_indices));

    for(uint32_t queue_index = 0; queue_index < queue_properties.size(); ++queue_index)
    {
        vk::QueueFlags flags = queue_properties[queue_index].queueFlags;

        if(queue_indices.present == -1 || queue_indices.present != queue_indices.graphics)
        {
            if(pdevice.getSurfaceSupportKHR(queue_index, surface))
            {
                queue_indices.present = queue_index;
            }

            if(flags & vk::QueueFlagBits::eGraphics)
            {
                queue_indices.graphics = queue_index;
            }
        }

        if(queue_indices.transfer == -1 || queue_indices.transfer == queue_indices.present || queue_indices.transfer == queue_indices.graphics)
        {
            if(flags & vk::QueueFlagBits::eTransfer)
            {
                queue_indices.transfer = queue_index;
            }
        }

        if(queue_indices.compute == -1 || queue_indices.compute == queue_indices.present || queue_indices.compute == queue_indices.graphics)
        {
            if(flags & vk::QueueFlagBits::eCompute)
            {
                queue_indices.compute = queue_index;
            }
        }
    }

    return queue_indices.present != -1 && queue_indices.graphics != -1 && queue_indices.transfer != -1 && queue_indices.compute != -1;
}

static uint32_t find_surface_format(vk::PhysicalDevice pdevice, vk::SurfaceKHR surface, std::span<vk::SurfaceFormatKHR> candidates)
{
    std::vector<vk::SurfaceFormatKHR> available = pdevice.getSurfaceFormatsKHR(surface);

    for(uint32_t candidate_index = 0; candidate_index < candidates.size(); ++candidate_index)
    {
        for(uint32_t available_index = 0; available_index < available.size(); ++available_index)
        {
            if(available[available_index] == candidates[candidate_index])
            {
                return candidate_index;
            }
        }
    }
    return -1;
}

static uint32_t find_present_mode(vk::PhysicalDevice pdevice, vk::SurfaceKHR surface, std::span<vk::PresentModeKHR> candidates)
{
    std::vector<vk::PresentModeKHR> available = pdevice.getSurfacePresentModesKHR(surface);

    for(uint32_t candidate_index = 0; candidate_index < candidates.size(); ++candidate_index)
    {
        for(uint32_t available_index = 0; available_index < available.size(); ++available_index)
        {
            if(available[available_index] == candidates[candidate_index])
            {
                return candidate_index;
            }
        }
    }
    return -1;
}

bool vulkan_engine_t::device_supports_swapchain(vk::PhysicalDevice pdevice)
{
    std::array surface_formats
    {
        vk::SurfaceFormatKHR{vk::Format::eR8G8B8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear},
        vk::SurfaceFormatKHR{vk::Format::eB8G8R8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear},
        vk::SurfaceFormatKHR{vk::Format::eR8G8B8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear},
        vk::SurfaceFormatKHR{vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear},
    };

    uint32_t found_format = find_surface_format(pdevice, surface, surface_formats);

    if(found_format == -1)
    {
        return false;
    }
    else
    {
        surface_format = surface_formats[found_format];
    }

    std::array present_modes
    {
        vk::PresentModeKHR::eMailbox,
        vk::PresentModeKHR::eImmediate,
        vk::PresentModeKHR::eFifoRelaxed,
        vk::PresentModeKHR::eFifo
    };

    uint32_t found_mode = find_present_mode(pdevice, surface, present_modes);

    if(found_mode == -1)
    {
        return false;
    }
    else
    {
        present_mode = present_modes[found_mode];
    }

    return true;
}

bool vulkan_engine_t::device_supports_features(vk::PhysicalDevice pdevice)
{
    static vk::PhysicalDeviceDescriptorIndexingFeatures descriptor_indexing{};
    descriptor_indexing.pNext = nullptr;
    descriptor_indexing.runtimeDescriptorArray = true;
    descriptor_indexing.shaderSampledImageArrayNonUniformIndexing = true;
    descriptor_indexing.descriptorBindingVariableDescriptorCount = true;
    descriptor_indexing.descriptorBindingPartiallyBound = true;

    static vk::PhysicalDeviceScalarBlockLayoutFeatures scalar_block_layout{true, &descriptor_indexing};
    static vk::PhysicalDeviceShaderDrawParametersFeatures shader_draw_parameters{true, &scalar_block_layout};

    gpu_vk13features.setPNext(&shader_draw_parameters)
    .setSynchronization2(true)
    .setDynamicRendering(true);

    gpu_features.pNext = &gpu_vk13features;

    gpu_properties = pdevice.getProperties2();
    gpu_features.features = pdevice.getFeatures();// get features2 seems to be broken, so just assume true for all of them...

    return gpu_features.features.samplerAnisotropy
    && gpu_vk13features.synchronization2
    && gpu_vk13features.dynamicRendering
    && shader_draw_parameters.shaderDrawParameters
    && scalar_block_layout.scalarBlockLayout;
}

vulkan_engine_t::vulkan_engine_t(GLFWwindow* window)
{
    glfwwindow = window;
    allocator = nullptr;
    instance = nullptr;
    debug_messenger = nullptr;
    surface = nullptr;
    physical_device = nullptr;
    device = nullptr;

    std::memset(&queue_indices, -1, sizeof(queue_indices));
    std::memset(&queues, 0, sizeof(queues));

    min_image_count = -1;

    surface_format.format = vk::Format::eB8G8R8A8Srgb;
    surface_format.colorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
    present_mode = vk::PresentModeKHR::eMailbox;
    sharing_mode = {};
    image_extent = vk::Extent2D{0, 0};

    instance_extensions.emplace_back(VK_KHR_SURFACE_EXTENSION_NAME);
    instance_extensions.emplace_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);

    device_extensions.emplace_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    //device_extensions.emplace_back(VK_KHR_16BIT_STORAGE_EXTENSION_NAME);
    //device_extensions.emplace_back(VK_AMD_GPU_SHADER_HALF_FLOAT_EXTENSION_NAME);

#ifndef NDEBUG
    validation_layers.emplace_back("VK_LAYER_KHRONOS_validation");
    validation_layers.emplace_back("VK_LAYER_KHRONOS_synchronization2");

    instance_extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

    VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);
}

void vulkan_engine_t::initialize()
{
    create_instance();
    create_debug_messenger();
    create_surface();
    pick_physical_device();
    create_logical_device();
    initialize_helpers();
    create_allocator();
    create_directional_shadow_sampler();
    create_shadow_cubemap_sampler();
    create_texture_sampler();
    create_swapchain();
    create_swapchain_views();
    queue_swapchain_destruction();
    create_depth_image();
    create_frames();
    create_buffers();
    allocate_frames();
    load_files();
    create_set_layouts();
    create_pipelines();
    initialize_imgui();
}

void vulkan_engine_t::create_instance()
{
    LogVulkan("creating instance");

    vk::ApplicationInfo app_info{};
    app_info.pApplicationName = "cheemsit-gui";
    app_info.pEngineName = "cheemsit-engine";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_3;

    auto instance_info = vk::InstanceCreateInfo{}
    .setPApplicationInfo(&app_info)
    .setPEnabledLayerNames(validation_layers)
    .setPEnabledExtensionNames(instance_extensions);

#ifndef NDEBUG
    for(const char* name : validation_layers)
    {
        LogVulkan("using validation layer {}", name);
    }

    for(const char* name : instance_extensions)
    {
        LogVulkan("using instance extension {}", name);
    }

    vk::DebugUtilsMessengerCreateInfoEXT messenger_info = make_messenger_create_info();
    instance_info.pNext = &messenger_info;
    instance = vk::createInstance(instance_info, nullptr);
#else
    instance_info.pNext = nullptr;
    instance = vk::createInstance(instance_info, nullptr);
#endif

    VULKAN_HPP_DEFAULT_DISPATCHER.init(instance);
}

void vulkan_engine_t::create_debug_messenger()
{
#ifndef NDEBUG
    LogVulkan("creating debug messenger");

    vk::DebugUtilsMessengerCreateInfoEXT messenger_info = make_messenger_create_info();
    debug_messenger = instance.createDebugUtilsMessengerEXT(messenger_info);
#endif
}

void vulkan_engine_t::create_surface()
{
    LogVulkan("creating surface");
/*
    if(xwindow)
    {
        vk::XcbSurfaceCreateInfoKHR surface_info{};
        surface_info.connection = xwindow->connection;
        surface_info.window = xwindow->window;
        surface = instance.createXcbSurfaceKHR(surface_info);
    }
    */
    if(glfwwindow)
    {
        resultcheck = glfwCreateWindowSurface(instance, glfwwindow, nullptr, reinterpret_cast<VkSurfaceKHR*>(&surface));
    }
}

void vulkan_engine_t::pick_physical_device()
{
    LogVulkan("picking physical device");

    for(vk::PhysicalDevice pdevice : instance.enumeratePhysicalDevices())
    {
        LogVulkan("checking {}", pdevice.getProperties().deviceName);

        if(!device_supports_extensions(pdevice))
        {
            LogVulkan("does not support extensions");
            continue;
        }

        if(!device_supports_swapchain(pdevice))
        {
            LogVulkan("does not support swapchain");
            continue;
        }

        if(!device_supports_queues(pdevice))
        {
            LogVulkan("does not support queues");
            continue;
        }

        if(!device_supports_features(pdevice))
        {
            LogVulkan("does not support features");
            continue;
        }

        physical_device = pdevice;

        if(gpu_properties.properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
        {
            break;
        }
    }

    if(!physical_device)
    {
        submit_error("could not find a physical device");
    }

    std::array directional_shadow_formats{vk::Format::eD32Sfloat, vk::Format::eD16Unorm};
    directional_shadow_format = find_image_format(vk::FormatFeatureFlagBits::eDepthStencilAttachment, directional_shadow_formats);

    std::array cube_shadow_formats{vk::Format::eD32Sfloat, vk::Format::eD16Unorm};
    cube_shadow_format = find_image_format(vk::FormatFeatureFlagBits::eDepthStencilAttachment, cube_shadow_formats);

    std::array depth_formats{vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint};
    depth_format = find_image_format(vk::FormatFeatureFlagBits::eDepthStencilAttachment, depth_formats);

    LogVulkan("using {}", gpu_properties.properties.deviceName);
    LogVulkan("swapchain format {} {} {}", vk::to_string(surface_format.format), vk::to_string(surface_format.colorSpace), vk::to_string(present_mode));
    LogVulkan("depth format {}", vk::to_string(depth_format));
    LogVulkan("shadow format {}", vk::to_string(cube_shadow_format));
    LogVulkan("graphics que = {}", queue_indices.graphics);
    LogVulkan("present que = {}", queue_indices.present);
    LogVulkan("transfer que = {}", queue_indices.transfer);
    LogVulkan("compute que = {}", queue_indices.compute);
}

void vulkan_engine_t::create_logical_device()
{
    LogVulkan("creating logical device");

    float priority = 1.f;

    std::array queue_indices_arr{queue_indices.graphics, queue_indices.present, queue_indices.transfer, queue_indices.compute};
    std::vector<vk::DeviceQueueCreateInfo> queue_infos{};

    vk::DeviceQueueCreateInfo queue_create_info{};
    queue_create_info.queueCount = 1;
    queue_create_info.pQueuePriorities = &priority;

    for(uint32_t queue_index : queue_indices_arr)
    {
        for(vk::DeviceQueueCreateInfo queue_info : queue_infos)
        {
            if(queue_info.queueFamilyIndex == queue_index) //remove duplicates
            {
                goto duplicate;
            }
        }

        queue_create_info.queueFamilyIndex = queue_index;
        queue_infos.emplace_back(queue_create_info);

        duplicate:;
    }

    for(const char *name: device_extensions)
    {
        LogVulkan("using device extension {}", name);
    }

    vk::DeviceCreateInfo device_info{};
    device_info.pNext = &gpu_features;
    device_info.pQueueCreateInfos = queue_infos.data();
    device_info.queueCreateInfoCount = queue_infos.size();
    device_info.pEnabledFeatures = nullptr;//&gpu_features.features;
    device_info.ppEnabledExtensionNames = device_extensions.data();
    device_info.enabledExtensionCount = device_extensions.size();

    device = physical_device.createDevice(device_info);
    vkutil::name_object(device, "logical device");

    VULKAN_HPP_DEFAULT_DISPATCHER.init(device);

    queues.graphics = device.getQueue(queue_indices.graphics, 0);
    queues.present = device.getQueue(queue_indices.present, 0);
    queues.transfer = device.getQueue(queue_indices.transfer, 0);
    queues.compute = device.getQueue(queue_indices.compute, 0);

    vkutil::name_object(queues.graphics, "graphics queue");
    vkutil::name_object(queues.present, "presentation queue");
    vkutil::name_object(queues.transfer, "transfer queue");
    vkutil::name_object(queues.compute, "compute queue");
}

void vulkan_engine_t::initialize_helpers()
{
    LogVulkan("initializing helpers");

    descriptor_allocator.init(device);
    descriptor_cache.init(device);
    descriptor_builder.init(&descriptor_allocator, &descriptor_cache);

    pipeline_cache.init(device);
    shader_cache.init(device);
    pipeline_builder.init(&pipeline_cache, &shader_cache);

    destruction_que.append([]()
    {
       gVulkan->descriptor_allocator.shutdown();
       gVulkan->descriptor_cache.shutdown();
       gVulkan->pipeline_cache.shutdown();
       gVulkan->shader_cache.shutdown();
    });
}

void vulkan_engine_t::create_allocator()
{
    LogVulkan("creating memory allocator");

    auto allocator_info = vma::AllocatorCreateInfo{}
    .setVulkanApiVersion(VK_API_VERSION_1_3)
    .setInstance(instance)
    .setPhysicalDevice(physical_device)
    .setDevice(device);

    allocator = vma::createAllocator(allocator_info);

    destruction_que.append(allocator, [](vma::Allocator allocator_)
    {
        allocator_.destroy();
    });
}

void vulkan_engine_t::create_depth_image()
{
    LogVulkan("creating depth image");

    auto image_info = vk::ImageCreateInfo{}
    .setImageType(vk::ImageType::e2D)
    .setFormat(depth_format)
    .setExtent( vk::Extent3D{image_extent, 1})
    .setMipLevels(1)
    .setArrayLayers(1)
    .setSamples(vk::SampleCountFlagBits::e1)
    .setTiling(vk::ImageTiling::eOptimal)
    .setUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment);

    auto allocation_info = vma::AllocationCreateInfo{}
    .setUsage(vma::MemoryUsage::eAutoPreferDevice)
    .setFlags(vma::AllocationCreateFlagBits::eStrategyBestFit);

    auto view_info = vk::ImageViewCreateInfo{}
    .setComponents(vk::ComponentMapping{vk::ComponentSwizzle::eR})
    .setFormat(depth_format)
    .setViewType(vk::ImageViewType::e2D);
    view_info.subresourceRange
    .setAspectMask(vk::ImageAspectFlagBits::eDepth)
    .setLayerCount(1)
    .setLevelCount(1)
    .setBaseMipLevel(0)
    .setBaseArrayLayer(0);

    swapchain_depth_image = allocate_image(image_info, view_info, allocation_info, "depth");
}

void vulkan_engine_t::create_hdr_color_image()
{
    LogVulkan("creating hdr color image");

    std::array formats{vk::Format::eR32G32B32A32Sfloat, vk::Format::eR16G16B16A16Sfloat};
    hdr_color_format = find_image_format(vk::FormatFeatureFlagBits::eColorAttachment, formats);

    auto image_info = vk::ImageCreateInfo{}
    .setFormat(hdr_color_format)
    .setTiling(vk::ImageTiling::eOptimal)
    .setUsage(vk::ImageUsageFlagBits::eColorAttachment)
    .setImageType(vk::ImageType::e2D)
    .setInitialLayout(vk::ImageLayout::eUndefined)
    .setExtent(vk::Extent3D{image_extent, 1})
    .setMipLevels(1)
    .setArrayLayers(1)
    .setSamples(vk::SampleCountFlagBits::e1);

    auto allocation_info = vma::AllocationCreateInfo{}
    .setUsage(vma::MemoryUsage::eAutoPreferDevice)
    .setFlags(vma::AllocationCreateFlagBits::eStrategyBestFit);

    auto view_info = vk::ImageViewCreateInfo{}
    .setComponents(vk::ComponentMapping{})
    .setFormat(hdr_color_format)
    .setViewType(vk::ImageViewType::e2D);
    view_info.subresourceRange
    .setAspectMask(vk::ImageAspectFlagBits::eColor)
    .setLayerCount(1)
    .setLevelCount(1)
    .setBaseMipLevel(0)
    .setBaseArrayLayer(0);

    hdr_color_image = allocate_image(image_info, view_info, allocation_info, "hdr color");
}

void vulkan_engine_t::create_swapchain(vk::SwapchainKHR old_swapchain)
{
    LogVulkan("creating swapchain");

    vk::SurfaceCapabilitiesKHR surface_capabilities = physical_device.getSurfaceCapabilitiesKHR(surface);

    const vk::Extent2D& current_extent = surface_capabilities.currentExtent;

    if(current_extent.width != std::numeric_limits<uint32_t>::max()) //we have to use the capabilities extent
    {
        image_extent = current_extent;
    }
    else
    {
        int width; int height;
        glfwGetFramebufferSize(glfwwindow, &width, &height);

        const vk::Extent2D& min_extent = surface_capabilities.minImageExtent;
        const vk::Extent2D& max_extent = surface_capabilities.maxImageExtent;

        width = std::clamp(uint32_t(width), min_extent.width, max_extent.width);
        height = std::clamp(uint32_t(height), min_extent.height, max_extent.height);

        image_extent = vk::Extent2D{uint32_t(width), uint32_t(height)};
    }

    LogVulkan("image extent {} {}", image_extent.width, image_extent.height);

    min_image_count = std::max(surface_capabilities.minImageCount, FRAMES_IN_FLIGHT);

    if UNLIKELY(surface_capabilities.maxImageCount != 0 && surface_capabilities.maxImageCount < min_image_count)
    {
        submit_error(fmt::format("max image count ({}) is less than frame overlap ({})", surface_capabilities.maxImageCount, FRAMES_IN_FLIGHT));
    }

    vk::SwapchainCreateInfoKHR swapchain_info{};
    swapchain_info.surface = surface;
    swapchain_info.minImageCount = min_image_count;
    swapchain_info.imageFormat = surface_format.format;
    swapchain_info.imageColorSpace = surface_format.colorSpace;
    swapchain_info.imageExtent = image_extent;
    swapchain_info.imageArrayLayers = 1;
    swapchain_info.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;

    const std::array used_queues{queue_indices.graphics, queue_indices.present};

    if(queue_indices.graphics != queue_indices.present)
    {
        sharing_mode = vk::SharingMode::eConcurrent;//todo switch to exclusive for better performance

        swapchain_info.queueFamilyIndexCount = used_queues.size();
        swapchain_info.pQueueFamilyIndices = used_queues.data();
    }
    else
    {
        sharing_mode = vk::SharingMode::eExclusive;
    }

    swapchain_info.imageSharingMode = sharing_mode;

    swapchain_info.preTransform = surface_capabilities.currentTransform; //image transformating eg rotation
    swapchain_info.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque; //blending with other windows
    swapchain_info.presentMode = present_mode;
    swapchain_info.clipped = true; //discards hidden pixels

    swapchain_info.oldSwapchain = old_swapchain;

    swapchain = device.createSwapchainKHR(swapchain_info);
    vkutil::name_object(swapchain, "swapchain");
}

void vulkan_engine_t::create_swapchain_views()
{
    LogVulkan("creating swapchain images");
    
    swapchain_images = device.getSwapchainImagesKHR(swapchain);
    swapchain_image_views.resize(swapchain_images.size());

    LogVulkan("swapchain image count {}", swapchain_images.size());

    auto subresource = vk::ImageSubresourceRange{}
    .setAspectMask(vk::ImageAspectFlagBits::eColor)
    .setBaseMipLevel(0)
    .setLevelCount(1)
    .setBaseArrayLayer(0)
    .setLayerCount(1);

    auto view_info = vk::ImageViewCreateInfo{}
    .setFormat(surface_format.format)
    .setComponents(vk::ComponentSwizzle::eIdentity)
    .setSubresourceRange(subresource)
    .setViewType(vk::ImageViewType::e2D);

    for(uint64_t index = 0; index < swapchain_images.size(); ++index)
    {
        vkutil::name_object(swapchain_images[index], fmt::format("swapchain image [{}]", index));

        view_info.setImage(swapchain_images[index]);

        swapchain_image_views[index] = device.createImageView(view_info);
        vkutil::name_object(swapchain_image_views[index], fmt::format("swapchain image view [{}]", index));
    }
}

void vulkan_engine_t::create_frames()
{
    LogVulkan("creating frames");

    auto pool_info = vk::CommandPoolCreateInfo{}
    .setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
    .setQueueFamilyIndex(queue_indices.graphics);

    graphics_command_pool = device.createCommandPool(pool_info);

    vkutil::name_object(graphics_command_pool, "graphics command pool");
    queue_destruction(&graphics_command_pool);

    auto shadowpass_pool_info = vk::CommandPoolCreateInfo{}
    .setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
    .setQueueFamilyIndex(queue_indices.graphics);

    shadowpass_cmdpool = device.createCommandPool(shadowpass_pool_info);

    vkutil::name_object(shadowpass_cmdpool, "shadowpass cmdpool");
    queue_destruction(&shadowpass_cmdpool);

    std::array<vk::CommandBuffer, FRAMES_IN_FLIGHT> graphics_buffers;
    std::array<vk::CommandBuffer, FRAMES_IN_FLIGHT> shadowpass_buffers;
    std::array<vk::CommandBuffer, FRAMES_IN_FLIGHT * 2> secondary_graphics_buffers;

    auto allocate_graphics_cmds = vk::CommandBufferAllocateInfo{}
    .setCommandPool(graphics_command_pool)
    .setCommandBufferCount(graphics_buffers.size())
    .setLevel(vk::CommandBufferLevel::ePrimary);

    auto allocate_shadowpass_cmds = vk::CommandBufferAllocateInfo{}
    .setCommandPool(shadowpass_cmdpool)
    .setCommandBufferCount(shadowpass_buffers.size())
    .setLevel(vk::CommandBufferLevel::ePrimary);

    auto allocate_secondary_cmds = vk::CommandBufferAllocateInfo{}
    .setCommandPool(graphics_command_pool)
    .setCommandBufferCount(secondary_graphics_buffers.size())
    .setLevel(vk::CommandBufferLevel::eSecondary);

    resultcheck = device.allocateCommandBuffers(&allocate_graphics_cmds, graphics_buffers.data());
    resultcheck = device.allocateCommandBuffers(&allocate_shadowpass_cmds, shadowpass_buffers.data());
    resultcheck = device.allocateCommandBuffers(&allocate_secondary_cmds, secondary_graphics_buffers.data());

    auto inheritance_info = vk::CommandBufferInheritanceInfo{};
    auto begin_first_secondary_graphics = vk::CommandBufferBeginInfo{}
    .setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit)
    .setPInheritanceInfo(&inheritance_info);

    for(size_t index = 0; index < frames.size(); ++index)
    {
        frame_data_t& frame = frames[index];

        frame.cmd = graphics_buffers[index];
        frame.shadowpass_cmd = shadowpass_buffers[index];
        frame.recording = secondary_graphics_buffers[index * 2];
        frame.pending = secondary_graphics_buffers[index * 2 + 1];

        frame.recording.begin(begin_first_secondary_graphics);

        vk::FenceCreateInfo fence_info{};
        fence_info.flags = vk::FenceCreateFlagBits::eSignaled;

        vk::SemaphoreCreateInfo semaphore_info{};

        frame.in_flight = device.createFence(fence_info);
        frame.image_available = device.createSemaphore(semaphore_info);
        frame.draw_finished = device.createSemaphore(semaphore_info);

        vkutil::name_object(frame.cmd, fmt::format("frame graphics command buffer [{}]", index));
        vkutil::name_object(frame.shadowpass_cmd, fmt::format("frame shadowpass command buffer [{}]", index));
        vkutil::name_object(frame.recording, fmt::format("frame graphics recording secondary command buffer [{}]", index));
        vkutil::name_object(frame.pending, fmt::format("frame graphics pending secondary command buffer [{}]", index));
        vkutil::name_object(frame.in_flight, fmt::format("frame in flight fence [{}]", index));
        vkutil::name_object(frame.image_available, fmt::format("frame image available semaphore #{}", index));
        vkutil::name_object(frame.draw_finished, fmt::format("frame render finished semaphore #{}", index));

        queue_destruction(&frame.in_flight);
        queue_destruction(&frame.image_available);
        queue_destruction(&frame.draw_finished);
    }
}

void vulkan_engine_t::create_pipelines()
{
    LogVulkan("creating pipelines");

    create_model_pipeline();
    create_wireframe_pipeline();
    create_line_pipeline();
    create_directional_light_pipeline();
    create_pointlight_pipeline();
    create_pointlight_mesh_pipeline();
    create_particle_pipeline();
    create_particle_compute_pipeline();
}

struct particle_control_data
{
    float circle_time;
    uint32_t particle_count;
};

void vulkan_engine_t::create_set_layouts()
{
    LogVulkan("creating set layouts");

    auto particle_control_create = vk::BufferCreateInfo{}
    .setSize(pad_uniform_buffer_size(sizeof(particle_control_data)) * frames.size())
    .setUsage(vk::BufferUsageFlagBits::eUniformBuffer);

    auto particle_control_allocation = vma::AllocationCreateInfo{}
    .setFlags(vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eStrategyBestFit)
    .setUsage(vma::MemoryUsage::eAutoPreferDevice);

    particle_control_buffer = allocate_buffer(particle_control_create, particle_control_allocation, "particle control");
    queue_destruction(&particle_control_buffer);

    auto particle_control_bind = descriptor_bind_info{}
    .setFlags({})
    .setBinding(0)
    .setType(vk::DescriptorType::eUniformBufferDynamic)
    .setStage(vk::ShaderStageFlagBits::eCompute);

    auto particle_control_buffer_info = vk::DescriptorBufferInfo{}
    .setBuffer(particle_control_buffer.buffer)
    .setOffset(0)
    .setRange(sizeof(particle_control_data));

    auto particle_buffer_bind = descriptor_bind_info{}
    .setFlags({})
    .setBinding(1)
    .setType(vk::DescriptorType::eStorageBuffer)
    .setStage(vk::ShaderStageFlagBits::eCompute);

    auto particle_buffer_info = vk::DescriptorBufferInfo{}
    .setBuffer(particle_emitter.instance_buffer.buffer)
    .setOffset(0)
    .setRange(sizeof(instance_data_t) * particle_emitter.instances);

    descriptor_builder
    .bind_buffers(particle_control_bind, &particle_control_buffer_info)
    .bind_buffers(particle_buffer_bind, &particle_buffer_info)
    .build(particle_set, particle_setlayout, "particles");
}

void vulkan_engine_t::allocate_frames()
{
    LogVulkan("allocating frames");

    auto mapped_sequential_allocation = vma::AllocationCreateInfo{}
    .setFlags(vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite)
    .setUsage(vma::MemoryUsage::eAutoPreferDevice);

    for(size_t index = 0; index < frames.size(); ++index)
    {
        frame_data_t& frame = frames[index];
        frame.entity_transforms_allocated = world_t::device_transforms_allocation_step;

        auto transform_buffer_info = vk::BufferCreateInfo{}
        .setSize(frames[index].entity_transforms_allocated * sizeof(packed_transform_t))
        .setUsage(vk::BufferUsageFlagBits::eStorageBuffer);

        auto directional_light_buffer_info = vk::BufferCreateInfo{}
        .setSize((sizeof(uint32_t) * 4) + (sizeof(directional_light_data_t) * light_manager_t::MAX_DIRECTIONAL_LIGHTS))
        .setUsage(vk::BufferUsageFlagBits::eStorageBuffer);

        auto pointlight_buffer_info = vk::BufferCreateInfo{}
        .setSize((light_manager_t::MAX_POINTLIGHTS * sizeof(pointlight_t)) + (sizeof(uint32_t) * 4))
        .setUsage(vk::BufferUsageFlagBits::eStorageBuffer);

        auto pointlight_projections_create = vk::BufferCreateInfo{}
        .setSize(light_manager_t::MAX_POINTLIGHTS * sizeof(pointlight_projection_t))
        .setUsage(vk::BufferUsageFlagBits::eStorageBuffer);

        allocated_buffer_t& transform_buffer = frames[index].entity_transform_buffer;
        allocated_buffer_t& pointlight_buffer = frames[index].pointlight_buffer;
        allocated_buffer_t& pointlight_projection_buffer = frames[index].pointlight_projection_buffer;

        transform_buffer = allocate_buffer(transform_buffer_info, mapped_sequential_allocation, fmt::format("entity transforms [{}]", index));
        frame.directional_light_buffer = allocate_buffer(directional_light_buffer_info, mapped_sequential_allocation, fmt::format("directional lights [{}]", index));
        pointlight_buffer = allocate_buffer(pointlight_buffer_info, mapped_sequential_allocation, fmt::format("pointlights [{}]", index));
        pointlight_projection_buffer = allocate_buffer(pointlight_projections_create, mapped_sequential_allocation, fmt::format("pointlight projections [{}]", index));

        auto destroy_buf = [](allocated_buffer_t* buffer)
        {
            gVulkan->destroy_buffer(*buffer);
        };

        destruction_que.append(&transform_buffer, destroy_buf);
        destruction_que.append(&frame.directional_light_buffer, destroy_buf);
        destruction_que.append(&pointlight_buffer, destroy_buf);
        destruction_que.append(&pointlight_projection_buffer, destroy_buf);

        {
            auto transform_descriptor = vk::DescriptorBufferInfo{}
            .setOffset(0)
            .setRange(VK_WHOLE_SIZE)
            .setBuffer(transform_buffer.buffer);

            auto directional_light_descriptor = vk::DescriptorBufferInfo{}
            .setOffset(0)
            .setRange(VK_WHOLE_SIZE)
            .setBuffer(frame.directional_light_buffer.buffer);

            auto pointlight_descriptor = vk::DescriptorBufferInfo{}
            .setOffset(0)
            .setRange(VK_WHOLE_SIZE)
            .setBuffer(pointlight_buffer.buffer);

            auto transform_bind = descriptor_bind_info{};
            transform_bind.binding = 0;
            transform_bind.type = vk::DescriptorType::eStorageBuffer;
            transform_bind.stage = vk::ShaderStageFlagBits::eVertex;

            auto directional_light_bind = descriptor_bind_info{}
            .setBinding(1)
            .setType(vk::DescriptorType::eStorageBuffer)
            .setStage(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment);

            auto pointlight_bind = descriptor_bind_info{};
            pointlight_bind.binding = 2;
            pointlight_bind.type = vk::DescriptorType::eStorageBuffer;
            pointlight_bind.stage = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

            descriptor_builder
            .bind_buffers(transform_bind, &transform_descriptor)
            .bind_buffers(directional_light_bind, &directional_light_descriptor)
            .bind_buffers(pointlight_bind, &pointlight_descriptor)
            .build(frames[index].world_set, world_set_layout, fmt::format("world [{}]", index));
        }
        {
            auto pointlight_projection_info = vk::DescriptorBufferInfo{}
            .setBuffer(pointlight_projection_buffer.buffer)
            .setRange(sizeof(pointlight_projection_t))
            .setOffset(0);

            auto pointlight_projection_bind = descriptor_bind_info{}
            .setBinding(0)
            .setType(vk::DescriptorType::eStorageBufferDynamic)
            .setStage(vk::ShaderStageFlagBits::eGeometry);

            auto single_pointlight_descriptor_info = vk::DescriptorBufferInfo{}
            .setBuffer(pointlight_buffer.buffer)
            .setRange(sizeof(pointlight_t))
            .setOffset(0);

            auto single_pointlight_bind = descriptor_bind_info{}
            .setBinding(1)
            .setType(vk::DescriptorType::eStorageBufferDynamic)
            .setStage(vk::ShaderStageFlagBits::eFragment);

            descriptor_builder
            .bind_buffers(pointlight_projection_bind, &pointlight_projection_info)
            .bind_buffers(single_pointlight_bind, &single_pointlight_descriptor_info)
            .build(frames[index].pointlight_projection_set, pointlight_projection_layout, fmt::format("pointlight projections {}", index));
        }
        {
            auto pointlight_shadowsampler_bind = descriptor_bind_info{}
            .setBinding(0)
            .setType(vk::DescriptorType::eSampler)
            .setStage(vk::ShaderStageFlagBits::eFragment);

            auto pointlight_shadow_bind = descriptor_bind_info{}
            .setBinding(1)
            .setType(vk::DescriptorType::eSampledImage)
            .setStage(vk::ShaderStageFlagBits::eFragment)
            .setFlags(vk::DescriptorBindingFlagBits::eVariableDescriptorCount | vk::DescriptorBindingFlagBits::ePartiallyBound)
            .setCount(light_manager_t::MAX_POINTLIGHTS);

            descriptor_builder
            .bind_samplers(pointlight_shadowsampler_bind, &cube_shadow_sampler)
            .bind_buffers(pointlight_shadow_bind, nullptr)
            .build(frames[index].pointlight_shadow_set, pointlight_shadow_set_layout, fmt::format("pointlight shadow images [{}]", index));
        }
        {
            auto buffer_info = vk::DescriptorBufferInfo{}
            .setBuffer(frame.directional_light_buffer.buffer)
            .setRange(sizeof(directional_light_data_t))
            .setOffset(0);

            auto bind_info = descriptor_bind_info{}
            .setBinding(0)
            .setType(vk::DescriptorType::eStorageBufferDynamic)
            .setStage(vk::ShaderStageFlagBits::eVertex);

            descriptor_builder
            .bind_buffers(bind_info, &buffer_info)
            .build(frame.directional_light_projection_set, directional_light_projection_layout, fmt::format("directional light projection [{}]", index));
        }
        {
            auto bind_info = descriptor_bind_info{}
            .setBinding(0)
            .setType(vk::DescriptorType::eCombinedImageSampler)
            .setStage(vk::ShaderStageFlagBits::eFragment)
            .setFlags(vk::DescriptorBindingFlagBits::eVariableDescriptorCount | vk::DescriptorBindingFlagBits::ePartiallyBound)
            .setCount(light_manager_t::MAX_DIRECTIONAL_LIGHTS);

            descriptor_builder
            .bind_images(bind_info, nullptr)
            .build(frame.directional_shadow_set, directional_shadow_layout, fmt::format("directional shadow [{}]", index));
        }
    }
}

void vulkan_engine_t::shutdown()
{
    LogVulkan("shutting down");

    device.waitIdle();

    for(material_t& material : get_world().materials)
    {
        device.destroy(material.pipeline);
    }
    get_world().materials.clear();

    for(model_t& model : get_world().models)
    {
        destroy_buffer(model.index_buffer);
        destroy_buffer(model.vertex_buffer);
    }
    get_world().models.clear();

    for(texture_t& texture : get_world().textures)
    {
        texture.destroy();
    }
    get_world().textures.clear();

    for(allocated_image_t shadowimage : get_world().lightmanager.cubemaps)
    {
        destroy_image(shadowimage);
    }

    for(allocated_image_t shadowimage : get_world().lightmanager.directional_maps)
    {
        destroy_image(shadowimage);
    }

    destruction_que.flush_reverse();

    device.destroy();
    instance.destroy(surface);

    if(debug_messenger)
    {
        instance.destroy(debug_messenger);
    }

    instance.destroy();
}

void vulkan_engine_t::create_model_pipeline()
{
    LogVulkan("creating model pipeline");

    slothandle_t<material_t> material = get_world().add_unique_material("default lit textured");

    pipeline_builder.include_shaders("default_lit.vert", "default_lit.frag");

    pipeline_builder
    .add_set_layout(global_set_layout)
    .add_set_layout(world_set_layout)
    .add_set_layout(pointlight_shadow_set_layout)
    .add_set_layout(directional_shadow_layout)
    .add_set_layout(texture_set_layout);

    std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    pipeline_builder.dynamic_state
    .setDynamicStates(dynamic_states);

    pipeline_builder.rendering
    .setColorAttachmentFormats(surface_format.format)
    .setDepthAttachmentFormat(depth_format);

    pipeline_builder.set_vertex_input(&vertex_t::position_normal_uv_input);

    pipeline_builder.input_assembly
    .setTopology(vk::PrimitiveTopology::eTriangleList)
    .setPrimitiveRestartEnable(false);

    auto[viewport, scissor] = whole_render_area();
    pipeline_builder.viewport
    .setViewports(viewport)
    .setScissors(scissor);

    pipeline_builder.rasterization
    .setDepthClampEnable(false)
    .setRasterizerDiscardEnable(false)
    .setPolygonMode(vk::PolygonMode::eFill)//line for wireframe
    .setCullMode(vk::CullModeFlagBits::eBack)
    .setFrontFace(vk::FrontFace::eClockwise)
    .setDepthBiasEnable(false)
    .setLineWidth(1.0f);

    pipeline_builder.multisample
    .setSampleShadingEnable(false)
    .setRasterizationSamples(vk::SampleCountFlagBits::e1)
    .setMinSampleShading(1.0f)
    .setAlphaToCoverageEnable(false)
    .setAlphaToOneEnable(false);

    using enum vk::ColorComponentFlagBits;
    auto color_blend_attachment = vk::PipelineColorBlendAttachmentState{}
    .setColorWriteMask(eR | eG | eB | eA)
    .setBlendEnable(false);

    pipeline_builder.color_blend
    .setLogicOpEnable(false)
    .setLogicOp(vk::LogicOp::eSet)
    .setAttachments(color_blend_attachment);

    pipeline_builder.depth_stencil
    .setDepthTestEnable(true)
    .setDepthWriteEnable(true)
    .setDepthCompareOp(vk::CompareOp::eGreater)
    .setDepthBoundsTestEnable(true)
    .setMinDepthBounds(0.0)
    .setMaxDepthBounds(1.0)
    .setStencilTestEnable(false);

    pipeline_builder.build(material->pipeline, material->pipeline_layout, material->name.data());
}

void vulkan_engine_t::create_wireframe_pipeline()
{
    LogVulkan("creating wireframe pipeline");

    slothandle_t<material_t> material = get_world().add_unique_material("default lit wireframe");

    pipeline_builder.include_shaders("default_lit.vert", "default_lit.frag");

    pipeline_builder.add_set_layout(global_set_layout);
    pipeline_builder.add_set_layout(world_set_layout);
    pipeline_builder.add_set_layout(pointlight_shadow_set_layout);
    pipeline_builder.add_set_layout(directional_shadow_layout);
    pipeline_builder.add_set_layout(texture_set_layout);

    std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    pipeline_builder.dynamic_state
    .setDynamicStates(dynamic_states);

    pipeline_builder.rendering
    .setColorAttachmentFormats(surface_format.format)
    .setDepthAttachmentFormat(depth_format);

    pipeline_builder.set_vertex_input(&vertex_t::position_normal_uv_input);

    pipeline_builder.input_assembly
    .setTopology(vk::PrimitiveTopology::eTriangleList)
    .setPrimitiveRestartEnable(false);

    auto[viewport, scissor] = whole_render_area();
    pipeline_builder.viewport
    .setViewports(viewport)
    .setScissors(scissor);

    pipeline_builder.rasterization
    .setDepthClampEnable(false)
    .setRasterizerDiscardEnable(false)
    .setPolygonMode(vk::PolygonMode::eLine)
    .setCullMode(vk::CullModeFlagBits::eNone)
    .setFrontFace(vk::FrontFace::eClockwise)
    .setDepthBiasEnable(false)
    .setLineWidth(1.0f);

    pipeline_builder.multisample
    .setSampleShadingEnable(false)
    .setRasterizationSamples(vk::SampleCountFlagBits::e1)
    .setMinSampleShading(1.0f)
    .setAlphaToCoverageEnable(false)
    .setAlphaToOneEnable(false);

    using enum vk::ColorComponentFlagBits;
    auto color_blend_attachment = vk::PipelineColorBlendAttachmentState{}
    .setColorWriteMask(eR | eG | eB | eA)
    .setBlendEnable(false);

    pipeline_builder.color_blend
    .setLogicOpEnable(false)
    .setLogicOp(vk::LogicOp::eSet)
    .setAttachments(color_blend_attachment);

    pipeline_builder.depth_stencil
    .setDepthTestEnable(true)
    .setDepthWriteEnable(true)
    .setDepthCompareOp(vk::CompareOp::eGreater)
    .setDepthBoundsTestEnable(true)
    .setMinDepthBounds(0.0)
    .setMaxDepthBounds(1.0)
    .setStencilTestEnable(false);

    pipeline_builder.build(material->pipeline, material->pipeline_layout, material->name.data());
}

struct gizmo_push_constants_t
{
    glm::mat4x4 transform;
    uint32_t force_depth_1;
};

void vulkan_engine_t::create_line_pipeline()
{
    LogVulkan("creating line pipeline");

    pipeline_builder.include_shaders("gizmo.vert", "gizmo.frag");

    pipeline_builder.add_set_layout(global_set_layout);

    std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    pipeline_builder.dynamic_state
    .setDynamicStates(dynamic_states);

    pipeline_builder.rendering
    .setColorAttachmentFormats(surface_format.format)
    .setDepthAttachmentFormat(depth_format);

    (void)pipeline_builder.vertex_input;

    pipeline_builder.input_assembly
    .setTopology(vk::PrimitiveTopology::eLineList)
    .setPrimitiveRestartEnable(false);

    auto[viewport, scissor] = whole_render_area();
    pipeline_builder.viewport
    .setViewports(viewport)
    .setScissors(scissor);

    pipeline_builder.rasterization
    .setDepthClampEnable(false)
    .setRasterizerDiscardEnable(false)
    .setPolygonMode(vk::PolygonMode::eFill)
    .setCullMode(vk::CullModeFlagBits::eNone)
    .setFrontFace(vk::FrontFace::eClockwise)
    .setDepthBiasEnable(false)
    .setLineWidth(1.0f);

    pipeline_builder.multisample
    .setSampleShadingEnable(false)
    .setRasterizationSamples(vk::SampleCountFlagBits::e1)
    .setMinSampleShading(1.0f)
    .setAlphaToCoverageEnable(false)
    .setAlphaToOneEnable(false);

    using enum vk::ColorComponentFlagBits;
    auto color_blend_attachment = vk::PipelineColorBlendAttachmentState{}
    .setColorWriteMask(eR | eG | eB | eA)
    .setBlendEnable(false);

    pipeline_builder.color_blend
    .setLogicOpEnable(false)
    .setLogicOp(vk::LogicOp::eSet)
    .setAttachments(color_blend_attachment);

    pipeline_builder.depth_stencil
    .setDepthTestEnable(false)
    .setDepthWriteEnable(false)
    .setDepthCompareOp(vk::CompareOp::eGreater)
    .setDepthBoundsTestEnable(false)
    .setMinDepthBounds(0.0)
    .setMaxDepthBounds(1.0)
    .setStencilTestEnable(false);

    pipeline_builder.build(line_pipeline, line_pipelinelayout, "gizmo line pipeline");
    queue_destruction(&line_pipeline);
}

void vulkan_engine_t::initialize_imgui()
{
    LogVulkan("intializing ImGUI");

    using enum vk::DescriptorType;
    constexpr std::array descriptor_pool_sizes
    {
        vk::DescriptorPoolSize{eSampler, 500 },
        vk::DescriptorPoolSize{eCombinedImageSampler, 500 },
        vk::DescriptorPoolSize{eSampledImage, 500 },
        vk::DescriptorPoolSize{eStorageImage, 500 },
        vk::DescriptorPoolSize{eUniformTexelBuffer, 500 },
        vk::DescriptorPoolSize{eStorageTexelBuffer, 500 },
        vk::DescriptorPoolSize{eUniformBuffer, 500 },
        vk::DescriptorPoolSize{eStorageBuffer, 500 },
        vk::DescriptorPoolSize{eUniformBufferDynamic, 500 },
        vk::DescriptorPoolSize{eStorageBufferDynamic, 500 },
        vk::DescriptorPoolSize{eInputAttachment, 500 }
    };

    auto pool_info = vk::DescriptorPoolCreateInfo{}
    .setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet)
    .setMaxSets(500)
    .setPoolSizes(descriptor_pool_sizes);

    ImGUI_pool = device.createDescriptorPool(pool_info);
    vkutil::name_object(ImGUI_pool, "ImGUI descriptor pool");

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = instance;
    init_info.PhysicalDevice = physical_device;
    init_info.Device = device;
    init_info.Queue = queues.graphics;
    init_info.DescriptorPool = ImGUI_pool;
    init_info.MinImageCount = min_image_count;
    init_info.ImageCount = swapchain_images.size();
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.surface_format = static_cast<VkFormat>(surface_format.format);
    init_info.depth_format = static_cast<VkFormat>(depth_format);

    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGui_ImplGlfw_InitForVulkan(glfwwindow, true);
    ImGui_ImplVulkan_Init(&init_info);
    ImGui_ImplVulkan_CreateFontsTexture(active_frame().recording);

    active_frame().next_render.append([]()
    {
        ImGui_ImplVulkan_DestroyFontUploadObjects();
    });

    destruction_que.append(ImGUI_pool, [](vk::DescriptorPool pool)
    {
        gVulkan->device.destroyDescriptorPool(pool);
        ImGui_ImplVulkan_Shutdown();
        ImGui::DestroyContext();
        ImPlot::DestroyContext();
    });
}

void vulkan_engine_t::recreate_swapchain()
{
    int width; int height;
    do
    {
        glfwGetWindowSize(glfwwindow, &width, &height);
    }
    while(width == 0 || height == 0);

    device.waitIdle();

    for(vk::ImageView view : swapchain_image_views)
    {
        device.destroyImageView(view);
    }

    destroy_image(swapchain_depth_image);

    vk::SwapchainKHR old_swapchain = swapchain;
    create_swapchain(old_swapchain);
    create_swapchain_views();
    create_depth_image();

    device.destroySwapchainKHR(old_swapchain);
}

void vulkan_engine_t::wait_all_frames()
{
    auto fold_fences = [this]<size_t... I>(std::index_sequence<I...>)
    {
        return std::array{frames[I].in_flight ...};
    };

    resultcheck = device.waitForFences(fold_fences(std::make_index_sequence<FRAMES_IN_FLIGHT>{}), true, std::nano::den);
}

allocated_buffer_t vulkan_engine_t::allocate_buffer(vk::BufferCreateInfo& bufferinfo, vma::AllocationCreateInfo& allocationinfo, std::string debug_name)
{
    allocated_buffer_t buffer;
    resultcheck = allocator.createBuffer(&bufferinfo, &allocationinfo, &buffer.buffer, &buffer.allocation, &buffer.info);

    if(!debug_name.empty())
    {
        vkutil::name_object(buffer.buffer, fmt::format("{} buffer", debug_name));
        vkutil::name_object(buffer.allocation, fmt::format("{} allocation", debug_name));
    }

    return buffer;
}

size_t vulkan_engine_t::pad_uniform_buffer_size(size_t size) const
{
    size_t min_alignment = gpu_properties.properties.limits.minUniformBufferOffsetAlignment;
    return (size + min_alignment - 1) & ~(min_alignment - 1);
}

std::pair<vk::Viewport, vk::Rect2D> vulkan_engine_t::whole_render_area() const
{
    vk::Rect2D render_area{};
    render_area.offset = vk::Offset2D{0, 0};
    render_area.extent = image_extent;

    auto viewport = vk::Viewport{}
    .setX(0)
    .setY(0)
    .setWidth((float)image_extent.width)
    .setHeight((float)image_extent.height)
    .setMinDepth(0)
    .setMaxDepth(1);

    return {viewport, render_area};
}

uint64_t vulkan_engine_t::frame_index() const
{
    return program_time.frame_count % frames.size();
}

frame_data_t& vulkan_engine_t::active_frame()
{
    return frames[frame_index()];
}

void vulkan_engine_t::destroy_buffer(allocated_buffer_t allocated_buffer)
{
    allocator.destroyBuffer(allocated_buffer.buffer, allocated_buffer.allocation);
}

allocated_buffer_t vulkan_engine_t::allocate_vertex_buffer(size_t buffer_size, std::string debug_name)
{
    auto buffer_info = vk::BufferCreateInfo{}
    .setSize(buffer_size)
    .setUsage(vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst);

    auto alloc_info = vma::AllocationCreateInfo{}
    .setFlags(vma::AllocationCreateFlagBits::eStrategyBestFit)
    .setUsage(vma::MemoryUsage::eAutoPreferDevice);

    return allocate_buffer(buffer_info, alloc_info, fmt::format("{} vertex", debug_name));
}

allocated_buffer_t vulkan_engine_t::allocate_instance_buffer(size_t buffer_size, std::string debug_name)
{
    auto buffer_info = vk::BufferCreateInfo{}
    .setSize(buffer_size)
    .setUsage(vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst);

    auto alloc_info = vma::AllocationCreateInfo{}
    .setFlags(vma::AllocationCreateFlagBits::eStrategyBestFit)
    .setUsage(vma::MemoryUsage::eAutoPreferDevice);

    return allocate_buffer(buffer_info, alloc_info, fmt::format("{} instance", debug_name));
}

allocated_buffer_t vulkan_engine_t::allocate_index_buffer(size_t buffer_size, std::string debug_name)
{
    auto buffer_info = vk::BufferCreateInfo{}
    .setSize(buffer_size)
    .setUsage(vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst);

    auto alloc_info = vma::AllocationCreateInfo{}
    .setFlags(vma::AllocationCreateFlagBits::eStrategyBestFit)
    .setUsage(vma::MemoryUsage::eAutoPreferDevice);

    return allocate_buffer(buffer_info, alloc_info, fmt::format("{} index", debug_name));
}

allocated_buffer_t vulkan_engine_t::allocate_staging_buffer(size_t buffer_size, std::string debug_name)
{
    auto buffer_info = vk::BufferCreateInfo{}
    .setSize(buffer_size)
    .setUsage(vk::BufferUsageFlagBits::eTransferSrc);

    auto alloc_info = vma::AllocationCreateInfo{}
    .setFlags(vma::AllocationCreateFlagBits::eHostAccessRandom | vma::AllocationCreateFlagBits::eStrategyMinTime)
    .setUsage(vma::MemoryUsage::eAutoPreferHost);

    debug_name = fmt::format("{} staging", debug_name.empty() ? "unspecified" : debug_name);
    allocated_buffer_t buffer = allocate_buffer(buffer_info, alloc_info, debug_name);

    active_frame().next_render.append(new allocated_buffer_t{buffer}, [](allocated_buffer_t* buffer)
    {
        gVulkan->destroy_buffer(*buffer);
        delete buffer;
    });

    return buffer;
}

void vulkan_engine_t::copy_vertex_attribute_buffer(allocated_buffer_t dst, allocated_buffer_t src, size_t size)
{
    auto buffer_copy = vk::BufferCopy2{}
    .setSize(size)
    .setDstOffset(0)
    .setSrcOffset(0);

    auto copy_info = vk::CopyBufferInfo2{}
    .setDstBuffer(dst.buffer)
    .setSrcBuffer(src.buffer)
    .setRegions(buffer_copy);

    auto buffer_barrier = vk::BufferMemoryBarrier2{}
    .setBuffer(copy_info.dstBuffer)
    .setSize(buffer_copy.size)
    .setOffset(0)
    .setSrcStageMask(vk::PipelineStageFlagBits2::eCopy)
    .setSrcAccessMask(vk::AccessFlagBits2::eTransferWrite)
    .setDstStageMask(vk::PipelineStageFlagBits2::eVertexAttributeInput)
    .setDstAccessMask(vk::AccessFlagBits2::eVertexAttributeRead);

    auto dependency = vk::DependencyInfo{}
    .setBufferMemoryBarriers(buffer_barrier);

    recording_mx.lock();
    active_frame().recording.copyBuffer2(copy_info);
    active_frame().recording.pipelineBarrier2(dependency);
    recording_mx.unlock();
}

void vulkan_engine_t::copy_index_buffer(allocated_buffer_t dst, allocated_buffer_t src, size_t size)
{
    auto buffer_copy = vk::BufferCopy2{}
    .setSize(size)
    .setDstOffset(0)
    .setSrcOffset(0);

    auto copy_info = vk::CopyBufferInfo2{}
    .setDstBuffer(dst.buffer)
    .setSrcBuffer(src.buffer)
    .setRegions(buffer_copy);

    auto buffer_barrier = vk::BufferMemoryBarrier2{}
    .setBuffer(copy_info.dstBuffer)
    .setSize(buffer_copy.size)
    .setOffset(0)
    .setSrcStageMask(vk::PipelineStageFlagBits2::eCopy)
    .setSrcAccessMask(vk::AccessFlagBits2::eTransferWrite)
    .setDstStageMask(vk::PipelineStageFlagBits2::eIndexInput)
    .setDstAccessMask(vk::AccessFlagBits2::eIndexRead);

    auto dependency = vk::DependencyInfo{}
    .setBufferMemoryBarriers(buffer_barrier);

    recording_mx.lock();
    active_frame().recording.copyBuffer2(copy_info);
    active_frame().recording.pipelineBarrier2(dependency);
    recording_mx.unlock();
}

allocated_image_t vulkan_engine_t::allocate_image(vk::ImageCreateInfo &imageinfo, vma::AllocationCreateInfo& allocationinfo, std::string debug_name)
{
    allocated_image_t image;
    resultcheck = allocator.createImage(&imageinfo, &allocationinfo, &image.image, &image.allocation, nullptr);

    if(!debug_name.empty())
    {
        vkutil::name_object(image.image, fmt::format("{} image", debug_name));
        vkutil::name_object(image.allocation, fmt::format("{} allocation", debug_name));
    }

    return image;
}

void vulkan_engine_t::destroy_image(allocated_image_t image)
{
    allocator.destroyImage(image.image, image.allocation);
    device.destroyImageView(image.view);
}

vk::ImageView vulkan_engine_t::make_texture_view(vk::Image image, std::string debug_name)
{
    auto subresource_range = vk::ImageSubresourceRange{}
    .setAspectMask(vk::ImageAspectFlagBits::eColor)
    .setBaseMipLevel(0)
    .setLevelCount(1)
    .setBaseArrayLayer(0)
    .setLayerCount(1);

    using enum vk::ComponentSwizzle;
    auto component_mapping = vk::ComponentMapping{}
    .setR(eR).setG(eG).setB(eB).setA(eA);

    auto view_info = vk::ImageViewCreateInfo{}
    .setSubresourceRange(subresource_range)
    .setImage(image)
    .setFormat(vk::Format::eR8G8B8A8Srgb)
    .setViewType(vk::ImageViewType::e2D)
    .setComponents(component_mapping);

    vk::ImageView view = device.createImageView(view_info);

    if(!debug_name.empty())
    {
        vkutil::name_object(view, fmt::format("{} image view", debug_name));
    }

    return view;
}

void vulkan_engine_t::load_files()
{
    LogFileLoader("loading textures and models");

    tf::Taskflow taskflow{};

    std::array textures
    {
        std::make_pair(gWorld->textures.add("gun"), "gun.png"),
        std::make_pair(gWorld->textures.add("cube"), "cube_image.1001.png"),
        std::make_pair(gWorld->textures.add("cat_woah"), "cat_woah.png"),
        std::make_pair(gWorld->textures.add("pony"), "pony.png"),
        std::make_pair(gWorld->textures.add("terrain"), "terrain.png"),
        std::make_pair(gWorld->textures.add("grass terrain"), "grass_terrain.png"),
        std::make_pair(gWorld->textures.add("rock"), "rock_color.png"),
        std::make_pair(gWorld->textures.add("brush"), "brush_color.png"),
        std::make_pair(gWorld->textures.add("katt star"), "kat_star.png"),
        std::make_pair(gWorld->textures.add("katt star blurred"), "kat_star_blurred.png")
    };

    taskflow.for_each(textures.begin(), textures.end(), [](std::pair<texture_handle_t, const char*> texture_file)
    {
        texture_file.first->load_from_file(texture_file.second);
    });

    std::array models
    {
        std::make_pair(gWorld->models.add("grass terrain base"), "grass_terrain_base.fbx"),
        std::make_pair(gWorld->models.add("grass terrain base rock"), "grass_terrain_base_rock.fbx"),
        std::make_pair(gWorld->models.add("grass terrain big rock"), "grass_terrain_big_rock.fbx"),
        std::make_pair(gWorld->models.add("grass terrain small rock"), "grass_terrain_small_rock.fbx"),
        std::make_pair(gWorld->models.add("grass terrain medium rock"), "grass_terrain_medium_rock.fbx"),
        std::make_pair(gWorld->models.add("brush"), "brush.fbx"),
        std::make_pair(gWorld->models.add("maxwell"), "maxwell_the_cat.fbx"),
        std::make_pair(gWorld->models.add("kat_gun"), "kat_gun.fbx"),
        std::make_pair(gWorld->models.add("cube"), "cube.fbx"),
        std::make_pair(gWorld->models.add("fish"), "fish.ply"),
        std::make_pair(gWorld->models.add("pony"), "pony.fbx"),
        std::make_pair(gWorld->models.add("Terrain"), "Terrain.obj"),
        std::make_pair(gWorld->models.add("sphere"), "sphere.fbx"),
        std::make_pair(gWorld->models.add("plane"), "plane.fbx")
    };

    taskflow.for_each(models.begin(), models.end(), [](std::pair<model_handle_t, const char*> model_file)
    {
        model_file.first->load_from_file(model_file.second);
    });

    taskflow.emplace([this]()
    {
        particle_emitter.name = "particles";
        particle_emitter.model = gWorld->find_model("plane").to_ptr();
        particle_emitter.allocate_instance_buffer(10000);
        queue_destruction(&particle_emitter.instance_buffer);
    });

    tf_executor->run(std::move(taskflow)).wait();
}

texture_image_t vulkan_engine_t::allocate_texture_image(vk::Extent3D extent, std::string debug_name)
{
    texture_image_t texture;
    texture.mip_levels = count_mipmap(extent.height, extent.height);

    auto image_info = vk::ImageCreateInfo{}
    .setImageType(vk::ImageType::e2D)
    .setFormat(vk::Format::eR8G8B8A8Srgb)
    .setExtent(extent)
    .setMipLevels(texture.mip_levels)
    .setArrayLayers(1)
    .setSamples(vk::SampleCountFlagBits::e1)
    .setTiling(vk::ImageTiling::eOptimal)
    .setUsage(vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc)
    .setSharingMode(vk::SharingMode::eExclusive)
    .setInitialLayout(vk::ImageLayout::eUndefined);

    auto subresource_range = vk::ImageSubresourceRange{}
    .setAspectMask(vk::ImageAspectFlagBits::eColor)
    .setBaseMipLevel(0)
    .setLevelCount(texture.mip_levels)
    .setBaseArrayLayer(0)
    .setLayerCount(1);

    auto allocation_info = vma::AllocationCreateInfo{}
    .setUsage(vma::MemoryUsage::eAutoPreferDevice);

    resultcheck = allocator.createImage(&image_info, &allocation_info, &texture.image, &texture.allocation, &texture.info);

    using enum vk::ComponentSwizzle;
    vk::ComponentMapping component_mapping{eR, eG, eB, eA};

    auto view_info = vk::ImageViewCreateInfo{}
    .setSubresourceRange(subresource_range)
    .setImage(texture.image)
    .setFormat(vk::Format::eR8G8B8A8Srgb)
    .setViewType(vk::ImageViewType::e2D)
    .setComponents(component_mapping);

    resultcheck = device.createImageView(&view_info, nullptr, &texture.view);

    if(!debug_name.empty())
    {
        vkutil::name_object(texture.image, fmt::format("{} texture image", debug_name));
        vkutil::name_object(texture.view, fmt::format("{} texture view", debug_name));
        vkutil::name_object(texture.allocation, fmt::format("{} texture allocation", debug_name));
    }

    return texture;
}

void vulkan_engine_t::copy_buffer2texture(texture_image_t& texture, allocated_buffer_t staging_buffer, vk::Extent3D extent)
{
    auto subresource_range = vk::ImageSubresourceRange{}
    .setAspectMask(vk::ImageAspectFlagBits::eColor)
    .setBaseMipLevel(0)
    .setLevelCount(texture.mip_levels)
    .setBaseArrayLayer(0)
    .setLayerCount(1);

    auto image2dst_barrier = vk::ImageMemoryBarrier2{}
    .setImage(texture.image)
    .setOldLayout(vk::ImageLayout::eUndefined)
    .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
    .setSubresourceRange(subresource_range)
    .setSrcStageMask(vk::PipelineStageFlagBits2::eNone)
    .setSrcAccessMask(vk::AccessFlagBits2::eNone)
    .setDstStageMask(vk::PipelineStageFlagBits2::eAllTransfer)
    .setDstAccessMask(AccessFlag::eTransferWrite)
    .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
    .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);

    auto image2dst_dependency = vk::DependencyInfo{}
    .setImageMemoryBarriers(image2dst_barrier);

    auto region = vk::BufferImageCopy2{}
    .setImageExtent(extent)
    .setImageOffset({});
    region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;

    auto copy_info = vk::CopyBufferToImageInfo2{}
    .setDstImage(texture.image)
    .setSrcBuffer(staging_buffer.buffer)
    .setDstImageLayout(vk::ImageLayout::eTransferDstOptimal)
    .setRegions(region);

    recording_mx.lock();
    active_frame().recording.pipelineBarrier2(image2dst_dependency);
    active_frame().recording.copyBufferToImage2(copy_info);
    recording_mx.unlock();
}

void vulkan_engine_t::generate_mipmaps(texture_image_t& texture, vk::Extent3D extent) //assumes image is in TrasnferDstOptimal
{
    auto image_dst2src = vk::ImageMemoryBarrier2{} //wait for the copy or blit
    .setImage(texture.image)
    .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
    .setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
    .setSrcStageMask(PipelineStage::eCopy | PipelineStage::eBlit)
    .setSrcAccessMask(AccessFlag::eTransferWrite)
    .setDstStageMask(PipelineStage::eBlit)
    .setDstAccessMask(AccessFlag::eTransferRead)
    .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
    .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
    image_dst2src.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    image_dst2src.subresourceRange.baseArrayLayer = 0;
    image_dst2src.subresourceRange.layerCount = 1;
    image_dst2src.subresourceRange.levelCount = 1;

    auto image_src2rdonly = vk::ImageMemoryBarrier2{}
    .setImage(texture.image)
    .setOldLayout(vk::ImageLayout::eTransferSrcOptimal)
    .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
    .setSrcStageMask(PipelineStage::eBlit)
    .setSrcAccessMask(AccessFlag::eTransferRead)
    .setDstStageMask(PipelineStage::eFragmentShader)
    .setDstAccessMask(AccessFlag::eShaderRead)
    .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
    .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
    image_src2rdonly.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    image_src2rdonly.subresourceRange.baseArrayLayer = 0;
    image_src2rdonly.subresourceRange.layerCount = 1;
    image_src2rdonly.subresourceRange.levelCount = 1;

    auto image_dst2rdonly = vk::ImageMemoryBarrier2{}
    .setImage(texture.image)
    .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
    .setNewLayout(vk::ImageLayout::eShaderReadOnlyOptimal)
    .setSrcStageMask(PipelineStage::eBlit)
    .setSrcAccessMask(AccessFlag::eTransferWrite)
    .setDstStageMask(PipelineStage::eFragmentShader)
    .setDstAccessMask(AccessFlag::eShaderRead)
    .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
    .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
    image_dst2rdonly.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    image_dst2rdonly.subresourceRange.baseArrayLayer = 0;
    image_dst2rdonly.subresourceRange.layerCount = 1;
    image_dst2rdonly.subresourceRange.levelCount = 1;
    image_dst2rdonly.subresourceRange.baseMipLevel = texture.mip_levels - 1;

    int32_t mip_width = extent.width;
    int32_t mip_height = extent.height;

    recording_mx.lock();

    for(uint32_t mip = 1; mip < texture.mip_levels; ++mip)
    {
        image_dst2src.subresourceRange.baseMipLevel = mip - 1;
        image_src2rdonly.subresourceRange.baseMipLevel = mip - 1;

        auto image_dst2src_dependency = vk::DependencyInfo{}.setImageMemoryBarriers(image_dst2src);
        auto image_src2rdonly_dependency = vk::DependencyInfo{}.setImageMemoryBarriers(image_src2rdonly);

        vk::ImageBlit2 blit{};
        blit.srcOffsets[0] = vk::Offset3D{0, 0, 0};
        blit.srcOffsets[1] = vk::Offset3D{mip_width, mip_height, 1};
        blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        blit.srcSubresource.mipLevel = mip - 1;
        blit.srcSubresource.layerCount = 1;
        blit.srcSubresource.baseArrayLayer = 0;

        mip_width = std::max(mip_width >> 1, 1);
        mip_height = std::max(mip_height >> 1, 1);

        blit.dstOffsets[0] = vk::Offset3D{0, 0, 0};
        blit.dstOffsets[1] = vk::Offset3D{mip_width, mip_height, 1};
        blit.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        blit.dstSubresource.mipLevel = mip;
        blit.dstSubresource.layerCount = 1;
        blit.dstSubresource.baseArrayLayer = 0;

        auto blit_info = vk::BlitImageInfo2{}
        .setSrcImage(texture.image)
        .setSrcImageLayout(vk::ImageLayout::eTransferSrcOptimal)
        .setDstImage(texture.image)
        .setDstImageLayout(vk::ImageLayout::eTransferDstOptimal)
        .setFilter(vk::Filter::eLinear)
        .setRegions(blit);

        active_frame().recording.pipelineBarrier2(image_dst2src_dependency);
        active_frame().recording.blitImage2(blit_info);
        active_frame().recording.pipelineBarrier2(image_src2rdonly_dependency);
    }

    auto image_dst2rdonly_dependency = vk::DependencyInfo{}.setImageMemoryBarriers(image_dst2rdonly);
    active_frame().recording.pipelineBarrier2(image_dst2rdonly_dependency); //barrier the last mip level

    recording_mx.unlock();
}

void vulkan_engine_t::create_buffers()
{
    LogVulkan("creating buffers");

    auto global_buffer_info = vk::BufferCreateInfo{}
    .setSize(pad_uniform_buffer_size(sizeof(global_device_data_t)) * frames.size())
    .setUsage(vk::BufferUsageFlagBits::eUniformBuffer);

    auto global_allocation_info = vma::AllocationCreateInfo{}
    .setFlags(vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eStrategyBestFit)
    .setUsage(vma::MemoryUsage::eAutoPreferDevice);

    global_buffer = allocate_buffer(global_buffer_info, global_allocation_info, "global device data");
    queue_destruction(&global_buffer);

    auto global_bind_info = descriptor_bind_info{}
    .setBinding(0)
    .setType(vk::DescriptorType::eUniformBufferDynamic)
    .setStage(vk::ShaderStageFlagBits::eAll);

    auto global_descriptor_info = vk::DescriptorBufferInfo{}
    .setBuffer(global_buffer.buffer)
    .setOffset(0)
    .setRange(sizeof(global_device_data_t));

    descriptor_builder
    .bind_buffers(global_bind_info, &global_descriptor_info)
    .build(global_descriptor_set, global_set_layout, "global");
}

frame_data_t& vulkan_engine_t::next_frame()
{
    return frames[(program_time.frame_count + 1) % frames.size()];
}

frame_data_t& vulkan_engine_t::prev_frame()
{
    return frames[(program_time.frame_count - 1) % frames.size()];
}

void vulkan_engine_t::create_texture_sampler()
{
    LogVulkan("creating texture sampler");

    float anisotropy = std::min(8.f, gpu_properties.properties.limits.maxSamplerAnisotropy);

    auto sampler_info = vk::SamplerCreateInfo{}
    .setMagFilter(vk::Filter::eLinear)
    .setMinFilter(vk::Filter::eLinear)
    .setAddressModeU(vk::SamplerAddressMode::eMirroredRepeat)
    .setAddressModeW(vk::SamplerAddressMode::eMirroredRepeat)
    .setAddressModeV(vk::SamplerAddressMode::eMirroredRepeat)
    .setAnisotropyEnable(true)
    .setMaxAnisotropy(anisotropy)
    .setBorderColor(vk::BorderColor::eFloatOpaqueBlack) //not used
    .setUnnormalizedCoordinates(false)
    .setCompareEnable(false)
    .setCompareOp(vk::CompareOp::eAlways) //not used
    .setMipmapMode(vk::SamplerMipmapMode::eNearest)
    .setMipLodBias(0.0f)
    .setMinLod(0.0f)
    .setMaxLod(100.f); //whatever

    texture_sampler = device.createSampler(sampler_info);

    vkutil::name_object(texture_sampler, "texture sampler");
    queue_destruction(&texture_sampler);
}

vk::Format vulkan_engine_t::find_image_format(vk::FormatFeatureFlags feature_flags, std::span<vk::Format> candidates) const
{
    for(vk::Format format : candidates)
    {
        vk::FormatProperties2 properties = physical_device.getFormatProperties2(format);

        if(properties.formatProperties.optimalTilingFeatures & feature_flags)
        {
            return format;
        }
    }

    vk::throwResultException(vk::Result::eErrorFormatNotSupported, nullptr);
}

vk::Format vulkan_engine_t::find_buffer_format(vk::FormatFeatureFlags feature_flags, std::span<vk::Format> candidates) const
{
    for(vk::Format format : candidates)
    {
        vk::FormatProperties2 properties = physical_device.getFormatProperties2(format);

        if(properties.formatProperties.bufferFeatures & feature_flags)
        {
            return format;
        }
    }

    vk::throwResultException(vk::Result::eErrorFormatNotSupported, nullptr);
}

void vulkan_engine_t::draw()
{
    static tf::Taskflow taskflow{};

    static std::vector<entity_batch_t> entity_batches{};
    static uint32_t swapchain_image = -1;

    static bool initialized = false;
    if(!initialized)
    {
        initialized = true;

        tf::Task acquire_swapchain_image_task = taskflow.emplace([this]() -> uint32_t
        {
            swapchain_image = acquire_swapchain_image(active_frame());
            return swapchain_image == -1 ? 0 : 1;
        })
        .name("acquire swapchain image");

        tf::Task make_entity_batches_task = taskflow.emplace([this]()
        {
            entity_batches = make_entity_batches();
        })
        .name("make entity batches");

        tf::Task recreate_swapchain_task = taskflow.emplace([this]()
        {
            recreate_swapchain();
        })
        .name("recreate swapchain");

        tf::Task prepare_render_task = taskflow.emplace([this]()
        {
            check_buffer_sizes(active_frame());
            prepare_frame(active_frame());
        })
        .name("prepare render");

        tf::Task upload_data_task = taskflow.emplace([this]() -> void
        {
            upload_device_global_data();
            upload_directional_lights();
            upload_pointlights();
            upoad_transforms();
            upload_particle_control();
            flush_uploads();
        })
        .name("upload data");

        tf::Task shadowpass_task = taskflow.emplace([this]()
        {
            for(uint32_t index = 0; index < world_data.directional_lights.size(); ++index)
            {
                directional_light_pass(active_frame(), entity_batches, index);
            }

            for(uint32_t index = 0; index < world_data.pointlights.size(); ++index)
            {
                pointlight_shadow_pass(active_frame(), entity_batches, index);
            }

            active_frame().shadowpass_cmd.end();
        })
        .name("shadow pass");

        tf::Task swapchainpass_task = taskflow.emplace([this]() -> void
        {
            compute_pass(active_frame());

            begin_swapchain_render(active_frame(), swapchain_image);

            pointlight_mesh_pass(active_frame());
            entity_pass(active_frame(), entity_batches);
            particle_pass(active_frame());
            ui_pass(active_frame());

            end_swapchain_render(active_frame(), swapchain_image);
        })
        .name("swapchain pass");

        tf::Task submit_commands_task = taskflow.emplace([this]()
        {
            submit_commands(active_frame());
            present_swapchain_image(active_frame(), swapchain_image);
        })
        .name("submit commands");

        acquire_swapchain_image_task.precede(recreate_swapchain_task, prepare_render_task);
        make_entity_batches_task.precede(shadowpass_task, swapchainpass_task);
        prepare_render_task.precede(upload_data_task, shadowpass_task, swapchainpass_task);
        submit_commands_task.succeed(upload_data_task, shadowpass_task, swapchainpass_task);

        taskflow.dump(std::cout);
    }

    tf_executor->run(taskflow).wait();
}

std::vector<entity_batch_t> vulkan_engine_t::make_entity_batches()
{
    std::vector<entity_batch_t> batches{};

    auto find_batch = [&](const entity_t& entity) -> entity_batch_t&
    {
        for(entity_batch_t& batch : batches) //exists?
        {
            if(entity.model == batch.model && entity.texture == batch.texture && entity.material == batch.material)
            {
                return batch;
            }
        }

        entity_batch_t& new_batch = batches.emplace_back();
        new_batch.model = entity.model;
        new_batch.texture = entity.texture;
        new_batch.material = entity.material;

        return new_batch;
    };

    const model_handle_t nullmodel = gWorld->find_model("null");
    const texture_handle_t nulltexture = gWorld->find_texture("null");
    const material_handle_t nullmaterial = gWorld->find_material("null");

    for(uint32_t index = 0; index < world_data.entities.size(); ++index)
    {
        const entity_t& entity = world_data.entities[index];

        if(entity.model != nullmodel && entity.texture != nulltexture && entity.material != nullmaterial)
        {
            entity_batch_t& batch = find_batch(entity);
            batch.indices.emplace_back(index);
        }
    }

    auto insertion_sort = [](std::span<entity_batch_t> span, bool(*greater)(const entity_batch_t&, const entity_batch_t&))
    {
        for(int64_t outer = 1; outer < span.size(); ++outer)
        {
            entity_batch_t checked = std::move(span[outer]);

            int64_t inner = outer - 1;
            while(inner >= 0 && greater(span[inner], checked))
            {
                span[inner + 1] = std::move(span[inner]);
                --inner;
            }

            span[inner + 1] = std::move(checked);
        }
    };

    if(!batches.empty())
    {
        insertion_sort({batches.data(), batches.data() + batches.size()}, [](const entity_batch_t& lhs, const entity_batch_t& rhs)
        {
            return lhs.model.handle.key_value() > rhs.model.handle.key_value(); //sort by models
        });

        int64_t range_start = 0;
        for(int64_t range_end = 0; range_end < batches.size(); ++range_end) //then sort by materials
        {
            if(batches[range_start].model != batches[range_end].model)
            {
                insertion_sort({batches.data() + range_start, batches.data() + range_end}, [](const entity_batch_t& lhs, const entity_batch_t& rhs)
                {
                    return lhs.material.handle.key_value() > rhs.material.handle.key_value();
                });

                range_start = range_end;
            }
        }
    }

    return batches;
}

allocated_image_t vulkan_engine_t::allocate_directional_shadowmap(uint32_t width_height, std::string debug_name)
{
    LogVulkan("allocating directional shadowmap");

    auto image_info = vk::ImageCreateInfo{}
    .setUsage(vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eDepthStencilAttachment)
    .setFormat(directional_shadow_format)
    .setTiling(vk::ImageTiling::eOptimal)
    .setImageType(vk::ImageType::e2D)
    .setArrayLayers(1)
    .setMipLevels(1)
    .setSamples(vk::SampleCountFlagBits::e1)
    .setSharingMode(vk::SharingMode::eExclusive)
    .setExtent(vk::Extent3D{width_height, width_height, 1})
    .setInitialLayout(vk::ImageLayout::eUndefined);

    auto allocation_info = vma::AllocationCreateInfo{}
    .setUsage(vma::MemoryUsage::eAutoPreferDevice)
    .setFlags(vma::AllocationCreateFlagBits::eStrategyBestFit);

    auto component_mapping = vk::ComponentMapping{};
    auto subresource_range = vk::ImageSubresourceRange{}
    .setAspectMask(vk::ImageAspectFlagBits::eDepth)
    .setBaseMipLevel(0)
    .setLevelCount(1)
    .setBaseArrayLayer(0)
    .setLayerCount(1);

    auto view_info = vk::ImageViewCreateInfo{}
    .setSubresourceRange(subresource_range)
    .setComponents(component_mapping)
    .setFormat(directional_shadow_format)
    .setViewType(vk::ImageViewType::e2D);

    return allocate_image(image_info, view_info, allocation_info, debug_name);
}

void vulkan_engine_t::reallocate_buffer(allocated_buffer_t& buffer, const vk::BufferCreateInfo& bufferinfo, const vma::AllocationCreateInfo& allocationinfo, std::string debug_name)
{
    active_frame().pre_render.append(new allocated_buffer_t{buffer}, [](allocated_buffer_t* buffer)
    {
        gVulkan->destroy_buffer(*buffer);
        delete buffer;
    });

    resultcheck = allocator.createBuffer(&bufferinfo, &allocationinfo, &buffer.buffer, &buffer.allocation, &buffer.info);

    if(!debug_name.empty())
    {
        vkutil::name_object(buffer.buffer, fmt::format("{} buffer", debug_name));
        vkutil::name_object(buffer.allocation, fmt::format("{} allocation", debug_name));
    }
}

allocated_image_t vulkan_engine_t::allocate_image(const vk::ImageCreateInfo& imageinfo, vk::ImageViewCreateInfo& viewinfo, const vma::AllocationCreateInfo& allocationinfo, std::string debug_name) const
{
    allocated_image_t result;

    resultcheck = allocator.createImage(&imageinfo, &allocationinfo, &result.image, &result.allocation, &result.info);
    viewinfo.image = result.image;
    resultcheck = device.createImageView(&viewinfo, nullptr, &result.view);

    if(!debug_name.empty())
    {
        vkutil::name_object(result.image, "{} image", debug_name);
        vkutil::name_object(result.view, "{} view", debug_name);
        vkutil::name_object(result.allocation, "{} allocation", debug_name);
    }

    return result;
}

allocated_image_t vulkan_engine_t::allocate_shadow_cubemap(uint32_t extent, std::string debug_name)
{
    allocated_image_t result;

    auto image_info = vk::ImageCreateInfo{}
    .setFlags(vk::ImageCreateFlagBits::eCubeCompatible)
    .setUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled)
    .setImageType(vk::ImageType::e2D)
    .setFormat(cube_shadow_format)
    .setTiling(vk::ImageTiling::eOptimal)
    .setExtent(vk::Extent3D{extent, extent, 1})
    .setInitialLayout(vk::ImageLayout::eUndefined)
    .setSharingMode(vk::SharingMode::eExclusive)
    .setSamples(vk::SampleCountFlagBits::e1)
    .setMipLevels(1)
    .setArrayLayers(6);

    auto allocation_info = vma::AllocationCreateInfo{}
    .setFlags(vma::AllocationCreateFlagBits::eStrategyBestFit)
    .setUsage(vma::MemoryUsage::eAutoPreferDevice);

    resultcheck = allocator.createImage(&image_info, &allocation_info, &result.image, &result.allocation, &result.info);

    auto view_info = vk::ImageViewCreateInfo{}
    .setImage(result.image)
    .setFormat(cube_shadow_format)
    .setViewType(vk::ImageViewType::eCube)
    .setComponents(vk::ComponentMapping{vk::ComponentSwizzle::eR});
    view_info.subresourceRange
    .setAspectMask(vk::ImageAspectFlagBits::eDepth)
    .setLevelCount(1)
    .setLayerCount(6);

    resultcheck = device.createImageView(&view_info, nullptr, &result.view);

    if(!debug_name.empty())
    {
        vkutil::name_object(result.image, "{} image", debug_name);
        vkutil::name_object(result.view, "{} view", debug_name);
        vkutil::name_object(result.allocation, "{} allocation", debug_name);
    }

    return result;
}

void vulkan_engine_t::create_directional_shadow_sampler()
{
    auto sampler_info = vk::SamplerCreateInfo{}
    .setCompareEnable(true)
    .setCompareOp(vk::CompareOp::eGreater)
    .setMinFilter(vk::Filter::eLinear)
    .setMagFilter(vk::Filter::eLinear)
    .setAddressModeU(vk::SamplerAddressMode::eClampToBorder)
    .setAddressModeV(vk::SamplerAddressMode::eClampToBorder)
    .setAddressModeW(vk::SamplerAddressMode::eClampToBorder)
    .setBorderColor(vk::BorderColor::eFloatOpaqueBlack);

    directional_shadow_sampler = device.createSampler(sampler_info);
    vkutil::name_object(directional_shadow_sampler, "directional shadow sampler");
    queue_destruction(&directional_shadow_sampler);
}

void vulkan_engine_t::create_shadow_cubemap_sampler()
{
    auto sampler_info = vk::SamplerCreateInfo{}
    .setCompareEnable(true)
    .setCompareOp(vk::CompareOp::eGreater)
    .setMinFilter(vk::Filter::eLinear)
    .setMagFilter(vk::Filter::eLinear)
    .setAddressModeU(vk::SamplerAddressMode::eClampToBorder)
    .setAddressModeV(vk::SamplerAddressMode::eClampToBorder)
    .setAddressModeW(vk::SamplerAddressMode::eClampToBorder)
    .setBorderColor(vk::BorderColor::eFloatOpaqueBlack);

    cube_shadow_sampler = device.createSampler(sampler_info);
    vkutil::name_object(cube_shadow_sampler, "cube shadow sampler");
    queue_destruction(&cube_shadow_sampler);
}

void vulkan_engine_t::create_pointlight_pipeline()
{
    LogVulkan("creating cubelight pipeline");

    pipeline_builder.include_shaders("cubelight.vert", "cubelight.geom", "cubelight.frag");

    pipeline_builder
    .add_set_layout(pointlight_projection_layout)
    .add_set_layout(world_set_layout);

    (void)pipeline_builder.dynamic_state;

    pipeline_builder.rendering
    .setDepthAttachmentFormat(cube_shadow_format);

    pipeline_builder.set_vertex_input(&vertex_t::position_input);

    pipeline_builder.input_assembly
    .setTopology(vk::PrimitiveTopology::eTriangleList)
    .setPrimitiveRestartEnable(false);

    vk::Viewport viewport{0, 0, light_manager_t::CUBE_SHADOW_RESOLUTION, light_manager_t::CUBE_SHADOW_RESOLUTION, 0.0, 1.0};
    vk::Rect2D scissor{{0, 0}, {light_manager_t::CUBE_SHADOW_RESOLUTION, light_manager_t::CUBE_SHADOW_RESOLUTION}};

    pipeline_builder.viewport
    .setViewports(viewport)
    .setScissors(scissor);

    pipeline_builder.rasterization
    .setDepthClampEnable(false)
    .setRasterizerDiscardEnable(false)
    .setPolygonMode(vk::PolygonMode::eFill)
    .setCullMode(vk::CullModeFlagBits::eBack)
    .setFrontFace(vk::FrontFace::eClockwise)
    .setDepthBiasEnable(false)
    .setDepthBiasConstantFactor(0.005)
    .setDepthBiasClamp(0.05)
    .setDepthBiasSlopeFactor(1.0);

    pipeline_builder.multisample
    .setSampleShadingEnable(false)
    .setRasterizationSamples(vk::SampleCountFlagBits::e1)
    .setMinSampleShading(1.0f)
    .setAlphaToCoverageEnable(false)
    .setAlphaToOneEnable(false);

    pipeline_builder.color_blend
    .setLogicOpEnable(false);

    pipeline_builder.depth_stencil
    .setDepthTestEnable(true)
    .setDepthWriteEnable(true)
    .setDepthCompareOp(vk::CompareOp::eLess)
    .setDepthBoundsTestEnable(true)
    .setMinDepthBounds(0.0)
    .setMaxDepthBounds(1.0)
    .setStencilTestEnable(false);

    pipeline_builder.build(pointlight_pipeline, pointlight_pipelinelayout, "pointlight pipeline");
    queue_destruction(&pointlight_pipeline);

}

void vulkan_engine_t::directional_light_pass(frame_data_t& frame, std::span<entity_batch_t> batches, uint32_t light_index)
{
    vkutil::push_label(frame.shadowpass_cmd, fmt::format("directional light pass {}", light_index));

    allocated_image_t shadowmap = world_data.directional_shadowmaps[light_index];

    vk::Rect2D render_area{{0, 0}, {light_manager_t::PLANE_SHADOW_RESOLUTION, light_manager_t::PLANE_SHADOW_RESOLUTION}};

    vk::ClearValue depth_clear{};
    depth_clear.depthStencil = 1.0f;

    auto shadow_attachment = vk::RenderingAttachmentInfo{}
    .setLoadOp(vk::AttachmentLoadOp::eClear)
    .setStoreOp(vk::AttachmentStoreOp::eStore)
    .setClearValue(depth_clear)
    .setImageLayout(vk::ImageLayout::eDepthAttachmentOptimal)
    .setImageView(shadowmap.view);

    auto rendering_info = vk::RenderingInfo{}
    .setPDepthAttachment(&shadow_attachment)
    .setRenderArea(render_area)
    .setLayerCount(1);

    auto shadowmap2depth_attachment = vk::ImageMemoryBarrier2{}
    .setImage(shadowmap.image)
    .setOldLayout(vk::ImageLayout::eUndefined)
    .setNewLayout(vk::ImageLayout::eDepthAttachmentOptimal)
    .setSrcStageMask(PipelineStage::eEarlyFragmentTests | PipelineStage::eLateFragmentTests)
    .setSrcAccessMask(AccessFlag::eDepthStencilAttachmentRead | AccessFlag::eDepthStencilAttachmentWrite)
    .setDstStageMask(PipelineStage::eEarlyFragmentTests | PipelineStage::eLateFragmentTests)
    .setDstAccessMask(AccessFlag::eDepthStencilAttachmentRead | AccessFlag::eDepthStencilAttachmentWrite)
    .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
    .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
    shadowmap2depth_attachment.subresourceRange
    .setAspectMask(vk::ImageAspectFlagBits::eDepth)
    .setLayerCount(1)
    .setLevelCount(1)
    .setBaseArrayLayer(0)
    .setBaseMipLevel(0);

    auto dependency_shadowmap2depth_attachment = vk::DependencyInfo{}
    .setImageMemoryBarriers(shadowmap2depth_attachment);

    auto shadowmap_depth_attachment2depth_rdonly = vk::ImageMemoryBarrier2{}
    .setImage(shadowmap.image)
    .setOldLayout(vk::ImageLayout::eDepthAttachmentOptimal)
    .setNewLayout(vk::ImageLayout::eDepthReadOnlyOptimal)
    .setSrcStageMask(PipelineStage::eEarlyFragmentTests | PipelineStage::eLateFragmentTests)
    .setSrcAccessMask(AccessFlag::eDepthStencilAttachmentWrite)
    .setDstStageMask(PipelineStage::eFragmentShader)
    .setDstAccessMask(AccessFlag::eShaderRead)
    .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
    .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
    shadowmap_depth_attachment2depth_rdonly.subresourceRange
    .setAspectMask(vk::ImageAspectFlagBits::eDepth)
    .setLayerCount(1)
    .setLevelCount(1)
    .setBaseArrayLayer(0)
    .setBaseMipLevel(0);

    auto dependency_shadowmap_depth_attachment2depth_rdonly = vk::DependencyInfo{}
    .setImageMemoryBarriers(shadowmap_depth_attachment2depth_rdonly);

    frame.shadowpass_cmd.pipelineBarrier2(dependency_shadowmap2depth_attachment);
    frame.shadowpass_cmd.beginRendering(rendering_info);

    frame.shadowpass_cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, directional_light_pipeline);

    std::array sets{frame.world_set, frame.directional_light_projection_set};
    std::array offsets{uint32_t((sizeof(uint32_t) * 4) + (sizeof(directional_light_data_t)) * light_index)};

    frame.shadowpass_cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, directional_light_pipelinelayout, 0, sets, offsets);

    model_handle_t last_model = nullptr;

    for(const entity_batch_t& batch : batches)
    {
        if(batch.model != last_model)
        {
            last_model = batch.model;
            last_model->bind_positions(frame.shadowpass_cmd);
        }

        const uint32_t num_mesh_indices = batch.model->mesh.indices.size();
        for(uint32_t entity_index : batch.indices)
        {
            frame.shadowpass_cmd.drawIndexed(num_mesh_indices, 1, 0, 0, entity_index);
        }
    }

    frame.shadowpass_cmd.endRendering();
    frame.shadowpass_cmd.pipelineBarrier2(dependency_shadowmap_depth_attachment2depth_rdonly);

    vkutil::pop_label(frame.shadowpass_cmd);
}

void vulkan_engine_t::pointlight_shadow_pass(frame_data_t& frame, std::span<entity_batch_t> batches, uint32_t pointlight_index)
{
    vkutil::push_label(frame.shadowpass_cmd, fmt::format("pointlight pass {}", pointlight_index));

    allocated_image_t cubemap = world_data.cube_shadowmaps[pointlight_index];

    vk::Rect2D render_area{{0, 0}, {light_manager_t::CUBE_SHADOW_RESOLUTION, light_manager_t::CUBE_SHADOW_RESOLUTION}};

    vk::ClearValue depth_clear{};
    depth_clear.depthStencil = 1.0f;

    auto shadow_attachment = vk::RenderingAttachmentInfo{}
    .setLoadOp(vk::AttachmentLoadOp::eClear)
    .setStoreOp(vk::AttachmentStoreOp::eStore)
    .setClearValue(depth_clear)
    .setImageLayout(vk::ImageLayout::eDepthAttachmentOptimal)
    .setImageView(cubemap.view);

    auto rendering_info = vk::RenderingInfo{}
    .setPDepthAttachment(&shadow_attachment)
    .setRenderArea(render_area)
    .setLayerCount(6);

    auto shadowmap2depth_attachment = vk::ImageMemoryBarrier2{}
    .setImage(cubemap.image)
    .setOldLayout(vk::ImageLayout::eUndefined)
    .setNewLayout(vk::ImageLayout::eDepthAttachmentOptimal)
    .setSrcStageMask(PipelineStage::eEarlyFragmentTests | PipelineStage::eLateFragmentTests)
    .setSrcAccessMask(AccessFlag::eDepthStencilAttachmentRead | AccessFlag::eDepthStencilAttachmentWrite)
    .setDstStageMask(PipelineStage::eEarlyFragmentTests | PipelineStage::eLateFragmentTests)
    .setDstAccessMask(AccessFlag::eDepthStencilAttachmentRead | AccessFlag::eDepthStencilAttachmentWrite)
    .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
    .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
    shadowmap2depth_attachment.subresourceRange
    .setAspectMask(vk::ImageAspectFlagBits::eDepth)
    .setLayerCount(6)
    .setLevelCount(1)
    .setBaseArrayLayer(0)
    .setBaseMipLevel(0);

    auto dependency_shadowmap2depth_attachment = vk::DependencyInfo{}
    .setImageMemoryBarriers(shadowmap2depth_attachment);

    auto shadowmap_depth_attachment2depth_rdonly = vk::ImageMemoryBarrier2{}
    .setImage(cubemap.image)
    .setOldLayout(vk::ImageLayout::eDepthAttachmentOptimal)
    .setNewLayout(vk::ImageLayout::eDepthReadOnlyOptimal)
    .setSrcStageMask(PipelineStage::eEarlyFragmentTests | PipelineStage::eLateFragmentTests)
    .setSrcAccessMask(AccessFlag::eDepthStencilAttachmentWrite)
    .setDstStageMask(PipelineStage::eFragmentShader)
    .setDstAccessMask(AccessFlag::eShaderRead)
    .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
    .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
    shadowmap_depth_attachment2depth_rdonly.subresourceRange
    .setAspectMask(vk::ImageAspectFlagBits::eDepth)
    .setLayerCount(6)
    .setLevelCount(1)
    .setBaseArrayLayer(0)
    .setBaseMipLevel(0);

    auto dependency_shadowmap_depth_attachment2depth_rdonly = vk::DependencyInfo{}
    .setImageMemoryBarriers(shadowmap_depth_attachment2depth_rdonly);

    frame.shadowpass_cmd.pipelineBarrier2(dependency_shadowmap2depth_attachment);
    frame.shadowpass_cmd.beginRendering(rendering_info);

    frame.shadowpass_cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pointlight_pipeline);

    std::array sets{frame.pointlight_projection_set, frame.world_set};
    std::array offsets{uint32_t(sizeof(pointlight_projection_t) * pointlight_index), uint32_t((sizeof(uint32_t) * 4) + (sizeof(pointlight_t) * pointlight_index))};

    frame.shadowpass_cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pointlight_pipelinelayout, 0, sets, offsets);

    model_handle_t last_model = nullptr;

    for(const entity_batch_t& batch : batches)
    {
        if(batch.model != last_model)
        {
            last_model = batch.model;
            last_model->bind_positions(frame.shadowpass_cmd);
        }

        const uint32_t num_mesh_indices = batch.model->mesh.indices.size();
        for(uint32_t entity_index : batch.indices)
        {
            frame.shadowpass_cmd.drawIndexed(num_mesh_indices, 1, 0, 0, entity_index);
        }
    }

    frame.shadowpass_cmd.endRendering();
    frame.shadowpass_cmd.pipelineBarrier2(dependency_shadowmap_depth_attachment2depth_rdonly);

    vkutil::pop_label(frame.shadowpass_cmd);
}

uint32_t vulkan_engine_t::acquire_swapchain_image(frame_data_t& frame)
{
    constexpr uint64_t timeout = std::nano::den * 10;
    resultcheck = device.waitForFences(frame.in_flight, true, timeout);

    auto acquire_image_info = vk::AcquireNextImageInfoKHR{}
    .setSwapchain(swapchain)
    .setSemaphore(frame.image_available)
    .setFence(nullptr)
    .setTimeout(timeout)
    .setDeviceMask(1);

    uint32_t swapchain_image_index;
    vk::Result acquire_image_result = device.acquireNextImage2KHR(&acquire_image_info, &swapchain_image_index);

    if(acquire_image_result == vk::Result::eErrorOutOfDateKHR)
    {
        return -1;
    }
    else if(acquire_image_result != vk::Result::eSuccess && acquire_image_result != vk::Result::eSuboptimalKHR)
    {
        vk::throwResultException(acquire_image_result, nullptr);
    }

    device.resetFences(frame.in_flight);

    return swapchain_image_index;
}

void vulkan_engine_t::entity_pass(frame_data_t& frame, std::span<entity_batch_t> batches)
{
    vkutil::push_label(frame.cmd, "entity pass");

    std::vector<vk::DescriptorImageInfo> directional_images{};
    directional_images.resize(world_data.directional_lights.size());

    for(uint32_t index = 0; index < world_data.directional_shadowmaps.size(); ++index)
    {
        directional_images[index] = vk::DescriptorImageInfo{}
        .setImageView(world_data.directional_shadowmaps[index].view)
        .setImageLayout(vk::ImageLayout::eDepthReadOnlyOptimal)
        .setSampler(directional_shadow_sampler);
    }

    auto write_directional_images = vk::WriteDescriptorSet{}
    .setDstSet(frame.directional_shadow_set)
    .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
    .setDstArrayElement(0)
    .setDstBinding(0)
    .setDescriptorCount(directional_images.size())
    .setPImageInfo(directional_images.data());

    std::vector<vk::DescriptorImageInfo> pointlight_images{};
    pointlight_images.resize(world_data.pointlights.size());

    for(uint32_t index = 0; index < world_data.pointlights.size(); ++index)
    {
        pointlight_images[index] = vk::DescriptorImageInfo{}
        .setImageView(world_data.cube_shadowmaps[index].view)
        .setImageLayout(vk::ImageLayout::eDepthReadOnlyOptimal);
    }

    auto write_pointlight_images = vk::WriteDescriptorSet{}
    .setDstSet(frame.pointlight_shadow_set)
    .setDescriptorType(vk::DescriptorType::eSampledImage)
    .setDstArrayElement(0)
    .setDstBinding(1)
    .setDescriptorCount(pointlight_images.size())
    .setPImageInfo(pointlight_images.data());

    device.updateDescriptorSets({write_directional_images, write_pointlight_images}, {});

    slothandle_t<model_t> last_model = nullptr;
    slothandle_t<texture_t> last_texture = nullptr;
    slothandle_t<material_t> last_masterial = nullptr;

    std::array sets{global_descriptor_set, frame.world_set, frame.pointlight_shadow_set, frame.directional_shadow_set};
    std::array offsets{uint32_t(pad_uniform_buffer_size(sizeof(global_device_data_t)) * frame_index())};

    for(const entity_batch_t& batch : batches)
    {
        if(last_masterial != batch.material)
        {
            frame.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, batch.material->pipeline);
            frame.cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, batch.material->pipeline_layout, 0, sets, offsets);

            last_masterial = batch.material;
        }

        if(last_model != batch.model)
        {
            last_model = batch.model;
            batch.model->bind_positions_normal_uv(frame.cmd);
        }

        if(last_texture != batch.texture)
        {
            last_texture = batch.texture;
            frame.cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, last_masterial->pipeline_layout, 4, last_texture->set, {});
        }

        const uint32_t mesh_indices = batch.model->mesh.indices.size();
        for(uint32_t entity_index : batch.indices)
        {
            frame.cmd.drawIndexed(mesh_indices, 1, 0, 0, entity_index);
        }
    }

    vkutil::pop_label(frame.cmd);
}

void vulkan_engine_t::particle_pass(frame_data_t& frame)
{
    vkutil::push_label(frame.cmd, "particle pass");

    material_handle_t material = gWorld->find_material("particle");
    texture_handle_t texture = gWorld->find_texture("katt star");

    std::array descriptor_sets{global_descriptor_set, texture->set};
    std::array set_offsets{uint32_t(pad_uniform_buffer_size(sizeof(global_device_data_t)) * frame_index())};

    std::array vertex_buffers{particle_emitter.model->vertex_buffer.buffer, particle_emitter.model->vertex_buffer.buffer, particle_emitter.instance_buffer.buffer};
    std::array vertex_offsets{0ul, particle_emitter.model->mesh.vertices.size() * sizeof(vertex_t::position), 0ul};

    frame.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, material->pipeline);
    frame.cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, material->pipeline_layout, 0, descriptor_sets, set_offsets);

    frame.cmd.bindIndexBuffer(particle_emitter.model->index_buffer.buffer, 0, vk::IndexType::eUint32);
    frame.cmd.bindVertexBuffers(0, vertex_buffers, vertex_offsets);

    frame.cmd.drawIndexed(particle_emitter.model->mesh.indices.size(), particle_emitter.instances, 0, 0, 0);

    vkutil::pop_label(frame.cmd);
}

void vulkan_engine_t::pointlight_mesh_pass(frame_data_t& frame)
{
    vkutil::push_label(frame.cmd, "pointlight mesh pass");

    model_handle_t sphere_model = gWorld->find_model("sphere");
    const uint32_t index_count = sphere_model->mesh.indices.size();

    std::array sets{global_descriptor_set, frame.world_set};
    std::array offsets{uint32_t(pad_uniform_buffer_size(sizeof(global_device_data_t)) * frame_index())};

    frame.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pointlight_mesh_pipeline);
    frame.cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pointlight_mesh_pipelinelayout, 0, sets, offsets);

    sphere_model->bind_positions(frame.cmd);

    for(uint32_t pointlight_index = 0; pointlight_index < world_data.pointlights.size(); ++pointlight_index)
    {
        frame.cmd.drawIndexed(index_count, 1, 0, 0, pointlight_index);
    }

    vkutil::pop_label(frame.cmd);
}

void vulkan_engine_t::ui_pass(frame_data_t& frame)
{
    vkutil::push_label(frame.cmd, "ui pass");

    frame.cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, line_pipeline);

    glm::dvec2 flt_extent{double(image_extent.width), double(image_extent.height)};
    flt_extent = glm::normalize(flt_extent);

    transform_t gizmo_transform{};
    gizmo_transform.scale = {0.225 * flt_extent.y,-0.225 * flt_extent.x,0.225};
    gizmo_transform.location = {-0.75, 0.75, 0.5};
    gizmo_transform.rotation = glm::conjugate(world_data.camera.rotation);
    glm::mat4x4 gizmo_matrix = gizmo_transform.world_matrix();

    gizmo_push_constants_t gizmo_constants{};
    gizmo_constants.transform = gizmo_matrix;
    gizmo_constants.force_depth_1 = 0;

    //frame.cmd.pushConstants(line_pipelinelayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, sizeof(gizmo_constants), &gizmo_constants);

    uint32_t global_device_data_offset = pad_uniform_buffer_size(sizeof(global_device_data_t)) * frame_index();
    frame.cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, line_pipelinelayout, 0, global_descriptor_set, global_device_data_offset);
    frame.cmd.draw(6, 1, 0, 0);

    vkutil::insert_label(frame.cmd, "ImGUI");
    ImGui_ImplVulkan_RenderDrawData(&imgui_data, frame.cmd);

    vkutil::pop_label(frame.cmd);
}

void vulkan_engine_t::prepare_frame(frame_data_t& frame)
{
    frame.pre_render.flush();
    frame.pre_render.queue.swap(frame.next_render.queue);

    auto inheritance = vk::CommandBufferInheritanceInfo{};

    auto begin_cmd = vk::CommandBufferBeginInfo{}
    .setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);

    frame.shadowpass_cmd.begin(begin_cmd);

    frame.cmd.begin(begin_cmd);

    frame.recording.end();
    frame.shadowpass_cmd.executeCommands(frame.recording); //becouse shadowpass in prior in submission-order

    std::swap(frame.recording, frame.pending);

    begin_cmd.setPInheritanceInfo(&inheritance);
    frame.recording.begin(begin_cmd);
}

void vulkan_engine_t::begin_swapchain_render(frame_data_t& frame, uint32_t swapchain_image)
{
    glm::vec3 sky_color = world_data.scene.sky_color;
    vk::ClearValue color_clear{{sky_color.r, sky_color.g, sky_color.b, 1.f}};
    vk::ClearValue depth_clear{};
    depth_clear.depthStencil = 0.0f; //map 0 to far plane and 1 to near plane

    auto[viewport, render_area] = whole_render_area();

    auto color_attachment = vk::RenderingAttachmentInfo{}
    .setImageView(swapchain_image_views[swapchain_image])
    .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
    .setClearValue(color_clear)
    .setLoadOp(vk::AttachmentLoadOp::eClear)
    .setStoreOp(vk::AttachmentStoreOp::eStore);

    auto depth_attachment = vk::RenderingAttachmentInfo{}
    .setImageView(swapchain_depth_image.view)
    .setImageLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal)
    .setClearValue(depth_clear)
    .setLoadOp(vk::AttachmentLoadOp::eClear)
    .setStoreOp(vk::AttachmentStoreOp::eStore);

    auto rendering_info = vk::RenderingInfo{}
    .setFlags(vk::RenderingFlagBits{})
    .setRenderArea(render_area)
    .setLayerCount(1)
    .setColorAttachments(color_attachment)
    .setPDepthAttachment(&depth_attachment);

    auto color_subresourcerange = vk::ImageSubresourceRange{}
    .setAspectMask(vk::ImageAspectFlagBits::eColor)
    .setBaseMipLevel(0)
    .setLevelCount(1)
    .setBaseArrayLayer(0)
    .setLayerCount(1);

    auto swapchain_image2color_attachment = vk::ImageMemoryBarrier2{}
    .setImage(swapchain_images[swapchain_image])
    .setOldLayout(vk::ImageLayout::eUndefined)
    .setNewLayout(vk::ImageLayout::eColorAttachmentOptimal)
    .setSubresourceRange(color_subresourcerange)
    .setSrcStageMask(PipelineStage::eFragmentShader)
    .setSrcAccessMask(AccessFlag::eNone)
    .setDstStageMask(PipelineStage::eColorAttachmentOutput)
    .setDstAccessMask(AccessFlag::eColorAttachmentRead | AccessFlag::eColorAttachmentWrite)
    .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
    .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);

    auto depth_subresourcerange = color_subresourcerange;
    depth_subresourcerange.setAspectMask(vk::ImageAspectFlagBits::eDepth);

    auto depth_image2depth_stencil_attachment = vk::ImageMemoryBarrier2{}
    .setImage(swapchain_depth_image.image)
    .setOldLayout(vk::ImageLayout::eUndefined)
    .setNewLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal)
    .setSubresourceRange(depth_subresourcerange)
    .setSrcStageMask(PipelineStage::eEarlyFragmentTests | PipelineStage::eLateFragmentTests)
    .setSrcAccessMask(AccessFlag::eDepthStencilAttachmentRead | AccessFlag::eDepthStencilAttachmentWrite)
    .setDstStageMask(PipelineStage::eEarlyFragmentTests | PipelineStage::eLateFragmentTests)
    .setDstAccessMask(AccessFlag::eDepthStencilAttachmentRead | AccessFlag::eDepthStencilAttachmentWrite)
    .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
    .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);

    std::array image_barriers{swapchain_image2color_attachment, depth_image2depth_stencil_attachment};

    auto image_barriers_dependency = vk::DependencyInfo{}
    .setImageMemoryBarriers(image_barriers);

    frame.cmd.pipelineBarrier2(image_barriers_dependency);
    frame.cmd.beginRendering(rendering_info);
    frame.cmd.setViewport(0, viewport);
    frame.cmd.setScissor(0, render_area);
}

void vulkan_engine_t::end_swapchain_render(frame_data_t& frame, uint32_t swapchain_image)
{
    auto color_subresourcerange = vk::ImageSubresourceRange{}
    .setAspectMask(vk::ImageAspectFlagBits::eColor)
    .setBaseMipLevel(0)
    .setLevelCount(1)
    .setBaseArrayLayer(0)
    .setLayerCount(1);

    auto swapchain_image2present_src = vk::ImageMemoryBarrier2{}
    .setImage(swapchain_images[swapchain_image])
    .setOldLayout(vk::ImageLayout::eColorAttachmentOptimal)
    .setNewLayout(vk::ImageLayout::ePresentSrcKHR)
    .setSubresourceRange(color_subresourcerange)
    .setSrcStageMask(PipelineStage::eColorAttachmentOutput)
    .setSrcAccessMask(AccessFlag::eColorAttachmentWrite)
    .setDstStageMask(PipelineStage::eColorAttachmentOutput)
    .setDstAccessMask(AccessFlag::eNone)
    .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
    .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);

    vk::DependencyInfo image_barriers_dependency{};
    image_barriers_dependency.setImageMemoryBarriers(swapchain_image2present_src);

    frame.cmd.endRendering();
    frame.cmd.pipelineBarrier2(image_barriers_dependency);
    frame.cmd.end();
}

void vulkan_engine_t::submit_commands(frame_data_t& frame)
{
    auto shadow_pass_cmd_info = vk::CommandBufferSubmitInfo{}
    .setCommandBuffer(frame.shadowpass_cmd);

    auto shadow_pass_submit = vk::SubmitInfo2{}
    .setCommandBufferInfos(shadow_pass_cmd_info);

    auto main_pass_cmd_info = vk::CommandBufferSubmitInfo{}
    .setCommandBuffer(frame.cmd);

    auto main_signal_info = vk::SemaphoreSubmitInfo{}
    .setSemaphore(frame.draw_finished)
    .setStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput);

    auto main_wait_info = vk::SemaphoreSubmitInfo{}
    .setSemaphore(frame.image_available)
    .setStageMask(vk::PipelineStageFlagBits2::eFragmentShader);

    auto main_pass_submit = vk::SubmitInfo2{}
    .setCommandBufferInfos(main_pass_cmd_info)
    .setWaitSemaphoreInfos(main_wait_info)
    .setSignalSemaphoreInfos(main_signal_info);

    queues.graphics.submit2({shadow_pass_submit, main_pass_submit}, frame.in_flight);
}

void vulkan_engine_t::present_swapchain_image(frame_data_t& frame, uint32_t swapchain_image)
{
    vk::Result present_result;

    auto present_info = vk::PresentInfoKHR{}
    .setImageIndices(swapchain_image)
    .setWaitSemaphores(frame.draw_finished)
    .setSwapchains(swapchain)
    .setResults(present_result);

    (void)queues.present.presentKHR(present_info);

    if(present_result == vk::Result::eErrorOutOfDateKHR || present_result == vk::Result::eSuboptimalKHR)
    {
        recreate_swapchain();
    }
    else if(present_result != vk::Result::eSuccess)
    {
        vk::throwResultException(present_result, nullptr);
    }
}

void vulkan_engine_t::queue_swapchain_destruction()
{
    destruction_que.append(&swapchain_image_views, [](std::vector<vk::ImageView>* views)
    {
        for(vk::ImageView view : *views)
        {
            gVulkan->device.destroyImageView(view);
        }
    });

    destruction_que.append(&swapchain_depth_image, [](allocated_image_t* image)
    {
        gVulkan->destroy_image(*image);
    });

    destruction_que.append(&swapchain, [](vk::SwapchainKHR* swapchain)
    {
        gVulkan->device.destroySwapchainKHR(*swapchain);
    });
}

void vulkan_engine_t::check_buffer_sizes(frame_data_t& frame)
{
    auto reallocate = [this, &frame](uint64_t new_size)
    {
        LogVulkan("reallocating transform buffer {}, from {} to {} num", frame_index(), frame.entity_transforms_allocated, new_size);
        frame.entity_transforms_allocated = new_size;

        auto buffer_info = vk::BufferCreateInfo{}
        .setSize(new_size * sizeof(packed_transform_t))
        .setUsage(vk::BufferUsageFlagBits::eStorageBuffer)
        .setSharingMode(vk::SharingMode::eExclusive);

        auto allocation_info = vma::AllocationCreateInfo{}
        .setFlags(vma::AllocationCreateFlagBits::eMapped | vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eStrategyBestFit)
        .setUsage(vma::MemoryUsage::eAutoPreferDevice);

        reallocate_buffer(frame.entity_transform_buffer, buffer_info, allocation_info, fmt::format("device entity transforms [{}]", frame_index()));

        auto descriptor_buffer_info = vk::DescriptorBufferInfo{}
        .setBuffer(frame.entity_transform_buffer.buffer)
        .setRange(frame.entity_transforms_allocated * sizeof(packed_transform_t))
        .setOffset(0);

        auto write_transform_set = vk::WriteDescriptorSet{}
        .setDstSet(frame.world_set)
        .setDescriptorType(vk::DescriptorType::eStorageBuffer)
        .setDstBinding(0)
        .setBufferInfo(descriptor_buffer_info);

        device.updateDescriptorSets(write_transform_set, {});
    };

    if(world_data.transforms.size() > frame.entity_transforms_allocated || int64_t(world_data.transforms.size()) < int64_t(frame.entity_transforms_allocated) - int64_t(world_t::device_transforms_allocation_step * 2))
    {
        reallocate(world_data.transforms.size() + world_t::device_transforms_allocation_step);
    }
}

void vulkan_engine_t::upload_device_global_data()
{
    int width; int height;
    glfwGetWindowSize(glfwwindow, &width, &height);

    global_device_data_t tmp_buffer{};
    tmp_buffer.statistics.deltatime = program_time.fp_delta;
    tmp_buffer.statistics.frame = program_time.frame_count;
    tmp_buffer.statistics.elapsed_seconds = program_time.total.tv_sec;
    tmp_buffer.statistics.elapsed_nanoseconds = program_time.total.tv_nsec;
    tmp_buffer.statistics.elapsed_time = timespec2double(program_time.total);
    tmp_buffer.statistics.screen_size.x = width;
    tmp_buffer.statistics.screen_size.y = height;
    tmp_buffer.camera.location = world_data.camera.location;
    tmp_buffer.camera.rotation = world_data.camera.rotation;
    tmp_buffer.camera.view = world_data.camera.view_matrix();
    tmp_buffer.camera.projection = world_data.camera.projection_matrix();
    tmp_buffer.camera.projection_view = tmp_buffer.camera.projection * tmp_buffer.camera.view;
    tmp_buffer.scene = world_data.scene;

    size_t offset = pad_uniform_buffer_size(sizeof(global_device_data_t)) * get_vulkan().frame_index();
    auto* memory = static_cast<uint8_t*>(global_buffer.info.pMappedData);

    memcpy(memory + offset, &tmp_buffer, sizeof(global_device_data_t));
}

extern "C" void upload_entity_transforms(packed_transform_t* dst, const transform_t* src, uint64_t count);

void vulkan_engine_t::upoad_transforms()
{
    frame_data_t& frame = active_frame();
    auto device_data = static_cast<packed_transform_t*>(frame.entity_transform_buffer.info.pMappedData);

    upload_entity_transforms(device_data, world_data.transforms.data(), world_data.entities.size());
}

void vulkan_engine_t::upload_directional_lights()
{
    frame_data_t& frame = active_frame();
    uint32_t num_lights = world_data.directional_lights.size();

    auto data = static_cast<uint8_t*>(frame.directional_light_buffer.info.pMappedData);
    memcpy(data, &num_lights, sizeof(uint32_t));
    data += sizeof(uint32_t) * 4;

    for(uint32_t index = 0; index < world_data.directional_lights.size(); ++index)
    {
        directional_light_data_t tmp_buffer;
        tmp_buffer.light = world_data.directional_lights[index];

        glm::mat4x4 light_view = glm::rotate(glm::identity<glm::mat4x4>(), -std::numbers::pi_v<float> / 2.0f, axis::right);
        light_view = glm::translate(light_view, {0, -50, 0});
        glm::mat4x4 light_projection = glm::ortho(-100.f, 100.f, 100.f, -100.f, 1.f, 100.f);

        tmp_buffer.perspective = light_projection * light_view;

        memcpy(data, &tmp_buffer, sizeof(directional_light_data_t));
        data += sizeof(directional_light_data_t);
    }
}

void vulkan_engine_t::upload_pointlights()
{
    frame_data_t& frame = active_frame();
    uint32_t num_lights = world_data.pointlights.size();

    auto pointlights = static_cast<uint8_t*>(frame.pointlight_buffer.info.pMappedData);
    memcpy(pointlights, &num_lights, sizeof(uint32_t));
    memcpy(pointlights + (sizeof(uint32_t) * 4), world_data.pointlights.data(), num_lights * sizeof(pointlight_t));

    auto projections = static_cast<pointlight_projection_t*>(frame.pointlight_projection_buffer.info.pMappedData);

    for(uint32_t index = 0; index < num_lights; ++index)
    {
        const pointlight_t& light = world_data.pointlights[index];
        pointlight_projection_t tmp_buffer;

        glm::mat4x4 cube_perspective = math::perspective(1.0f, std::numbers::pi_v<float> / 2.0f, 0.1, light.strength);
        tmp_buffer[0] = cube_perspective * math::view(axis::right, axis::up, light.location);
        tmp_buffer[1] = cube_perspective * math::view(axis::left, axis::up, light.location);
        tmp_buffer[2] = cube_perspective * math::view(axis::up, axis::backward, light.location);
        tmp_buffer[3] = cube_perspective * math::view(axis::down, axis::forward, light.location);
        tmp_buffer[4] = cube_perspective * math::view(axis::forward, axis::up, light.location);
        tmp_buffer[5] = cube_perspective * math::view(axis::backward, axis::up, light.location);

        memcpy(projections + index, &tmp_buffer, sizeof(pointlight_projection_t));
    }
}

constexpr float PI2 = std::numbers::pi * 2.0;

void vulkan_engine_t::upload_particle_control()
{
    size_t particle_control_offset = pad_uniform_buffer_size(sizeof(particle_control_data)) * frame_index();
    uint8_t* data = (uint8_t*)particle_control_buffer.info.pMappedData;
    data += particle_control_offset;

    static particle_control_data tmp_buffer{0, 0};
    tmp_buffer.circle_time += program_time.fp_delta / 100.0;

    while(tmp_buffer.circle_time >= PI2)
    {
        tmp_buffer.circle_time -= PI2;
    }

    tmp_buffer.particle_count = particle_emitter.instances;
    memcpy(data, &tmp_buffer, sizeof(tmp_buffer));
}

void vulkan_engine_t::flush_uploads()
{
    frame_data_t& frame = active_frame();
    size_t device_global_offset = pad_uniform_buffer_size(sizeof(global_device_data_t)) * frame_index();
    size_t particle_control_offset = pad_uniform_buffer_size(sizeof(particle_control_data)) * frame_index();

    allocator.flushAllocations(
            {get_vulkan().global_buffer.allocation, frame.entity_transform_buffer.allocation, frame.directional_light_buffer.allocation, frame.pointlight_buffer.allocation, frame.pointlight_projection_buffer.allocation, particle_emitter.instance_buffer.allocation},
            {device_global_offset, 0, 0, 0, 0, particle_control_offset},
            {sizeof(global_device_data_t), world_data.entities.size() * sizeof(packed_transform_t), (sizeof(uint32_t) * 4) + (sizeof(directional_light_data_t) * world_data.directional_lights.size()), (world_data.pointlights.size() * sizeof(pointlight_t)) + (sizeof(uint32_t) * 4), world_data.pointlights.size() * sizeof(pointlight_projection_t), sizeof(particle_control_data)});
}

void vulkan_engine_t::create_pointlight_mesh_pipeline()
{
    LogVulkan("creating pointlight mesh pipeline");

    pipeline_builder.include_shaders("uniform_color_translucent.vert", "uniform_color_translucent.frag");

    pipeline_builder
    .add_set_layout(global_set_layout)
    .add_set_layout(world_set_layout);

    std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    pipeline_builder.dynamic_state
    .setDynamicStates(dynamic_states);

    pipeline_builder.rendering
    .setColorAttachmentFormats(surface_format.format)
    .setDepthAttachmentFormat(depth_format);

    pipeline_builder.set_vertex_input(&vertex_t::position_input);

    pipeline_builder.input_assembly
    .setTopology(vk::PrimitiveTopology::eTriangleList)
    .setPrimitiveRestartEnable(false);

    auto[viewport, scissor] = whole_render_area();
    pipeline_builder.viewport
    .setViewports(viewport)
    .setScissors(scissor);

    pipeline_builder.rasterization
    .setDepthClampEnable(false)
    .setRasterizerDiscardEnable(false)
    .setPolygonMode(vk::PolygonMode::eFill)
    .setCullMode(vk::CullModeFlagBits::eBack)
    .setFrontFace(vk::FrontFace::eClockwise)
    .setDepthBiasEnable(false);

    pipeline_builder.multisample
    .setSampleShadingEnable(false)
    .setRasterizationSamples(vk::SampleCountFlagBits::e1)
    .setMinSampleShading(1.0f)
    .setAlphaToCoverageEnable(false)
    .setAlphaToOneEnable(false);

    using enum vk::ColorComponentFlagBits;
    auto color_blend_attachment = vk::PipelineColorBlendAttachmentState{}
    .setColorWriteMask(eR | eG | eB | eA)
    .setBlendEnable(false);

    pipeline_builder.color_blend
    .setLogicOpEnable(false)
    .setLogicOp(vk::LogicOp::eSet)
    .setAttachments(color_blend_attachment);

    pipeline_builder.depth_stencil
    .setDepthTestEnable(true)
    .setDepthWriteEnable(true)
    .setDepthCompareOp(vk::CompareOp::eGreater)
    .setDepthBoundsTestEnable(true)
    .setMinDepthBounds(0.0)
    .setMaxDepthBounds(1.0)
    .setStencilTestEnable(false);

    pipeline_builder.build(pointlight_mesh_pipeline, pointlight_mesh_pipelinelayout, "pointlight mesh");
    queue_destruction(&pointlight_mesh_pipeline);
}

void vulkan_engine_t::create_directional_light_pipeline()
{
    LogVulkan("creating directional light pipeline");

    pipeline_builder.include_shaders("directional_light.vert", "directional_light.frag");

    pipeline_builder
    .add_set_layout(world_set_layout)
    .add_set_layout(directional_light_projection_layout);

    (void)pipeline_builder.dynamic_state;

    pipeline_builder.rendering
    .setDepthAttachmentFormat(directional_shadow_format);

    pipeline_builder.set_vertex_input(&vertex_t::position_input);

    pipeline_builder.input_assembly
    .setTopology(vk::PrimitiveTopology::eTriangleList)
    .setPrimitiveRestartEnable(false);

    vk::Viewport viewport{0, 0, light_manager_t::PLANE_SHADOW_RESOLUTION, light_manager_t::PLANE_SHADOW_RESOLUTION, 0.0, 1.0};
    vk::Rect2D scissor{{0, 0}, {light_manager_t::PLANE_SHADOW_RESOLUTION, light_manager_t::PLANE_SHADOW_RESOLUTION}};

    pipeline_builder.viewport
    .setViewports(viewport)
    .setScissors(scissor);

    pipeline_builder.rasterization
    .setDepthClampEnable(false)
    .setRasterizerDiscardEnable(false)
    .setPolygonMode(vk::PolygonMode::eFill)//line for wireframe
    .setCullMode(vk::CullModeFlagBits::eBack)
    .setFrontFace(vk::FrontFace::eClockwise)
    .setDepthBiasEnable(false)
    .setLineWidth(1.0f);

    pipeline_builder.multisample
    .setSampleShadingEnable(false)
    .setRasterizationSamples(vk::SampleCountFlagBits::e1)
    .setMinSampleShading(1.0f)
    .setAlphaToCoverageEnable(false)
    .setAlphaToOneEnable(false);

    using enum vk::ColorComponentFlagBits;
    auto color_blend_attachment = vk::PipelineColorBlendAttachmentState{}
    .setColorWriteMask(eR)
    .setBlendEnable(false);

    pipeline_builder.color_blend
    .setLogicOpEnable(false)
    .setLogicOp(vk::LogicOp::eSet)
    .setAttachments(color_blend_attachment);

    pipeline_builder.depth_stencil
    .setDepthTestEnable(true)
    .setDepthWriteEnable(true)
    .setDepthCompareOp(vk::CompareOp::eLess)
    .setDepthBoundsTestEnable(true)
    .setMinDepthBounds(0.0)
    .setMaxDepthBounds(1.0)
    .setStencilTestEnable(false);

    pipeline_builder.build(directional_light_pipeline, directional_light_pipelinelayout, "directional light");
    queue_destruction(&directional_light_pipeline);
}

void vulkan_engine_t::destroy_pipelines()
{
    for(material_t& material : gWorld->materials)
    {
        if(material.name != name_t::null_name) //fixme this is silly, remove null name material and have handles be null instead
        {
            device.destroyPipeline(material.pipeline);
            material.pipeline = nullptr;
        }
    }

    device.destroyPipeline(directional_light_pipeline); directional_light_pipeline = nullptr;
    device.destroyPipeline(pointlight_pipeline); pointlight_pipeline = nullptr;
    device.destroyPipeline(line_pipeline); line_pipeline = nullptr;
    device.destroyPipeline(pointlight_mesh_pipeline); pointlight_mesh_pipeline = nullptr;
    device.destroyPipeline(animate_particle_pipeline); animate_particle_pipeline = nullptr;
}

void vulkan_engine_t::create_particle_pipeline()
{
    LogVulkan("creating particle pipeline");

    material_handle_t material = gWorld->add_unique_material("particle");

    pipeline_builder.include_shaders("particle.vert", "particle.frag");
    pipeline_builder.add_set_layouts(global_set_layout, texture_set_layout);
    pipeline_builder.set_vertex_input(&vertex_t::position_normal_uv_instance_input);
    //pipeline_builder.set_dynamic_states(vk::DynamicState::eViewport, vk::DynamicState::eScissor);

    std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    pipeline_builder.dynamic_state.setDynamicStates(dynamic_states);

    pipeline_builder.rendering
    .setDepthAttachmentFormat(depth_format)
    .setColorAttachmentFormats(surface_format.format);

    pipeline_builder.input_assembly
    .setTopology(vk::PrimitiveTopology::eTriangleList)
    .setPrimitiveRestartEnable(false);


    auto[viewport, scissor] = whole_render_area();
    pipeline_builder.viewport
    .setViewports(viewport)
    .setScissors(scissor);

    pipeline_builder.rasterization
    .setDepthClampEnable(false)
    .setRasterizerDiscardEnable(false)
    .setPolygonMode(vk::PolygonMode::eFill)
    .setCullMode(vk::CullModeFlagBits::eNone)
    .setFrontFace(vk::FrontFace::eClockwise)
    .setDepthBiasEnable(false)
    .setLineWidth(1.0f);

    pipeline_builder.multisample
    .setSampleShadingEnable(false)
    .setRasterizationSamples(vk::SampleCountFlagBits::e1)
    .setMinSampleShading(1.0f)
    .setAlphaToCoverageEnable(false)
    .setAlphaToOneEnable(false);

    using enum vk::ColorComponentFlagBits;
    auto color_blend_attachment = vk::PipelineColorBlendAttachmentState{}
    .setColorWriteMask(eR | eG | eB | eA)
    .setBlendEnable(false);

    pipeline_builder.color_blend
    .setLogicOpEnable(false)
    .setLogicOp(vk::LogicOp::eSet)
    .setAttachments(color_blend_attachment);

    pipeline_builder.depth_stencil
    .setDepthTestEnable(true)
    .setDepthWriteEnable(true)
    .setDepthCompareOp(vk::CompareOp::eGreater)
    .setDepthBoundsTestEnable(true)
    .setMinDepthBounds(0.0)
    .setMaxDepthBounds(1.0)
    .setStencilTestEnable(false);

    pipeline_builder.build(material->pipeline, material->pipeline_layout, "particle");
}

void vulkan_engine_t::create_particle_compute_pipeline()
{
    LogVulkan("creating particle compute pipeline");

    pipeline_layout_cache_t::layout_info_t pipeline_layout_info{};
    pipeline_layout_info.set_layouts.emplace_back(particle_setlayout);

    animate_particle_layout = pipeline_builder.layout_cache->create_layout(pipeline_layout_info);
    vk::ShaderModule shader_module = pipeline_builder.shader_cache->create_module("particle.comp");

    auto shader_stage_info = vk::PipelineShaderStageCreateInfo{}
    .setStage(vk::ShaderStageFlagBits::eCompute)
    .setPName("main")
    .setModule(shader_module);

    auto pipeline_info = vk::ComputePipelineCreateInfo{}
    .setStage(shader_stage_info)
    .setLayout(animate_particle_layout);

    auto[result, value] = device.createComputePipeline(pipeline_builder.layout_cache->pipeline_cache, pipeline_info);
    resultcheck = result;
    animate_particle_pipeline = value;

    vkutil::name_object(animate_particle_pipeline, "animate particle pipeline");
    queue_destruction(&animate_particle_pipeline);
}

void vulkan_engine_t::compute_pass(frame_data_t& frame)
{
    vkutil::push_label(frame.cmd, "compute pass");

    auto wait4prev = vk::BufferMemoryBarrier2{}
    .setSize(VK_WHOLE_SIZE)
    .setOffset(0)
    .setBuffer(particle_emitter.instance_buffer.buffer)
    .setSrcStageMask(PipelineStage::eVertexAttributeInput | PipelineStage::eCopy)
    .setSrcAccessMask(AccessFlag::eVertexAttributeRead | AccessFlag::eTransferWrite)
    .setDstStageMask(PipelineStage::eComputeShader)
    .setDstAccessMask(AccessFlag::eShaderWrite | AccessFlag::eShaderRead);

    auto write_buffer_barrier = vk::BufferMemoryBarrier2{}
    .setSize(VK_WHOLE_SIZE)
    .setOffset(0)
    .setBuffer(particle_emitter.instance_buffer.buffer)
    .setSrcStageMask(PipelineStage::eComputeShader)
    .setSrcAccessMask(AccessFlag::eShaderWrite)
    .setDstStageMask(PipelineStage::eVertexAttributeInput)
    .setDstAccessMask(AccessFlag::eVertexAttributeRead);

    auto wait4prev_dependency = vk::DependencyInfo{}
    .setBufferMemoryBarriers(wait4prev);

    auto write_buffer_dependency = vk::DependencyInfo{}
    .setBufferMemoryBarriers(write_buffer_barrier);

    frame.cmd.pipelineBarrier2(wait4prev_dependency);

    std::array descriptor_sets{particle_set};
    std::array set_offsets{uint32_t(pad_uniform_buffer_size(sizeof(particle_control_data)) * frame_index())};

    frame.cmd.bindPipeline(vk::PipelineBindPoint::eCompute, animate_particle_pipeline);
    frame.cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, animate_particle_layout, 0, descriptor_sets, set_offsets);

    uint32_t particle_groupcount = (particle_emitter.instances / 256) + 1;
    frame.cmd.dispatch(particle_groupcount, 1, 1);

    frame.cmd.pipelineBarrier2(write_buffer_dependency);

    vkutil::pop_label(frame.cmd);
}




