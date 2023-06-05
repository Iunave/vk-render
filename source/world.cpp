//
// Created by user on 4/14/23.
//

#include "world.hpp"
#include "math.hpp"
#include "bezier.hpp"
#include "log.hpp"
#include "vulkan_engine.hpp"
#include "glfw_window.hpp"
#include "camera.hpp"
#include "time.hpp"
#include <GLFW/glfw3.h>

glm::mat4x4 transform_t::world_matrix() const
{
    glm::mat4x4 rotation_matrix = glm::mat4_cast(rotation);

    glm::mat4x4 translate_scale;
    translate_scale[0] = {scale.x, 0, 0, 0};
    translate_scale[1] = {0, scale.y, 0, 0};
    translate_scale[2] = {0, 0, scale.z, 0};
    translate_scale[3] = {location, 1};

    return translate_scale * rotation_matrix;
}

void transform_t::rotate(float angle, glm::vec3 axis)
{
    rotation = glm::normalize(glm::angleAxis(angle, axis) * rotation);
}

transform_t& entity_t::transform()
{
    transform_t* transforms_data = get_world().transforms.data();
    uint64_t this_index = get_world().entities.get_index(this);
    return transforms_data[this_index];
}

transform_t entity_t::transform() const
{
    return const_cast<entity_t*>(this)->transform();
}

slothandle_t<entity_t> world_t::find_entity(name_t name, bool checked)
{
    for(entity_t& entity : entities)
    {
        if(entity.name == name)
        {
            return entities.get_handle(&entity);
        }
    }

    if(!checked)
    {
        return nullptr;
    }
    else
    {
        LogWorld("no entity named {}", name.str());
        abort();
    }
}

slothandle_t<material_t> world_t::find_material(name_t name, bool checked)
{
    for(material_t& material : materials)
    {
        if(material.name == name)
        {
            return materials.get_handle(&material);
        }
    }

    if(!checked)
    {
        return nullptr;
    }
    else
    {
        LogWorld("no material named {}", name.str());
        abort();
    }
}

slothandle_t<model_t> world_t::find_model(name_t name, bool checked)
{
    for(model_t& model : models)
    {
        if(model.name == name)
        {
            return models.get_handle(&model);
        }
    }

    if(!checked)
    {
        return nullptr;
    }
    else
    {
        LogWorld("no model named {}", name.str());
        abort();
    }
}

slothandle_t<texture_t> world_t::find_texture(name_t name, bool checked)
{
    for(texture_t& texture : textures)
    {
        if(texture.name == name)
        {
            return textures.get_handle(&texture);
        }
    }

    if(!checked)
    {
        return nullptr;
    }
    else
    {
        LogWorld("no texture named {}", name.str());
        abort();
    }
}

void world_t::generate_world(uint64_t entity_count)
{
    transform_t terrain_transform{};
    terrain_transform.rotation = glm::angleAxis(-std::numbers::pi_v<float> / 2.0f, axis::right);
    terrain_transform.location = {0, 0, 0};
    terrain_transform.scale = {2, 2, 2};

    slothandle<entity_t> terrain_base = spawn_entity(entity_name_constructor{"terrain base", "grass terrain base", "grass terrain", "default lit textured"});
    slothandle<entity_t> terrain_base_rock = spawn_entity(entity_name_constructor{"base rock", "grass terrain base rock", "rock", "default lit textured"});
    slothandle<entity_t> terrain_big_rock = spawn_entity(entity_name_constructor{"big rock", "grass terrain big rock", "rock", "default lit textured"});
    slothandle<entity_t> terrain_medium_rock = spawn_entity(entity_name_constructor{"medium rock", "grass terrain medium rock", "rock", "default lit textured"});
    slothandle<entity_t> terrain_small_rock = spawn_entity(entity_name_constructor{"small rock", "grass terrain small rock", "rock", "default lit textured"});
    slothandle<entity_t> terrain_brush = spawn_entity(entity_name_constructor{"brush", "brush", "brush", "default lit textured"});

    terrain_base->transform() = terrain_transform;
    terrain_base_rock->transform() = terrain_transform;
    terrain_big_rock->transform() = terrain_transform;
    terrain_medium_rock->transform() = terrain_transform;
    terrain_small_rock->transform() = terrain_transform;
    terrain_brush->transform() = terrain_transform;


    double position_range = 200.0;

    for(size_t index = 0; index < entity_count; ++index)
    {
        slothandle<entity_t> entity = spawn_entity(entity_random_constructor{});

        entity->transform().location = math::rand_pos(-position_range, position_range);

        if(entity->name == "null" || entity->model->name == "Terrain")
        {
            destroy_entity(entity);
        }
        else if(entity->name == "pony")
        {
            entity->transform().scale = {0.02, 0.02, 0.02};
        }
        else if(entity->name == "kat_gun")
        {
            entity->transform().scale = {3, 3, 3};
        }
        else if(entity->name == "fish")
        {
            entity->transform().scale = {0.5, 0.5, 0.5};
        }
    }

    lightmanager.spawn_directional_light(directionallight_t{axis::down, 0.f, {1, 1, 1}, 50.0f});
    lightmanager.spawn_pointlight({0, 100, 0}, {1.0, 0.0, 0.0}, 1000.f);
    lightmanager.spawn_pointlight({0, 100, 0}, {0.0, 1.0, 0.0}, 1000.f);
    lightmanager.spawn_pointlight({0, 100, 0}, {0.0, 0.0, 1.0}, 1000.f);
}

