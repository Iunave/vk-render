#ifndef BUILD_MATH_HPP
#define BUILD_MATH_HPP

#include <cstdint>
#include <cmath>
#include <array>
#include <numbers>
#include <iostream>
#include "vector_types.hpp"

extern "C" double randnormal();
extern "C" double randrange_flt(double, double);
extern "C" int64_t randrange_int(int64_t, int64_t);

namespace math
{
    inline __attribute__((always_inline)) double randrange(double min, double max)
    {
        return randrange_flt(min, max);
    }

    inline __attribute__((always_inline)) int64_t randrange(int64_t min, int64_t max)
    {
        return randrange_int(min, max);
    }

    template<typename T> requires std::is_integral_v<T>
    T pack_norm(float val)
    {
        return std::round(val * float(std::numeric_limits<T>::max()));
    }

    template<typename T>
    T frac(T val)
    {
        T whole = std::floor(val);
        return val - whole;
    }

    glm::vec2 oct_encode(glm::vec3 n);
    glm::vec3 oct_decode(glm::vec2 f);
    void oct_error_test();

    glm::mat4x4 view(glm::vec3 Cx, glm::vec3 Cy, glm::vec3 Cz, glm::vec3 location);
    glm::mat4x4 view(glm::quat orientation, glm::vec3 location);
    glm::mat4x4 view(glm::vec3 forward, glm::vec3 up, glm::vec3 location);
    glm::mat4x4 perspective(float aspect, float fov, float near, float far);

    template<typename T>
    inline constexpr T pow2(T x)
    {
        return x * x;
    }

    inline constexpr double mod(double x, double y)
    {
        return (x-y) * std::floor(x/y);
    }

    inline constexpr double saw(double x)
    {
        return x - std::floor(x);
    }

    template<typename T, typename F>
    constexpr T lerp(T a, T b, F t)
    {
        return t * b + (1 - t) * a;
    }

    // circle equation = (x−h)2+(y−k)2=r2
    //x = cx + r * cos(a)
    //y = cy + r * sin(a)
    template<typename F>
    inline constexpr glm::vec<2, F> walk_circle(glm::vec<2, F> origin, F radius, F radians)
    {
        origin.x += (radius * std::cos(radians));
        origin.y += (radius * std::sin(radians));
        return origin;
    }

    template<typename F>
    inline std::array<glm::vec<2, F>, 3> make_equilateral_triangle(glm::vec<2, F> origin, F radius)
    {
        return
        {
            walk_circle(origin, radius, std::numbers::pi_v<F> / 2),
            walk_circle(origin, radius, 11 * std::numbers::pi_v<F> / 6),
            walk_circle(origin, radius, 7 * std::numbers::pi_v<F> / 6)
        };
    }

    inline vec2f circumcentre(vec2f a, vec2f b, vec2f c)
    {
        float t = pow2(a.x) + pow2(a.y) - pow2(b.x) - pow2(b.y);
        float u = pow2(a.x) + pow2(a.y) - pow2(c.x) - pow2(c.y);
        float J = (a.x - b.x) * (a.y - c.y) - (a.x - c.x) * (a.y - b.y);

        float x = (-(a.y - b.y)*u + (a.y - c.y)*t) / (J + J);
        float y = (-(a.x - b.x)*u + (a.x - c.x)*t) / (J + J);

        return {x, y};
    }

    inline vec2f centroid(vec2f a, vec2f b, vec2f c)
    {
        float x = a.x + b.x + c.x;
        float y = a.y + b.y + c.y;
        x /= 3;
        y /= 3;
        return {x, y};
    }

    inline mat4x4f projection(float left, float right, float bottom, float top, float near, float far)
    {
        return mat4x4f
        {
            {(2.0f * near) / (right - left), 0, (right + left) / (right - left), 0},
            {0, (2.0f * near) / (top - bottom), (top + bottom) / (top - bottom), 0},
            {0, 0, -(far + near) / (far - near), (-2.0f * far * near) / (far - near)},
            {0, 0, -1, 0}
        };
    }

    inline std::vector<float> MakeArcLengthTable(uint64_t NumPoints, Eigen::Matrix<vec3f, 4, 1> polynomial_coefficients)
    {
        std::vector<float> Table;
        Table.resize(NumPoints);
        Table[0] = 0.0;

        vec3f Previous{0};
        vec3f Current{0};

        for(uint64_t Index = 1; Index < NumPoints; ++Index)
        {
            float t = float(Index) / float(NumPoints - 1);

            Vector4f t_matrix{1, t, t*t, t*t*t};
            Current = t_matrix.cwiseProduct(polynomial_coefficients).sum();

            float ArcLength = length(Current - Previous);
            Table[Index] = Table[Index - 1] + ArcLength;

            Previous = Current;
        }

        return Table;
    }

