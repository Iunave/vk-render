#include "entity_manager.hpp"

/*
auto entity_manager_t::find_suitable_region(const entity_t& entity) -> decltype(regions)::iterator
{
    for(auto it = regions.begin(); it != regions.end(); ++it)
    {
        if(entity.model == it->model && entity.material == it->material)
        {
            return it;
        }
    }

    entity_region_t& new_region = regions.emplace_back();
    new_region.begin = entities.size();
    new_region.end = entities.size();
    new_region.model = entity.model;
    new_region.material = entity.material;

    return regions.end() - 1;
}

auto entity_manager_t::find_owning_region(const entity_t* entity) -> std::vector<entity_region_t>::iterator
{
    uint32_t entity_index = entities.get_index(entity);

    entity_region_t phony_region{};
    phony_region.begin = entity_index;

    return std::lower_bound(regions.begin(), regions.end(), phony_region,[](const entity_region_t& lhs, const entity_region_t& rhs) -> bool
    {
        return lhs.begin < rhs.begin;
    });
}

slothandle<entity_t> entity_manager_t::add(const entity_t& entity)
{
    using entity_stored_t = decltype(entities)::item_store_t;
    using entity_key_t = decltype(entities)::key_t;

    auto found_region = find_suitable_region(entity);

    slothandle<entity_t> handle = entities.add(); //we dont care about the returned handle, well make it point to our insertion place instead

    for(auto region = regions.end() - 1; region != found_region; --region)     //begin by moving the last item to the end, then move next untill we get to this region
    {
        entity_stored_t& first_item = entities.items[region->begin];
        entity_stored_t& end_item = entities.items[region->end]; //this is always going to be a moved-from item, since we start by moving into the new slot added by map.add()
        entity_key_t& moved_item_key = entities.keys[first_item.self_key_offset];

        moved_item_key.item_offset = region->end;
        end_item = std::move(first_item);

        region->begin += 1;
        region->end += 1;
    }

    entities[found_region->end] = entity;     //the free slot is now at the end of our region
    handle.handle.key_offset = found_region->end;     //make the handle point to it
    found_region->end += 1;

    return handle;
}

bool entity_manager_t::remove(slothandle<entity_t> handle)
{
    entity_t* entity = entities[handle.handle];
    if(entity == nullptr)
    {
        return false;
    }

    return remove(entity);
}

bool entity_manager_t::remove(entity_t* entity)
{
    using entity_stored_t = decltype(entities)::item_store_t;
    using entity_key_t = decltype(entities)::key_t;

    auto owning_region = find_owning_region(entity);
    uint32_t removed_item = entities.get_index(entity);

    auto stored_item = reinterpret_cast<entity_stored_t*>(entity);

    entity_key_t& tail_key = entities.keys[entities.freelist_tail]; //set old tail to point to the new tail
    tail_key.next_free = stored_item->self_key_offset;
    entities.freelist_tail = stored_item->self_key_offset;

    entities.keys[stored_item->self_key_offset].item_id += 1; //invalidate handles to this key... might wrap around

    entity_stored_t& last_item = entities.items[owning_region->end - 1]; //remove in the owning region
    entity_key_t& last_item_key = entities.keys[last_item.self_key_offset];

    last_item_key.item_offset = removed_item;
    entities.items[removed_item] = std::move(last_item);

    removed_item = owning_region->end - 1;
    owning_region->end -= 1;

    for(auto region = owning_region + 1; region != regions.end(); ++region) //move in items from the next regions
    {
        last_item = entities.items[region->end - 1];
        last_item_key = entities.keys[last_item.self_key_offset];

        last_item_key.item_offset = removed_item;
        entities.items[removed_item] = std::move(last_item);

        removed_item = region->end - 1;

        region->begin -= 1;
        region->end -= 1;
    }

    if(owning_region->begin == owning_region->end)
    {
        memcpy(std::to_address(owning_region), std::to_address(owning_region + 1), std::distance(owning_region, regions.end() - 1) * sizeof(entity_region_t));
        regions.pop_back();
    }

    entities.item_count -= 1;

    return true;
}

void entity_manager_t::reevaluate(entity_t* entity) //todo
{
    using entity_stored_t = decltype(entities)::item_store_t;
    using entity_key_t = decltype(entities)::key_t;

    auto owning_region = find_owning_region(entity);

    if(owning_region->model == entity->model && owning_region->material == entity->material)
    {
        return; //nothing to do
    }

    auto suitable_region = find_suitable_region(*entity);

    entity_t saved_entity = std::move(*entity);
    uint32_t removed_item = entities.get_index(entity);

    entity_stored_t& last_item = entities.items[owning_region->end - 1]; //remove in the owning region
    entity_key_t& last_item_key = entities.keys[last_item.self_key_offset];

    last_item_key.item_offset = removed_item;
    entities.items[removed_item] = std::move(last_item);

    removed_item = owning_region->end - 1;
    owning_region->end -= 1;

    for(auto region = owning_region + 1; region != suitable_region; ++region) //move in items from the next regions
    {
        last_item = entities.items[region->end - 1];
        last_item_key = entities.keys[last_item.self_key_offset];

        last_item_key.item_offset = removed_item;
        entities.items[removed_item] = std::move(last_item);

        removed_item = region->end - 1;

        region->begin -= 1;
        region->end -= 1;
    }

    last_item = entities.items[suitable_region->end - 1];
    last_item_key = entities.keys[last_item.self_key_offset];

    last_item_key.item_offset = removed_item;
    entities.items[removed_item] = std::move(last_item);

    entities[suitable_region->end - 1] = std::move(saved_entity);
    entities.keys[entities.items[suitable_region->end - 1].self_key_offset].item_offset = suitable_region->end - 1;
}
*/