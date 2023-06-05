#ifndef CHEEMSIT_GUI_VK_VULKAN_UTILITY_HPP
#define CHEEMSIT_GUI_VK_VULKAN_UTILITY_HPP

#include <cstdint>
#include <string_view>
#include <string>
#include <fmt/format.h>
#include "name.hpp"
#include "vk-render.hpp"
#include "vector_types.hpp"
#include "vulkan_memory_allocator.hpp"

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

class vulkan_engine_t;

using PipelineStage = vk::PipelineStageFlagBits2;
using AccessFlag = vk::AccessFlagBits2;

class result_checker_t
{
public:
    result_checker_t& operator=(vk::Result val);
    result_checker_t& operator=(VkResult val);

    operator vk::Result() const {return Value;}

    vk::Result Value = vk::Result::eSuccess;
};
inline constinit thread_local result_checker_t resultcheck{};

struct allocated_buffer_t
{
    void* map();
    void unmap();

    vk::Buffer buffer;
    vma::Allocation allocation;
    vma::AllocationInfo info;
};

struct allocated_image_t
{
    vk::Image image;
    vk::ImageView view;
    vma::Allocation allocation;
    vma::AllocationInfo info;
};

struct cube_image_t
{
    vk::Image image;
    vk::ImageView view;
    vma::Allocation allocation;
    vma::AllocationInfo info;
    vk::ImageView face_views[6];
};

struct texture_image_t
{
    vk::Image image;
    vk::ImageView view;
    vma::Allocation allocation;
    vma::AllocationInfo info;
    uint32_t mip_levels;
};

struct material_t
{
    material_t(std::string_view in_name)
    {
        name = in_name;
    }

    name_t name;

    vk::PipelineLayout pipeline_layout = nullptr;
    vk::Pipeline pipeline = nullptr;
};

struct texture_t
{
    texture_t(std::string_view in_name)
    {
        name = in_name;
    }

    void destroy();

    void load_from_file(std::string filename);

    name_t name;
    texture_image_t image;

    vk::DescriptorSetLayout set_layout = nullptr;
    vk::DescriptorSet set = nullptr;
};

struct vertex_input_t
{
    std::vector<vk::VertexInputBindingDescription> bindings;
    std::vector<vk::VertexInputAttributeDescription> attributes;
    vk::PipelineVertexInputStateCreateFlagBits flags = {};
};

struct vertex_t
{
    static const vertex_input_t position_normal_uv_instance_input;
    static const vertex_input_t position_normal_uv_input;
    static const vertex_input_t position_input;

    glm::vec3 position;
    glm::vec<2, int16_t> normal; //oct encoded
    glm::vec<2, uint16_t> uv;
};

struct mesh_t
{
    std::vector<vertex_t> vertices;
    std::vector<uint32_t> indices;
};

struct model_t
{
    model_t(std::string_view in_name)
    {
        name = in_name;
    }

    void load_from_file(std::string filename);

    void bind_positions(vk::CommandBuffer cmd) const;
    void bind_positions_normal_uv(vk::CommandBuffer cmd) const;

    name_t name;
    mesh_t mesh;

    allocated_buffer_t vertex_buffer;
    allocated_buffer_t index_buffer;
};

struct instance_data_t
{
    glm::vec3 position;
    glm::vec3 scale;
};

struct particle_emitter_t
{
    void allocate_instance_buffer(uint64_t instances_);

    name_t name;
    model_t* model;
    uint32_t instances;
    allocated_buffer_t instance_buffer;
};

namespace vkutil
{
    void name_object(vk::DebugUtilsObjectNameInfoEXT info);
    void name_allocation(vma::Allocation object, std::string name);

    template<typename objT, typename... varargs>
    inline void name_object(objT object, std::string fmt, varargs&&... strarg)
    {
#ifndef NDEBUG
        if constexpr(sizeof...(strarg) != 0)
        {
            fmt = fmt::format(fmt::runtime(fmt), std::forward<varargs>(strarg)...);
        }

        if constexpr(std::is_same_v<objT, vma::Allocation>)
        {
            name_allocation(object, fmt);
        }
        else
        {
            vk::DebugUtilsObjectNameInfoEXT name_info{};
            name_info.objectHandle = std::bit_cast<uint64_t>(object);
            name_info.objectType = objT::objectType;
            name_info.pObjectName = fmt.c_str();

            name_object(name_info);
        }
#endif
    }

    inline static constexpr std::array<float, 4> default_label_color{0, 0, 0, 1};

    void push_label(vk::Queue queue, std::string label_name, std::array<float, 4> color = default_label_color);
    void insert_label(vk::Queue queue, std::string label_name, std::array<float, 4> color = default_label_color);
    void pop_label(vk::Queue queue);

    void push_label(vk::CommandBuffer cmd, std::string label_name, std::array<float, 4> color = default_label_color);
    void insert_label(vk::CommandBuffer cmd, std::string label_name, std::array<float, 4> color = default_label_color);
    void pop_label(vk::CommandBuffer cmd);

    mesh_t load_model_file(std::string filename);
    texture_image_t load_image_texture(std::string filename);
    void load_image2buffer(std::string filename, allocated_buffer_t& out_buffer, vk::Extent3D& out_image_extent);
}


#endif //CHEEMSIT_GUI_VK_VULKAN_UTILITY_HPP
