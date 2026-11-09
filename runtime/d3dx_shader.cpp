/* SPDX-License-Identifier: GPL-3.0-only
 * Native HLSL -> SM1-3 compiler and D3DX buffer/constant-table ABI.
 * Compiler: vkd3d-shader (LGPL-2.1-or-later), loaded as a native ELF library.
 */
#include "../third_party/vkd3d/include/vkd3d_shader.h"
#include "d3d9_bridge.h"
#include <algorithm>
#include <SDL2/SDL.h>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
static PFN_vkd3d_shader_compile compile_hlsl;
static PFN_vkd3d_shader_free_shader_code free_code;
static PFN_vkd3d_shader_free_messages free_messages;
static std::once_flag compiler_once;
static void init_compiler() {
  const char *path = getenv("SKYDREAM_VKD3D_SHADER");
#if defined(_WIN32)
  const char *default_name = "libvkd3d-shader-1.dll";
#elif defined(__APPLE__)
  const char *default_name = "libvkd3d-shader.1.dylib";
#else
  const char *default_name = "libvkd3d-shader.so.1";
#endif
  void *lib = SDL_LoadObject(path ? path : default_name);
  if (!lib) {
    LOG("D3DX: native HLSL compiler unavailable: %s", SDL_GetError());
    return;
  }
  compile_hlsl = (PFN_vkd3d_shader_compile)SDL_LoadFunction(lib, "vkd3d_shader_compile");
  free_code = (PFN_vkd3d_shader_free_shader_code)SDL_LoadFunction(
      lib, "vkd3d_shader_free_shader_code");
  free_messages =
      (PFN_vkd3d_shader_free_messages)SDL_LoadFunction(lib, "vkd3d_shader_free_messages");
  if (!compile_hlsl || !free_code || !free_messages) {
    compile_hlsl = nullptr;
    LOG("D3DX: incompatible vkd3d-shader library");
  } else
    LOG("D3DX: native vkd3d-shader HLSL compiler loaded");
}
struct Constant {
  uint32_t name, set, index, count, cls, type, rows, cols, elements, members,
      bytes, defaults;
};
struct Table {
  uint32_t data, size, version, creator, roots;
  std::vector<Constant> constants;
};
static std::recursive_mutex table_lock;
static std::unordered_map<uint32_t, Table> tables;
static uint32_t buffer_vt, table_vt;
static uint32_t make_buffer(const void *data, size_t size, uint32_t vt = 0) {
  uint32_t obj = gmalloc(16), p = gmalloc(std::max((size_t)1, size));
  if (size)
    memcpy(GPTR(p), data, size);
  M32(obj) = vt ? vt : buffer_vt;
  M32(obj + 4) = 1;
  M32(obj + 8) = p;
  M32(obj + 12) = size;
  return obj;
}
SHIM(BufferQuery) {
  if (ARG(2))
    M32(ARG(2)) = 0;
  static const uint8_t unknown[16] = {0,    0, 0, 0, 0, 0, 0, 0,
                                      0xc0, 0, 0, 0, 0, 0, 0, 0x46},
                       buffer[16] = {0x08, 0xfb, 0xa5, 0x8b, 0x95, 0x51,
                                     0xe2, 0x40, 0xac, 0x58, 0x0d, 0x98,
                                     0x9c, 0x3a, 0x01, 0x02},
                       ctable[16] = {0x8f, 0x75, 0x3c, 0xab, 0x3e, 0x09,
                                     0x56, 0x43, 0xb7, 0x62, 0x4d, 0xb1,
                                     0x8f, 0x1b, 0x3a, 0x01};
  if (ARG(1) && ARG(2) &&
      (!memcmp(GPTR(ARG(1)), unknown, 16) ||
       !memcmp(GPTR(ARG(1)), buffer, 16) ||
       (M32(ARG(0)) == table_vt && !memcmp(GPTR(ARG(1)), ctable, 16)))) {
    M32(ARG(2)) = ARG(0);
    __atomic_add_fetch((uint32_t *)GPTR(ARG(0) + 4), 1, __ATOMIC_RELAXED);
    RET(0, 3);
  }
  RET(0x80004002u, 3);
}
SHIM(BufferAddRef) {
  RET(__atomic_add_fetch((uint32_t *)GPTR(ARG(0) + 4), 1, __ATOMIC_RELAXED), 1);
}
SHIM(BufferRelease) {
  uint32_t obj = ARG(0), n = __atomic_sub_fetch((uint32_t *)GPTR(obj + 4), 1,
                                                __ATOMIC_ACQ_REL);
  if (!n) {
    std::lock_guard<std::recursive_mutex> guard(table_lock);
    tables.erase(obj);
    gfree(M32(obj + 8));
    gfree(obj);
  }
  RET(n, 1);
}
SHIM(BufferPointer) { RET(M32(ARG(0) + 8), 1); }
SHIM(BufferSize) { RET(M32(ARG(0) + 12), 1); }
static Constant *constant(Table &t, uint32_t handle) {
  if (handle >= 0x10000) {
    for (unsigned n = 0; n < t.roots; n++)
      if (!strcmp(GSTR(t.constants[n].name), GSTR(handle)))
        return &t.constants[n];
    return nullptr;
  }
  if (!handle || handle > t.constants.size())
    return nullptr;
  return &t.constants[handle - 1];
}
static uint32_t find_constant(Table &t, uint32_t handle, const char *name) {
  if (!name || handle)
    return 0;
  for (size_t i = 0; i < t.constants.size(); i++)
    if (!strcmp(GSTR(t.constants[i].name), name))
      return i + 1;
  return 0;
}
SHIM(TableDesc) {
  std::lock_guard<std::recursive_mutex> guard(table_lock);
  auto it = tables.find(ARG(0));
  if (it == tables.end() || !ARG(1))
    RET(D3DERR_INVALIDCALL, 2);
  uint32_t values[] = {it->second.creator, it->second.version,
                       it->second.roots};
  memcpy(GPTR(ARG(1)), values, sizeof values);
  RET(0, 2);
}
SHIM(ConstantDesc) {
  std::lock_guard<std::recursive_mutex> guard(table_lock);
  auto it = tables.find(ARG(0));
  if (it == tables.end() || !ARG(3))
    RET(D3DERR_INVALIDCALL, 4);
  auto *v = constant(it->second, ARG(1));
  if (!v)
    RET(D3DERR_INVALIDCALL, 4);
  if (ARG(2) && M32(ARG(3)))
    memcpy(GPTR(ARG(2)), v, sizeof *v);
  M32(ARG(3)) = 1;
  RET(0, 4);
}
SHIM(SamplerIndex) {
  std::lock_guard<std::recursive_mutex> guard(table_lock);
  auto it = tables.find(ARG(0));
  auto *v = it == tables.end() ? nullptr : constant(it->second, ARG(1));
  RET(v && v->set == 3 ? v->index : 0xffffffffu, 2);
}
SHIM(GetConstant) {
  std::lock_guard<std::recursive_mutex> guard(table_lock);
  auto it = tables.find(ARG(0));
  RET(it != tables.end() && !ARG(1) && ARG(2) < it->second.roots ? ARG(2) + 1
                                                                 : 0,
      3);
}
SHIM(GetConstantByName) {
  std::lock_guard<std::recursive_mutex> guard(table_lock);
  auto it = tables.find(ARG(0));
  RET(it == tables.end() ? 0 : find_constant(it->second, ARG(1), GSTR(ARG(2))),
      3);
}
SHIM(GetConstantElement) {
  std::lock_guard<std::recursive_mutex> guard(table_lock);
  auto it = tables.find(ARG(0));
  auto *v = it == tables.end() ? nullptr : constant(it->second, ARG(1));
  if (!v || ARG(2) >= std::max(1u, v->elements))
    RET(0, 3);
  Constant e = *v;
  unsigned elements = std::max(1u, v->elements);
  e.index += ARG(2) * v->count / elements;
  e.count /= elements;
  e.elements = 1;
  e.bytes /= elements;
  if (e.defaults)
    e.defaults += ARG(2) * e.bytes;
  it->second.constants.push_back(e);
  RET(it->second.constants.size(), 3);
}
static HRESULT set_registers(Table &t, Constant &v, IDirect3DDevice9 *dev,
                             const void *data, unsigned count) {
  if (!dev)
    return D3DERR_INVALIDCALL;
  bool vs = (t.version >> 16) == 0xfffe;
  count = std::min(count, v.count);
  if (!count)
    return 0;
  switch (v.set) {
  case 0:
    return vs ? dev->SetVertexShaderConstantB(v.index, (const BOOL *)data,
                                              count)
              : dev->SetPixelShaderConstantB(v.index, (const BOOL *)data,
                                             count);
  case 1:
    return vs ? dev->SetVertexShaderConstantI(v.index, (const int *)data, count)
              : dev->SetPixelShaderConstantI(v.index, (const int *)data, count);
  case 2:
    return vs ? dev->SetVertexShaderConstantF(v.index, (const float *)data,
                                              count)
              : dev->SetPixelShaderConstantF(v.index, (const float *)data,
                                             count);
  default:
    return D3DERR_INVALIDCALL;
  }
}
SHIM(SetDefaults) {
  std::lock_guard<std::recursive_mutex> guard(table_lock);
  auto it = tables.find(ARG(0));
  if (it == tables.end())
    RET(D3DERR_INVALIDCALL, 2);
  HRESULT hr = 0;
  for (unsigned n = 0; n < it->second.roots; n++) {
    auto &v = it->second.constants[n];
    if (!v.defaults)
      continue;
    hr = set_registers(it->second, v, (IDirect3DDevice9 *)com_host(ARG(1)),
                       GPTR(v.defaults), v.count);
    if (FAILED(hr))
      break;
  }
  RET(hr, 2);
}
/* mode: scalar/array:0 float,1 int,2 bool; vector:3; matrices:4/5 transpose. */
static void set_value(CPU *c, unsigned mode, unsigned nargs,
                      bool immediate = false, bool pointers = false,
                      bool raw = false) {
  std::lock_guard<std::recursive_mutex> guard(table_lock);
  auto it = tables.find(ARG(0));
  if (it == tables.end())
    RET(D3DERR_INVALIDCALL, nargs);
  auto *v = constant(it->second, ARG(2));
  if (!v || v->members || v->set == 3)
    RET(D3DERR_INVALIDCALL, nargs);
  unsigned elements = nargs == 5 ? ARG(4) : 1, available;
  if (raw) {
    available = ARG(4) / 4;
    mode = v->type == 1 ? 2 : v->type == 2 ? 1 : 0;
  } else
    available = elements * (mode >= 4 ? 16 : mode == 3 ? 4 : 1);
  available = std::min(available, 65536u);
  if (!immediate && !ARG(3))
    RET(D3DERR_INVALIDCALL, nargs);
  std::vector<float> values(v->count * 4, 0);
  std::vector<int> ints(v->count * 4, 0);
  unsigned used = 0;
  auto read = [&](unsigned i) -> double {
    uint32_t word;
    if (immediate)
      word = ARG(3);
    else if (pointers) {
      uint32_t p = M32(ARG(3) + (i / 16) * 4);
      if (!p)
        return 0;
      word = M32(p + (i % 16) * 4);
    } else
      word = M32(ARG(3) + i * 4);
    if (mode == 1 || mode == 2)
      return (int32_t)word;
    float f;
    memcpy(&f, &word, 4);
    return f;
  };
  unsigned per = std::max(1u, v->rows * v->cols),
           n = std::min(std::max(1u, v->elements),
                        raw ? (available / per) : elements);
  if (mode < 3 && !raw)
    n = std::min(std::max(1u, v->elements), (available + per - 1) / per);
  for (unsigned e = 0; e < n; e++)
    for (unsigned row = 0; row < v->rows; row++)
      for (unsigned col = 0; col < v->cols; col++) {
        unsigned input =
            mode >= 4   ? e * 16 + (mode == 5 ? col * 4 + row : row * 4 + col)
            : mode == 3 ? e * 4 + col
                        : e * per + row * v->cols + col;
        if (input >= available)
          continue;
        unsigned reg = e * (v->cls == 3   ? v->cols
                            : v->cls == 2 ? v->rows
                                          : 1) +
                       (v->cls == 3   ? col
                        : v->cls == 2 ? row
                                      : 0),
                 component = v->cls == 3 ? row : col,
                 index = v->set == 0 ? e * per + row * v->cols + col
                                     : reg * 4 + component;
        if (index >= values.size())
          continue;
        double value = read(input);
        values[index] = (float)value;
        ints[index] = v->set == 0 ? value != 0 : (int)value;
        used = std::max(used, v->set == 0 ? index + 1 : reg + 1);
      }
  HRESULT hr = set_registers(
      it->second, *v, (IDirect3DDevice9 *)com_host(ARG(1)),
      v->set == 2 ? (const void *)values.data() : (const void *)ints.data(),
      used);
  RET(hr, nargs);
}
SHIM(SetValue) { set_value(c, 0, 5, false, false, true); }
SHIM(SetBool) { set_value(c, 2, 4, true); }
SHIM(SetBoolArray) { set_value(c, 2, 5); }
SHIM(SetInt) { set_value(c, 1, 4, true); }
SHIM(SetIntArray) { set_value(c, 1, 5); }
SHIM(SetFloat) { set_value(c, 0, 4, true); }
SHIM(SetFloatArray) { set_value(c, 0, 5); }
SHIM(SetVector) { set_value(c, 3, 4); }
SHIM(SetVectorArray) { set_value(c, 3, 5); }
SHIM(SetMatrix) { set_value(c, 4, 4); }
SHIM(SetMatrixArray) { set_value(c, 4, 5); }
SHIM(SetMatrixPointerArray) { set_value(c, 4, 5, false, true); }
SHIM(SetMatrixTranspose) { set_value(c, 5, 4); }
SHIM(SetMatrixTransposeArray) { set_value(c, 5, 5); }
SHIM(SetMatrixTransposePointerArray) { set_value(c, 5, 5, false, true); }
static uint32_t make_table(const void *code, size_t bytes) {
  const uint32_t *words = (const uint32_t *)code;
  size_t count = bytes / 4;
  for (size_t n = 1; n < count;) {
    uint32_t token = words[n++];
    if ((token & 0xffff) == 0xffff)
      break;
    if ((token & 0xffff) != 0xfffe) {
      unsigned len = (token >> 24) & 15;
      if (!len)
        break;
      n += len;
      continue;
    }
    unsigned len = token >> 16;
    if (n + len > count)
      break;
    if (len >= 8 && words[n] == 0x42415443) {
      const uint8_t *base = (const uint8_t *)(words + n + 1);
      size_t size = (len - 1) * 4;
      auto u32 = [&](size_t at) {
        uint32_t v = 0;
        if (at + 4 <= size)
          memcpy(&v, base + at, 4);
        return v;
      };
      auto u16 = [&](size_t at) {
        uint16_t v = 0;
        if (at + 2 <= size)
          memcpy(&v, base + at, 2);
        return v;
      };
      uint32_t obj = make_buffer(base, size, table_vt), data = M32(obj + 8);
      Table t{data, (uint32_t)size, u32(8), 0, u32(12), {}};
      auto string_at = [&](uint32_t offset) -> uint32_t {
        return offset < size && memchr(base + offset, 0, size - offset)
                   ? data + offset
                   : 0;
      };
      t.creator = string_at(u32(4));
      uint32_t constants = u32(12), offset = u32(16);
      if (constants > 4096 || (uint64_t)offset + constants * 20 > size) {
        gfree(data);
        gfree(obj);
        return 0;
      }
      for (unsigned i = 0; i < constants; i++) {
        size_t p = offset + i * 20, type = u32(p + 12), def = u32(p + 16);
        if (type + 16 > size) {
          gfree(data);
          gfree(obj);
          return 0;
        }
        Constant v = {string_at(u32(p)),
                      u16(p + 4),
                      u16(p + 6),
                      u16(p + 8),
                      u16(type),
                      u16(type + 2),
                      u16(type + 4),
                      u16(type + 6),
                      u16(type + 8),
                      u16(type + 10),
                      0,
                      0};
        v.bytes = v.rows * v.cols * std::max(1u, v.elements) * 4;
        unsigned defaultbytes = v.count * (v.set == 0 ? 4 : 16);
        if (def && def <= size && defaultbytes <= size - def)
          v.defaults = data + def;
        t.constants.push_back(v);
      }
      std::lock_guard<std::recursive_mutex> guard(table_lock);
      tables.emplace(obj, std::move(t));
      return obj;
    }
    n += len;
  }
  return 0;
}
struct Includes {
  CPU *cpu;
  uint32_t object;
  std::unordered_map<const void *, uint32_t> pointers;
};
static int open_include(const char *filename, bool local, const char *parent,
                        void *context, vkd3d_shader_code *out) {
  auto &i = *(Includes *)context;
  if (!i.object)
    return -1;
  uint32_t name = gstrdup(filename), result = gmalloc(8);
  M32(result) = M32(result + 4) = 0;
  CPU saved = *i.cpu;
  uint32_t a[] = {i.object, local ? 0u : 1u,
                  name,     i.pointers.count(parent) ? i.pointers[parent] : 0,
                  result,   result + 4};
  uint32_t hr = rt_invoke(i.cpu, M32(M32(i.object)), 6, a);
  *i.cpu = saved;
  uint32_t p = M32(result), size = M32(result + 4);
  gfree(name);
  gfree(result);
  if ((int32_t)hr < 0 || !p || size > 16 * 1024 * 1024)
    return -1;
  out->code = GPTR(p);
  out->size = size;
  i.pointers[out->code] = p;
  return 0;
}
static void close_include(const vkd3d_shader_code *code, void *context) {
  auto &i = *(Includes *)context;
  auto it = i.pointers.find(code->code);
  if (it == i.pointers.end())
    return;
  CPU saved = *i.cpu;
  uint32_t a[] = {i.object, it->second};
  rt_invoke(i.cpu, M32(M32(i.object) + 4), 2, a);
  *i.cpu = saved;
  i.pointers.erase(it);
}
SHIM(D3DXCompileShader) {
  uint32_t out = ARG(7), errors = ARG(8), table = ARG(9);
  if (out)
    M32(out) = 0;
  if (errors)
    M32(errors) = 0;
  if (table)
    M32(table) = 0;
  if (!ARG(0) || !ARG(1) || !ARG(5) || !out)
    RET(D3DERR_INVALIDCALL, 10);
  std::call_once(compiler_once, init_compiler);
  if (!compile_hlsl)
    RET(D3DERR_NOTAVAILABLE, 10);
  std::vector<vkd3d_shader_macro> macros;
  if (ARG(2))
    for (unsigned n = 0; n < 1024; n++) {
      uint32_t p = ARG(2) + n * 8;
      if (!M32(p))
        break;
      macros.push_back({GSTR(M32(p)), GSTR(M32(p + 4))});
    }
  Includes includes{c, ARG(3), {}};
  includes.pointers[GPTR(ARG(0))] = ARG(0);
  vkd3d_shader_preprocess_info pp = {
      VKD3D_SHADER_STRUCTURE_TYPE_PREPROCESS_INFO,
      nullptr,
      macros.data(),
      (unsigned)macros.size(),
      open_include,
      close_include,
      &includes};
  vkd3d_shader_hlsl_source_info hlsl = {
      VKD3D_SHADER_STRUCTURE_TYPE_HLSL_SOURCE_INFO,
      &pp,
      GSTR(ARG(4)),
      {},
      GSTR(ARG(5))};
  vkd3d_shader_compile_option option = {
      VKD3D_SHADER_COMPILE_OPTION_PACK_MATRIX_ORDER,
      (ARG(6) & 8) ? VKD3D_SHADER_COMPILE_OPTION_PACK_MATRIX_ROW_MAJOR
                   : VKD3D_SHADER_COMPILE_OPTION_PACK_MATRIX_COLUMN_MAJOR};
  vkd3d_shader_compile_info info = {VKD3D_SHADER_STRUCTURE_TYPE_COMPILE_INFO,
                                    &hlsl,
                                    {GPTR(ARG(0)), ARG(1)},
                                    VKD3D_SHADER_SOURCE_HLSL,
                                    VKD3D_SHADER_TARGET_D3D_BYTECODE,
                                    &option,
                                    1,
                                    VKD3D_SHADER_LOG_WARNING,
                                    "Skyrim runtime shader"};
  vkd3d_shader_code code = {};
  char *messages = nullptr;
  int status = compile_hlsl(&info, &code, &messages);
  if (messages) {
    if (errors)
      M32(errors) = make_buffer(messages, strlen(messages) + 1);
    LOG("D3DX HLSL: %s", messages);
    free_messages(messages);
  }
  if (status < 0) {
    LOG("D3DX: HLSL compile failed %s / %s (%d)", hlsl.entry_point,
        hlsl.profile, status);
    free_code(&code);
    RET(0x80004005u, 10);
  }
  M32(out) = make_buffer(code.code, code.size);
  if (table) {
    uint32_t ct = make_table(code.code, code.size);
    if (!ct) {
      LOG("D3DX: compiler emitted no valid CTAB");
      gfree(M32(M32(out) + 8));
      gfree(M32(out));
      M32(out) = 0;
      free_code(&code);
      RET(0x80004005u, 10);
    }
    M32(table) = ct;
  }
  LOG("D3DX: native HLSL %s / %s -> %zu bytes", hlsl.entry_point, hlsl.profile,
      code.size);
  free_code(&code);
  RET(0, 10);
}
extern "C" void d3dx_shader_init() {
  ShimFn functions[] = {shim_BufferQuery,
                        shim_BufferAddRef,
                        shim_BufferRelease,
                        shim_BufferPointer,
                        shim_BufferSize,
                        shim_TableDesc,
                        shim_ConstantDesc,
                        shim_SamplerIndex,
                        shim_GetConstant,
                        shim_GetConstantByName,
                        shim_GetConstantElement,
                        shim_SetDefaults,
                        shim_SetValue,
                        shim_SetBool,
                        shim_SetBoolArray,
                        shim_SetInt,
                        shim_SetIntArray,
                        shim_SetFloat,
                        shim_SetFloatArray,
                        shim_SetVector,
                        shim_SetVectorArray,
                        shim_SetMatrix,
                        shim_SetMatrixArray,
                        shim_SetMatrixPointerArray,
                        shim_SetMatrixTranspose,
                        shim_SetMatrixTransposeArray,
                        shim_SetMatrixTransposePointerArray};
  buffer_vt = gmalloc(5 * 4);
  table_vt = gmalloc(sizeof functions / sizeof *functions * 4);
  for (unsigned i = 0; i < sizeof functions / sizeof *functions; i++) {
    uint32_t thunk = rt_thunk(functions[i], "native D3DX COM");
    M32(table_vt + i * 4) = thunk;
    if (i < 5)
      M32(buffer_vt + i * 4) = thunk;
  }
  static ShimDef defs[] = {
      {"d3dx9_42.dll", "D3DXCompileShader", shim_D3DXCompileShader},
      {nullptr, nullptr, nullptr}};
  rt_register_shims(defs);
}