    template<typename T>
    inline T MapToArcLengthTable(const std::vector<T>& Table, T u)
    {
        float DesiredArcLength = *(Table.end() - 1) * u;

        auto FirstMoreOrEqual = std::lower_bound(Table.begin(), Table.end(), DesiredArcLength);
        uint64_t FirstMoreOrEqualIndex = std::distance(Table.begin(), FirstMoreOrEqual);
        T LengthAfter = *FirstMoreOrEqual;

        if(LengthAfter == DesiredArcLength)
        {
            return T(FirstMoreOrEqualIndex) / T(Table.size() - 1);
        }

        T LengthBefore = *(FirstMoreOrEqual - 1);
        T LengthBetween = LengthAfter - LengthBefore;
        T LengthFraction = (DesiredArcLength - LengthBefore) / LengthBetween;

        return (T(FirstMoreOrEqualIndex - 1) + LengthFraction) / T(Table.size() - 1);
    }

    inline std::vector<vec3f> map_bezier(vec3f p0, vec3f p1, vec3f p2, vec3f p3, uint64_t NumPoints = 1000)
    {
        static const Matrix<float, 4, 4> bezier_identity = []()
        {
            Matrix<float, 4, 4> matrix;
            matrix
            << 1, 0, 0, 0
            , -3, 3, 0, 0
            , 3, -6, 3, 0
            , -1, 3, -3, 1;

            return matrix;
        }();

        Eigen::Matrix<vec3f, 4, 1> point_matrix{p0, p1, p2, p3};
        Eigen::Matrix<vec3f, 4, 1> polynomial_coefficients = bezier_identity * point_matrix;

        std::vector<vec3f> points; //for drawing a bezier we dont want to map to arch length since denser packed points in curves works in our favour
        points.resize(NumPoints);

        for(uint64_t Index = 0; Index < NumPoints; ++Index)
        {
            float t = float(Index) / float(NumPoints - 1);
            Vector4f t_matrix{1, t, t * t, t * t * t};
            vec3f point = t_matrix.cwiseProduct(polynomial_coefficients).sum();
            points[Index] = point;
        }

        return points;
    }

    inline uint8_t flt2coloru8(float val)
    {
        return uint8_t(std::round(val * UINT8_MAX));
    }

    inline uint32_t fltrgba2u8rgba(float r, float g, float b, float a)
    {
        uint32_t u8r = flt2coloru8(r);
        uint32_t u8g = flt2coloru8(g);
        uint32_t u8b = flt2coloru8(b);
        uint32_t u8a = flt2coloru8(a);
        return (u8r << 0) | (u8g << 8) | (u8b << 16) | (u8a << 24);
    }

    inline glm::mat4x4 translate(float x, float y, float z)
    {
        return glm::mat4x4
        {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            x, y, z, 1
        };
    }

    inline glm::mat4x4 scale(float x, float y, float z)
    {
        return glm::mat4x4
        {
            x, 0, 0, 0,
            0, y, 0, 0,
            0, 0, z, 0,
            0, 0, 0, 1
        };
    }

    inline glm::mat4x4 rotate(glm::vec3 axis, float rad)
    {
        glm::mat4x4 res{1};
        glm::rotate(res, rad, axis);
        return res;
    }

    inline vector3 rand_axis()
    {
        constexpr float atleast = FLT_EPSILON * 10.f;
        vector3 random;
        do
        {
            random = {randrange(-1.0, 1.0), randrange(-1.0, 1.0), randrange(-1.0, 1.0)};
        }
        while(random.x <= atleast && random.y <= atleast && random.z <= atleast);

        return glm::normalize(random);
    }

    inline vector3 rand_pos(double min, double max)
    {
        return {math::randrange(min, max),math::randrange(min, max),math::randrange(min, max)};
    }

    inline vec4f rand_color()
    {
        return
        {
            static_cast<float>(randnormal()),
            static_cast<float>(randnormal()),
            static_cast<float>(randnormal()),
            static_cast<float>(randnormal())
        };
    }
}

#endif //BUILD_MATH_HPP
