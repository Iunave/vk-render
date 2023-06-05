#ifndef CHEEMSIT_GUI_VK_VULKAN_PIPELINE_HPP
#define CHEEMSIT_GUI_VK_VULKAN_PIPELINE_HPP

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

#include <unordered_map>
#include <set>

#include "vk-render.hpp"
#include "../shader/shader_include.hpp"
#include "name.hpp"
#include <string>

class pipeline_layout_cache_t
{
public:

    struct layout_info_t
    {
        friend bool operator==(const layout_info_t& lhs, const layout_info_t& rhs);

        std::vector<vk::DescriptorSetLayout> set_layouts;
        std::vector<vk::PushConstantRange> push_constants;
    };

    struct layout_info_hasher
    {
        size_t operator()(const layout_info_t& layout_info) const;
    };

    std::vector<uint8_t> read_pipeline_cache_data();
    void save_pipeline_cache_data();

    void init(vk::Device in_device);
    void shutdown();

    vk::PipelineLayout create_layout(const layout_info_t& layout_info);

public:

    vk::Device device = nullptr;

    std::string pipeline_cache_path{};
    vk::PipelineCache pipeline_cache = nullptr;

    std::unordered_map<layout_info_t, vk::PipelineLayout, layout_info_hasher> layouts;
};

struct shader_module_t
{
    std::string name;
    vk::ShaderModule module;
};

class shader_module_cache_t
{
public:
    shader_module_t* find(std::string shader_name);
    vk::ShaderModule create_module(std::string shader_name);
    bool destroy_module(std::string shader_name);

    void init(vk::Device device_){device = device_;}
    void reset(){shutdown();}
    void shutdown();

    vk::Device device;
    std::vector<shader_module_t> shader_modules;
};

struct vertex_input_t;

class graphics_pipeline_builder_t
{
public:

    graphics_pipeline_builder_t()
    {
        reset();
    }

    template<typename... T>
    void include_shaders(T&&... shaders)
    {
        (include_shader(std::forward<T>(shaders), nullptr, "main"), ...);
    }

    template<typename... T>
    void add_set_layouts(T... layouts)
    {
        (add_set_layout(layouts), ...);
    }

    template<typename... T>
    void set_dynamic_states(T... states)
    {
        (dynamic_states.emplace_back(states), ...); //todo
    }

    graphics_pipeline_builder_t& include_shader(std::string shader_name, vk::SpecializationInfo* specialization = nullptr, std::string_view entrypoint = "main");
    graphics_pipeline_builder_t& add_set_layout(vk::DescriptorSetLayout set_layout);
    graphics_pipeline_builder_t& add_push_constant(vk::PushConstantRange push_constant);
    graphics_pipeline_builder_t& add_push_constant(uint32_t size, uint32_t offset, vk::ShaderStageFlagBits stage);

    graphics_pipeline_builder_t& set_vertex_input(const vertex_input_t* input);

    void build(vk::Pipeline& pipeline, vk::PipelineLayout& layout, std::string debug_name = "");

    void init(pipeline_layout_cache_t* in_layout_cache, shader_module_cache_t* in_shader_cache);
    void reset();

    vk::PipelineDynamicStateCreateInfo dynamic_state;
    vk::PipelineRenderingCreateInfo rendering;
    vk::PipelineVertexInputStateCreateInfo vertex_input;
    vk::PipelineInputAssemblyStateCreateInfo input_assembly;
    vk::PipelineViewportStateCreateInfo viewport;
    vk::PipelineRasterizationStateCreateInfo rasterization;
    vk::PipelineMultisampleStateCreateInfo multisample;
    vk::PipelineColorBlendStateCreateInfo color_blend;
    vk::PipelineDepthStencilStateCreateInfo depth_stencil;

    std::vector<vk::PipelineShaderStageCreateInfo> shader_stages;
    std::vector<vk::DynamicState> dynamic_states;
    pipeline_layout_cache_t::layout_info_t pipeline_layout_info;

    pipeline_layout_cache_t* layout_cache = nullptr;
    shader_module_cache_t* shader_cache = nullptr;
};

#endif //CHEEMSIT_GUI_VK_VULKAN_PIPELINE_HPP
