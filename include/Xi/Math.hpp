#ifndef XI_MATH_HPP
#define XI_MATH_HPP

#include "Primitives.hpp"
#include <cmath>

namespace Xi {
// Forward declarations
template <typename T> class Array;

struct Vector2 {
  f32 x, y;
};
struct Vector3 {
  f32 x, y, z;
};
struct Vector4 {
  f32 x, y, z, w;
};

struct Matrix4 {
  f32 m[4][4];

  static inline Matrix4 identity();
  static inline Matrix4 translate(f32 x, f32 y, f32 z);
  static inline Matrix4 rotateX(f32 rad);
  static inline Matrix4 rotateY(f32 rad);
  static inline Matrix4 rotateZ(f32 rad);
  static inline Matrix4 lookAt(Vector3 eye, Vector3 center, Vector3 up);
  static inline Matrix4 perspective(f32 fov, f32 ar, f32 n, f32 f);
  static inline Matrix4 ortho(f32 l, f32 r, f32 b, f32 t, f32 n, f32 f);
  static inline Matrix4 transpose(const Matrix4 &in);
  static inline Matrix4 multiply(const Matrix4 &a, const Matrix4 &b);
  static inline Matrix4 inverse(const Matrix4 &m);
  static inline f32 det(const Matrix4 &m);

  inline Matrix4 operator*(const Matrix4 &other) const {
    return multiply(*this, other);
  }
};

using Tensor = Array<f32>;

namespace Math {
// --- Scalar Base Functions ---
#define SC_W(name, func)                                                       \
  inline f32 name(f32 x) { return func(x); }

SC_W(sin, __builtin_sinf)
SC_W(cos, __builtin_cosf) SC_W(tan, __builtin_tanf) SC_W(asin, __builtin_asinf)
    SC_W(acos, __builtin_acosf) SC_W(atan, __builtin_atanf)
        SC_W(sinh, __builtin_sinhf) SC_W(cosh, __builtin_coshf)
            SC_W(tanh, __builtin_tanhf) SC_W(asinh, __builtin_asinhf)
                SC_W(acosh, __builtin_acoshf) SC_W(atanh, __builtin_atanhf)
                    SC_W(exp, __builtin_expf) SC_W(log, __builtin_logf)
                        SC_W(log10, __builtin_log10f)
                            SC_W(log2, __builtin_log2f)
                                SC_W(sqrt, __builtin_sqrtf) inline f32
    sqr(f32 x) {
  return x * x;
}
inline i32 floor(f32 x) { return (i32)__builtin_floorf(x); }
inline i32 ceil(f32 x) { return (i32)__builtin_ceilf(x); }
inline i32 round(f32 x) { return (i32)__builtin_roundf(x); }
inline f32 floor(f32 x, int) { return __builtin_floorf(x); }
inline f32 ceil(f32 x, int) { return __builtin_ceilf(x); }
inline f32 round(f32 x, int) { return __builtin_roundf(x); }
inline f32 abs(f32 x) { return __builtin_fabsf(x); }
inline f32 sgn(f32 x) {
  return (x > 0.0f) ? 1.0f : ((x < 0.0f) ? -1.0f : 0.0f);
}
inline f32 min(f32 a, f32 b) { return (a < b) ? a : b; }
inline f32 max(f32 a, f32 b) { return (a > b) ? a : b; }
inline f32 clamp(f32 v, f32 mn, f32 mx) { return min(max(v, mn), mx); }
inline f32 pow(f32 b, f32 e) { return __builtin_powf(b, e); }
inline f32 inverse(f32 x) { return 1.0f / x; }
inline f32 relu(f32 x) { return max(0.0f, x); }
inline f32 sigmoid(f32 x) { return 1.0f / (1.0f + __builtin_expf(-x)); }

// --- Vector Overloads ---
#define VC_W(name)                                                             \
  inline Vector2 name(Vector2 v) {                                             \
    return {Xi::Math::name(v.x), Xi::Math::name(v.y)};                         \
  }                                                                            \
  inline Vector3 name(Vector3 v) {                                             \
    return {Xi::Math::name(v.x), Xi::Math::name(v.y), Xi::Math::name(v.z)};    \
  }                                                                            \
  inline Vector4 name(Vector4 v) {                                             \
    return {Xi::Math::name(v.x), Xi::Math::name(v.y), Xi::Math::name(v.z),     \
            Xi::Math::name(v.w)};                                              \
  }

VC_W(sin)
VC_W(cos) VC_W(tan) VC_W(asin) VC_W(acos) VC_W(atan) VC_W(sinh) VC_W(cosh)
    VC_W(tanh) VC_W(asinh) VC_W(acosh) VC_W(atanh) VC_W(exp) VC_W(log)
        VC_W(log10) VC_W(log2) VC_W(sqrt) VC_W(sqr) VC_W(abs) VC_W(sgn)
            VC_W(inverse) VC_W(relu) VC_W(sigmoid)

