/* SPDX-License-Identifier: GPL-3.0-only
 * Native implementations of Skyrim's D3DX9 image/math import surface. */
#include "d3d9_bridge.h"
extern "C" {
#include "d3dx_image.h"
#include "d3dx_math.h"
}
#include <algorithm>
#include <cmath>
#include <fstream>
#include <mutex>
#include <vector>
extern HRESULT native_d3dx_surface_memory(IDirect3DSurface9 *, const RECT *,
                                          const void *, D3DFORMAT, uint32_t,
                                          const RECT *, uint32_t, uint32_t);
static constexpr HRESULT INVALID_DATA = (HRESULT)0x88760b59u;
static void called(const char *name) {
  static std::mutex lock;
  static std::vector<std::string> names;
  std::lock_guard<std::mutex> guard(lock);
  for (auto &s : names)
    if (s == name)
      return;
  names.emplace_back(name);
  LOG("D3DX: native %s", name);
}
static void put_info(uint32_t dest, const NativeD3DXImage &i) {
  if (dest) {
    uint32_t values[] = {i.width,  i.height, i.depth,      i.mips,
                         i.format, i.type,   i.file_format};
    memcpy(GPTR(dest), values, sizeof values);
  }
}
static std::vector<uint8_t> read_file(uint32_t filename) {
  char path[4096];
  if (!filename)
    return {};
  rt_host_path(GSTR(filename), path, sizeof path);
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file)
    return {};
  auto n = file.tellg();
  if (n <= 0 || n > INT32_MAX)
    return {};
  std::vector<uint8_t> bytes((size_t)n);
  file.seekg(0);
  if (!file.read((char *)bytes.data(), bytes.size()))
    return {};
  return bytes;
}
#define MATH2(name, fn)                                                        \
  SHIM(name) {                                                                 \
    fn((float *)GPTR(ARG(0)), (const float *)GPTR(ARG(1)));                    \
    RET(ARG(0), 2);                                                            \
  }
#define MATH3(name, fn)                                                        \
  SHIM(name) {                                                                 \
    fn((float *)GPTR(ARG(0)), (const float *)GPTR(ARG(1)),                     \
       (const float *)GPTR(ARG(2)));                                           \
    RET(ARG(0), 3);                                                            \
  }
