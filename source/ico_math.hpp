#pragma once
// Минимальная математика для лабораторной. Никаких зависимостей.
//
// ВАЖНО: матрица хранится ПО СТОЛБЦАМ: m[столбец][строка].
// Именно так хранит mat4 GLSL, поэтому массив можно копировать в uniform-буфер
// через memcpy без транспонирования.

#include <cmath>
#include <cstring>

namespace ico {

struct Mat4 {
    float m[4][4]; // m[столбец][строка]
};

inline Mat4 identity() {
    Mat4 r;
    std::memset(&r, 0, sizeof(r));
    r.m[0][0] = r.m[1][1] = r.m[2][2] = r.m[3][3] = 1.0f;
    return r;
}

// Произведение a * b (сначала применяется b, потом a)
inline Mat4 mul(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int c = 0; c < 4; c++) {
        for (int row = 0; row < 4; row++) {
            float s = 0.0f;
            for (int k = 0; k < 4; k++) s += a.m[k][row] * b.m[c][k];
            r.m[c][row] = s;
        }
    }
    return r;
}

inline Mat4 translate(float x, float y, float z) {
    Mat4 r = identity();
    r.m[3][0] = x; r.m[3][1] = y; r.m[3][2] = z;
    return r;
}

inline Mat4 scale(float x, float y, float z) {
    Mat4 r = identity();
    r.m[0][0] = x; r.m[1][1] = y; r.m[2][2] = z;
    return r;
}

inline Mat4 rotate_x(float a) {
    Mat4 r = identity();
    float c = std::cos(a), s = std::sin(a);
    r.m[1][1] = c;  r.m[2][1] = -s;
    r.m[1][2] = s;  r.m[2][2] = c;
    return r;
}

inline Mat4 rotate_y(float a) {
    Mat4 r = identity();
    float c = std::cos(a), s = std::sin(a);
    r.m[0][0] = c;  r.m[2][0] = s;
    r.m[0][2] = -s; r.m[2][2] = c;
    return r;
}

inline Mat4 rotate_z(float a) {
    Mat4 r = identity();
    float c = std::cos(a), s = std::sin(a);
    r.m[0][0] = c;  r.m[1][0] = -s;
    r.m[0][1] = s;  r.m[1][1] = c;
    return r;
}

// Перспективная проекция для Vulkan (см. лекцию 1):
//  - камера в начале координат смотрит вдоль +Z, X вправо, Y вверх;
//  - глубина после деления на w лежит в [0, 1];
//  - ось Y перевёрнута (-1/tan), потому что в Vulkan Y экрана направлена вниз,
//    и мы хотим, чтобы +Y мира был НАД центром экрана.
inline Mat4 perspective(float fov_y_radians, float aspect, float near_z, float far_z) {
    float t = std::tan(fov_y_radians * 0.5f);
    Mat4 r;
    std::memset(&r, 0, sizeof(r));
    r.m[0][0] = 1.0f / (aspect * t);
    r.m[1][1] = -1.0f / t;
    r.m[2][2] = far_z / (far_z - near_z);
    r.m[2][3] = 1.0f;                                   // w' = z
    r.m[3][2] = -far_z * near_z / (far_z - near_z);
    return r;
}

// Ортографическая проекция для Vulkan. Пригодится для доп. задания 1.
// Параметры: границы видимой коробки в пространстве камеры.
inline Mat4 orthographic(float left, float right, float bottom, float top,
                         float near_z, float far_z) {
    Mat4 r = identity();
    r.m[0][0] = 2.0f / (right - left);
    r.m[1][1] = -2.0f / (top - bottom);                 // тот же переворот Y
    r.m[2][2] = 1.0f / (far_z - near_z);
    r.m[3][0] = -(right + left) / (right - left);
    r.m[3][1] = (top + bottom) / (top - bottom);
    r.m[3][2] = -near_z / (far_z - near_z);
    return r;
}

} // namespace ico
