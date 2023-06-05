//
// Created by user on 5/12/23.
//

#include <string>
#include <fmt/format.h>
#include <fcntl.h>
#include <ctime>
#include <memory>
#include <ratio>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cerrno>
#include <cstring>
#include <vector>
#include <sys/stat.h>
#include <span>

#include "vulkan_pipeline.hpp"
#include "vulkan_utility.hpp"
#include "log.hpp"

#ifndef SHADER_DIRECTORY
#define SHADER_DIRECTORY "../shader/bin/"
#endif

vk::PipelineLayout pipeline_layout_cache_t::create_layout(const layout_info_t& layout_info)
{
    auto found_layout = layouts.find(layout_info);

    if(found_layout == layouts.end())
    {
        LogVulkan("creating cached pipeline layout [{}]", layouts.size());

        auto info = vk::PipelineLayoutCreateInfo{}
        .setSetLayouts(layout_info.set_layouts)
        .setPushConstantRanges(layout_info.push_constants);

        vk::PipelineLayout new_layout = device.createPipelineLayout(info);

        layouts.insert(std::make_pair(layout_info, new_layout));
        vkutil::name_object(new_layout, fmt::format("cached pipeline layout [{}]", layouts.size() - 1));

        return new_layout;
    }
    else
    {
        return found_layout->second;
    }
}

