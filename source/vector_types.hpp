#ifndef VECTOR_TYPES_HPP
#define VECTOR_TYPES_HPP

#include <utility>
#include <cmath>

#pragma push_macro("Success")
#pragma push_macro("Status")
#undef Success
#undef Status
#define EIGEN_DEFAULT_TO_ROW_MAJOR
#include "eigen/Eigen/Eigen"
#pragma pop_macro("Status")
#pragma pop_macro("Success")

#define GLM_FORCE_INTRINSICS
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
//#define GLM_FORCE_QUAT_DATA_XYZW
#define GLM_FORCE_LEFT_HANDED
#include "glm/glm/glm.hpp"
#include "glm/glm/gtc/matrix_transform.hpp"
#include "glm/glm/gtc/quaternion.hpp"
#include "glm/glm/gtc/type_ptr.hpp"

using Eigen::Matrix;
using Eigen::Vector3f;
using Eigen::Vector4f;

using mat3x3f = glm::mat<3, 3, float, glm::defaultp>;
using mat4x4f = glm::mat<4, 4, float, glm::defaultp>;
using mat3x3d = glm::mat<3, 3, double, glm::defaultp>;
using mat4x4d = glm::mat<4, 4, double, glm::defaultp>;

namespace internal
{
    template<typename E, size_t N>
    struct vector_data
    {
        E _data_[N];
    };

    template<typename E>
    struct vector_data<E, 2>
    {
        E x;
        E y;
    };

    template<typename E>
    struct vector_data<E, 3>
    {
        E x;
        E y;
        E z;
    };

    template<typename E>
    struct vector_data<E, 4>
    {
        E x;
        E y;
        E z;
        E w;
    };
}
/// we need to implement our own vector for eigen to be able to multiply a matrix of vectors bv a matrix
template<typename E, size_t N>
class vector : public internal::vector_data<E, N>
{
public:

#define SIMD(op, lhs, rhs)\
[&]<size_t... I>(std::index_sequence<I...>) -> vector\
{\
    return {((lhs)[I] op (rhs)[I])...};\
}(std::make_index_sequence<N>());

    static constexpr vector broadcast(E val)
    {
        auto fold = [val]<size_t... I>(std::index_sequence<I...>) -> vector
        {
            return {((void)I, val)...};
        };
        return fold(std::make_index_sequence<N>{});
    }

    constexpr vector() = default;
    constexpr vector(const vector& other) = default;

    template<typename... Ts> requires(sizeof...(Ts) == N)
    constexpr vector(Ts... vals)
        : internal::vector_data<E, N>{E(vals)...}
    {
    }

    inline constexpr explicit vector(E scalar)
    {
        *this = broadcast(scalar);
    }

    constexpr E* data()
    {
        return reinterpret_cast<E*>(this);
    }

    constexpr const E* data() const
    {
        return reinterpret_cast<const E*>(this);
    }

    constexpr E operator[](size_t idx) const
    {
        return data()[idx];
    }

    constexpr E& operator[](size_t idx)
    {
        return data()[idx];
    }

    constexpr vector operator+(const vector& other) const
    {
        return SIMD(+, *this, other);
    }
    constexpr vector operator-(const vector& other) const
    {
        return SIMD(-, *this, other);
    }
    constexpr vector operator*(const vector& other) const
    {
        return SIMD(*, *this, other);
    }
    constexpr vector operator/(const vector& other) const
    {
        return SIMD(/, *this, other);
    }

    constexpr vector operator+(E scalar) const
    {
        vector rhs = broadcast(scalar);
        return SIMD(+, *this, rhs);
    }
    constexpr vector operator-(E scalar) const
    {
        vector rhs = broadcast(scalar);
        return SIMD(-, *this, rhs);
    }
    constexpr vector operator*(E scalar) const
    {
        vector rhs = broadcast(scalar);
        return SIMD(*, *this, rhs);
    }
    constexpr vector operator/(E scalar) const
    {
        vector rhs = broadcast(scalar);
        return SIMD(/, *this, rhs);
    }

    inline constexpr friend vector operator+(E scalar, vector rhs)
    {
        vector lhs = broadcast(scalar);
        return SIMD(+, lhs, rhs)
    }
    inline constexpr friend vector operator-(E scalar, vector rhs)
    {
        vector lhs = broadcast(scalar);
        return SIMD(-, lhs, rhs)
    }
    inline constexpr friend vector operator*(E scalar, vector rhs)
    {
        vector lhs = broadcast(scalar);
        return SIMD(*, lhs, rhs)
    }
    inline constexpr friend vector operator/(E scalar, vector rhs)
    {
        vector lhs = broadcast(scalar);
        return SIMD(/, lhs, rhs)
    }

    constexpr vector& operator+=(const vector& other)
    {
        return *this = SIMD(+, *this, other);
    }
    constexpr vector& operator-=(const vector& other)
    {
        return *this = SIMD(-, *this, other);
    }
    constexpr vector& operator*=(const vector& other)
    {
        return *this = SIMD(*, *this, other);
    }
    constexpr vector& operator/=(const vector& other)
    {
        return *this = SIMD(/, *this, other);
    }

