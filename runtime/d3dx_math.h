/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
float *native_d3dx_vec3_normalize(float *, const float *);
float *native_d3dx_vec3_coord(float *, const float *, const float *);
float *native_d3dx_vec3_normal(float *, const float *, const float *);
float *native_d3dx_plane_normalize(float *, const float *);
float *native_d3dx_plane_transform(float *, const float *, const float *);
float *native_d3dx_matrix_multiply(float *, const float *, const float *);
float *native_d3dx_matrix_transpose(float *, const float *);
float *native_d3dx_matrix_multiply_transpose(float *, const float *,
                                             const float *);
float *native_d3dx_matrix_inverse(float *, float *, const float *);
