#ifndef CHEEMSIT_GUI_VK_VULKAN_DESCRIPTOR_HPP
#define CHEEMSIT_GUI_VK_VULKAN_DESCRIPTOR_HPP

#include "vk-render.hpp"
#include "name.hpp"
#include <vector>
#include <string_view>
#include <unordered_map>

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

class descriptor_allocator_t
{
public:
    void init(vk::Device in_device);
    void shutdown();

    void reset();

    vk::DescriptorPool grab_pool();
    vk::DescriptorSet allocate_set(vk::DescriptorSetLayout layout, uint32_t variable_descriptor_count);

public:

    vk::Device device = nullptr;

    vk::DescriptorPool current_pool = nullptr;

    std::vector<vk::DescriptorPool> used_pools;
    std::vector<vk::DescriptorPool> free_pools;
};

class descriptor_layout_cache_t
{
public:

    struct layout_info_t
    {
        friend bool operator==(const layout_info_t& lhs, const layout_info_t& rhs);

        std::vector<vk::DescriptorSetLayoutBinding> bindings;
        std::vector<vk::DescriptorBindingFlags> flags;
    };

    struct layout_info_hasher
    {
        size_t operator()(const layout_info_t& layout_info) const;
    };

    void init(vk::Device in_device);
    void shutdown();

    vk::DescriptorSetLayout create_layout(layout_info_t& info);

public:

    vk::Device device = nullptr;

    std::unordered_map<layout_info_t, vk::DescriptorSetLayout, layout_info_hasher> layouts;
};

// When you make the VkDescriptorSetLayoutBinding for your variable length array, the `descriptorCount` member is the maximum number of descriptors in that array.
struct descriptor_bind_info
{
    descriptor_bind_info& setBinding(uint32_t binding_) {binding = binding_; return *this;}
    descriptor_bind_info& setCount(uint32_t count_) {count = count_; return *this;}
    descriptor_bind_info& setType(vk::DescriptorType type_) {type = type_; return *this;}
    descriptor_bind_info& setStage(vk::ShaderStageFlags stage_) {stage = stage_; return *this;}
    descriptor_bind_info& setFlags(vk::DescriptorBindingFlags flags_) {flags = flags_; return *this;}

    uint32_t binding = -1;
    uint32_t count = 1;
    vk::DescriptorType type = {};
    vk::ShaderStageFlags stage = {};
    vk::DescriptorBindingFlags flags = {};
};

class descriptor_builder_t
{
public:

    descriptor_builder_t& bind_samplers(descriptor_bind_info bind_info, const vk::Sampler* samplers);
    descriptor_builder_t& bind_buffers(descriptor_bind_info bind_info, const vk::DescriptorBufferInfo* buffer_info); //buffer info is an optonal array of bindinfo.count size to write
    descriptor_builder_t& bind_images(descriptor_bind_info bind_info, const vk::DescriptorImageInfo* image_info);//image info is an optonal array of bindinfo.count size to write

    void build(vk::DescriptorSet& out_set, vk::DescriptorSetLayout& out_layout, std::string debug_name = "");

    void init(descriptor_allocator_t* in_allocator, descriptor_layout_cache_t* in_cache);

public:
    descriptor_allocator_t* allocator = nullptr;
    descriptor_layout_cache_t* cache = nullptr;

    std::vector<vk::WriteDescriptorSet> writes;
    descriptor_layout_cache_t::layout_info_t layout_info;
};


#endif //CHEEMSIT_GUI_VK_VULKAN_DESCRIPTOR_HPP
