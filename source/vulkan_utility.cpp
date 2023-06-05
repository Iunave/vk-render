#include "vulkan_utility.hpp"
#include "stb_image.h"
#include <fmt/format.h>
#include "object_loader.hpp"
#include "vulkan_engine.hpp"
#include "log.hpp"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include "math.hpp"

constexpr std::string_view image_dir = "../assets/image/";
constexpr std::string_view model_dir = "../assets/model/";

result_checker_t& result_checker_t::operator=(vk::Result val)
{
    Value = val;

#ifndef NDEBUG
    if(Value != vk::Result::eSuccess)
    {
        LogVulkan("resultcheck failed. vkResult = {}", vk::to_string(val));
        abort();
    }
#endif

    return *this;
}

result_checker_t& result_checker_t::operator=(VkResult val)
{
    return *this = static_cast<vk::Result>(val);
}

const vertex_input_t vertex_t::position_normal_uv_instance_input = []()
{
    vertex_input_t description{};

    description.bindings.emplace_back()
    .setBinding(0)
    .setStride(sizeof(position))
    .setInputRate(vk::VertexInputRate::eVertex);

    description.bindings.emplace_back()
    .setBinding(1)
    .setStride(sizeof(normal) + sizeof(uv))
    .setInputRate(vk::VertexInputRate::eVertex);

    description.bindings.emplace_back()
    .setBinding(2)
    .setStride(sizeof(instance_data_t))
    .setInputRate(vk::VertexInputRate::eInstance);

    description.attributes.emplace_back()
    .setBinding(0)
    .setOffset(0)
    .setLocation(0)
    .setFormat(vk::Format::eR32G32B32Sfloat);

    description.attributes.emplace_back()
    .setBinding(1)
    .setOffset(0)
    .setLocation(1)
    .setFormat(vk::Format::eR16G16Snorm);

    description.attributes.emplace_back()
    .setBinding(1)
    .setOffset(sizeof(normal))
    .setLocation(2)
    .setFormat(vk::Format::eR16G16Unorm);

    description.attributes.emplace_back()
    .setBinding(2)
    .setOffset(0)
    .setLocation(3)
    .setFormat(vk::Format::eR32G32B32Sfloat); //position

    description.attributes.emplace_back()
    .setBinding(2)
    .setOffset(sizeof(glm::vec3))
    .setLocation(4)
    .setFormat(vk::Format::eR32G32B32Sfloat); //scale

    return description;
}();

const vertex_input_t vertex_t::position_normal_uv_input = []()
{
    vertex_input_t description{};

    description.bindings.emplace_back()
    .setBinding(0)
    .setStride(sizeof(position))
    .setInputRate(vk::VertexInputRate::eVertex);

    description.bindings.emplace_back()
    .setBinding(1)
    .setStride(sizeof(normal) + sizeof(uv))
    .setInputRate(vk::VertexInputRate::eVertex);

    description.attributes.emplace_back()
    .setBinding(0)
    .setOffset(0)
    .setLocation(0)
    .setFormat(vk::Format::eR32G32B32Sfloat);

    description.attributes.emplace_back()
    .setBinding(1)
    .setOffset(0)
    .setLocation(1)
    .setFormat(vk::Format::eR16G16Snorm);

    description.attributes.emplace_back()
    .setBinding(1)
    .setOffset(sizeof(normal))
    .setLocation(2)
    .setFormat(vk::Format::eR16G16Unorm);

    return description;
}();

const vertex_input_t vertex_t::position_input = []()
{
    vertex_input_t description{};

    description.bindings.emplace_back()
    .setBinding(0)
    .setStride(sizeof(position))
    .setInputRate(vk::VertexInputRate::eVertex);

    description.attributes.emplace_back()
    .setBinding(0)
    .setOffset(0)
    .setLocation(0)
    .setFormat(vk::Format::eR32G32B32Sfloat);

    return description;
}();

void* allocated_buffer_t::map()
{
    return gVulkan->allocator.mapMemory(allocation);
}

void allocated_buffer_t::unmap()
{
    gVulkan->allocator.unmapMemory(allocation);
}

static const char* bool_to_string(bool b)
{
    return b ? "yes" : "no";
}

