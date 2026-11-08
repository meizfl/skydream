/* SPDX-License-Identifier: GPL-3.0-only
 * Native row-vector D3DX mathematics, with alias-safe outputs. */
#include "d3dx_math.h"
#include <math.h>
#include <string.h>
float *native_d3dx_vec3_normalize(float *out, const float *v) {
  float n = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  float t[3] = {0};
  if (n)
    for (int i = 0; i < 3; i++)
      t[i] = v[i] / n;
  memcpy(out, t, sizeof t);
  return out;
}
float *native_d3dx_vec3_coord(float *out, const float *v, const float *m) {
  float t[3], w = v[0] * m[3] + v[1] * m[7] + v[2] * m[11] + m[15];
  for (int i = 0; i < 3; i++)
    t[i] = (v[0] * m[i] + v[1] * m[4 + i] + v[2] * m[8 + i] + m[12 + i]) / w;
  memcpy(out, t, sizeof t);
  return out;
}
float *native_d3dx_vec3_normal(float *out, const float *v, const float *m) {
  float t[3];
  for (int i = 0; i < 3; i++)
    t[i] = v[0] * m[i] + v[1] * m[4 + i] + v[2] * m[8 + i];
  memcpy(out, t, sizeof t);
  return out;
}
float *native_d3dx_plane_normalize(float *out, const float *v) {
  float n = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]), t[4] = {0};
  if (n)
    for (int i = 0; i < 4; i++)
      t[i] = v[i] / n;
  memcpy(out, t, sizeof t);
  return out;
}
float *native_d3dx_plane_transform(float *out, const float *v, const float *m) {
  float t[4];
  for (int i = 0; i < 4; i++)
    t[i] = v[0] * m[i] + v[1] * m[4 + i] + v[2] * m[8 + i] + v[3] * m[12 + i];
  memcpy(out, t, sizeof t);
  return out;
}
float *native_d3dx_matrix_multiply(float *out, const float *a, const float *b) {
  float t[16];
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++)
      t[r * 4 + c] = a[r * 4] * b[c] + a[r * 4 + 1] * b[4 + c] +
                     a[r * 4 + 2] * b[8 + c] + a[r * 4 + 3] * b[12 + c];
  memcpy(out, t, sizeof t);
  return out;
}
float *native_d3dx_matrix_transpose(float *out, const float *a) {
  float t[16];
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++)
      t[r * 4 + c] = a[c * 4 + r];
  memcpy(out, t, sizeof t);
  return out;
}
float *native_d3dx_matrix_multiply_transpose(float *out, const float *a,
                                             const float *b) {
  float t[16];
  native_d3dx_matrix_multiply(t, a, b);
  return native_d3dx_matrix_transpose(out, t);
}
float *native_d3dx_matrix_inverse(float *out, float *det, const float *a) {
  double t[4][8], d = 1;
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 8; c++)
      t[r][c] = c < 4 ? a[r * 4 + c] : (c - 4 == r);
  for (int c = 0; c < 4; c++) {
    int p = c;
    for (int r = c + 1; r < 4; r++)
      if (fabs(t[r][c]) > fabs(t[p][c]))
        p = r;
    if (!t[p][c]) {
      if (det)
        *det = 0;
      return NULL;
    }
    if (p != c) {
      for (int k = 0; k < 8; k++) {
        double v = t[c][k];
        t[c][k] = t[p][k];
        t[p][k] = v;
      }
      d = -d;
    }
    double v = t[c][c];
    d *= v;
    for (int k = 0; k < 8; k++)
      t[c][k] /= v;
    for (int r = 0; r < 4; r++)
      if (r != c) {
        v = t[r][c];
        for (int k = 0; k < 8; k++)
          t[r][k] -= v * t[c][k];
      }
  }
  if (det)
    *det = (float)d;
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++)
      out[r * 4 + c] = (float)t[r][c + 4];
  return out;
}