std::vector<uint8_t> pipeline_layout_cache_t::read_pipeline_cache_data()
{
    LogVulkan("reading pipeline cache from {}", pipeline_cache_path);

    syscheck = open(pipeline_cache_path.c_str(), O_CREAT | O_RDONLY, S_IRUSR | S_IWUSR);
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

void pipeline_layout_cache_t::save_pipeline_cache_data()
{
    if(pipeline_cache_path.empty())
    {
        return;
    }

    LogVulkan("saving pipeline cache to {}", pipeline_cache_path);

    std::vector<uint8_t> pipeline_cache_data = device.getPipelineCacheData(pipeline_cache);

    syscheck = open64(pipeline_cache_path.c_str(), O_WRONLY);
    int fd = syscheck;

    size_t nwrite = write(fd, pipeline_cache_data.data(), pipeline_cache_data.size());
    if(nwrite != pipeline_cache_data.size())
    {
        syscall_error("failed to write");
    }

    syscheck = close(fd);
}

void pipeline_layout_cache_t::init(vk::Device in_device)
{
    pipeline_cache_path = "pipeline_cache_data";
    device = in_device;

    std::vector<uint8_t> pipeline_cache_data = read_pipeline_cache_data();

    auto cache_info = vk::PipelineCacheCreateInfo{}
    .setFlags({})
    .setInitialDataSize(pipeline_cache_data.size())
    .setPInitialData(pipeline_cache_data.data());

    pipeline_cache = device.createPipelineCache(cache_info);
    vkutil::name_object(pipeline_cache, "pipeline cache");
}

void pipeline_layout_cache_t::shutdown()
{
    save_pipeline_cache_data();
    device.destroyPipelineCache(pipeline_cache);

    for(auto& pair : layouts)
    {
        device.destroyPipelineLayout(pair.second);
    }
    layouts.clear();
}

template<typename T>
static bool all_exist_in(std::span<T> all, std::span<T> in)
{
    for(const T& lhs : all)
    {
        bool match = false;
        for(const T& rhs : in)
        {
            if(lhs == rhs)
            {
                match = true;
                break;
            }
        }

        if(!match)
        {
            return false;
        }
    }

    return true;
}

bool operator==(const pipeline_layout_cache_t::layout_info_t& lhs, const pipeline_layout_cache_t::layout_info_t& rhs)
{
    if(lhs.set_layouts.size() != rhs.set_layouts.size() || lhs.push_constants.size() != rhs.push_constants.size())
    {
        return false;
    }

    return all_exist_in(std::span{lhs.set_layouts}, std::span{rhs.set_layouts})
        && all_exist_in(std::span{lhs.push_constants}, std::span{rhs.push_constants});
}

size_t pipeline_layout_cache_t::layout_info_hasher::operator()(const pipeline_layout_cache_t::layout_info_t& layout_info) const
{
    size_t hash = layout_info.set_layouts.size() + layout_info.push_constants.size();

    for(vk::DescriptorSetLayout set_layout : layout_info.set_layouts)
    {
        hash ^= __builtin_bit_cast(size_t, set_layout);
    }

    for(vk::PushConstantRange push_range : layout_info.push_constants)
    {
        size_t stage_flags = 0;
        memcpy(&stage_flags, &push_range.stageFlags, sizeof(push_range.stageFlags));

        hash ^= rotleft(stage_flags, 32) | rotleft(size_t(push_range.size), 40) | rotleft(size_t(push_range.offset), 48);
    }

    LogTemp("pipeline layout hashed to {}", hash);
    return hash;
}

vk::ShaderModule shader_module_cache_t::create_module(std::string shader_name)
{
    if(shader_module_t* shader = find(shader_name))
    {
        return shader->module;
    }

    LogVulkan("creating shader module {}", shader_name);

    std::string path = std::string{SHADER_DIRECTORY}.append(shader_name).append(".spv");
    std::vector<uint8_t> code = read_file_binary(path);

    auto create_info = vk::ShaderModuleCreateInfo{}
    .setPCode(reinterpret_cast<const uint32_t*>(code.data()))
    .setCodeSize(code.size());

    vk::ShaderModule new_module = device.createShaderModule(create_info);
    shader_modules.emplace_back(shader_module_t{shader_name, new_module});

    return new_module;
}

bool shader_module_cache_t::destroy_module(std::string shader_name)
{
    if(shader_module_t* shader = find(shader_name))
    {
        LogVulkan("destroying shader module {}", shader_name);

        device.destroyShaderModule(shader->module);

        *shader = std::move(shader_modules.back());
        shader_modules.pop_back();

        return true;
    }
    return false;
}

shader_module_t* shader_module_cache_t::find(std::string shader_name)
{
    for(shader_module_t& shader : shader_modules)
    {
        if(shader.name == shader_name)
        {
            return &shader;
        }
    }
    return nullptr;
}

void shader_module_cache_t::shutdown()
{
    for(const shader_module_t& shader : shader_modules)
    {
        device.destroyShaderModule(shader.module);
    }
    shader_modules.clear();
}

graphics_pipeline_builder_t& graphics_pipeline_builder_t::include_shader(std::string shader_name, vk::SpecializationInfo* specialization, std::string_view entrypoint)
{
    vk::ShaderModule module = shader_cache->create_module(shader_name);

    size_t extension_pos = shader_name.find_last_of('.');
    assert(extension_pos != std::string::npos);

    vk::ShaderStageFlagBits shader_stage;

    if(strcmp(shader_name.data() + extension_pos, ".vert") == 0)
    {
        shader_stage = vk::ShaderStageFlagBits::eVertex;
    }
    else if(strcmp(shader_name.data() + extension_pos, ".geom") == 0)
    {
        shader_stage = vk::ShaderStageFlagBits::eGeometry;
    }
    else if(strcmp(shader_name.data() + extension_pos, ".frag") == 0)
    {
        shader_stage = vk::ShaderStageFlagBits::eFragment;
    }
    else if(strcmp(shader_name.data() + extension_pos, ".comp") == 0)
    {
        shader_stage = vk::ShaderStageFlagBits::eCompute;
    }

    shader_stages.emplace_back()
    .setStage(shader_stage)
    .setPName(entrypoint.data())
    .setModule(module)
    .setPSpecializationInfo(specialization);

    return *this;
}

graphics_pipeline_builder_t& graphics_pipeline_builder_t::add_set_layout(vk::DescriptorSetLayout set_layout)
{
    pipeline_layout_info.set_layouts.emplace_back(set_layout);
    return *this;
}

graphics_pipeline_builder_t& graphics_pipeline_builder_t::add_push_constant(vk::PushConstantRange push_constant)
{
    pipeline_layout_info.push_constants.emplace_back(push_constant);
    return *this;
}

graphics_pipeline_builder_t& graphics_pipeline_builder_t::add_push_constant(uint32_t size, uint32_t offset, vk::ShaderStageFlagBits stage)
{
    pipeline_layout_info.push_constants.emplace_back()
    .setSize(size)
    .setOffset(offset)
    .setStageFlags(stage);

    return *this;
}

graphics_pipeline_builder_t& graphics_pipeline_builder_t::set_vertex_input(const vertex_input_t* input)
{
    vertex_input
    .setFlags(input->flags)
    .setVertexBindingDescriptions(input->bindings)
    .setVertexAttributeDescriptions(input->attributes);

    return *this;
}

void graphics_pipeline_builder_t::build(vk::Pipeline& pipeline, vk::PipelineLayout& layout, std::string debug_name)
{
    LogVulkan("building {} graphics pipeline", debug_name.empty() ? "unknown" : debug_name);

    layout = layout_cache->create_layout(pipeline_layout_info);

    auto create_info = vk::GraphicsPipelineCreateInfo{}
    .setPNext(&rendering)
    .setLayout(layout)
    .setStages(shader_stages)
    .setPDynamicState(&dynamic_state)
    .setPVertexInputState(&vertex_input)
    .setPInputAssemblyState(&input_assembly)
    .setPViewportState(&viewport)
    .setPRasterizationState(&rasterization)
    .setPMultisampleState(&multisample)
    .setPColorBlendState(&color_blend)
    .setPDepthStencilState(&depth_stencil);

    vk::ResultValue<vk::Pipeline> result_value = layout_cache->device.createGraphicsPipeline(layout_cache->pipeline_cache, create_info);
    resultcheck = result_value.result;
    pipeline = result_value.value;

    if(!debug_name.empty())
    {
        vkutil::name_object(pipeline, debug_name);
    }

    reset();
}

void graphics_pipeline_builder_t::init(pipeline_layout_cache_t* in_layout_cache, shader_module_cache_t* in_shader_cache)
{
    layout_cache = in_layout_cache;
    shader_cache = in_shader_cache;
}

void graphics_pipeline_builder_t::reset()
{
    shader_stages.clear();
    pipeline_layout_info.set_layouts.clear();
    pipeline_layout_info.push_constants.clear();

    dynamic_state = vk::PipelineDynamicStateCreateInfo{};
    rendering = vk::PipelineRenderingCreateInfo{};
    vertex_input = vk::PipelineVertexInputStateCreateInfo{};
    input_assembly = vk::PipelineInputAssemblyStateCreateInfo{};
    viewport = vk::PipelineViewportStateCreateInfo{};
    rasterization = vk::PipelineRasterizationStateCreateInfo{};
    multisample = vk::PipelineMultisampleStateCreateInfo{};
    color_blend = vk::PipelineColorBlendStateCreateInfo{};
    depth_stencil = vk::PipelineDepthStencilStateCreateInfo{};
}