mesh_t vkutil::load_model_file(std::string filename)
{
    LogFileLoader("loading {}", filename);

    std::vector<uint8_t> memory = read_file_binary(std::string(model_dir).append(filename));

    Assimp::Importer importer{};

    int import_flags = aiProcess_Triangulate | aiProcess_OptimizeMeshes | aiProcess_ImproveCacheLocality | aiProcess_JoinIdenticalVertices | aiProcess_FlipUVs | aiProcess_GenNormals;
    const aiScene* scene = importer.ReadFileFromMemory(memory.data(), memory.size(), import_flags);

    if(scene == nullptr || scene->mRootNode == nullptr || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)
    {
        LogFileLoader("failed to import {}\n{}", filename, importer.GetErrorString());
        abort();
    }

    assert(scene->mNumMeshes == 1);
    aiMesh* mesh = scene->mMeshes[0];

    LogFileLoader("{} has |normals ? {}| |color ? {}| |texture ? {}|", filename, bool_to_string(mesh->HasNormals()), bool_to_string(mesh->HasVertexColors(0)), bool_to_string(mesh->HasTextureCoords(0)));

    std::vector<vertex_t> vertices(mesh->mNumVertices);

    for(size_t index = 0; index < mesh->mNumVertices; ++index)
    {
        vertices[index].position = std::bit_cast<glm::vec3>(mesh->mVertices[index]);

        if(mesh->HasNormals())
        {
            glm::vec2 oct_encoded = math::oct_encode(std::bit_cast<glm::vec3>(mesh->mNormals[index]));

            vertices[index].normal.x = math::pack_norm<int16_t>(oct_encoded.x);
            vertices[index].normal.y = math::pack_norm<int16_t>(oct_encoded.y);
        }
        else
        {
            vertices[index].normal = {0, 0};
        }
/*
        if(mesh->HasVertexColors(0))
        {
            aiColor4D color = mesh->mColors[0][index];

            vertices[index].color.r = math::pack_norm<uint16_t>(color.r);
            vertices[index].color.g = math::pack_norm<uint16_t>(color.g);
            vertices[index].color.b = math::pack_norm<uint16_t>(color.b);
        }
        else
        {
            vertices[index].color = {0,0,0};
        }
*/
        if(mesh->HasTextureCoords(0))
        {
            aiVector3D texcoord = mesh->mTextureCoords[0][index];

            vertices[index].uv.x = math::pack_norm<uint16_t>(texcoord.x);
            vertices[index].uv.y = math::pack_norm<uint16_t>(texcoord.y);
        }
        else
        {
            vertices[index].uv = {0, 0};
        }
    }

    std::vector<uint32_t> indices; //always triangles

    for(size_t index = 0; index < mesh->mNumFaces; ++index)
    {
        aiFace& face = mesh->mFaces[index];
        assert(face.mNumIndices == 3); //otherwise not triangulated

        indices.push_back(face.mIndices[0]);
        indices.push_back(face.mIndices[1]);
        indices.push_back(face.mIndices[2]);
    }

    return mesh_t{vertices, indices};
}

void model_t::load_from_file(std::string filename)
{
    mesh = vkutil::load_model_file(filename);

    size_t vertex_buffer_size = mesh.vertices.size() * sizeof(vertex_t);
    size_t indices_size = mesh.indices.size() * sizeof(uint32_t);

    allocated_buffer_t vertex_staging_buffer = gVulkan->allocate_staging_buffer(vertex_buffer_size, fmt::format("{} vertices", name));
    allocated_buffer_t index_staging_buffer = gVulkan->allocate_staging_buffer(indices_size, fmt::format("{} indices", name));

    uint8_t* vertex_data = (uint8_t*)gVulkan->allocator.mapMemory(vertex_staging_buffer.allocation);

    for(uint64_t index = 0; index < mesh.vertices.size(); ++index)
    {
        reinterpret_cast<decltype(vertex_t::position)*>(vertex_data)[index] = mesh.vertices[index].position;
    }

    vertex_data += mesh.vertices.size() * sizeof(vertex_t::position);

    for(uint64_t index = 0; index < mesh.vertices.size(); ++index)
    {
        *reinterpret_cast<decltype(vertex_t::normal)*>(vertex_data) = mesh.vertices[index].normal;
        vertex_data += sizeof(vertex_t::normal);

        *reinterpret_cast<decltype(vertex_t::uv)*>(vertex_data) = mesh.vertices[index].uv;
        vertex_data += sizeof(vertex_t::uv);
    }

    gVulkan->allocator.unmapMemory(vertex_staging_buffer.allocation);

    void* index_data = gVulkan->allocator.mapMemory(index_staging_buffer.allocation);
    memcpy(index_data, mesh.indices.data(), indices_size);
    gVulkan->allocator.unmapMemory(index_staging_buffer.allocation);

    vertex_buffer = gVulkan->allocate_vertex_buffer(vertex_buffer_size, filename);
    index_buffer = gVulkan->allocate_index_buffer(indices_size, filename);

    gVulkan->copy_vertex_attribute_buffer(vertex_buffer, vertex_staging_buffer, vertex_buffer_size);
    gVulkan->copy_index_buffer(index_buffer, index_staging_buffer, indices_size);
}