static const char test_include_source[] = "float4 palette() { return COLOUR; }";
static unsigned test_includes_opened, test_includes_closed;
SHIM(TestIncludeOpen) {
  if (strcmp(GSTR(ARG(2)), "palette.h"))
    RET(0x80004005u, 6);
  M32(ARG(4)) = gstrdup(test_include_source);
  M32(ARG(5)) = sizeof(test_include_source) - 1;
  ++test_includes_opened;
  RET(0, 6);
}
SHIM(TestIncludeClose) {
  gfree(ARG(1));
  ++test_includes_closed;
  RET(0, 2);
}

/* Exercised through --d3dx-test, including the guest stdcall/COM boundary. */
extern "C" int d3dx_shader_selftest(CPU *c) {
  const char *source = "float4 tint; float4 main(float4 p : POSITION) : "
                       "POSITION { return p + tint; }";
  uint32_t text = gstrdup(source), entry = gstrdup("main"),
           profile = gstrdup("vs_3_0"), outs = gmalloc(12);
  uint32_t args[] = {text,     (uint32_t)strlen(source),
                     0,        0,
                     entry,    profile,
                     0,        outs,
                     outs + 4, outs + 8};
  uint32_t hr =
      rt_invoke(c, rt_import("d3dx9_42.dll", "D3DXCompileShader"), 10, args);
  int failed = 0;
  if (hr || !M32(outs) || !M32(outs + 8)) {
    LOG("D3DX selftest compile failed %08x", hr);
    failed = 1;
  } else {
    uint32_t blob = M32(outs), table = M32(outs + 8), a[] = {blob};
    uint32_t code = rt_invoke(c, M32(M32(blob) + 3 * 4), 1, a),
             size = rt_invoke(c, M32(M32(blob) + 4 * 4), 1, a);
    if (size < 8 || M32(code) != 0xfffe0300u)
      failed = 1;
    uint32_t name = gstrdup("tint"), get[] = {table, 0, name};
    uint32_t handle = rt_invoke(c, M32(M32(table) + 9 * 4), 3, get);
    uint32_t desc = gmalloc(48), count = gmalloc(4);
    M32(count) = 1;
    uint32_t da[] = {table, handle, desc, count};
    hr = rt_invoke(c, M32(M32(table) + 6 * 4), 4, da);
    if (hr || !handle || M32(desc + 4) != 2 || M32(desc + 28) != 4 ||
        strcmp(GSTR(M32(desc)), "tint"))
      failed = 1;
    uint32_t rel[] = {table};
    rt_invoke(c, M32(M32(table) + 2 * 4), 1, rel);
    rel[0] = blob;
    rt_invoke(c, M32(M32(blob) + 2 * 4), 1, rel);
    gfree(name);
    gfree(desc);
    gfree(count);
  }
  if (M32(outs + 4)) {
    uint32_t error = M32(outs + 4), a[] = {error};
    rt_invoke(c, M32(M32(error) + 8), 1, a);
  }
  const char *pixel_source =
      "#include \"palette.h\"\nfloat4 main() : COLOR {return palette();}";
  gfree(text);
  text = gstrdup(pixel_source);
  gfree(profile);
  profile = gstrdup("ps_3_0");
  uint32_t macro = gmalloc(16), macro_name = gstrdup("COLOUR"),
           macro_value = gstrdup("float4(1,0,0,1)"), include = gmalloc(4),
           ivt = gmalloc(8);
  M32(macro) = macro_name;
  M32(macro + 4) = macro_value;
  M32(macro + 8) = M32(macro + 12) = 0;
  M32(ivt) = rt_thunk(shim_TestIncludeOpen, "D3DX test include open");
  M32(ivt + 4) = rt_thunk(shim_TestIncludeClose, "D3DX test include close");
  M32(include) = ivt;
  uint32_t pixel_args[] = {text,     (uint32_t)strlen(pixel_source),
                           macro,    include,
                           entry,    profile,
                           0,        outs,
                           outs + 4, 0};
  hr = rt_invoke(c, rt_import("d3dx9_42.dll", "D3DXCompileShader"), 10,
                 pixel_args);
  if (hr || !M32(outs) || test_includes_opened != 1 ||
      test_includes_closed != 1)
    failed = 1;
  for (unsigned offset : {0u, 4u})
    if (M32(outs + offset)) {
      uint32_t a[] = {M32(outs + offset)};
      rt_invoke(c, M32(M32(a[0]) + 8), 1, a);
    }
  gfree(text);
  text = gstrdup("this is invalid HLSL!");
  pixel_args[0] = text;
  pixel_args[1] = strlen(GSTR(text));
  pixel_args[2] = pixel_args[3] = 0;
  hr = rt_invoke(c, rt_import("d3dx9_42.dll", "D3DXCompileShader"), 10,
                 pixel_args);
  if (!hr || M32(outs) || !M32(outs + 4))
    failed = 1;
  if (M32(outs + 4)) {
    uint32_t a[] = {M32(outs + 4)};
    rt_invoke(c, M32(M32(a[0]) + 8), 1, a);
  }
  gfree(macro);
  gfree(macro_name);
  gfree(macro_value);
  gfree(include);
  gfree(ivt);
  gfree(outs);
  gfree(text);
  gfree(entry);
  gfree(profile);
  LOG("D3DX shader selftest: %s", failed ? "FAILED" : "PASS");
  return failed;
}
