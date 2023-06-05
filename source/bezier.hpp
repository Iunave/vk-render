#ifndef CHEEMSIT_GUI_VK_BEZIER_HPP
#define CHEEMSIT_GUI_VK_BEZIER_HPP

#include <cstdint>
#include <array>
#include <vector>
#include <span>
#include "glm/glm/glm.hpp"

//cubic bezier utilities
template<typename T = float, size_t N = 3, typename vec_t = glm::vec<N, T>>
struct bezier
{
    static vec_t position(std::array<vec_t, 4> polynomial_coefficients, T t);
    static vec_t velocity(std::array<vec_t, 4> polynomial_coefficients, T t);
    static vec_t acceleration(std::array<vec_t, 4> polynomial_coefficients, T t);
    static vec_t jolt(std::array<vec_t, 4> polynomial_coefficients, T t);

    static std::array<vec_t, 4> polynomials(std::array<vec_t, 4> control);
    static std::vector<T> make_arc_length_table(std::array<vec_t, 4> polynomial_coefficients, size_t count);
    static T u2t(T u, std::span<const T> table);

    static std::vector<vec_t> walk(std::array<vec_t, 4> control, size_t count);
    static std::vector<vec_t> walk_mapped(std::array<vec_t, 4> control, std::span<const T> table, size_t count);

   /*
    *  generates a spline with random positions
    *  C_continuity == 0 == no continuity
    *  C_continuity == 1 == velocity continuity
    *  C_continuity == 2 == acceleration continuity
    */
    static std::vector<std::array<vec_t, 4>> generate_spline(size_t count, bool loop, size_t C_continuity);
};

using bezierf2 = bezier<float, 2>;
using bezierd2 = bezier<double, 2>;
using bezierf3 = bezier<float, 3>;
using bezierd3 = bezier<double, 3>;

#endif //CHEEMSIT_GUI_VK_BEZIER_HPP
