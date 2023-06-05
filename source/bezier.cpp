#include "bezier.hpp"
#include "math.hpp"
#include <algorithm>

template<typename T, typename vec_t>
static vec_t polynomials_x_ts(std::array<vec_t, 4> polynomial_coefficients, std::array<T, 4> ts)
{
    return polynomial_coefficients[0] * ts[0]
         + polynomial_coefficients[1] * ts[1]
         + polynomial_coefficients[2] * ts[2]
         + polynomial_coefficients[3] * ts[3];
}

template<typename T, size_t N, typename vec_t>
vec_t bezier<T, N, vec_t>::position(std::array<vec_t, 4> polynomial_coefficients, T t)
{
    std::array<T, 4> ts{T(1), T(t), t*t, t*t*t};
    return polynomials_x_ts(polynomial_coefficients, ts);
}

template<typename T, size_t N, typename vec_t>
vec_t bezier<T, N, vec_t>::velocity(std::array<vec_t, 4> polynomial_coefficients, T t)
{
    std::array<T, 4> ts{T(0), T(1), T(2.0)*t, T(3.0)*(t*t)};
    return polynomials_x_ts(polynomial_coefficients, ts);
}

template<typename T, size_t N, typename vec_t>
vec_t bezier<T, N, vec_t>::acceleration(std::array<vec_t, 4> polynomial_coefficients, T t)
{
    std::array<T, 4> ts{T(0), T(0), T(2.0), T(6.0)*t};
    return polynomials_x_ts(polynomial_coefficients, ts);
}

template<typename T, size_t N, typename vec_t>
vec_t bezier<T, N, vec_t>::jolt(std::array<vec_t, 4> polynomial_coefficients, T t)
{
    std::array<T, 4> ts{0, 0, 0, 6.0};
    return polynomials_x_ts(polynomial_coefficients, ts);
}

template<typename T, size_t N, typename vec_t>
std::array<vec_t, 4> bezier<T, N, vec_t>::polynomials(std::array<vec_t, 4> control)
{
    std::array<vec_t, 4> res{};
    res[0] = control[0];
    res[1] = T(-3.0) * control[0] + T(3.0) * control[1];
    res[2] = T(3.0) * control[0] + T(-6.0) * control[1] + T(3.0) * control[2];
    res[3] = -control[0] + T(3.0) * control[1] + T(-3.0) * control[2] + control[3];
    return res;
}

template<typename T, size_t N, typename vec_t>
std::vector<T> bezier<T, N, vec_t>::make_arc_length_table(std::array<vec_t, 4> control, size_t count)
{
    std::array<vec_t, 4> polynomial_coefficients = polynomials(control);
    std::vector<T> table(count);
    vec_t prev = control[0];

    for(size_t point = 1; point < count; ++point)
    {
        T t = T(point) / T(count - 1);

        vec_t curr = position(polynomial_coefficients, t);
        
        T length = glm::distance(curr, prev);
        table[point] = table[point - 1] + length;
        
        prev = curr;
    }
    
    return table;
}

template<typename T, size_t N, typename vec_t>
T bezier<T, N, vec_t>::u2t(T u, std::span<const T> table)
{
    T target_len = table.back() * u;

    auto lower_bound = std::lower_bound(table.begin(), table.end(), target_len);
    size_t lower_bound_idx = std::distance(table.begin(), lower_bound);

    T len_after = *lower_bound;
    T len_before = *(lower_bound - 1);
    T len_between = len_after - len_before;
    T len_lerp = (target_len - len_before) / len_between;

    return (T(lower_bound_idx - 1) + len_lerp) / T(table.size() - 1);
}

template<typename T, size_t N, typename vec_t>
std::vector<vec_t> bezier<T, N, vec_t>::walk(std::array<vec_t, 4> control, size_t count)
{
    std::vector<vec_t> points(count);
    points[0] = control[0];
    points[count - 1] = control[3];

    std::array<vec_t, 4> polynomial_coefficients = polynomials(control);

    for(size_t point = 1; point < (count - 1); ++point)
    {
        T t = T(point) / T(count - 1);
        points[point] = position(polynomial_coefficients, t);
    }

    return points;
}

template<typename T, size_t N, typename vec_t>
std::vector<vec_t> bezier<T, N, vec_t>::walk_mapped(std::array<vec_t, 4> control, std::span<const T> table, size_t count)
{
    std::vector<vec_t> points(count);
    points[0] = control[0];
    points[count - 1] = control[3];

    std::array<vec_t, 4> polynomial_coefficients = polynomials(control);

    for(size_t point = 1; point < (count - 1); ++point)
    {
        T u = T(point) / T(count - 1);
        T t = u2t(u, table);

        points[point] = position(polynomial_coefficients, t);
    }

    return points;
}

template<typename T, size_t N, typename vec_t>
std::vector<std::array<vec_t, 4>> bezier<T, N, vec_t>::generate_spline(size_t count, bool loop, size_t C_continuity)
{
    auto create_point = []() -> vec_t
    {
        auto fold = []<size_t... I>(std::index_sequence<I...>) -> vec_t
        {
            return {((void)I, math::randrange(-200.0, 200.0))...};
        };
        return fold(std::make_index_sequence<N>{});
    };

    std::vector<std::array<vec_t, 4>> beziers(count);
    beziers[0][0] = create_point();
    beziers[0][1] = create_point();
    beziers[0][2] = create_point();
    beziers[0][3] = create_point();

    for(size_t rcx = 1; rcx < count; ++rcx)
    {
        beziers[rcx][0] = beziers[rcx - 1][3];

        if(C_continuity == 0) //A(1) == B(0)
        {
            if(loop && rcx == count - 1)
            {
                beziers[rcx][1] = create_point();
                beziers[rcx][2] = create_point();
                beziers[rcx][3] = beziers[0][0];
            }
            else
            {
                beziers[rcx][1] = create_point();
                beziers[rcx][2] = create_point();
                beziers[rcx][3] = create_point();
            }
        }
        else if(C_continuity == 1) //A'(1) = B'(0)
        {
            if(loop && rcx == count - 1)
            {
                beziers[rcx][1] = T(2.0) * beziers[rcx - 1][3] - beziers[rcx - 1][2];
                beziers[rcx][2] = T(2.0) * beziers[0][0] - beziers[0][1];
                beziers[rcx][3] = beziers[0][0];
            }
            else
            {
                beziers[rcx][1] = T(2.0) * beziers[rcx - 1][3] - beziers[rcx - 1][2];
                beziers[rcx][2] = create_point();
                beziers[rcx][3] = create_point();
            }
        }
        else if(C_continuity == 2) //A''(1) = B''(0)
        {
            if(loop && rcx == count - 1)
            {
                beziers[rcx][1] = T(2.0) * beziers[rcx - 1][3] - beziers[rcx - 1][2];
                beziers[rcx][2] = beziers[0][1] + T(4.0) * (beziers[0][0] - beziers[0][1]);
                beziers[rcx][3] = beziers[0][0];
            }
            else
            {
                beziers[rcx][1] = T(2.0) * beziers[rcx - 1][3] - beziers[rcx - 1][2];
                beziers[rcx][2] = beziers[rcx - 1][1] + T(4.0) * (beziers[rcx - 1][3] - beziers[rcx - 1][2]);
                beziers[rcx][3] = create_point();
            }
        }
    }

    return beziers;
}

template struct bezier<float, 2>;
template struct bezier<double, 2>;
template struct bezier<float, 3>;
template struct bezier<double, 3>;