MATH2(D3DXVec3Normalize, native_d3dx_vec3_normalize)
MATH3(D3DXVec3TransformCoord, native_d3dx_vec3_coord)
MATH3(D3DXVec3TransformNormal, native_d3dx_vec3_normal)
MATH2(D3DXPlaneNormalize, native_d3dx_plane_normalize)
MATH3(D3DXPlaneTransform, native_d3dx_plane_transform)
MATH2(D3DXMatrixTranspose, native_d3dx_matrix_transpose)
MATH3(D3DXMatrixMultiply, native_d3dx_matrix_multiply)
MATH3(D3DXMatrixMultiplyTranspose, native_d3dx_matrix_multiply_transpose)
SHIM(D3DXMatrixInverse) {
  float *p =
      native_d3dx_matrix_inverse((float *)GPTR(ARG(0)), (float *)GPTR(ARG(1)),
                                 (const float *)GPTR(ARG(2)));
  RET(p ? ARG(0) : 0, 3);
}
SHIM(D3DXGetImageInfoFromFileInMemory) {
  called("GetImageInfoFromFileInMemory");
  NativeD3DXImage i;
  HRESULT hr =
      native_d3dx_image_info(GPTR(ARG(0)), ARG(1), &i) ? D3D_OK : INVALID_DATA;
  if (SUCCEEDED(hr))
    put_info(ARG(2), i);
  else
    LOG("D3DX: unsupported memory image (%u bytes, magic %08x)", ARG(1),
        ARG(1) >= 4 ? M32(ARG(0)) : 0);
  RET(hr, 3);
}
SHIM(D3DXGetImageInfoFromFileA) {
  called("GetImageInfoFromFileA");
  auto bytes = read_file(ARG(0));
  NativeD3DXImage i;
  HRESULT hr = native_d3dx_image_info(bytes.data(), bytes.size(), &i)
                   ? D3D_OK
                   : INVALID_DATA;
  if (SUCCEEDED(hr))
    put_info(ARG(1), i);
  RET(hr, 2);
}
static HRESULT upload(IDirect3DTexture9 *t, uint32_t level, const uint8_t *data,
                      uint32_t pitch, uint32_t rows) {
  D3DLOCKED_RECT lock;
  HRESULT hr = t->LockRect(level, &lock, nullptr, 0);
  if (FAILED(hr))
    return hr;
  for (uint32_t r = 0; r < rows; r++)
    memcpy((uint8_t *)lock.pBits + (size_t)r * lock.Pitch,
           data + (size_t)r * pitch, std::min(pitch, (uint32_t)lock.Pitch));
  return t->UnlockRect(level);
}
static HRESULT load_texture(
    IDirect3DDevice9 *d, const void *data, size_t bytes, uint32_t type,
    uint32_t *out, uint32_t info, uint32_t requested_w = 0xffffffffu,
    uint32_t requested_h = 0xffffffffu, uint32_t requested_mips = 0xffffffffu,
    uint32_t usage = 0, D3DFORMAT requested_fmt = D3DFMT_UNKNOWN,
    D3DPOOL pool = D3DPOOL_MANAGED, uint32_t filter = 3, uint32_t key = 0) {
  if (!d || !out)
    return D3DERR_INVALIDCALL;
  *out = 0;
  NativeD3DXImage i;
  if (!native_d3dx_image_info(data, bytes, &i) || i.type != type)
    return INVALID_DATA;
  put_info(info, i);
  uint32_t width =
               requested_w && requested_w < 0xfffffffdu ? requested_w : i.width,
           height = requested_h && requested_h < 0xfffffffdu ? requested_h
                                                             : i.height;
  uint32_t mips = requested_mips && requested_mips < 0xfffffffdu
                      ? std::min(requested_mips, i.mips)
                      : i.mips;
  D3DFORMAT fmt =
      requested_fmt == D3DFMT_UNKNOWN ? (D3DFORMAT)i.format : requested_fmt;
  if (type != 3 &&
      (width != i.width || height != i.height || fmt != (D3DFORMAT)i.format))
    return D3DERR_NOTAVAILABLE;
  if (key && requested_fmt == D3DFMT_UNKNOWN)
    fmt = D3DFMT_A8R8G8B8;
  if (type == 3) {
    IDirect3DTexture9 *t = nullptr;
    HRESULT hr =
        d->CreateTexture(width, height, mips, usage, fmt, pool, &t, nullptr);
    if (FAILED(hr) && requested_fmt == D3DFMT_UNKNOWN) {
      fmt = D3DFMT_A8R8G8B8;
      hr = d->CreateTexture(width, height, mips, usage, fmt, pool, &t, nullptr);
    }
    if (FAILED(hr)) {
      LOG("D3DX: CreateTexture failed %ux%u mips=%u fmt=%u hr=%08x", width,
          height, mips, fmt, (uint32_t)hr);
      return hr;
    }
    if (i.file_format == 4) {
      for (uint32_t l = 0; l < mips; l++) {
        size_t at;
        uint32_t w, h, z, pitch, rows;
        if (!native_d3dx_dds_level(&i, bytes, 0, l, &at, &w, &h, &z, &pitch,
                                   &rows)) {
          hr = INVALID_DATA;
          break;
        }
        if (fmt == (D3DFORMAT)i.format && width == i.width &&
            height == i.height && !key)
          hr = upload(t, l, (const uint8_t *)data + at, pitch, rows);
        else {
          IDirect3DSurface9 *surface = nullptr;
          hr = t->GetSurfaceLevel(l, &surface);
          if (SUCCEEDED(hr)) {
            RECT sr = {0, 0, (LONG)w, (LONG)h};
            hr = native_d3dx_surface_memory(
                surface, nullptr, (const uint8_t *)data + at,
                (D3DFORMAT)i.format, pitch, &sr, filter, key);
            surface->Release();
          }
        }
        if (FAILED(hr))
          break;
      }
    } else {
      int w, h;
      uint8_t *pixels = native_d3dx_decode_rgba(data, bytes, &w, &h);
      if (!pixels)
        hr = INVALID_DATA;
      else {
        for (size_t p = 0; p < (size_t)w * h; p++)
          std::swap(pixels[p * 4], pixels[p * 4 + 2]);
        IDirect3DSurface9 *surface = nullptr;
        hr = t->GetSurfaceLevel(0, &surface);
        if (SUCCEEDED(hr)) {
          RECT sr = {0, 0, w, h};
          hr = native_d3dx_surface_memory(surface, nullptr, pixels,
                                          D3DFMT_A8R8G8B8, w * 4, &sr, filter,
                                          key);
          surface->Release();
        }
        native_d3dx_free_image(pixels);
      }
    }
    if (FAILED(hr)) {
      t->Release();
      return hr;
    }
    *out = d3d9_wrap(t, IFACE_IDirect3DTexture9);
    return D3D_OK;
  }
  if (type == 5) {
    if (i.width != i.height || i.file_format != 4)
      return INVALID_DATA;
    IDirect3DCubeTexture9 *t = nullptr;
    HRESULT hr =
        d->CreateCubeTexture(i.width, mips, usage, fmt, pool, &t, nullptr);
    if (FAILED(hr))
      return hr;
    for (uint32_t face = 0; face < 6 && SUCCEEDED(hr); face++)
      for (uint32_t l = 0; l < mips; l++) {
        size_t at;
        uint32_t w, h, z, pitch, rows;
        if (!native_d3dx_dds_level(&i, bytes, face, l, &at, &w, &h, &z, &pitch,
                                   &rows)) {
          hr = INVALID_DATA;
          break;
        }
        D3DLOCKED_RECT lock;
        hr = t->LockRect((D3DCUBEMAP_FACES)face, l, &lock, nullptr, 0);
        if (FAILED(hr))
          break;
        for (uint32_t r = 0; r < rows; r++)
          memcpy((uint8_t *)lock.pBits + (size_t)r * lock.Pitch,
                 (const uint8_t *)data + at + (size_t)r * pitch,
                 std::min(pitch, (uint32_t)lock.Pitch));
        hr = t->UnlockRect((D3DCUBEMAP_FACES)face, l);
        if (FAILED(hr))
          break;
      }
    if (FAILED(hr)) {
      t->Release();
      return hr;
    }
    *out = d3d9_wrap(t, IFACE_IDirect3DCubeTexture9);
    return D3D_OK;
  }
  if (type == 4) {
    if (i.file_format != 4)
      return INVALID_DATA;
    IDirect3DVolumeTexture9 *t = nullptr;
    HRESULT hr = d->CreateVolumeTexture(i.width, i.height, i.depth, mips, usage,
                                        fmt, pool, &t, nullptr);
    if (FAILED(hr))
      return hr;
    for (uint32_t l = 0; l < mips; l++) {
      size_t at;
      uint32_t w, h, z, pitch, rows;
      if (!native_d3dx_dds_level(&i, bytes, 0, l, &at, &w, &h, &z, &pitch,
                                 &rows)) {
        hr = INVALID_DATA;
        break;
      }
      D3DLOCKED_BOX lock;
      hr = t->LockBox(l, &lock, nullptr, 0);
      if (FAILED(hr))
        break;
      for (uint32_t slice = 0; slice < z; slice++)
        for (uint32_t r = 0; r < rows; r++)
          memcpy((uint8_t *)lock.pBits + (size_t)slice * lock.SlicePitch +
                     (size_t)r * lock.RowPitch,
                 (const uint8_t *)data + at +
                     ((size_t)slice * rows + r) * pitch,
                 std::min(pitch, (uint32_t)lock.RowPitch));
      hr = t->UnlockBox(l);
      if (FAILED(hr))
        break;
    }
    if (FAILED(hr)) {
      t->Release();
      return hr;
    }
    *out = d3d9_wrap(t, IFACE_IDirect3DVolumeTexture9);
    return D3D_OK;
  }
  return D3DERR_INVALIDCALL;
}
static void memory_texture(CPU *c, uint32_t type) {
  if (!ARG(3))
    RET(D3DERR_INVALIDCALL, 4);
  called(type == 3   ? "CreateTextureFromFileInMemory"
         : type == 5 ? "CreateCubeTextureFromFileInMemory"
                     : "CreateVolumeTextureFromFileInMemory");
  uint32_t out = 0;
  HRESULT hr = load_texture((IDirect3DDevice9 *)com_host(ARG(0)), GPTR(ARG(1)),
                            ARG(2), type, &out, 0);
  if (ARG(3))
    M32(ARG(3)) = out;
  if (FAILED(hr))
    LOG("D3DX: image upload failed type=%u bytes=%u hr=%08x", type, ARG(2),
        (uint32_t)hr);
  RET(hr, 4);
}
SHIM(D3DXCreateTextureFromFileInMemory) { memory_texture(c, 3); }
SHIM(D3DXCreateCubeTextureFromFileInMemory) { memory_texture(c, 5); }
SHIM(D3DXCreateVolumeTextureFromFileInMemory) { memory_texture(c, 4); }
SHIM(D3DXCreateTextureFromFileExA) {
  if (!ARG(13))
    RET(D3DERR_INVALIDCALL, 14);
  called("CreateTextureFromFileExA");
  auto bytes = read_file(ARG(1));
  uint32_t out = 0;
  HRESULT hr =
      load_texture((IDirect3DDevice9 *)com_host(ARG(0)), bytes.data(),
                   bytes.size(), 3, &out, ARG(11), ARG(2), ARG(3), ARG(4),
                   ARG(5), (D3DFORMAT)ARG(6), (D3DPOOL)ARG(7), ARG(8), ARG(10));
  if (ARG(13))
    M32(ARG(13)) = out;
  if (FAILED(hr))
    LOG("D3DX: file texture failed %s hr=%08x", GSTR(ARG(1)), (uint32_t)hr);
  RET(hr, 14);
}
// Surface conversion is implemented below; all pointers stay in host space.

