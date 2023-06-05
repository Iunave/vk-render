#ifndef CHEEMSIT_GUI_VK_VULKAN_MATERIAL_HPP
#define CHEEMSIT_GUI_VK_VULKAN_MATERIAL_HPP

#include <vulkan/vulkan.hpp>
#include "shader/shader_include.hpp"

namespace shader_stage
{
    enum type
    {
        eVertex                 = 0,
        eTessellationControl    = 1,
        eTessellationEvaluation = 2,
        eGeometry               = 3,
        eFragment               = 4,
        eCompute                = 5,
        eNum
    };
}

struct shader_t
{
    vk::ShaderModule module;
    vk::ShaderStageFlags stage;
    shadersrc_t source;
};

class shader_effect_t
{
public:

    std::array<shader_t, shader_stage::eNum> shaders;
};

#endif //CHEEMSIT_GUI_VK_VULKAN_MATERIAL_HPP
