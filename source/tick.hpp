#ifndef CHEEMSIT_GUI_TICK_HPP
#define CHEEMSIT_GUI_TICK_HPP

#include <cstdint>
#include <map>

namespace tick
{
    using pfn = void(*)(void*, double);

    template<typename T>
    using m_pfn_tick = void(T::*)(double);

    void dispatch(double delta_time);

    void add(pfn fn, void* data = nullptr);
    bool remove(pfn fn, void* data = nullptr);

    template<typename T>
    void add(m_pfn_tick<T> fn, void* self)
    {
        add(*reinterpret_cast<pfn*>(&fn), self);
    }

    template<typename T>
    bool remove(m_pfn_tick<T> fn, void* self)
    {
        return remove(*reinterpret_cast<pfn*>(&fn), self);
    }
}

///helper for registering and unregistering to the tick dispatch
template<typename T>
class auto_tick_t
{
public:

    explicit auto_tick_t(bool auto_register = false)
    {
        if(auto_register)
        {
            register_tick();
        }
    }

    virtual ~auto_tick_t()
    {
        unregister_tick();
    }

    void register_tick()
    {
        tick::add(&T::tick, static_cast<T*>(this));
    }

    bool unregister_tick()
    {
        return tick::remove(&T::tick, static_cast<T*>(this));
    }
};

#endif //CHEEMSIT_GUI_TICK_HPP
