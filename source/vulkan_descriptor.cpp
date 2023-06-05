#include "vulkan_descriptor.hpp"
#include <array>
#include <fmt/format.h>
#include "vulkan_utility.hpp"
#include "vulkan_pipeline.hpp"
#include "log.hpp"

using enum vk::DescriptorType;
constexpr std::array descriptor_pool_sizes //configure this
{
    vk::DescriptorPoolSize{eUniformBuffer, 1000},
    vk::DescriptorPoolSize{eUniformBufferDynamic, 1000},
    vk::DescriptorPoolSize{eStorageBuffer, 1000},
    vk::DescriptorPoolSize{eStorageBufferDynamic, 1000},
    vk::DescriptorPoolSize{eCombinedImageSampler, 1000},
    vk::DescriptorPoolSize{eSampler, 1000},
    vk::DescriptorPoolSize{eSampledImage, 1000}
};


namespace descriptor_tracker
{
    constexpr std::array<vk::DescriptorType, 17> types
    {
        vk::DescriptorType::eSampler,
        vk::DescriptorType::eCombinedImageSampler,
        vk::DescriptorType::eSampledImage,
        vk::DescriptorType::eStorageImage,
        vk::DescriptorType::eUniformTexelBuffer,
        vk::DescriptorType::eStorageTexelBuffer,
        vk::DescriptorType::eUniformBuffer,
        vk::DescriptorType::eStorageBuffer,
        vk::DescriptorType::eUniformBufferDynamic,
        vk::DescriptorType::eStorageBufferDynamic,
        vk::DescriptorType::eInputAttachment,
        vk::DescriptorType::eInlineUniformBlock,
        vk::DescriptorType::eAccelerationStructureKHR,
        vk::DescriptorType::eAccelerationStructureNV,
        vk::DescriptorType::eMutableVALVE,
        vk::DescriptorType::eSampleWeightImageQCOM,
        vk::DescriptorType::eBlockMatchImageQCOM
    };

    std::array<uint64_t, 17> allocated_descriptors{};
    uint64_t pool_count = 0;

    uint64_t& allocated(vk::DescriptorType type)
    {
        switch(type)
        {
            case vk::DescriptorType::eSampler: return allocated_descriptors[0];
            case vk::DescriptorType::eCombinedImageSampler: return allocated_descriptors[1];
            case vk::DescriptorType::eSampledImage: return allocated_descriptors[2];
            case vk::DescriptorType::eStorageImage: return allocated_descriptors[3];
            case vk::DescriptorType::eUniformTexelBuffer: return allocated_descriptors[4];
            case vk::DescriptorType::eStorageTexelBuffer: return allocated_descriptors[5];
            case vk::DescriptorType::eUniformBuffer: return allocated_descriptors[6];
            case vk::DescriptorType::eStorageBuffer: return allocated_descriptors[7];
            case vk::DescriptorType::eUniformBufferDynamic: return allocated_descriptors[8];
            case vk::DescriptorType::eStorageBufferDynamic: return allocated_descriptors[9];
            case vk::DescriptorType::eInputAttachment: return allocated_descriptors[10];
            case vk::DescriptorType::eInlineUniformBlock: return allocated_descriptors[11];
            case vk::DescriptorType::eAccelerationStructureKHR: return allocated_descriptors[12];
            case vk::DescriptorType::eAccelerationStructureNV: return allocated_descriptors[13];
            case vk::DescriptorType::eMutableVALVE: return allocated_descriptors[14];
            case vk::DescriptorType::eSampleWeightImageQCOM: return allocated_descriptors[15];
            case vk::DescriptorType::eBlockMatchImageQCOM: return allocated_descriptors[16];
        }
    }

    uint64_t reserved(vk::DescriptorType type)
    {
        for(vk::DescriptorPoolSize pool_size : descriptor_pool_sizes)
        {
            if(pool_size.type == type)
            {
                return pool_size.descriptorCount * pool_count;
            }
        }
        return 0;
    }