bool world_t::destroy_entity(slothandle<entity_t> entity)
{
    uint64_t removed_entity = entities.remove(entity.handle);
    if(removed_entity == UINT64_MAX)
    {
        return false;
    }

    transforms[removed_entity] = transforms.back();
    transforms.pop_back();

    return true;
}

world_t::world_t()
{
    scene_data.ambiance_color = {1.0, 1.0, 1.0};
    scene_data.ambiance_strength = 0.01;
    scene_data.sky_color = {0.1, 0.1, 0.1};

    device_transforms_num = device_transforms_allocation_step;

    models.add("null");
    textures.add("null");
    materials.add("null");
}

slothandle_t<texture_t> world_t::add_texture(std::string name, std::string filename)
{
    texture_handle_t handle = textures.add(name);
    handle->load_from_file(filename);
    return handle;
}

slothandle_t<model_t> world_t::add_model(std::string name, std::string filename)
{
    model_handle_t handle = models.add(name);
    handle->load_from_file(filename);
    return handle;
}

slothandle_t<material_t> world_t::add_unique_material(name_t name)
{
    if(slothandle_t<material_t> material = find_material(name, false))
    {
        return material;
    }
    else
    {
        return materials.add(name);
    }
}

void entity_name_constructor::operator()(entity_t* entity) const
{
    entity->name = name;
    entity->model = get_world().find_model(model);
    entity->texture = get_world().find_texture(texture);
    entity->material = get_world().find_material(material);
}

void entity_random_constructor::operator()(entity_t* entity) const
{
    auto random_item = []<typename T>(const slotmap_t<T>& map)
    {
        return map.get_handle(math::randrange(0l, map.size() - 1));
    };

    entity->model = random_item(get_world().models);
    entity->texture = random_item(get_world().textures);
    entity->material = random_item(get_world().materials);
    entity->name = entity->model->name;
}

slothandle<directionallight_t> light_manager_t::spawn_directional_light(directionallight_t light_data)
{
    if(directional_lights.size() == MAX_DIRECTIONAL_LIGHTS)
    {
        LogWorld("cannot spawn directional light, buffer is full");
        return nullptr;
    }

    directional_maps.emplace_back() = get_vulkan().allocate_directional_shadowmap(PLANE_SHADOW_RESOLUTION, fmt::format("directional map [{}]", directional_lights.size()));
    return directional_lights.add(light_data);
}

bool light_manager_t::destroy_directional_light(slothandle<directionallight_t> light)
{
    uint64_t removed_light = directional_lights.remove(light.handle);

    if(removed_light == UINT64_MAX)
    {
        return false;
    }

    gVulkan->active_frame().next_render.append(new allocated_image_t{directional_maps[removed_light]}, [](allocated_image_t* image)
    {
        gVulkan->destroy_image(*image);
        delete image;
    });

    directional_maps[removed_light] = directional_maps.back();
    directional_maps.pop_back();

    return true;
}

slothandle<pointlight_t> light_manager_t::spawn_pointlight(glm::vec3 location, glm::vec3 color, float strength)
{
    if(pointlights.size() == MAX_POINTLIGHTS)
    {
        LogWorld("cannot spawn pointlight, buffer is full");
        return nullptr;
    }

    cubemaps.emplace_back() = get_vulkan().allocate_shadow_cubemap(CUBE_SHADOW_RESOLUTION, fmt::format("cubemap [{}]", pointlights.size()));

    slothandle<pointlight_t> new_light = pointlights.add();
    new_light->location = location;
    new_light->color = color;
    new_light->strength = strength;

    return new_light;
}

bool light_manager_t::destroy_pointlight(slothandle<pointlight_t> light) //fixme destroying to many lights (>= 4) in one frame crashes the driver
{
    uint64_t removed_light = pointlights.remove(light.handle);

    if(removed_light == UINT64_MAX)
    {
        return false;
    }

    gVulkan->active_frame().next_render.append(new allocated_image_t{cubemaps[removed_light]}, [](allocated_image_t* image)
    {
        gVulkan->destroy_image(*image);
        delete image;
    });

    cubemaps[removed_light] = cubemaps.back();
    cubemaps.pop_back();

    return true;
}
