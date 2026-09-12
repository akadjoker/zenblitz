#ifndef ENGINE_MATRIX4_H
#define ENGINE_MATRIX4_H

#include "engine/Geom.h"
#include <cmath>
#include <cstring>

namespace engine
{
    struct Vec2f
    {
        float x = 0, y = 0;
        Vec2f() = default;
        Vec2f(float x_, float y_) : x(x_), y(y_) {}
    };

    struct Vec4f
    {
        float x = 0, y = 0, z = 0, w = 0;
        Vec4f() = default;
        Vec4f(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
        Vec4f(const blitz::Vector &v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}

        Vec4f operator*(float s) const { return Vec4f(x * s, y * s, z * s, w * s); }
        Vec4f operator+(const Vec4f &o) const { return Vec4f(x + o.x, y + o.y, z + o.z, w + o.w); }
    };

    struct Matrix4
    {
        Vec4f col0, col1, col2, col3;

        Matrix4()
            : col0(1, 0, 0, 0), col1(0, 1, 0, 0), col2(0, 0, 1, 0), col3(0, 0, 0, 1) {}
        Matrix4(const Vec4f &c0, const Vec4f &c1, const Vec4f &c2, const Vec4f &c3)
            : col0(c0), col1(c1), col2(c2), col3(c3) {}

        static Matrix4 fromBlitz(const blitz::Matrix &m, const blitz::Vector &pos)
        {
            return Matrix4(Vec4f(m.i, 0.0f), Vec4f(m.j, 0.0f), Vec4f(m.k, 0.0f), Vec4f(pos, 1.0f));
        }
        static Matrix4 fromBlitz(const blitz::Transform &t) { return fromBlitz(t.m, t.v); }

        static Matrix4 identity() { return Matrix4(); }
        static Matrix4 translation(const blitz::Vector &t)
        {
            return Matrix4(Vec4f(1, 0, 0, 0), Vec4f(0, 1, 0, 0), Vec4f(0, 0, 1, 0), Vec4f(t, 1.0f));
        }
        static Matrix4 scale(const blitz::Vector &s)
        {
            return Matrix4(Vec4f(s.x, 0, 0, 0), Vec4f(0, s.y, 0, 0), Vec4f(0, 0, s.z, 0), Vec4f(0, 0, 0, 1));
        }
        static Matrix4 ortho(float left, float right, float bottom, float top, float nearZ, float farZ)
        {
            Matrix4 r;
            r.col0.x = 2.0f / (right - left);
            r.col1.y = 2.0f / (top - bottom);
            r.col2.z = -2.0f / (farZ - nearZ);
            r.col3.x = -(right + left) / (right - left);
            r.col3.y = -(top + bottom) / (top - bottom);
            r.col3.z = -(farZ + nearZ) / (farZ - nearZ);
            return r;
        }

        static Matrix4 perspective(float fovYDeg, float aspect, float nearZ, float farZ)
        {
            const float f = 1.0f / std::tan(fovYDeg * 0.5f * 3.14159265359f / 180.0f);
            Matrix4 r;
            r.col0 = Vec4f(f / aspect, 0, 0, 0);
            r.col1 = Vec4f(0, f, 0, 0);
            r.col2 = Vec4f(0, 0, (farZ + nearZ) / (nearZ - farZ), -1.0f);
            r.col3 = Vec4f(0, 0, (2.0f * farZ * nearZ) / (nearZ - farZ), 0);
            return r;
        }

        static Matrix4 lookAt(const blitz::Vector &eye, const blitz::Vector &target, const blitz::Vector &up)
        {
            blitz::Vector f = (target - eye); f = f * (1.0f / f.length());
            blitz::Vector s = f.cross(up); s = s * (1.0f / s.length());
            blitz::Vector u = s.cross(f);
            Matrix4 r;
            r.col0 = Vec4f(s.x, u.x, -f.x, 0);
            r.col1 = Vec4f(s.y, u.y, -f.y, 0);
            r.col2 = Vec4f(s.z, u.z, -f.z, 0);
            r.col3 = Vec4f(-s.dot(eye), -u.dot(eye), f.dot(eye), 1);
            return r;
        }

        Vec4f operator*(const Vec4f &v) const
        {
            return col0 * v.x + col1 * v.y + col2 * v.z + col3 * v.w;
        }
        Matrix4 operator*(const Matrix4 &o) const
        {
            return Matrix4(*this * o.col0, *this * o.col1, *this * o.col2, *this * o.col3);
        }

        blitz::Vector transformPoint(const blitz::Vector &p) const
        {
            Vec4f r = *this * Vec4f(p, 1.0f);
            return blitz::Vector(r.x, r.y, r.z);
        }

        const float *data() const { return &col0.x; }
    };
}

#endif