                inline Vector2 floor(Vector2 v, int i) {
  return {(f32)floor(v.x, i), (f32)floor(v.y, i)};
}
inline Vector3 floor(Vector3 v, int i) {
  return {(f32)floor(v.x, i), (f32)floor(v.y, i), (f32)floor(v.z, i)};
}
inline Vector4 floor(Vector4 v, int i) {
  return {(f32)floor(v.x, i), (f32)floor(v.y, i), (f32)floor(v.z, i),
          (f32)floor(v.w, i)};
}

// --- Tensor Reduction Kernels ---
inline f32 sum(Vector2 v) { return v.x + v.y; }
inline f32 sum(Vector3 v) { return v.x + v.y + v.z; }
inline f32 sum(Vector4 v) { return v.x + v.y + v.z + v.w; }
inline f32 mean(Vector2 v) { return sum(v) / 2.0f; }
inline f32 mean(Vector3 v) { return sum(v) / 3.0f; }
inline f32 mean(Vector4 v) { return sum(v) / 4.0f; }

template <typename Arr>
auto sum(const Arr &a) ->
    typename RemoveRef<decltype(const_cast<Arr &>(a)[0])>::Type {
  typename RemoveRef<decltype(const_cast<Arr &>(a)[0])>::Type res = 0;
  for (usz i = 0; i < a.size(); ++i)
    res += a[i];
  return res;
}

template <typename Arr>
auto mean(const Arr &a) ->
    typename RemoveRef<decltype(const_cast<Arr &>(a)[0])>::Type {
  usz n = a.size();
  if (n == 0)
    return 0;
  return sum(a) / (f32)n;
}

template <typename Arr>
auto var(const Arr &a) ->
    typename RemoveRef<decltype(const_cast<Arr &>(a)[0])>::Type {
  usz n = a.size();
  if (n < 2)
    return 0;
  auto m = mean(a);
  auto res = (decltype(m))0;
  for (usz i = 0; i < n; ++i) {
    auto d = a[i] - m;
    res += d * d;
  }
  return res / (f32)n;
}

template <typename Arr>
auto std(const Arr &a) ->
    typename RemoveRef<decltype(const_cast<Arr &>(a)[0])>::Type {
  return Xi::Math::sqrt(var(a));
}

template <typename Arr>
auto min(const Arr &a) ->
    typename RemoveRef<decltype(const_cast<Arr &>(a)[0])>::Type {
  if (a.size() == 0)
    return 0;
  auto m = a[0];
  for (usz i = 1; i < a.size(); ++i)
    if (a[i] < m)
      m = a[i];
  return m;
}

template <typename Arr>
auto max(const Arr &a) ->
    typename RemoveRef<decltype(const_cast<Arr &>(a)[0])>::Type {
  if (a.size() == 0)
    return 0;
  auto m = a[0];
  for (usz i = 1; i < a.size(); ++i)
    if (a[i] > m)
      m = a[i];
  return m;
}

template <typename Arr> f32 norm(const Arr &a, f32 p = 2.0f) {
  f32 res = 0;
  usz n = a.size();
  if (p == 1.0f) {
    for (usz i = 0; i < n; ++i)
      res += Xi::Math::abs(a[i]);
  } else if (p == 2.0f) {
    for (usz i = 0; i < n; ++i)
      res += a[i] * a[i];
    res = Xi::Math::sqrt(res);
  } else {
    for (usz i = 0; i < n; ++i)
      res += Xi::Math::pow(Xi::Math::abs(a[i]), p);
    res = Xi::Math::pow(res, 1.0f / p);
  }
  return res;
}

// --- Tensor (Element-wise) ---
#define TS_W(name)                                                             \
  template <typename T> Array<T> name(const Array<T> &a) {                     \
    Array<T> res;                                                              \
    res.allocate(a.size());                                                    \
    for (usz i = 0; i < a.size(); ++i)                                         \
      res[i] = Xi::Math::name(a[i]);                                           \
    return res;                                                                \
  }

TS_W(sin)
TS_W(cos) TS_W(tan) TS_W(asin) TS_W(acos) TS_W(atan) TS_W(sinh) TS_W(cosh)
    TS_W(tanh) TS_W(asinh) TS_W(acosh) TS_W(atanh) TS_W(exp) TS_W(log)
        TS_W(log10) TS_W(log2) TS_W(sqrt) TS_W(sqr) TS_W(abs) TS_W(sgn)
            TS_W(inverse) TS_W(relu) TS_W(sigmoid)