    void track(vk::DescriptorType type, uint64_t count)
    {
        allocated(type) += count;
    }

    void untrack(vk::DescriptorType type, uint64_t count)
    {
        allocated(type) -= count;
    }

    void report_usage(vk::DescriptorType type)
    {
        uint64_t reserved_descriptors = reserved(type);
        uint64_t used_descriptors = allocated(type);
        double part = reserved_descriptors != 0 ? (double(used_descriptors) / double(reserved_descriptors)) : 1.0;

        LogTemp("{}: {} / {} ({})", vk::to_string(type), used_descriptors, reserved_descriptors, part);
    }

    void report_usage()
    {
        LogTemp("");
        LogTemp("descriptor usage statistics");

        for(vk::DescriptorType type : types)
        {
            report_usage(type);
        }

        LogTemp("");
    }

    void reset()
    {
        memset(allocated_descriptors.data(), 0, allocated_descriptors.size() * sizeof(uint64_t));
        pool_count = 0;
    }
};

void descriptor_allocator_t::init(vk::Device in_device)
{
    device = in_device;
}

void descriptor_allocator_t::shutdown()
{
    descriptor_tracker::report_usage();
    descriptor_tracker::reset();

    for(vk::DescriptorPool pool : used_pools)
    {
        device.destroyDescriptorPool(pool);
    }
    used_pools.clear();

    for(vk::DescriptorPool pool : free_pools)
    {
        device.destroyDescriptorPool(pool);
    }
    free_pools.clear();
}

vk::DescriptorPool descriptor_allocator_t::grab_pool()
{
    vk::DescriptorPool pool;

    if(!free_pools.empty())
    {
        pool = free_pools.back();
        free_pools.pop_back();
    }
    else
    {
        LogVulkan("allocating new descriptor pool");

        auto pool_info = vk::DescriptorPoolCreateInfo{}
        .setFlags({})
        .setPoolSizes(descriptor_pool_sizes)
        .setMaxSets(1000);

        pool = device.createDescriptorPool(pool_info);
    }

    vkutil::name_object(pool, fmt::format("descriptor pool in use [{}]", used_pools.size()));
    descriptor_tracker::pool_count += 1;

    return pool;
}

vk::DescriptorSet descriptor_allocator_t::allocate_set(vk::DescriptorSetLayout layout, uint32_t variable_descriptor_count)
{
    vk::DescriptorSet set = nullptr;

    if(!current_pool)
    {
        current_pool = grab_pool();
        used_pools.push_back(current_pool);
    }

    auto variable_count = vk::DescriptorSetVariableDescriptorCountAllocateInfo{}
    .setDescriptorSetCount(1)
    .setDescriptorCounts(variable_descriptor_count);

    auto alloc_info = vk::DescriptorSetAllocateInfo{}
    .setPNext(&variable_count)
    .setDescriptorPool(current_pool)
    .setDescriptorSetCount(1)
    .setSetLayouts(layout);

    vk::Result alloc_result = device.allocateDescriptorSets(&alloc_info, &set);

    if(alloc_result == vk::Result::eErrorFragmentedPool || alloc_result == vk::Result::eErrorOutOfPoolMemory)
    {
        current_pool = grab_pool();
        used_pools.push_back(current_pool);

        alloc_info.setDescriptorPool(current_pool);

        resultcheck = device.allocateDescriptorSets(&alloc_info, &set);
    }
    else
    {
        resultcheck = alloc_result;
    }

    return set;
}

void descriptor_allocator_t::reset()
{
    LogVulkan("resetting descriptor pools");

    descriptor_tracker::report_usage();
    descriptor_tracker::reset();

    for(uint64_t index = 0; index < used_pools.size(); ++index)
    {
        device.resetDescriptorPool(used_pools[index]);
        free_pools.emplace_back(used_pools[index]);

        vkutil::name_object(used_pools[index], fmt::format("free descriptor pool [{}]", free_pools.size() - 1));
    }

    used_pools.clear();
    current_pool = nullptr;
}