SHIM(D3DXLoadSurfaceFromMemory) {
  called("LoadSurfaceFromMemory");
  HRESULT hr = native_d3dx_surface_memory(
      (IDirect3DSurface9 *)com_host(ARG(0)), (const RECT *)GPTR(ARG(2)),
      GPTR(ARG(3)), (D3DFORMAT)ARG(4), ARG(5), (const RECT *)GPTR(ARG(7)),
      ARG(8), ARG(9));
  if (FAILED(hr))
    LOG("D3DX: LoadSurfaceFromMemory failed hr=%08x", (uint32_t)hr);
  RET(hr, 10);
}
SHIM(D3DXLoadSurfaceFromSurface) {
  called("LoadSurfaceFromSurface");
  auto *dst = (IDirect3DSurface9 *)com_host(ARG(0));
  auto *src = (IDirect3DSurface9 *)com_host(ARG(3));
  if (!dst || !src)
    RET(D3DERR_INVALIDCALL, 8);
  D3DSURFACE_DESC sd, dd;
  src->GetDesc(&sd);
  dst->GetDesc(&dd);
  RECT sr = ARG(5) ? *(const RECT *)GPTR(ARG(5))
                   : RECT{0, 0, (LONG)sd.Width, (LONG)sd.Height},
       dr = ARG(2) ? *(const RECT *)GPTR(ARG(2))
                   : RECT{0, 0, (LONG)dd.Width, (LONG)dd.Height};
  IDirect3DDevice9 *d = nullptr;
  src->GetDevice(&d);
  HRESULT hr = d->StretchRect(
      src, &sr, dst, &dr, (ARG(6) & 7) == 1 ? D3DTEXF_POINT : D3DTEXF_LINEAR);
  if (FAILED(hr)) {
    IDirect3DSurface9 *read = src, *temp = nullptr;
    D3DLOCKED_RECT lock;
    hr = read->LockRect(&lock, nullptr, D3DLOCK_READONLY);
    if (FAILED(hr) && sd.Pool == D3DPOOL_DEFAULT) {
      hr = d->CreateOffscreenPlainSurface(sd.Width, sd.Height, sd.Format,
                                          D3DPOOL_SYSTEMMEM, &temp, nullptr);
      if (SUCCEEDED(hr))
        hr = d->GetRenderTargetData(src, temp);
      if (SUCCEEDED(hr)) {
        read = temp;
        hr = read->LockRect(&lock, nullptr, D3DLOCK_READONLY);
      }
    }
    if (SUCCEEDED(hr)) {
      hr = native_d3dx_surface_memory(dst, &dr, lock.pBits, sd.Format,
                                      lock.Pitch, &sr, ARG(6), ARG(7));
      read->UnlockRect();
    }
    if (temp)
      temp->Release();
  }
  d->Release();
  if (FAILED(hr))
    LOG("D3DX: LoadSurfaceFromSurface failed hr=%08x", (uint32_t)hr);
  RET(hr, 8);
}
extern "C" void d3dx_shader_init(void);
extern "C" void d3dx_init(void) {
#ifdef GAME_SKYRIM
  static const ShimDef defs[] = {
#define S(n) {"d3dx9_42.dll", #n, shim_##n}
      S(D3DXGetImageInfoFromFileInMemory),
      S(D3DXGetImageInfoFromFileA),
      S(D3DXCreateTextureFromFileInMemory),
      S(D3DXCreateCubeTextureFromFileInMemory),
      S(D3DXCreateVolumeTextureFromFileInMemory),
      S(D3DXCreateTextureFromFileExA),
      S(D3DXLoadSurfaceFromMemory),
      S(D3DXLoadSurfaceFromSurface),
      S(D3DXVec3Normalize),
      S(D3DXVec3TransformCoord),
      S(D3DXVec3TransformNormal),
      S(D3DXPlaneNormalize),
      S(D3DXPlaneTransform),
      S(D3DXMatrixMultiply),
      S(D3DXMatrixTranspose),
      S(D3DXMatrixMultiplyTranspose),
      S(D3DXMatrixInverse),
      {nullptr, nullptr, nullptr}
#undef S
  };
  rt_register_shims(defs);
  d3dx_shader_init();
  LOG("D3DX9: native image, matrix and shader interfaces enabled; no PE helper "
      "image");
#endif
}