                template <typename Arr>
                Arr softmax(const Arr &a) {
  Arr res = a;
  usz n = a.size();
  if (n == 0)
    return res;
  auto m = max(a);
  f32 s = 0;
  for (usz i = 0; i < n; ++i) {
    res[i] = Xi::Math::exp(a[i] - m);
    s += res[i];
  }
  for (usz i = 0; i < n; ++i)
    res[i] /= s;
  return res;
}

// --- Tensor Join ---
template <typename T>
Array<T> concatT(const Array<Array<T>> &arrays, u32 axis = 0) {
  usz total = 0;
  for (usz i = 0; i < arrays.size(); ++i)
    total += arrays[i].size();
  Array<T> res;
  res.allocate(total);
  usz offset = 0;
  for (usz i = 0; i < arrays.size(); ++i) {
    for (usz j = 0; j < arrays[i].size(); ++j)
      res[offset++] = arrays[i][j];
  }
  return res;
}

template <typename T>
Array<T> stackT(const Array<Array<T>> &arrays, u32 axis = 0) {
  // For 1D into 2D (flat), same as concat. Semantic differs in multi-dim.
  return concatT(arrays, axis);
}

// --- Linear Algebra ---
inline f32 dot(Vector3 a, Vector3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

template <typename Arr> f32 dot(const Arr &a, const Arr &b) {
  f32 res = 0;
  usz n = a.size() < b.size() ? a.size() : b.size();
  for (usz i = 0; i < n; ++i)
    res += a[i] * b[i];
  return res;
}

template <typename Arr>
Arr matmul(const Arr &a, const Arr &b, usz M, usz N, usz P) {
  Arr res;
  res.allocate(M * P);
  for (usz i = 0; i < M; ++i) {
    for (usz j = 0; j < P; ++j) {
      f32 s = 0;
      for (usz k = 0; k < N; ++k)
        s += a[i * N + k] * b[k * P + j];
      res[i * P + j] = s;
    }
  }
  return res;
}

// Matrix4 specialized functions in Math
inline Matrix4 inverse(const Matrix4 &m) { return Matrix4::inverse(m); }
inline f32 det(const Matrix4 &m) { return Matrix4::det(m); }

// --- Transformation Wrappers ---
inline Matrix4 identity() { return Matrix4::identity(); }
inline Matrix4 translate(f32 x, f32 y, f32 z) {
  return Matrix4::translate(x, y, z);
}
inline Matrix4 rotateX(f32 rad) { return Matrix4::rotateX(rad); }
inline Matrix4 rotateY(f32 rad) { return Matrix4::rotateY(rad); }
inline Matrix4 rotateZ(f32 rad) { return Matrix4::rotateZ(rad); }
inline Matrix4 lookAt(Vector3 eye, Vector3 center, Vector3 up) {
  return Matrix4::lookAt(eye, center, up);
}
inline Matrix4 perspective(f32 fov, f32 ar, f32 n, f32 f) {
  return Matrix4::perspective(fov, ar, n, f);
}
inline Matrix4 orthographic(f32 l, f32 r, f32 b, f32 t, f32 n, f32 f) {
  return Matrix4::ortho(l, r, b, t, n, f);
}
inline Matrix4 ortho(f32 l, f32 r, f32 b, f32 t, f32 n, f32 f) {
  return Matrix4::ortho(l, r, b, t, n, f);
}
} // namespace Math

// --- Matrix4 Implementation ---
inline Matrix4 Matrix4::identity() {
  Matrix4 r = {0};
  r.m[0][0] = 1;
  r.m[1][1] = 1;
  r.m[2][2] = 1;
  r.m[3][3] = 1;
  return r;
}
inline Matrix4 Matrix4::translate(f32 x, f32 y, f32 z) {
  Matrix4 r = identity();
  r.m[3][0] = x;
  r.m[3][1] = y;
  r.m[3][2] = z;
  return r;
}
inline Matrix4 Matrix4::rotateX(f32 rad) {
  Matrix4 r = identity();
  f32 c = Math::cos(rad), s = Math::sin(rad);
  r.m[1][1] = c;
  r.m[1][2] = s;
  r.m[2][1] = -s;
  r.m[2][2] = c;
  return r;
}
inline Matrix4 Matrix4::rotateY(f32 rad) {
  Matrix4 r = identity();
  f32 c = Math::cos(rad), s = Math::sin(rad);
  r.m[0][0] = c;
  r.m[0][2] = -s;
  r.m[2][0] = s;
  r.m[2][2] = c;
  return r;
}
inline Matrix4 Matrix4::rotateZ(f32 rad) {
  Matrix4 r = identity();
  f32 c = Math::cos(rad), s = Math::sin(rad);
  r.m[0][0] = c;
  r.m[0][1] = s;
  r.m[1][0] = -s;
  r.m[1][1] = c;
  return r;
}
inline Matrix4 Matrix4::transpose(const Matrix4 &in) {
  Matrix4 out;
  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 4; ++c)
      out.m[r][c] = in.m[c][r];
  return out;
}
inline Matrix4 Matrix4::multiply(const Matrix4 &a, const Matrix4 &b) {
  Matrix4 r = {0};
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j)
      for (int k = 0; k < 4; ++k)
        r.m[i][j] += a.m[i][k] * b.m[k][j];
  return r;
}
inline f32 Matrix4::det(const Matrix4 &m) {
  // Simplified 4x4 determinant
  f32 res = 0;
  // ... recursive or closed form ...
  // We'll provide a basic implementation for now
  return m.m[0][0] * m.m[1][1] * m.m[2][2] * m.m[3][3]; // Placeholder
}
inline Matrix4 Matrix4::inverse(const Matrix4 &m) {
  // Placeholder for matrix inverse
  return m;
}
inline Matrix4 Matrix4::lookAt(Vector3 eye, Vector3 center, Vector3 up) {
  auto norm = [](Vector3 v) {
    f32 l = Math::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return (l == 0) ? Vector3{0, 0, 0} : Vector3{v.x / l, v.y / l, v.z / l};
  };
  auto cross = [](Vector3 a, Vector3 b) {
    return Vector3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
                   a.x * b.y - a.y * b.x};
  };
  Vector3 f = norm({center.x - eye.x, center.y - eye.y, center.z - eye.z});
  Vector3 s = norm(cross(f, up));
  Vector3 u = cross(s, f);
  Matrix4 r = identity();
  r.m[0][0] = s.x;
  r.m[1][0] = s.y;
  r.m[2][0] = s.z;
  r.m[0][1] = u.x;
  r.m[1][1] = u.y;
  r.m[2][1] = u.z;
  r.m[0][2] = -f.x;
  r.m[1][2] = -f.y;
  r.m[2][2] = -f.z;
  r.m[3][0] = -Math::dot(s, eye);
  r.m[3][1] = -Math::dot(u, eye);
  r.m[3][2] = Math::dot(f, eye);
  return r;
}
inline Matrix4 Matrix4::perspective(f32 fov, f32 ar, f32 n, f32 f) {
  f32 thf = Math::tan(fov / 2.0f);
  Matrix4 r = {0};
  r.m[0][0] = 1.0f / (ar * thf);
  r.m[1][1] = 1.0f / thf;
  r.m[2][2] = -(f + n) / (f - n);
  r.m[2][3] = -1.0f;
  r.m[3][2] = -(2.0f * f * n) / (f - n);
  return r;
}
inline Matrix4 Matrix4::ortho(f32 l, f32 r, f32 b, f32 t, f32 n, f32 f) {
  Matrix4 res = identity();
  res.m[0][0] = 2.0f / (r - l);
  res.m[1][1] = 2.0f / (t - b);
  res.m[2][2] = -2.0f / (f - n);
  res.m[3][0] = -(r + l) / (r - l);
  res.m[3][1] = -(t + b) / (t - b);
  res.m[3][2] = -(f + n) / (f - n);
  return res;
}