void descriptor_layout_cache_t::init(vk::Device in_device)
{
    device = in_device;
}

void descriptor_layout_cache_t::shutdown()
{
    for(auto& pair : layouts)
    {
        device.destroyDescriptorSetLayout(pair.second);
    }
    layouts.clear();
}

vk::DescriptorSetLayout descriptor_layout_cache_t::create_layout(layout_info_t& info)
{
    assert(info.bindings.size() == info.flags.size());

    auto* ascending_bindings = TYPE_ALLOCA(vk::DescriptorSetLayoutBinding, info.bindings.size());
    auto* ascending_flags = TYPE_ALLOCA(vk::DescriptorBindingFlags, info.flags.size());
    
    for(size_t index = 0; index < info.bindings.size(); ++index) //inserts the bindings in ascending order
    {
        uint32_t binding = info.bindings[index].binding;
        assert(binding < info.bindings.size());
        
        ascending_bindings[binding] = info.bindings[index];
        ascending_flags[binding] = info.flags[index];
    }

    memcpy(info.bindings.data(), ascending_bindings, sizeof(vk::DescriptorSetLayoutBinding) * info.bindings.size());
    memcpy(info.flags.data(), ascending_flags, sizeof(vk::DescriptorBindingFlags) * info.flags.size());

    auto found_layout = layouts.find(info);
    if(found_layout == layouts.end())
    {
        LogVulkan("creating cached descriptor layout [{}]", layouts.size());

        auto binding_flags = vk::DescriptorSetLayoutBindingFlagsCreateInfo{}
        .setPBindingFlags(ascending_flags)
        .setBindingCount(info.flags.size());

        auto layout_info = vk::DescriptorSetLayoutCreateInfo{}
        .setPNext(&binding_flags)
        .setFlags({})
        .setPBindings(ascending_bindings)
        .setBindingCount(info.bindings.size());

        vk::DescriptorSetLayout new_layout = device.createDescriptorSetLayout(layout_info);

        layouts.insert(std::make_pair(info, new_layout));
        vkutil::name_object(new_layout, fmt::format("cached descriptor layout [{}]", layouts.size() - 1));

        return new_layout;
    }
    else
    {
        return found_layout->second;
    }
}

bool operator==(const descriptor_layout_cache_t::layout_info_t& lhs, const descriptor_layout_cache_t::layout_info_t& rhs)
{
    if(lhs.bindings.size() != rhs.bindings.size())
    {
        return false;
    }

    for(uint64_t binding = 0; binding < lhs.bindings.size(); ++binding) //bindings are ASSUMED sorted by ascending binding so they match
    {
        if(lhs.bindings[binding].descriptorType != rhs.bindings[binding].descriptorType
        || lhs.bindings[binding].descriptorCount != rhs.bindings[binding].descriptorCount
        || lhs.bindings[binding].stageFlags != rhs.bindings[binding].stageFlags
        || lhs.flags[binding] != rhs.flags[binding])
        {
            return false;
        }
    }

    return true;
}

size_t descriptor_layout_cache_t::layout_info_hasher::operator()(const layout_info_t& layout_info) const
{
    size_t hash = layout_info.bindings.size() + layout_info.flags.size();

    for(uint64_t index = 0; index < layout_info.bindings.size(); ++index)
    {
        vk::DescriptorSetLayoutBinding binding = layout_info.bindings[index];
        vk::DescriptorBindingFlags binding_flagbits = layout_info.flags[index];

        size_t stage_flags = 0;
        memcpy(&stage_flags, &binding.stageFlags, sizeof(vk::ShaderStageFlags));

        size_t binding_flags = 0;
        memcpy(&binding_flags, &binding_flagbits, sizeof(vk::DescriptorBindingFlags));

        hash ^= rotleft(size_t(binding.binding), 8)
        | rotleft(size_t(binding.descriptorType), 16)
        | rotleft(size_t(binding.descriptorCount), 24)
        | rotleft(stage_flags, 32)
        | rotleft(binding_flags, 40);
    }

    LogTemp("descriptor layout hashed to {}", hash);
    return hash;
}

