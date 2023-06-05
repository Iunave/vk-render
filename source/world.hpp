#ifndef CHEEMSIT_GUI_VK_WORLD_HPP
#define CHEEMSIT_GUI_VK_WORLD_HPP

#include "vk-render.hpp"
#include "slotmap.hpp"
#include "vulkan_model.hpp"
#include "vulkan_utility.hpp"
#include "entity_manager.hpp"
#include "camera.hpp"
#include <span>

using model_handle_t = slothandle_t<model_t>;
using material_handle_t = slothandle_t<material_t>;
using texture_handle_t = slothandle_t<texture_t>;

struct packed_transform_t
{
    uint32_t rot_wx;
    uint32_t rot_yz;
    glm::vec3 position;
    glm::vec3 scale;
};

struct transform_t
{
    glm::mat4x4 world_matrix() const;
    void rotate(float angle, glm::vec3 axis);

    glm::vec3 forward_vector() const {return rotation * axis::forward;}
    glm::vec3 backard_vector() const {return rotation * axis::backward;}
    glm::vec3 left_vector() const {return rotation * axis::left;}
    glm::vec3 right_vector() const {return rotation * axis::right;}
    glm::vec3 up_vector() const {return rotation * axis::up;}
    glm::vec3 down_vector() const {return rotation * axis::down;}

    glm::quat rotation{1, 0, 0, 0};
    glm::vec3 location{0, 0, 0};
    glm::vec3 scale{1, 1, 1};
};

template<typename T>
concept entity_constructor_c = requires(T constructor, struct entity_t* entity)
{
    {constructor(entity)} -> std::same_as<void>;
};

struct entity_t
{
    entity_t(name_t in_name = "null", slothandle<model_t> in_model = nullptr, slothandle<texture_t> in_texture = nullptr, slothandle_t<material_t> in_material = nullptr, transform_t in_transform  = {})
        : name(in_name)
        , model(in_model)
        , texture(in_texture)
        , material(in_material)
    {
        transform() = in_transform;
    }

    template<entity_constructor_c T>
    entity_t(T proxy)
    {
        transform() = transform_t{};
        proxy(this);
    }

    transform_t& transform();
    transform_t transform() const;

    name_t name;
    slothandle<model_t> model;
    slothandle<texture_t> texture; //todo not all entities have a texture so maybe move this some other place
    slothandle<material_t> material;
};

struct entity_name_constructor
{
    void operator()(entity_t* entity) const;

    name_t name;
    name_t model;
    name_t texture;
    name_t material;
};

struct entity_random_constructor
{
    void operator()(entity_t* entity) const;
};

struct directionallight_t
{
    glm::vec3 direction;
    float pad0;
    glm::vec3 color;
    float strength;
};

struct pointlight_t
{
    glm::vec3 location;
    float pad0;
    glm::vec3 color;
    float strength;
};

using pointlight_projection_t = std::array<glm::mat4x4, 6>;

class light_manager_t
{
public:
    static constexpr uint32_t MAX_DIRECTIONAL_LIGHTS = 1;
    static constexpr uint32_t MAX_POINTLIGHTS = 128;
    static constexpr uint32_t PLANE_SHADOW_RESOLUTION = 8192;
    static constexpr uint32_t CUBE_SHADOW_RESOLUTION = 8192;

    slothandle<directionallight_t> spawn_directional_light(directionallight_t light_data);
    bool destroy_directional_light(slothandle<directionallight_t>);

    slothandle<pointlight_t> spawn_pointlight(glm::vec3 location = {0, 0, 0}, glm::vec3 color = {0, 0, 0}, float strength = 0.0);
    bool destroy_pointlight(slothandle<pointlight_t> light);

    slotmap_t<directionallight_t> directional_lights;
    std::vector<allocated_image_t> directional_maps;

    slotmap_t<pointlight_t> pointlights;
    std::vector<allocated_image_t> cubemaps;
};

class particle_manager_t
{
public:
    static constexpr uint32_t MAX_PARTICLES = 4096;


};

struct global_device_data_t
{
    struct statistics_t
    {
        int32_t elapsed_seconds;
        int32_t elapsed_nanoseconds;
        uint32_t frame;
        float deltatime;
        glm::uvec2 screen_size;
        float elapsed_time;
        float pad1;
    } statistics;

    struct camera_t
    {
        glm::vec3 location;
        float pad0;
        glm::quat rotation;
        glm::mat4x4 view;
        glm::mat4x4 projection;
        glm::mat4x4 projection_view;
    } camera;

    struct scene_t
    {
        glm::vec3 ambiance_color;
        float ambiance_strength;
        glm::vec3 sky_color;
        float pad0;
    } scene;
};

struct render_thread_data_t
{
    camera_t camera;
    global_device_data_t::scene_t scene;

    std::span<entity_t> entities;
    std::span<transform_t> transforms;
    std::span<directionallight_t> directional_lights;
    std::span<allocated_image_t> directional_shadowmaps;
    std::span<pointlight_t> pointlights;
    std::span<allocated_image_t> cube_shadowmaps;
};

class world_t
{
public:
    static constexpr size_t device_transforms_allocation_step = 1024;

    world_t();

    slothandle_t<entity_t> find_entity(name_t name, bool checked = true);
    slothandle_t<model_t> find_model(name_t name, bool checked = true);
    slothandle_t<material_t> find_material(name_t name, bool checked = true);
    slothandle_t<texture_t> find_texture(name_t name, bool checked = true);

    void reallocate_transform_buffer(size_t new_size);
    void generate_world(uint64_t entity_count);

    void upload_device_global_data();
    void upoad_transforms();
    void upload_lights();
    void flush_uploads();

    template<entity_constructor_c T>
    slothandle<entity_t> spawn_entity(T proxy)
    {
        transforms.emplace_back();
        return entities.add(std::forward<T>(proxy));
    }

    void prepare_entity_spawn();
    bool destroy_entity(slothandle<entity_t> entity);

    slothandle_t<material_t> add_unique_material(name_t name);
    slothandle_t<texture_t> add_texture(std::string name, std::string filename);
    slothandle_t<model_t> add_model(std::string name, std::string filename);

    global_device_data_t::scene_t scene_data;

    camera_t camera;

    uint64_t device_transforms_num;
    allocated_buffer_t transform_buffer;

    slotmap_t<entity_t> entities;
    std::vector<transform_t> transforms;

    light_manager_t lightmanager;

    slotmap_t<model_t> models;
    slotmap_t<material_t> materials;
    slotmap_t<texture_t> textures;
};

inline world_t* gWorld = nullptr;
inline world_t& get_world()
{
    return *gWorld;
}

template<> inline slotmap_t<entity_t>& find_storage_by_type() {return get_world().entities;}
template<> inline slotmap_t<material_t>& find_storage_by_type() {return get_world().materials;}
template<> inline slotmap_t<model_t>& find_storage_by_type() {return get_world().models;}
template<> inline slotmap_t<texture_t>& find_storage_by_type() {return get_world().textures;}
template<> inline slotmap_t<directionallight_t>& find_storage_by_type() {return get_world().lightmanager.directional_lights;}
template<> inline slotmap_t<pointlight_t>& find_storage_by_type() {return get_world().lightmanager.pointlights;}

#endif //CHEEMSIT_GUI_VK_WORLD_HPP
