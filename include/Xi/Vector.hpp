#ifndef XI_Vector
#define XI_Vector

// --- Standard C++ ---
#include <type_traits>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>

// --- Xi ---
#include "Array.hpp"
#include "String.hpp"
#include "Math.hpp"

namespace Xi
{
    // --- Math ---
    struct Vector2
    {
        f32 x, y;
    };
    struct Vector3
    {
        f32 x, y, z;
    };
    struct Vector4
    {
        f32 x, y, z, w;
    };

    struct Matrix4
    {
        f32 m[4][4];

        static Matrix4 identity()
        {
            Matrix4 r = {0};
            r.m[0][0] = 1;
            r.m[1][1] = 1;
            r.m[2][2] = 1;
            r.m[3][3] = 1;
            return r;
        }

        static Matrix4 translate(f32 x, f32 y, f32 z)
        {
            Matrix4 r = identity();
            r.m[3][0] = x;
            r.m[3][1] = y;
            r.m[3][2] = z;
            return r;
        }

        static Matrix4 rotateY(f32 rad)
        {
            Matrix4 r = identity();
            f32 c = cos(rad);
            f32 s = sin(rad);
            r.m[0][0] = c;
            r.m[0][2] = -s;
            r.m[2][0] = s;
            r.m[2][2] = c;
            return r;
        }

        static Matrix4 rotateX(f32 rad)
        {
            Matrix4 r = identity();
            f32 c = cos(rad);
            f32 s = sin(rad);
            r.m[1][1] = c;
            r.m[1][2] = s;
            r.m[2][1] = -s;
            r.m[2][2] = c;
            return r;
        }

        static Matrix4 multiply(const Matrix4 &a, const Matrix4 &b)
        {
            Matrix4 r = {0};
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 4; ++j)
                    for (int k = 0; k < 4; ++k)
                        r.m[i][j] += a.m[i][k] * b.m[k][j];
            return r;
        }

        static Matrix4 transpose(const Matrix4 &in)
        {
            Matrix4 out;
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c)
                    out.m[r][c] = in.m[c][r];
            return out;
        }

        // Vulkan Perspective (Y-Flip)
        static Matrix4 perspective(f32 fov, f32 ar, f32 n, f32 f)
        {
            Matrix4 r = {0};
            f32 t = tan(fov / 2);
            r.m[0][0] = 1.0f / (ar * t);
            r.m[1][1] = -1.0f / t;
            r.m[2][2] = f / (f - n);
            r.m[2][3] = 1.0f;
            r.m[3][2] = -(f * n) / (f - n);
            return r;
        }

        static Matrix4 lookAt(Vector3 eye, Vector3 center, Vector3 up)
        {
            auto norm = [](Vector3 v)
            {f32 l=sqrt(v.x*v.x+v.y*v.y+v.z*v.z); return (l==0)?Vector3{}:Vector3{v.x/l,v.y/l,v.z/l}; };
            Vector3 z = norm({center.x - eye.x, center.y - eye.y, center.z - eye.z});
            Vector3 x = norm({up.y * z.z - up.z * z.y, up.z * z.x - up.x * z.z, up.x * z.y - up.y * z.x});
            Vector3 y = {z.y * x.z - z.z * x.y, z.z * x.x - z.x * x.z, z.x * x.y - z.y * x.x};
            Matrix4 r = identity();
            r.m[0][0] = x.x;
            r.m[0][1] = y.x;
            r.m[0][2] = z.x;
            r.m[1][0] = x.y;
            r.m[1][1] = y.y;
            r.m[1][2] = z.y;
            r.m[2][0] = x.z;
            r.m[2][1] = y.z;
            r.m[2][2] = z.z;
            r.m[3][0] = -(x.x * eye.x + x.y * eye.y + x.z * eye.z);
            r.m[3][1] = -(y.x * eye.x + y.y * eye.y + y.z * eye.z);
            r.m[3][2] = -(z.x * eye.x + z.y * eye.y + z.z * eye.z);
            return r;
        }

        static Matrix4 ortho(f32 left, f32 right, f32 bottom, f32 top, f32 n, f32 f)
        {
            Matrix4 r = {0};
            r.m[0][0] = 2.0f / (right - left);
            r.m[1][1] = 2.0f / (top - bottom);
            r.m[2][2] = 1.0f / (f - n);
            r.m[3][0] = -(right + left) / (right - left);
            r.m[3][1] = -(top + bottom) / (top - bottom);
            r.m[3][2] = -n / (f - n);
            r.m[3][3] = 1.0f;
            return r;
        }

        Matrix4 operator*(const Matrix4 &r) const { return multiply(*this, r); }
    };

    // --- Transform ---
    struct Transform3
    {
        Vector3 position = {0, 0, 0};
        Vector3 rotation = {0, 0, 0};
        Vector3 scale = {1, 1, 1};
        u32 transformVersion = 1;

        void touch()
        {
            transformVersion++;
            if (transformVersion == 0)
                transformVersion = 1;
        }
        Matrix4 getMatrix() const
        {
            return Matrix4::rotateX(rotation.x) * Matrix4::rotateY(rotation.y) * Matrix4::translate(position.x, position.y, position.z);
        }

        void lookAt(Vector3 target, Vector3 up = {0, 1, 0})
        {
            // 1. Calculer le vecteur directionnel
            Vector3 direction = {
                target.x - position.x,
                target.y - position.y,
                target.z - position.z};

            // 2. Calculer la distance horizontale (plan XZ)
            float horizontalDistance = sqrt(direction.x * direction.x + direction.z * direction.z);

            // 3. Calculer les angles d'Euler
            // Pitch (Rotation X) : inclinaison vers le haut ou le bas
            rotation.x = -atan2(direction.y, horizontalDistance);

            // Yaw (Rotation Y) : orientation gauche ou droite
            // Note : On ajoute PI selon l'orientation par défaut de votre modèle
            rotation.y = atan2(direction.x, direction.z);

            // 4. Marquer la transformation comme modifiée
            this->touch();
        }
    };
}

#endif