descriptor_builder_t& descriptor_builder_t::bind_samplers(descriptor_bind_info bind_info, const vk::Sampler* samplers)
{
#ifndef NDEBUG
    for(const vk::DescriptorSetLayoutBinding binding : layout_info.bindings)
    {
        assert(binding.binding != bind_info.binding && "duplicate bindings found");
    }
#endif

    layout_info.bindings.emplace_back()
    .setBinding(bind_info.binding)
    .setDescriptorCount(bind_info.count)
    .setDescriptorType(bind_info.type)
    .setStageFlags(bind_info.stage)
    .setPImmutableSamplers(samplers);

    layout_info.flags.emplace_back(bind_info.flags);

    return *this;
}

descriptor_builder_t& descriptor_builder_t::bind_buffers(descriptor_bind_info bind_info, const vk::DescriptorBufferInfo* buffer_info)
{
#ifndef NDEBUG
    for(const vk::DescriptorSetLayoutBinding binding : layout_info.bindings)
    {
        assert(binding.binding != bind_info.binding && "duplicate bindings found");
    }
#endif

    layout_info.bindings.emplace_back()
    .setBinding(bind_info.binding)
    .setDescriptorCount(bind_info.count)
    .setDescriptorType(bind_info.type)
    .setStageFlags(bind_info.stage);

    layout_info.flags.emplace_back(bind_info.flags);

    if(buffer_info)
    {
        writes.emplace_back()
        .setDescriptorCount(bind_info.count)
        .setDescriptorType(bind_info.type)
        .setDstBinding(bind_info.binding)
        .setDstArrayElement(0)
        .setPBufferInfo(buffer_info);
    }

    return *this;
}

descriptor_builder_t& descriptor_builder_t::bind_images(descriptor_bind_info bind_info, const vk::DescriptorImageInfo* image_info)
{
#ifndef NDEBUG
    for(const vk::DescriptorSetLayoutBinding binding : layout_info.bindings)
    {
        assert(binding.binding != bind_info.binding && "duplicate bindings found");
    }
#endif

    layout_info.bindings.emplace_back()
    .setBinding(bind_info.binding)
    .setDescriptorCount(bind_info.count)
    .setDescriptorType(bind_info.type)
    .setStageFlags(bind_info.stage);

    layout_info.flags.emplace_back(bind_info.flags);

    if(image_info)
    {
        writes.emplace_back()
        .setDescriptorCount(bind_info.count)
        .setDescriptorType(bind_info.type)
        .setDstBinding(bind_info.binding)
        .setDstArrayElement(0)
        .setPImageInfo(image_info);
    }

    return *this;
}

void descriptor_builder_t::init(descriptor_allocator_t* in_allocator, descriptor_layout_cache_t* in_cache)
{
    allocator = in_allocator;
    cache = in_cache;
}

void descriptor_builder_t::build(vk::DescriptorSet& out_set, vk::DescriptorSetLayout& out_layout, std::string debug_name)
{
    LogVulkan("building {} descriptor", debug_name.empty() ? "unspecified" : debug_name);

    out_layout = cache->create_layout(layout_info);
    out_set = allocator->allocate_set(out_layout, layout_info.bindings.back().descriptorCount);

    if(!debug_name.empty())
    {
        vkutil::name_object(out_set, fmt::format("{} descriptor set", debug_name));
    }

    for(const vk::DescriptorSetLayoutBinding& binding : layout_info.bindings)
    {
        descriptor_tracker::track(binding.descriptorType, binding.descriptorCount);
    }

    if(!writes.empty())
    {
        for(vk::WriteDescriptorSet& write : writes)
        {
            write.setDstSet(out_set);
        }

        allocator->device.updateDescriptorSets(writes, {});
    }

    writes.clear();
    layout_info.bindings.clear();
    layout_info.flags.clear();
}
