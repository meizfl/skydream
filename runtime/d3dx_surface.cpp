/* SPDX-License-Identifier: GPL-3.0-only */
#include "d3d9_bridge.h"
#include <algorithm>
#include <cmath>
#include <vector>
extern void native_d3dx_decode_block(D3DFORMAT, const uint8_t *, uint8_t *, int,
                                     int, int);
static unsigned pixel_size(D3DFORMAT f) {
  switch (f) {
  case D3DFMT_A8R8G8B8:
  case D3DFMT_X8R8G8B8:
  case D3DFMT_A8B8G8R8:
  case D3DFMT_X8B8G8R8:
    return 4;
  case D3DFMT_R8G8B8:
    return 3;
  case D3DFMT_R5G6B5:
  case D3DFMT_A1R5G5B5:
  case D3DFMT_X1R5G5B5:
  case D3DFMT_A4R4G4B4:
  case D3DFMT_A8L8:
    return 2;
  case D3DFMT_L8:
  case D3DFMT_A8:
    return 1;
  default:
    return 0;
  }
}
static bool compressed(D3DFORMAT f) {
  return f >= D3DFMT_DXT1 && f <= D3DFMT_DXT5;
}
static void unpack(const uint8_t *s, D3DFORMAT f, uint8_t *p) {
  unsigned v;
  p[3] = 255;
  switch (f) {
  case D3DFMT_A8R8G8B8:
  case D3DFMT_X8R8G8B8:
  case D3DFMT_R8G8B8:
    p[0] = s[0];
    p[1] = s[1];
    p[2] = s[2];
    if (f == D3DFMT_A8R8G8B8)
      p[3] = s[3];
    break;
  case D3DFMT_A8B8G8R8:
  case D3DFMT_X8B8G8R8:
    p[0] = s[2];
    p[1] = s[1];
    p[2] = s[0];
    if (f == D3DFMT_A8B8G8R8)
      p[3] = s[3];
    break;
  case D3DFMT_L8:
  case D3DFMT_A8L8:
    p[0] = p[1] = p[2] = s[0];
    if (f == D3DFMT_A8L8)
      p[3] = s[1];
    break;
  case D3DFMT_A8:
    p[0] = p[1] = p[2] = 255;
    p[3] = s[0];
    break;
  default:
    v = s[0] | s[1] << 8;
    if (f == D3DFMT_R5G6B5) {
      p[0] = (v & 31) * 255 / 31;
      p[1] = ((v >> 5) & 63) * 255 / 63;
      p[2] = ((v >> 11) & 31) * 255 / 31;
    } else if (f == D3DFMT_A4R4G4B4) {
      p[0] = (v & 15) * 17;
      p[1] = ((v >> 4) & 15) * 17;
      p[2] = ((v >> 8) & 15) * 17;
      p[3] = (v >> 12) * 17;
    } else {
      p[0] = (v & 31) * 255 / 31;
      p[1] = ((v >> 5) & 31) * 255 / 31;
      p[2] = ((v >> 10) & 31) * 255 / 31;
      if (f == D3DFMT_A1R5G5B5)
        p[3] = (v & 0x8000) ? 255 : 0;
    }
    break;
  }
}
static void pack(uint8_t *d, D3DFORMAT f, const uint8_t *p) {
  unsigned v;
  switch (f) {
  case D3DFMT_A8R8G8B8:
  case D3DFMT_X8R8G8B8:
    memcpy(d, p, 4);
    if (f == D3DFMT_X8R8G8B8)
      d[3] = 255;
    break;
  case D3DFMT_A8B8G8R8:
  case D3DFMT_X8B8G8R8:
    d[0] = p[2];
    d[1] = p[1];
    d[2] = p[0];
    d[3] = f == D3DFMT_A8B8G8R8 ? p[3] : 255;
    break;
  case D3DFMT_R8G8B8:
    memcpy(d, p, 3);
    break;
  case D3DFMT_A8:
    d[0] = p[3];
    break;
  case D3DFMT_L8:
  case D3DFMT_A8L8:
    d[0] = (p[2] * 77 + p[1] * 150 + p[0] * 29) >> 8;
    if (f == D3DFMT_A8L8)
      d[1] = p[3];
    break;
  default:
    if (f == D3DFMT_R5G6B5)
      v = (p[2] >> 3) << 11 | (p[1] >> 2) << 5 | (p[0] >> 3);
    else if (f == D3DFMT_A4R4G4B4)
      v = (p[3] >> 4) << 12 | (p[2] >> 4) << 8 | (p[1] >> 4) << 4 | (p[0] >> 4);
    else
      v = (f == D3DFMT_X1R5G5B5 || p[3] >= 128 ? 0x8000 : 0) |
          (p[2] >> 3) << 10 | (p[1] >> 3) << 5 | (p[0] >> 3);
    d[0] = v;
    d[1] = v >> 8;
  }
}
HRESULT native_d3dx_surface_memory(IDirect3DSurface9 *dst, const RECT *rect,
                                   const void *src, D3DFORMAT fmt,
                                   uint32_t pitch, const RECT *source,
                                   uint32_t filter, uint32_t key) {
  if (!dst || !src || !source)
    return D3DERR_INVALIDCALL;
  D3DSURFACE_DESC desc;
  if (FAILED(dst->GetDesc(&desc)))
    return D3DERR_INVALIDCALL;
  RECT dr = rect ? *rect : RECT{0, 0, (LONG)desc.Width, (LONG)desc.Height},
       sr = *source;
  int w = dr.right - dr.left, h = dr.bottom - dr.top, sw = sr.right - sr.left,
      sh = sr.bottom - sr.top;
  if (w <= 0 || h <= 0 || sw <= 0 || sh <= 0 || dr.left < 0 || dr.top < 0 ||
      dr.right > (LONG)desc.Width || dr.bottom > (LONG)desc.Height ||
      sr.left < 0 || sr.top < 0 || sw > 16384 || sh > 16384)
    return D3DERR_INVALIDCALL;
  unsigned sp = pixel_size(fmt), dp = pixel_size(desc.Format);
  bool same = fmt == desc.Format && w == sw && h == sh && !key;
  if (!same && (!dp || (!sp && !compressed(fmt)))) {
    LOG("D3DX: unsupported surface conversion %u -> %u", fmt, desc.Format);
    return D3DERR_NOTAVAILABLE;
  }
  if (compressed(desc.Format) &&
      (!same || ((dr.left | dr.top | sr.left | sr.top) & 3)))
    return D3DERR_NOTAVAILABLE;
  std::vector<uint8_t> rgba;
  if (!same) {
    rgba.resize((size_t)sw * sh * 4);
    if (compressed(fmt)) {
      unsigned bb = fmt == D3DFMT_DXT1 ? 8 : 16;
      if ((sr.left | sr.top) & 3)
        return D3DERR_INVALIDCALL;
      for (int y = 0; y < sh; y += 4)
        for (int x = 0; x < sw; x += 4)
          native_d3dx_decode_block(
              fmt,
              (const uint8_t *)src + (size_t)((sr.top + y) / 4) * pitch +
                  ((sr.left + x) / 4) * bb,
              rgba.data() + ((size_t)y * sw + x) * 4, sw * 4,
              std::min(4, sw - x), std::min(4, sh - y));
    } else {
      if ((uint64_t)sr.right * sp > pitch)
        return D3DERR_INVALIDCALL;
      for (int y = 0; y < sh; y++)
        for (int x = 0; x < sw; x++)
          unpack((const uint8_t *)src + (size_t)(sr.top + y) * pitch +
                     (sr.left + x) * sp,
                 fmt, &rgba[((size_t)y * sw + x) * 4]);
    }
    if (key)
      for (size_t i = 0; i < rgba.size(); i += 4) {
        uint32_t p;
        memcpy(&p, &rgba[i], 4);
        if (p == key)
          rgba[i + 3] = 0;
      }
  }
  IDirect3DSurface9 *target = dst, *temp = nullptr;
  IDirect3DDevice9 *dev = nullptr;
  D3DLOCKED_RECT lock;
  HRESULT hr = target->LockRect(&lock, &dr, 0);
  if (FAILED(hr)) {
    dst->GetDevice(&dev);
    hr = dev->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format,
                                          D3DPOOL_SYSTEMMEM, &temp, nullptr);
    if (SUCCEEDED(hr)) {
      target = temp;
      hr = target->LockRect(&lock, &dr, 0);
    }
  }
  if (SUCCEEDED(hr)) {
    if (same) {
      unsigned rows = compressed(fmt) ? (h + 3) / 4 : h,
               bytes = compressed(fmt)
                           ? ((w + 3) / 4) * (fmt == D3DFMT_DXT1 ? 8 : 16)
                           : w * sp;
      size_t offset = compressed(fmt)
                          ? (size_t)(sr.top / 4) * pitch +
                                (sr.left / 4) * (fmt == D3DFMT_DXT1 ? 8 : 16)
                          : (size_t)sr.top * pitch + sr.left * sp;
      if (bytes > pitch || bytes > (unsigned)lock.Pitch)
        hr = D3DERR_INVALIDCALL;
      else
        for (unsigned y = 0; y < rows; y++)
          memcpy((uint8_t *)lock.pBits + (size_t)y * lock.Pitch,
                 (const uint8_t *)src + offset + (size_t)y * pitch, bytes);
    } else
      for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
          uint8_t p[4];
          if ((filter & 7) <= 2) {
            int sx = std::min(sw - 1, x * sw / w),
                sy = std::min(sh - 1, y * sh / h);
            memcpy(p, &rgba[((size_t)sy * sw + sx) * 4], 4);
          } else {
            float fx = (x + .5f) * sw / w - .5f, fy = (y + .5f) * sh / h - .5f;
            int ix = (int)floorf(fx), iy = (int)floorf(fy);
            float ax = fx - ix, ay = fy - iy;
            for (int c = 0; c < 4; c++) {
              float sum = 0;
              for (int yy = 0; yy < 2; yy++)
                for (int xx = 0; xx < 2; xx++)
                  sum += rgba[((size_t)std::clamp(iy + yy, 0, sh - 1) * sw +
                               std::clamp(ix + xx, 0, sw - 1)) *
                                  4 +
                              c] *
                         (xx ? ax : 1 - ax) * (yy ? ay : 1 - ay);
              p[c] = (uint8_t)std::clamp(sum + .5f, 0.f, 255.f);
            }
          }
          pack((uint8_t *)lock.pBits + (size_t)y * lock.Pitch + x * dp,
               desc.Format, p);
        }
    target->UnlockRect();
    if (SUCCEEDED(hr) && temp) {
      POINT at = {dr.left, dr.top};
      hr = dev->UpdateSurface(temp, &dr, dst, &at);
    }
  }
  if (temp)
    temp->Release();
  if (dev)
    dev->Release();
  return hr;
}