// --- Vector Operators ---
inline Vector2 operator+(Vector2 a, Vector2 b) {
  return {a.x + b.x, a.y + b.y};
}
inline Vector2 operator-(Vector2 a, Vector2 b) {
  return {a.x - b.x, a.y - b.y};
}
inline Vector2 operator*(Vector2 a, f32 s) { return {a.x * s, a.y * s}; }
inline Vector2 operator/(Vector2 a, f32 s) { return {a.x / s, a.y / s}; }
inline Vector3 operator+(Vector3 a, Vector3 b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline Vector3 operator-(Vector3 a, Vector3 b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline Vector3 operator*(Vector3 a, f32 s) {
  return {a.x * s, a.y * s, a.z * s};
}
inline Vector3 operator/(Vector3 a, f32 s) {
  return {a.x / s, a.y / s, a.z / s};
}
inline Vector4 operator+(Vector4 a, Vector4 b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
}
inline Vector4 operator-(Vector4 a, Vector4 b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w};
}
inline Vector4 operator*(Vector4 a, f32 s) {
  return {a.x * s, a.y * s, a.z * s, a.w * s};
}
inline Vector4 operator/(Vector4 a, f32 s) {
  return {a.x / s, a.y / s, a.z / s, a.w / s};
}
} // namespace Xi

#endif