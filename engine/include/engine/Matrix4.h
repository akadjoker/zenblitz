/*
** Matrix4.h — the 4x4, column-major matrix the GPU pipeline needs
** (uniform buffer layout, orthographic projection, point transform).
**
** blitz::Matrix (Geom.h) is 3x3 + a separate translation, which is what
** Blitz3D's own commands (Position/Rotate/Move/TFormPoint) and the ported
** engine code (entity/world/camera/collision) already expect — brought in
** as-is rather than replaced, so nothing outside this one file needs a
** different math convention. Matrix4 exists only because a GPU shader
** wants one 4x4 block of 16 floats; the original had exactly the same
** split (Matrix vs. D3DMATRIX, converted only at draw time in
** gxruntime/gxscene.cpp) — this is that same boundary, kept local to
** engine/render rather than pulled in as a general-purpose math library.
**
** Layout and formulas here are deliberately the same as a GPU expects
** (column-major, OpenGL-style orthographic projection) — this is standard
** graphics math, not a specific library's API; it happens to match what
** akadjoker/math's Mat4 also implements, verified against it while writing
** this rather than copied wholesale.
*/
#ifndef ENGINE_MATRIX4_H
#define ENGINE_MATRIX4_H

#include "engine/Geom.h"
#include <cstring>

namespace engine
{
    /* 2D point/UV — Batch's texture-coordinate parameters. blitz::Vector is
       3-component, so this is the plain pair the 2D side needs instead. */
    struct Vec2f
    {
        float x = 0, y = 0;
        Vec2f() = default;
        Vec2f(float x_, float y_) : x(x_), y(y_) {}
    };

    /* One column: x,y,z,w. Plain struct, no SIMD — this matrix is built a
       few times a frame (once per Flip/pushMatrix), not per-vertex. */
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

        /* combines a blitz::Matrix (rotation/scale) with a translation —
           the exact split Transform (Matrix m + Vector v) already keeps,
           and the same combination the original's D3DMATRIX conversion
           did at draw time. */
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
        /* eye-space column-major orthographic projection, right-handed,
           z mapped to [-1, 1] — the OpenGL glOrtho convention. */
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

        /* raw column-major float[16], ready for a GPU uniform buffer —
           col0 first, x/y/z/w within each column, matching the layout a
           shader's `mat4` expects and what sizeof(Matrix4) reports (16
           floats, no padding: Vec4f has no alignment requirement beyond
           float, so this struct is exactly 64 bytes). */
        const float *data() const { return &col0.x; }
    };
}

#endif