    constexpr vector operator&(const vector& other) const
    {
        return SIMD(&, *this, other);
    }
    constexpr vector operator|(const vector& other) const
    {
        return SIMD(|, *this, other);
    }
    constexpr vector operator^(const vector& other) const
    {
        return SIMD(^, *this, other);
    }

    constexpr vector& operator&=(const vector& other)
    {
        return *this = SIMD(&, *this, other);
    }
    constexpr vector& operator|=(const vector& other)
    {
        return *this = SIMD(|, *this, other);
    }
    constexpr vector& operator^=(const vector& other)
    {
        return *this = SIMD(^, *this, other);
    }

#undef SIMD
};

template<typename E, typename... Es>
vector(E, Es...) -> vector<E, sizeof...(Es) + 1>;

using vec2f = glm::fvec2;
using vec3f = glm::fvec3;
using vec4f = glm::fvec4;

using vec2d = glm::dvec2;
using vec3d = glm::dvec3;
using vec4d = glm::dvec4;

using vec2i32 = vector<int32_t, 2>;
using vec3i32 = vector<int32_t, 3>;
using vec4i32 = vector<int32_t, 4>;

using vec2i64 = vector<int64_t, 2>;
using vec3i64 = vector<int64_t, 3>;
using vec4i64 = vector<int64_t, 4>;

using vec2u32 = vector<uint32_t, 2>;
using vec3u32 = vector<uint32_t, 3>;
using vec4u32 = vector<uint32_t, 4>;

using vec2u64 = vector<uint64_t, 2>;
using vec3u64 = vector<uint64_t, 3>;
using vec4u64 = vector<uint64_t, 4>;

using vector2f = glm::fvec2;
using vector3f = glm::fvec3;
using vector3d = glm::dvec3;
using vector3 = glm::dvec3;
using vector4 = glm::dvec4;
using quaternion = glm::dquat;
using matrix4x4 = glm::dmat4x4;

namespace color
{
    inline constexpr vector4 red{1.0, 0.0, 0.0, 1.0};
    inline constexpr vector4 green{0.0, 1.0, 0.0, 1.0};
    inline constexpr vector4 blue{0.0, 0.0, 1.0, 1.0};
}

//world space axis
namespace axis
{
    inline constexpr glm::vec3 X{1.0, 0.0, 0.0};
    inline constexpr glm::vec3 Y{0.0, 1.0, 0.0};
    inline constexpr glm::vec3 Z{0.0, 0.0, 1.0};

    inline constexpr glm::vec3 left = -X;
    inline constexpr glm::vec3 right = X;

    inline constexpr glm::vec3 forward = Z;
    inline constexpr glm::vec3 backward = -Z;

    inline constexpr glm::vec3 up = Y;
    inline constexpr glm::vec3 down = -Y;
}

namespace Eigen
{
    template<typename BinaryOp>
    struct ScalarBinaryOpTraits<float, Vector3f, BinaryOp>
    {
        using ReturnType = Vector3f;
    };

    template<typename BinaryOp>
    struct ScalarBinaryOpTraits<Vector3f, float, BinaryOp>
    {
        using ReturnType = Vector3f;
    };

    template<typename E, size_t N, typename BinaryOp>
    struct ScalarBinaryOpTraits<E, vector<E, N>, BinaryOp>
    {
        using ReturnType = vector<E, N>;
    };

    template<typename E, size_t N, typename BinaryOp>
    struct ScalarBinaryOpTraits<E, glm::vec<N, E>, BinaryOp>
    {
        using ReturnType = glm::vec<N, E>;
    };
}

namespace math
{
    template<typename E, size_t N>
    inline constexpr E sum(vector<E, N> v)
    {
        return [&]<size_t... I>(std::index_sequence<I...>) -> E
        {
            return (v[I] + ...);
        }(std::make_index_sequence<N>{});
    }

    template<typename E, size_t N>
    inline constexpr vector<E, N> sqrt(vector<E, N> v)
    {
        return [&]<size_t... I>(std::index_sequence<I...>) -> vector<E, N>
        {
            return {std::sqrt(v[I])...};
        }(std::make_index_sequence<N>{});
    }

    template<typename E, size_t N>
    inline constexpr E length(vector<E, N> v)
    {
        return std::sqrt(sum(v * v));
    }

    template<typename E, size_t N>
    inline constexpr E distance(vector<E, N> v1, vector<E, N> v2)
    {
        return std::sqrt(sum((v2 - v1) * (v2 - v1)));
    }

    template<typename E, size_t N>
    inline constexpr vector<E, N> normalize(vector<E, N> v)
    {
        return v / vector<E, N>::broadcast(length(v));
    }
}

#endif //VECTOR_TYPES_HPP