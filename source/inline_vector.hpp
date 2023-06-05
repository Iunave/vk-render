#ifndef CHEEMSIT_GUI_VK_INLINE_VECTOR_HPP
#define CHEEMSIT_GUI_VK_INLINE_VECTOR_HPP

#include <memory_resource>
#include <memory>
#include <cstring>
#include <cstdlib>

template<typename T, size_t N>
class inline_vector //todo
{
public:

    union
    {
        T* start;
        T static_buffer[N];
    };
    T* end;

    constexpr inline_vector()
    {
        std::memset(this, 0, sizeof(inline_vector));
    }

    void append(const T& element)
    {
        ptrdiff_t distance = std::distance(start, end);

        if(distance == N)
        {
            T* heap = malloc(distance * sizeof(T) * 2);
            std::memcpy(heap, static_buffer, sizeof(static_buffer));
            start = heap;
            end = start + distance;
        }

        new(end - 1) T{element};
        end += 1;
    }

    T* data()
    {
        if(__builtin_expect(std::distance(start, end) > N), 0)
        {
            return start;
        }
        else
        {
            return &static_buffer[0];
        }
    }
};

#endif //CHEEMSIT_GUI_VK_INLINE_VECTOR_HPP