void model_t::bind_positions(vk::CommandBuffer cmd) const
{
    cmd.bindVertexBuffers(0, {vertex_buffer.buffer}, {0});
    cmd.bindIndexBuffer(index_buffer.buffer, 0, vk::IndexType::eUint32);
}

void model_t::bind_positions_normal_uv(vk::CommandBuffer cmd) const
{
    cmd.bindVertexBuffers(0, {vertex_buffer.buffer, vertex_buffer.buffer}, {0, mesh.vertices.size() * sizeof(vertex_t::position)});
    cmd.bindIndexBuffer(index_buffer.buffer, 0, vk::IndexType::eUint32);
}

void particle_emitter_t::allocate_instance_buffer(uint64_t instances_)
{
    instances = instances_;
    uint64_t buffer_size = instances * sizeof(instance_data_t);

    instance_buffer = gVulkan->allocate_instance_buffer(buffer_size, fmt::format("{} instance", name.str()));
    allocated_buffer_t instance_staging_buffer = gVulkan->allocate_staging_buffer(buffer_size, fmt::format("{} instance", name.str()));

    instance_data_t* data = (instance_data_t*)instance_staging_buffer.map();

    for(uint64_t index = 0; index < instances; ++index)
    {
        obscuring:
        data[index].position = math::rand_pos(-10000.0, 10000.0);

        if((data[index].position.x < 300.0 && data[index].position.x > -300.0)
        && (data[index].position.y < 300.0 && data[index].position.y > -300.0)
        && (data[index].position.z < 300.0 && data[index].position.z > -300.0))
        {
            goto obscuring;
        }

        data[index].scale = glm::vec3{static_cast<float>(math::randrange(1.0, 10.0))};
    }

    instance_staging_buffer.unmap();

    gVulkan->copy_vertex_attribute_buffer(instance_buffer, instance_staging_buffer, buffer_size);
}

texture_image_t vkutil::load_image_texture(std::string filename)
{
    LogFileLoader("loading {}", filename);

    std::vector<uint8_t> memory = read_file_binary(std::string(image_dir).append(filename));

    int width; int height; int channels;
    uint8_t* pixels = stbi_load_from_memory(memory.data(), memory.size(), &width, &height, &channels, STBI_rgb_alpha);

    if(!pixels)
    {
        LogFileLoader("failed to load image {}", filename); abort();
    }

    vk::Extent3D image_extent{static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    LogFileLoader("{} dimensions {} x {}", filename, width, height);

    uint64_t image_size = width * height * 4; //4 bytes per pixel
    allocated_buffer_t buffer = gVulkan->allocate_staging_buffer(image_size, filename);

    void* staging_data = get_vulkan().allocator.mapMemory(buffer.allocation);
    memcpy(staging_data, pixels, image_size);
    get_vulkan().allocator.unmapMemory(buffer.allocation);

    stbi_image_free(pixels);

    texture_image_t image = get_vulkan().allocate_texture_image(image_extent, filename);

    get_vulkan().copy_buffer2texture(image, buffer, image_extent);
    get_vulkan().generate_mipmaps(image, image_extent);

    return image;
}

void vkutil::load_image2buffer(std::string filename, allocated_buffer_t& out_buffer, vk::Extent3D& out_image_extent)
{
    LogFileLoader("loading {}", filename);

    std::vector<uint8_t> memory = read_file_binary(std::string(image_dir).append(filename));

    int width; int height; int channels;
    uint8_t* pixels = stbi_load_from_memory(memory.data(), memory.size(), &width, &height, &channels, STBI_rgb_alpha);

    if(!pixels)
    {
        LogFileLoader("failed to load image {}", filename); abort();
    }

    out_image_extent = vk::Extent3D{static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    LogFileLoader("{} dimensions {} x {}", filename, width, height);

    uint64_t image_size = width * height * 4; //4 bytes per pixel
    out_buffer = gVulkan->allocate_staging_buffer(image_size, filename);

    void* staging_data = get_vulkan().allocator.mapMemory(out_buffer.allocation);
    memcpy(staging_data, pixels, image_size);
    get_vulkan().allocator.unmapMemory(out_buffer.allocation);

    stbi_image_free(pixels);
}

void vkutil::name_object(vk::DebugUtilsObjectNameInfoEXT info)
{
#ifndef NDEBUG
    get_vulkan().device.setDebugUtilsObjectNameEXT(info);
#endif
}

void vkutil::name_allocation(vma::Allocation object, std::string name)
{
#ifndef NDEBUG
    get_vulkan().allocator.setAllocationName(object, name.c_str());
#endif
}

void vkutil::push_label(vk::Queue queue, std::string label_name, std::array<float, 4> color)
{
#ifndef NDEBUG
        auto label_info = vk::DebugUtilsLabelEXT{}
        .setColor(color)
        .setPLabelName(label_name.c_str());

        queue.beginDebugUtilsLabelEXT(label_info);
#endif
}

void vkutil::insert_label(vk::Queue queue, std::string label_name, std::array<float, 4> color)
{
#ifndef NDEBUG
    auto label_info = vk::DebugUtilsLabelEXT{}
    .setColor(color)
    .setPLabelName(label_name.c_str());

    queue.insertDebugUtilsLabelEXT(label_info);
#endif
}

void vkutil::pop_label(vk::Queue queue)
{
#ifndef NDEBUG
    queue.endDebugUtilsLabelEXT();
#endif
}

void vkutil::push_label(vk::CommandBuffer cmdbuf, std::string label_name, std::array<float, 4> color)
{
#ifndef NDEBUG
    auto label_info = vk::DebugUtilsLabelEXT{}
    .setColor(color)
    .setPLabelName(label_name.c_str());

    cmdbuf.beginDebugUtilsLabelEXT(label_info);
#endif
}

void vkutil::insert_label(vk::CommandBuffer cmdbuf, std::string label_name, std::array<float, 4> color)
{
#ifndef NDEBUG
    auto label_info = vk::DebugUtilsLabelEXT{}
    .setColor(color)
    .setPLabelName(label_name.c_str());

    cmdbuf.insertDebugUtilsLabelEXT(label_info);
#endif
}

void vkutil::pop_label(vk::CommandBuffer cmdbuf)
{
#ifndef NDEBUG
    cmdbuf.endDebugUtilsLabelEXT();
#endif
}

inline std::mutex build_desc_mx{};

void texture_t::load_from_file(std::string filename)
{
    image = vkutil::load_image_texture(filename);

    auto image_info = vk::DescriptorImageInfo{}
    .setImageView(image.view)
    .setImageLayout(vk::ImageLayout::eReadOnlyOptimal)
    .setSampler(get_vulkan().texture_sampler);

    auto bind_info = descriptor_bind_info{};
    bind_info.binding = 0;
    bind_info.type = vk::DescriptorType::eCombinedImageSampler;
    bind_info.stage = vk::ShaderStageFlagBits::eFragment;

    build_desc_mx.lock();

    get_vulkan().descriptor_builder
    .bind_images(bind_info, &image_info)
    .build(set, get_vulkan().texture_set_layout, filename);

    build_desc_mx.unlock();
}

void texture_t::destroy()
{
    get_vulkan().device.destroyImageView(image.view);
    get_vulkan().allocator.destroyImage(image.image, image.allocation);
}
