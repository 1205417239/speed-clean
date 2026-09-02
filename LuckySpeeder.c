/*

MIT License

Copyright (c) 2024 kekeimiku
Copyright (c) 2024 ac0d3r

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#include "LuckySpeeder.h"
#include "fishhook.h"
#include <dlfcn.h>
#include <time.h>
#include <stdio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdbool.h>
#include <stdarg.h>
#include <math.h>
#include <dispatch/dispatch.h>

// 兼容 Objective-C 类型（原通过 substrate.h 间接引入）
typedef bool BOOL;
#ifndef YES
#define YES true
#endif
#ifndef NO
#define NO false
#endif

// 不依赖 libsubstrate 编译，运行时 dlsym 动态查找 MSHookFunction
// TrollStore 注入的进程无越狱环境，强依赖会导致 dylib 加载失败
typedef void (*MSHookFunctionPtr)(void *, void *, void **);
static MSHookFunctionPtr _MSHookFunction(void) {
  static MSHookFunctionPtr fn = NULL;
  static BOOL checked = NO;
  if (!checked) {
    fn = (MSHookFunctionPtr)dlsym(RTLD_DEFAULT, "MSHookFunction");
    checked = YES;
  }
  return fn;
}
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <os/lock.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <mach/mach_time.h>

#if !TARGET_OS_TV
#include "hwbphook.h"
#include "port_clock_gettime.h"
#endif

static float timeScale_speed = 1.0;
static float last_game_value = 1.0;
// 弹窗可见性: 弹窗出现时暂停, 弹窗消失时立即恢复
static int g_popup_visible = 0;
static double force_resume_until = 0;
static int is_force_recovering(void) { return CFAbsoluteTimeGetCurrent() < force_resume_until; }
void set_popup_visible(int visible) {
  g_popup_visible = visible;
}
void force_resume(void) {
  last_game_value = 1.0f;
  g_popup_visible = 0;
  force_resume_until = CFAbsoluteTimeGetCurrent() + 15.0;
}
int is_paused(void) { return (last_game_value <= 0.001f || g_popup_visible); }

static void (*original_timeScale)(float) = NULL;
static void my_timeScale(float value) {
  last_game_value = value;
  // 只做timeScale一层加速; 游戏传0时正常暂停, 弹窗读秒/掉帧倒计时走原始真实时间
  float scaled = value * timeScale_speed;
  if (scaled > 10.0f) scaled = 10.0f;  // 防止游戏自身timeScale(如1.5)叠加后过高导致掉帧
  original_timeScale(value <= 0.001f ? value : scaled);
}

int hook_timeScale(void) {
  if (original_timeScale) return 0;

  intptr_t unity_vmaddr_slide = 0;
  uint32_t image_count = _dyld_image_count();
  const char *image_name;
  for (uint32_t i = 0; i < image_count; ++i) {
    image_name = _dyld_get_image_name(i);
    if (strstr(image_name, "UnityFramework.framework/UnityFramework")) {
      unity_vmaddr_slide = _dyld_get_image_vmaddr_slide(i);
      break;
    }
  }
  if (!unity_vmaddr_slide) return -1;

  size_t size;

  uint8_t *cstring_section_data = (uint8_t *)getsectiondata((const struct mach_header_64 *)unity_vmaddr_slide, "__TEXT", "__cstring", &size);
  if (!cstring_section_data) return -1;

  uint8_t *time_scale_function_address = (uint8_t *)memmem(cstring_section_data, size, "UnityEngine.Time::set_timeScale(System.Single)", 0x2F);
  if (!time_scale_function_address) return -1;

  uintptr_t il2cpp_section_base = 0;
  il2cpp_section_base = (uintptr_t)getsectiondata((const struct mach_header_64 *)unity_vmaddr_slide, "__TEXT", "il2cpp", &size);
  if (!il2cpp_section_base) {
    il2cpp_section_base = (uintptr_t)getsectiondata((const struct mach_header_64 *)unity_vmaddr_slide, "__TEXT", "__text", &size);
    if (!il2cpp_section_base) return -1;
  };

  uint8_t *il2cpp_end = (uint8_t *)(size + il2cpp_section_base);
  if (il2cpp_section_base + 4 >= size + il2cpp_section_base) return -1;

  uintptr_t first_instruction = *(uint32_t *)il2cpp_section_base;
  uintptr_t resolved_address, function_offset, second_instruction;

  while (1) {
    second_instruction = *(uint32_t *)(il2cpp_section_base + 4);
    if ((first_instruction & 0x9F000000) == 0x90000000 && (second_instruction & 0xFF800000) == 0x91000000) {
      resolved_address = (il2cpp_section_base & 0xFFFFFFFFFFFFF000LL) + (uint32_t)((((first_instruction >> 3) & 0xFFFFFFFC) | ((first_instruction >> 29) & 3)) << 12);
      function_offset = (second_instruction >> 10) & 0xFFF;
      if ((second_instruction & 0xC00000) != 0) function_offset <<= 12;
      if ((uint8_t *)(resolved_address + function_offset) == time_scale_function_address) break;
    }
    il2cpp_section_base += 4;
    first_instruction = second_instruction;
    if ((uint8_t *)(il2cpp_section_base + 8) >= il2cpp_end) return -1;
  }

  uintptr_t current_address = il2cpp_section_base;
  uintptr_t current_instruction, code_section_address;

  do {
    current_instruction = *(uint32_t *)(current_address - 4);
    current_address -= 4;
  } while ((current_instruction & 0x9F000000) != 0x90000000);

  code_section_address = (current_address & 0xFFFFFFFFFFFFF000LL) + (uint32_t)((((current_instruction >> 3) & 0xFFFFFFFC) | ((current_instruction >> 29) & 3)) << 12);

  uintptr_t method_data = *(uint32_t *)(current_address + 4);
  uintptr_t function_data_offset;

  if ((method_data & 0x1000000) != 0)
    function_data_offset = 8 * ((method_data >> 10) & 0xFFF);
  else
    function_data_offset = (method_data >> 10) & 0xFFF;

  if (*(uintptr_t *)(code_section_address + function_data_offset)) {
    original_timeScale = *(void (**)(float))(function_data_offset + code_section_address);
  } else {
    typedef void *(*resolve_icall_t)(const char *);
    resolve_icall_t resolve_icall = (resolve_icall_t)dlsym(RTLD_DEFAULT, "il2cpp_resolve_icall");
    if (resolve_icall) {
      original_timeScale = (void (*)(float))resolve_icall((const char *)time_scale_function_address);
    } else {
      for (int i = 2; i < 10; i++) {
        uint32_t bl_candidate = *(uint32_t *)(il2cpp_section_base + i * 4);
        if ((bl_candidate & 0xFC000000) != 0x94000000) continue;
        int32_t imm26 = bl_candidate & 0x3FFFFFF;
        if (imm26 & 0x2000000) imm26 |= (int32_t)0xFC000000;
        uintptr_t bl_addr = il2cpp_section_base + (uintptr_t)i * 4;
        uintptr_t target = bl_addr + (intptr_t)imm26 * 4;
        original_timeScale = (void (*)(float))((resolve_icall_t)target)((const char *)time_scale_function_address);
        break;
      }
    }
  }

  if (original_timeScale) {
    *(uintptr_t *)(function_data_offset + code_section_address) = (uintptr_t)my_timeScale;
    return 0;
  }

  return -1;
}

void set_timeScale(float value) {
  if (!original_timeScale) return;
  timeScale_speed = value;  // 去掉×3，游戏内部已clamp，设再高也没用
  my_timeScale(last_game_value);
}

void reset_timeScale(void) { set_timeScale(1.0); }


#pragma mark - Unity Time.get_realtimeSinceStartup (加速游戏计时器，不受 timeScale 影响)
static float realtime_speed = 1.0;
static float (*original_realtime)(void) = NULL;
static float realtime_accumulated = 0;
static float realtime_last = 0;
static BOOL realtime_initialized = NO;

static int last_paused_state = -1;
static float my_realtime(void) {
  float current = original_realtime ? original_realtime() : 0.0f;
  if (current <= 0.0f) current = (float)CFAbsoluteTimeGetCurrent();  // 钩子失效时回退系统时间, 避免realtime一直为0
  if (!realtime_initialized) {
    realtime_initialized = YES;
    realtime_last = current;
    realtime_accumulated = current;
    return current;
  }
  float delta = current - realtime_last;
  if (delta < 0) delta = 0;
  int paused = (last_game_value <= 0.001f || g_popup_visible);
  if (paused) {
        realtime_accumulated = current;
        realtime_last = current;
        return current;
    }
    realtime_accumulated = current;  // 不二次加速, 返回原始真实时间
  realtime_last = current;
  return realtime_accumulated;
}

int hook_time_realtimeSinceStartup(void) {
  if (original_realtime) return 0;
  intptr_t unity_vmaddr_slide = 0;
  uint32_t image_count = _dyld_image_count();
  const char *image_name;
  for (uint32_t i = 0; i < image_count; ++i) {
    image_name = _dyld_get_image_name(i);
    if (strstr(image_name, "UnityFramework.framework/UnityFramework")) {
      unity_vmaddr_slide = _dyld_get_image_vmaddr_slide(i);
      break;
    }
  }
  if (!unity_vmaddr_slide) return -1;
  size_t size;
  uint8_t *cstring_section_data = (uint8_t *)getsectiondata((const struct mach_header_64 *)unity_vmaddr_slide, "__TEXT", "__cstring", &size);
  if (!cstring_section_data) return -1;
  const char *method_name = "UnityEngine.Time::get_realtimeSinceStartup()";
  uint8_t *realtime_function_address = (uint8_t *)memmem(cstring_section_data, size, method_name, strlen(method_name));
  if (!realtime_function_address) return -1;
  uintptr_t il2cpp_section_base = 0;
  il2cpp_section_base = (uintptr_t)getsectiondata((const struct mach_header_64 *)unity_vmaddr_slide, "__TEXT", "il2cpp", &size);
  if (!il2cpp_section_base) {
    il2cpp_section_base = (uintptr_t)getsectiondata((const struct mach_header_64 *)unity_vmaddr_slide, "__TEXT", "__text", &size);
    if (!il2cpp_section_base) return -1;
  };
  uint8_t *il2cpp_end = (uint8_t *)(size + il2cpp_section_base);
  if (il2cpp_section_base + 4 >= size + il2cpp_section_base) return -1;
  uintptr_t first_instruction = *(uint32_t *)il2cpp_section_base;
  uintptr_t resolved_address, function_offset, second_instruction;
  while (1) {
    second_instruction = *(uint32_t *)(il2cpp_section_base + 4);
    if ((first_instruction & 0x9F000000) == 0x90000000 && (second_instruction & 0xFF800000) == 0x91000000) {
      resolved_address = (il2cpp_section_base & 0xFFFFFFFFFFFFF000LL) + (uint32_t)((((first_instruction >> 3) & 0xFFFFFFFC) | ((first_instruction >> 29) & 3)) << 12);
      function_offset = (second_instruction >> 10) & 0xFFF;
      if ((second_instruction & 0xC00000) != 0) function_offset <<= 12;
      if ((uint8_t *)(resolved_address + function_offset) == realtime_function_address) break;
    }
    il2cpp_section_base += 4;
    first_instruction = second_instruction;
    if ((uint8_t *)(il2cpp_section_base + 8) >= il2cpp_end) return -1;
  }
  uintptr_t current_address = il2cpp_section_base;
  uintptr_t current_instruction, code_section_address;
  do {
    current_instruction = *(uint32_t *)(current_address - 4);
    current_address -= 4;
  } while ((current_instruction & 0x9F000000) != 0x90000000);
  code_section_address = (current_address & 0xFFFFFFFFFFFFF000LL) + (uint32_t)((((current_instruction >> 3) & 0xFFFFFFFC) | ((current_instruction >> 29) & 3)) << 12);
  uintptr_t method_data = *(uint32_t *)(current_address + 4);
  uintptr_t function_data_offset;
  if ((method_data & 0x1000000) != 0)
    function_data_offset = 8 * ((method_data >> 10) & 0xFFF);
  else
    function_data_offset = (method_data >> 10) & 0xFFF;
  if (*(uintptr_t *)(code_section_address + function_data_offset)) {
    original_realtime = *(float (**)(void))(function_data_offset + code_section_address);
  } else {
    typedef void *(*resolve_icall_t)(const char *);
    resolve_icall_t resolve_icall = (resolve_icall_t)dlsym(RTLD_DEFAULT, "il2cpp_resolve_icall");
    if (resolve_icall) {
      original_realtime = (float (*)(void))resolve_icall((const char *)realtime_function_address);
    }
  }
  if (original_realtime) {
    *(uintptr_t *)(function_data_offset + code_section_address) = (uintptr_t)my_realtime;
    return 0;
  }
  return -1;
}

void set_realtime(float value) { realtime_speed = value; }
void reset_realtime(void) { realtime_speed = 1.0; }

#pragma mark - Unity Time.get_unscaledTime (另一个不受 timeScale 影响的真实时间)
static float unscaled_time_speed = 1.0;
static float (*original_unscaled_time)(void) = NULL;
static float unscaled_time_accumulated = 0;
static float unscaled_time_last = 0;
static BOOL unscaled_time_initialized = NO;

static float my_unscaled_time(void) {
  if (!original_unscaled_time) return 0;
  float current = original_unscaled_time();
  if (!unscaled_time_initialized) {
    unscaled_time_initialized = YES;
    unscaled_time_last = current;
    unscaled_time_accumulated = current;
    return current;
  }
  float delta = current - unscaled_time_last;
  if (delta < 0) delta = 0;
      // 弹窗可见时暂停(timeScale<=0 或 弹窗显示中), 弹窗消失立即恢复
    if (last_game_value <= 0.001f || g_popup_visible) {
        unscaled_time_accumulated = current;
        unscaled_time_last = current;
        return current;
    }
    unscaled_time_accumulated = current;  // 不二次加速, 返回原始真实时间
  unscaled_time_last = current;
  return unscaled_time_accumulated;
}

int hook_time_unscaledTime(void) {
  if (original_unscaled_time) return 0;
  intptr_t unity_vmaddr_slide = 0;
  uint32_t image_count = _dyld_image_count();
  const char *image_name;
  for (uint32_t i = 0; i < image_count; ++i) {
    image_name = _dyld_get_image_name(i);
    if (strstr(image_name, "UnityFramework.framework/UnityFramework")) {
      unity_vmaddr_slide = _dyld_get_image_vmaddr_slide(i);
      break;
    }
  }
  if (!unity_vmaddr_slide) return -1;
  size_t size;
  uint8_t *cstring_section_data = (uint8_t *)getsectiondata((const struct mach_header_64 *)unity_vmaddr_slide, "__TEXT", "__cstring", &size);
  if (!cstring_section_data) return -1;
  const char *method_name = "UnityEngine.Time::get_unscaledTime()";
  uint8_t *unscaled_function_address = (uint8_t *)memmem(cstring_section_data, size, method_name, strlen(method_name));
  if (!unscaled_function_address) return -1;
  uintptr_t il2cpp_section_base = 0;
  il2cpp_section_base = (uintptr_t)getsectiondata((const struct mach_header_64 *)unity_vmaddr_slide, "__TEXT", "il2cpp", &size);
  if (!il2cpp_section_base) {
    il2cpp_section_base = (uintptr_t)getsectiondata((const struct mach_header_64 *)unity_vmaddr_slide, "__TEXT", "__text", &size);
    if (!il2cpp_section_base) return -1;
  };
  uint8_t *il2cpp_end = (uint8_t *)(size + il2cpp_section_base);
  if (il2cpp_section_base + 4 >= size + il2cpp_section_base) return -1;
  uintptr_t first_instruction = *(uint32_t *)il2cpp_section_base;
  uintptr_t resolved_address, function_offset, second_instruction;
  while (1) {
    second_instruction = *(uint32_t *)(il2cpp_section_base + 4);
    if ((first_instruction & 0x9F000000) == 0x90000000 && (second_instruction & 0xFF800000) == 0x91000000) {
      resolved_address = (il2cpp_section_base & 0xFFFFFFFFFFFFF000LL) + (uint32_t)((((first_instruction >> 3) & 0xFFFFFFFC) | ((first_instruction >> 29) & 3)) << 12);
      function_offset = (second_instruction >> 10) & 0xFFF;
      if ((second_instruction & 0xC00000) != 0) function_offset <<= 12;
      if ((uint8_t *)(resolved_address + function_offset) == unscaled_function_address) break;
    }
    il2cpp_section_base += 4;
    first_instruction = second_instruction;
    if ((uint8_t *)(il2cpp_section_base + 8) >= il2cpp_end) return -1;
  }
  uintptr_t current_address = il2cpp_section_base;
  uintptr_t current_instruction, code_section_address;
  do {
    current_instruction = *(uint32_t *)(current_address - 4);
    current_address -= 4;
  } while ((current_instruction & 0x9F000000) != 0x90000000);
  code_section_address = (current_address & 0xFFFFFFFFFFFFF000LL) + (uint32_t)((((current_instruction >> 3) & 0xFFFFFFFC) | ((current_instruction >> 29) & 3)) << 12);
  uintptr_t method_data = *(uint32_t *)(current_address + 4);
  uintptr_t function_data_offset;
  if ((method_data & 0x1000000) != 0)
    function_data_offset = 8 * ((method_data >> 10) & 0xFFF);
  else
    function_data_offset = (method_data >> 10) & 0xFFF;
  if (*(uintptr_t *)(code_section_address + function_data_offset)) {
    original_unscaled_time = *(float (**)(void))(function_data_offset + code_section_address);
  } else {
    typedef void *(*resolve_icall_t)(const char *);
    resolve_icall_t resolve_icall = (resolve_icall_t)dlsym(RTLD_DEFAULT, "il2cpp_resolve_icall");
    if (resolve_icall) {
      original_unscaled_time = (float (*)(void))resolve_icall((const char *)unscaled_function_address);
    }
  }
  if (original_unscaled_time) {
    *(uintptr_t *)(function_data_offset + code_section_address) = (uintptr_t)my_unscaled_time;
    return 0;
  }
  return -1;
}

void set_unscaledTime(float value) { unscaled_time_speed = value; }
void reset_unscaledTime(void) { unscaled_time_speed = 1.0; }

#pragma mark - Unity Time.get_timeSinceLevelLoad (关卡时间，可能被游戏clamp)
static float timeSinceLevelLoad_speed = 1.0;
static float (*original_timeSinceLevelLoad)(void) = NULL;
static float timeSinceLevelLoad_accumulated = 0;
static float timeSinceLevelLoad_last = 0;
static BOOL timeSinceLevelLoad_initialized = NO;

static float my_timeSinceLevelLoad(void) {
  if (!original_timeSinceLevelLoad) return 0;
  float current = original_timeSinceLevelLoad();
  if (!timeSinceLevelLoad_initialized) {
    timeSinceLevelLoad_initialized = YES;
    timeSinceLevelLoad_last = current;
    timeSinceLevelLoad_accumulated = current;
    return current;
  }
  float delta = current - timeSinceLevelLoad_last;
  if (delta < 0) delta = 0;
      // 弹窗可见时暂停(timeScale<=0 或 弹窗显示中), 弹窗消失立即恢复
    if (last_game_value <= 0.001f || g_popup_visible) {
        timeSinceLevelLoad_accumulated = current;
        timeSinceLevelLoad_last = current;
        return current;
    }
    timeSinceLevelLoad_accumulated = current;  // 不二次加速, 返回原始真实时间
  timeSinceLevelLoad_last = current;
  return timeSinceLevelLoad_accumulated;
}

int hook_time_timeSinceLevelLoad(void) {
  if (original_timeSinceLevelLoad) return 0;
  intptr_t unity_vmaddr_slide = 0;
  uint32_t image_count = _dyld_image_count();
  const char *image_name;
  for (uint32_t i = 0; i < image_count; ++i) {
    image_name = _dyld_get_image_name(i);
    if (strstr(image_name, "UnityFramework.framework/UnityFramework")) {
      unity_vmaddr_slide = _dyld_get_image_vmaddr_slide(i);
      break;
    }
  }
  if (!unity_vmaddr_slide) return -1;
  size_t size;
  uint8_t *cstring_section_data = (uint8_t *)getsectiondata((const struct mach_header_64 *)unity_vmaddr_slide, "__TEXT", "__cstring", &size);
  if (!cstring_section_data) return -1;
  const char *method_name = "UnityEngine.Time::get_timeSinceLevelLoad()";
  uint8_t *tsl_function_address = (uint8_t *)memmem(cstring_section_data, size, method_name, strlen(method_name));
  if (!tsl_function_address) return -1;
  uintptr_t il2cpp_section_base = 0;
  il2cpp_section_base = (uintptr_t)getsectiondata((const struct mach_header_64 *)unity_vmaddr_slide, "__TEXT", "il2cpp", &size);
  if (!il2cpp_section_base) {
    il2cpp_section_base = (uintptr_t)getsectiondata((const struct mach_header_64 *)unity_vmaddr_slide, "__TEXT", "__text", &size);
    if (!il2cpp_section_base) return -1;
  };
  uint8_t *il2cpp_end = (uint8_t *)(size + il2cpp_section_base);
  if (il2cpp_section_base + 4 >= size + il2cpp_section_base) return -1;
  uintptr_t first_instruction = *(uint32_t *)il2cpp_section_base;
  uintptr_t resolved_address, function_offset, second_instruction;
  while (1) {
    second_instruction = *(uint32_t *)(il2cpp_section_base + 4);
    if ((first_instruction & 0x9F000000) == 0x90000000 && (second_instruction & 0xFF800000) == 0x91000000) {
      resolved_address = (il2cpp_section_base & 0xFFFFFFFFFFFFF000LL) + (uint32_t)((((first_instruction >> 3) & 0xFFFFFFFC) | ((first_instruction >> 29) & 3)) << 12);
      function_offset = (second_instruction >> 10) & 0xFFF;
      if ((second_instruction & 0xC00000) != 0) function_offset <<= 12;
      if ((uint8_t *)(resolved_address + function_offset) == tsl_function_address) break;
    }
    il2cpp_section_base += 4;
    first_instruction = second_instruction;
    if ((uint8_t *)(il2cpp_section_base + 8) >= il2cpp_end) return -1;
  }
  uintptr_t current_address = il2cpp_section_base;
  uintptr_t current_instruction, code_section_address;
  do {
    current_instruction = *(uint32_t *)(current_address - 4);
    current_address -= 4;
  } while ((current_instruction & 0x9F000000) != 0x90000000);
  code_section_address = (current_address & 0xFFFFFFFFFFFFF000LL) + (uint32_t)((((current_instruction >> 3) & 0xFFFFFFFC) | ((current_instruction >> 29) & 3)) << 12);
  uintptr_t method_data = *(uint32_t *)(current_address + 4);
  uintptr_t function_data_offset;
  if ((method_data & 0x1000000) != 0)
    function_data_offset = 8 * ((method_data >> 10) & 0xFFF);
  else
    function_data_offset = (method_data >> 10) & 0xFFF;
  if (*(uintptr_t *)(code_section_address + function_data_offset)) {
    original_timeSinceLevelLoad = *(float (**)(void))(function_data_offset + code_section_address);
  } else {
    typedef void *(*resolve_icall_t)(const char *);
    resolve_icall_t resolve_icall = (resolve_icall_t)dlsym(RTLD_DEFAULT, "il2cpp_resolve_icall");
    if (resolve_icall) {
      original_timeSinceLevelLoad = (float (*)(void))resolve_icall((const char *)tsl_function_address);
    }
  }
  if (original_timeSinceLevelLoad) {
    *(uintptr_t *)(function_data_offset + code_section_address) = (uintptr_t)my_timeSinceLevelLoad;
    return 0;
  }
  return -1;
}

void set_timeSinceLevelLoad(float value) { timeSinceLevelLoad_speed = value; }
void reset_timeSinceLevelLoad(void) { timeSinceLevelLoad_speed = 1.0; }

static float gettimeofday_speed = 1.0;

#define USec_Scale (1000000LL)
static time_t gettimeofday_pre_sec = 0;
static suseconds_t gettimeofday_pre_usec = 0;
static time_t gettimeofday_true_pre_sec = 0;
static suseconds_t gettimeofday_true_pre_usec = 0;
static os_unfair_lock gettimeofday_lock = OS_UNFAIR_LOCK_INIT;

static int (*original_gettimeofday)(struct timeval *, void *) = NULL;

// my_gettimeofday fix from AccDemo
static int my_gettimeofday(struct timeval *tv, struct timezone *tz) {
  os_unfair_lock_lock(&gettimeofday_lock);
  int ret = original_gettimeofday(tv, tz);
  if (!ret) {
    if (!gettimeofday_pre_sec) {
      gettimeofday_pre_sec = tv->tv_sec;
      gettimeofday_true_pre_sec = tv->tv_sec;
      gettimeofday_pre_usec = tv->tv_usec;
      gettimeofday_true_pre_usec = tv->tv_usec;
    } else {
      int64_t true_curSec = tv->tv_sec * USec_Scale + tv->tv_usec;
      int64_t true_preSec = gettimeofday_true_pre_sec * USec_Scale + gettimeofday_true_pre_usec;
      int64_t invl = true_curSec - true_preSec;
      invl *= gettimeofday_speed;

      int64_t curSec = gettimeofday_pre_sec * USec_Scale + gettimeofday_pre_usec;
      curSec += invl;

      time_t used_sec = curSec / USec_Scale;
      suseconds_t used_usec = curSec % USec_Scale;

      gettimeofday_true_pre_sec = tv->tv_sec;
      gettimeofday_true_pre_usec = tv->tv_usec;
      tv->tv_sec = used_sec;
      tv->tv_usec = used_usec;
      gettimeofday_pre_sec = used_sec;
      gettimeofday_pre_usec = used_usec;
    }
  }
  os_unfair_lock_unlock(&gettimeofday_lock);
  return ret;
}

int hook_gettimeofday(void) {
  if (original_gettimeofday) return 0;
  MSHookFunctionPtr fn = _MSHookFunction();
  if (!fn) return -1;
  fn((void *)gettimeofday, (void *)my_gettimeofday, (void **)&original_gettimeofday);
  return original_gettimeofday ? 0 : -1;
}

void set_gettimeofday(float value) {
  os_unfair_lock_lock(&gettimeofday_lock);
  gettimeofday_speed = value;
  os_unfair_lock_unlock(&gettimeofday_lock);
}

void reset_gettimeofday(void) { set_gettimeofday(1.0); }

static float clock_gettime_speed = 1.0;

#define NSec_Scale (1000000000LL)
static time_t clock_gettime_pre_sec = 0;
static long clock_gettime_pre_nsec = 0;
static time_t clock_gettime_true_pre_sec = 0;
static long clock_gettime_true_pre_nsec = 0;
static os_unfair_lock clock_gettime_lock = OS_UNFAIR_LOCK_INIT;

static int (*original_clock_gettime)(clockid_t clock_id, struct timespec *tp) = NULL;

// my_clock_gettime fix from AccDemo
static int my_clock_gettime(clockid_t clk_id, struct timespec *tp) {
  os_unfair_lock_lock(&clock_gettime_lock);
#if TARGET_OS_TV
  int ret = original_clock_gettime(clk_id, tp);
#else
  int ret = port_clock_gettime(clk_id, tp);
#endif
  if (!ret) {
    if (!clock_gettime_pre_sec) {
      clock_gettime_pre_sec = tp->tv_sec;
      clock_gettime_true_pre_sec = tp->tv_sec;
      clock_gettime_pre_nsec = tp->tv_nsec;
      clock_gettime_true_pre_nsec = tp->tv_nsec;
    } else {
      int64_t true_curSec = tp->tv_sec * NSec_Scale + tp->tv_nsec;
      int64_t true_preSec = clock_gettime_true_pre_sec * NSec_Scale + clock_gettime_true_pre_nsec;
      int64_t invl = true_curSec - true_preSec;
      invl *= clock_gettime_speed;

      int64_t curSec = clock_gettime_pre_sec * NSec_Scale + clock_gettime_pre_nsec;
      curSec += invl;

      time_t used_sec = curSec / NSec_Scale;
      long used_nsec = curSec % NSec_Scale;

      clock_gettime_true_pre_sec = tp->tv_sec;
      clock_gettime_true_pre_nsec = tp->tv_nsec;
      tp->tv_sec = used_sec;
      tp->tv_nsec = used_nsec;
      clock_gettime_pre_sec = used_sec;
      clock_gettime_pre_nsec = used_nsec;
    }
  }
  os_unfair_lock_unlock(&clock_gettime_lock);
  return ret;
}

#if TARGET_OS_TV
int hook_clock_gettime(void) {
  if (original_clock_gettime) return 0;

  struct rebinding rebindings = { "clock_gettime", my_clock_gettime, (void *)&original_clock_gettime };
  return rebind_symbols(&rebindings, 1);
}
#else
int hook_clock_gettime(void) {
  if (original_clock_gettime) return 0;

  original_clock_gettime = dlsym(RTLD_DEFAULT, "clock_gettime");
  if (!original_clock_gettime) return -1;

  void *original[] = { (void *)original_clock_gettime };
  void *hooked[] = { (void *)my_clock_gettime };
  bool success = hwbp_hook(original, hooked, 1);

  if (!success) return -1;

  return 0;
}
#endif

void set_clock_gettime(float value) {
  os_unfair_lock_lock(&clock_gettime_lock);
  clock_gettime_speed = value;
  os_unfair_lock_unlock(&clock_gettime_lock);
}

#if TARGET_OS_TV
void reset_clock_gettime(void) { set_clock_gettime(1.0); }
#else
void reset_clock_gettime(void) {
  if (!original_clock_gettime) return;

  void *original[] = { (void *)original_clock_gettime };
  hwbp_unhook(original, 1);
  set_clock_gettime(1.0);
  original_clock_gettime = NULL;
}
#endif

static float mach_absolute_time_speed = 1.0;

static uint64_t mach_absolute_base_time = 0;
static uint64_t mach_absolute_start_time = 0;
static uint64_t mach_absolute_last_time = 0;
static os_unfair_lock mach_absolute_base_time_lock = OS_UNFAIR_LOCK_INIT;

static uint64_t (*original_mach_absolute_time)(void) = NULL;

static uint64_t my_mach_absolute_time(void) {
  os_unfair_lock_lock(&mach_absolute_base_time_lock);
  uint64_t current_time = original_mach_absolute_time();
  uint64_t result;

  if (mach_absolute_last_time) {
    uint64_t delta = current_time - mach_absolute_base_time;
    result = mach_absolute_start_time + (uint64_t)(delta * mach_absolute_time_speed);
    if (result <= mach_absolute_last_time) {
      mach_absolute_base_time = current_time;
      mach_absolute_start_time = mach_absolute_last_time + 1;
      result = mach_absolute_start_time;
    }
  } else {
    mach_absolute_base_time = current_time;
    mach_absolute_start_time = current_time;
    result = current_time;
  }

  mach_absolute_last_time = result;
  os_unfair_lock_unlock(&mach_absolute_base_time_lock);
  return result;
}

int hook_mach_absolute_time(void) {
  if (original_mach_absolute_time) return 0;
  MSHookFunctionPtr fn = _MSHookFunction();
  if (!fn) return -1;
  fn((void *)mach_absolute_time, (void *)my_mach_absolute_time, (void **)&original_mach_absolute_time);
  return original_mach_absolute_time ? 0 : -1;
}

void set_mach_absolute_time(float value) {
  os_unfair_lock_lock(&mach_absolute_base_time_lock);
  uint64_t current_time = original_mach_absolute_time();
  mach_absolute_base_time = current_time;
  mach_absolute_start_time = mach_absolute_last_time;
  mach_absolute_time_speed = value;
  os_unfair_lock_unlock(&mach_absolute_base_time_lock);
}

void reset_mach_absolute_time(void) { set_mach_absolute_time(1.0); }

// 只针对 UnityFramework hook mach_absolute_time，不影响系统库（避免 CoreAnimation 渲染卡死）
int hook_mach_absolute_time_in_unity(void) {
  if (original_mach_absolute_time) return 0;
  // 找到 UnityFramework
  void *unity_header = NULL;
  intptr_t unity_slide = 0;
  for (uint32_t i = 0; i < _dyld_image_count(); i++) {
    const struct mach_header *header = _dyld_get_image_header(i);
    if (header->magic != MH_MAGIC_64) continue;
    const char *name = _dyld_get_image_name(i);
    if (name && strstr(name, "UnityFramework")) {
      unity_header = (void *)header;
      unity_slide = _dyld_get_image_vmaddr_slide(i);
      break;
    }
  }
  if (!unity_header) return -1;
  struct rebinding rebindings = { "mach_absolute_time", my_mach_absolute_time, (void *)&original_mach_absolute_time };
  return rebind_symbols_image(unity_header, unity_slide, &rebindings, 1);
}

#pragma mark - Unity Time.get_fixedUnscaledTime (不受timeScale影响的固定时间，游戏计时器可能用这个)
static float fixedUnscaledTime_speed = 1.0;
static float (*original_fixedUnscaledTime)(void) = NULL;
static float fixedUnscaledTime_accumulated = 0;
static float fixedUnscaledTime_last = 0;
static BOOL fixedUnscaledTime_initialized = NO;

static float my_fixedUnscaledTime(void) {
  if (!original_fixedUnscaledTime) return 0;
  float current = original_fixedUnscaledTime();
  if (!fixedUnscaledTime_initialized) {
    fixedUnscaledTime_initialized = YES;
    fixedUnscaledTime_last = current;
    fixedUnscaledTime_accumulated = current;
    return current;
  }
  float delta = current - fixedUnscaledTime_last;
  if (delta < 0) delta = 0;
      // 弹窗可见时暂停(timeScale<=0 或 弹窗显示中), 弹窗消失立即恢复
    if (last_game_value <= 0.001f || g_popup_visible) {
        fixedUnscaledTime_accumulated = current;
        fixedUnscaledTime_last = current;
        return current;
    }
    fixedUnscaledTime_accumulated = current;  // 不二次加速, 返回原始真实时间
  fixedUnscaledTime_last = current;
  return fixedUnscaledTime_accumulated;
}

int hook_time_fixedUnscaledTime(void) {
  if (original_fixedUnscaledTime) return 0;
  intptr_t unity_vmaddr_slide = 0;
  uint32_t image_count = _dyld_image_count();
  const char *image_name;
  for (uint32_t i = 0; i < image_count; ++i) {
    image_name = _dyld_get_image_name(i);
    if (strstr(image_name, "UnityFramework.framework/UnityFramework")) {
      unity_vmaddr_slide = _dyld_get_image_vmaddr_slide(i);
      break;
    }
  }
  if (!unity_vmaddr_slide) return -1;
  size_t size;
  uint8_t *cstring_section_data = (uint8_t *)getsectiondata((const struct mach_header_64 *)unity_vmaddr_slide, "__TEXT", "__cstring", &size);
  if (!cstring_section_data) return -1;
  const char *method_name = "UnityEngine.Time::get_fixedUnscaledTime()";
  uint8_t *fut_function_address = (uint8_t *)memmem(cstring_section_data, size, method_name, strlen(method_name));
  if (!fut_function_address) return -1;
  uintptr_t il2cpp_section_base = 0;
  il2cpp_section_base = (uintptr_t)getsectiondata((const struct mach_header_64 *)unity_vmaddr_slide, "__TEXT", "il2cpp", &size);
  if (!il2cpp_section_base) {
    il2cpp_section_base = (uintptr_t)getsectiondata((const struct mach_header_64 *)unity_vmaddr_slide, "__TEXT", "__text", &size);
    if (!il2cpp_section_base) return -1;
  };
  uint8_t *il2cpp_end = (uint8_t *)(size + il2cpp_section_base);
  if (il2cpp_section_base + 4 >= size + il2cpp_section_base) return -1;
  uintptr_t first_instruction = *(uint32_t *)il2cpp_section_base;
  uintptr_t resolved_address, function_offset, second_instruction;
  while (1) {
    second_instruction = *(uint32_t *)(il2cpp_section_base + 4);
    if ((first_instruction & 0x9F000000) == 0x90000000 && (second_instruction & 0xFF800000) == 0x91000000) {
      resolved_address = (il2cpp_section_base & 0xFFFFFFFFFFFFF000LL) + (uint32_t)((((first_instruction >> 3) & 0xFFFFFFFC) | ((first_instruction >> 29) & 3)) << 12);
      function_offset = (second_instruction >> 10) & 0xFFF;
      if ((second_instruction & 0xC00000) != 0) function_offset <<= 12;
      if ((uint8_t *)(resolved_address + function_offset) == fut_function_address) break;
    }
    il2cpp_section_base += 4;
    first_instruction = second_instruction;
    if ((uint8_t *)(il2cpp_section_base + 8) >= il2cpp_end) return -1;
  }
  uintptr_t current_address = il2cpp_section_base;
  uintptr_t current_instruction, code_section_address;
  do {
    current_instruction = *(uint32_t *)(current_address - 4);
    current_address -= 4;
  } while ((current_instruction & 0x9F000000) != 0x90000000);
  code_section_address = (current_address & 0xFFFFFFFFFFFFF000LL) + (uint32_t)((((current_instruction >> 3) & 0xFFFFFFFC) | ((current_instruction >> 29) & 3)) << 12);
  uintptr_t method_data = *(uint32_t *)(current_address + 4);
  uintptr_t function_data_offset;
  if ((method_data & 0x1000000) != 0)
    function_data_offset = 8 * ((method_data >> 10) & 0xFFF);
  else
    function_data_offset = (method_data >> 10) & 0xFFF;
  if (*(uintptr_t *)(code_section_address + function_data_offset)) {
    original_fixedUnscaledTime = *(float (**)(void))(function_data_offset + code_section_address);
  } else {
    typedef void *(*resolve_icall_t)(const char *);
    resolve_icall_t resolve_icall = (resolve_icall_t)dlsym(RTLD_DEFAULT, "il2cpp_resolve_icall");
    if (resolve_icall) {
      original_fixedUnscaledTime = (float (*)(void))resolve_icall((const char *)fut_function_address);
    }
  }
  if (original_fixedUnscaledTime) {
    *(uintptr_t *)(function_data_offset + code_section_address) = (uintptr_t)my_fixedUnscaledTime;
    return 0;
  }
  return -1;
}

void set_fixedUnscaledTime(float value) { fixedUnscaledTime_speed = value; }
void reset_fixedUnscaledTime(void) { fixedUnscaledTime_speed = 1.0; }

#pragma mark - Unity Time.get_time (受timeScale影响的主时间，游戏计时器可能用这个)
static float time_speed = 1.0;
static float (*original_time)(void) = NULL;
static float time_accumulated = 0;
static float time_last = 0;
static BOOL time_initialized = NO;

static float my_time(void) {
  if (!original_time) return 0;
  float current = original_time();
  // Time.time本身已随timeScale加速, 不再二次累加, 避免时间暴涨/回跳触发游戏卡帧检测
  time_accumulated = current;
  time_last = current;
  int paused = (last_game_value <= 0.001f || g_popup_visible);
  return current;
}

int hook_time_time(void) {
  if (original_time) return 0;
  intptr_t unity_vmaddr_slide = 0;
  uint32_t image_count = _dyld_image_count();
  const char *image_name;
  for (uint32_t i = 0; i < image_count; ++i) {
    image_name = _dyld_get_image_name(i);
    if (strstr(image_name, "UnityFramework.framework/UnityFramework")) {
      unity_vmaddr_slide = _dyld_get_image_vmaddr_slide(i);
      break;
    }
  }
  if (!unity_vmaddr_slide) return -1;
  size_t size;
  uint8_t *cstring_section_data = (uint8_t *)getsectiondata((const struct mach_header_64 *)unity_vmaddr_slide, "__TEXT", "__cstring", &size);
  if (!cstring_section_data) return -1;
  const char *method_name = "UnityEngine.Time::get_time()";
  uint8_t *t_function_address = (uint8_t *)memmem(cstring_section_data, size, method_name, strlen(method_name));
  if (!t_function_address) return -1;
  uintptr_t il2cpp_section_base = 0;
  il2cpp_section_base = (uintptr_t)getsectiondata((const struct mach_header_64 *)unity_vmaddr_slide, "__TEXT", "il2cpp", &size);
  if (!il2cpp_section_base) {
    il2cpp_section_base = (uintptr_t)getsectiondata((const struct mach_header_64 *)unity_vmaddr_slide, "__TEXT", "__text", &size);
    if (!il2cpp_section_base) return -1;
  };
  uint8_t *il2cpp_end = (uint8_t *)(size + il2cpp_section_base);
  if (il2cpp_section_base + 4 >= size + il2cpp_section_base) return -1;
  uintptr_t first_instruction = *(uint32_t *)il2cpp_section_base;
  uintptr_t resolved_address, function_offset, second_instruction;
  while (1) {
    second_instruction = *(uint32_t *)(il2cpp_section_base + 4);
    if ((first_instruction & 0x9F000000) == 0x90000000 && (second_instruction & 0xFF800000) == 0x91000000) {
      resolved_address = (il2cpp_section_base & 0xFFFFFFFFFFFFF000LL) + (uint32_t)((((first_instruction >> 3) & 0xFFFFFFFC) | ((first_instruction >> 29) & 3)) << 12);
      function_offset = (second_instruction >> 10) & 0xFFF;
      if ((second_instruction & 0xC00000) != 0) function_offset <<= 12;
      if ((uint8_t *)(resolved_address + function_offset) == t_function_address) break;
    }
    il2cpp_section_base += 4;
    first_instruction = second_instruction;
    if ((uint8_t *)(il2cpp_section_base + 8) >= il2cpp_end) return -1;
  }
  uintptr_t current_address = il2cpp_section_base;
  uintptr_t current_instruction, code_section_address;
  do {
    current_instruction = *(uint32_t *)(current_address - 4);
    current_address -= 4;
  } while ((current_instruction & 0x9F000000) != 0x90000000);
  code_section_address = (current_address & 0xFFFFFFFFFFFFF000LL) + (uint32_t)((((current_instruction >> 3) & 0xFFFFFFFC) | ((current_instruction >> 29) & 3)) << 12);
  uintptr_t method_data = *(uint32_t *)(current_address + 4);
  uintptr_t function_data_offset;
  if ((method_data & 0x1000000) != 0)
    function_data_offset = 8 * ((method_data >> 10) & 0xFFF);
  else
    function_data_offset = (method_data >> 10) & 0xFFF;
  if (*(uintptr_t *)(code_section_address + function_data_offset)) {
    original_time = *(float (**)(void))(function_data_offset + code_section_address);
  } else {
    typedef void *(*resolve_icall_t)(const char *);
    resolve_icall_t resolve_icall = (resolve_icall_t)dlsym(RTLD_DEFAULT, "il2cpp_resolve_icall");
    if (resolve_icall) {
      original_time = (float (*)(void))resolve_icall((const char *)t_function_address);
    }
  }
  if (original_time) {
    *(uintptr_t *)(function_data_offset + code_section_address) = (uintptr_t)my_time;
    return 0;
  }
  return -1;
}

void set_time_time(float value) { time_speed = value; }
void reset_time_time(void) { time_speed = 1.0; }

#pragma mark - Unity Time.get_deltaTime (每帧增量，游戏可能用它累加时间，直接hook突破clamp)
static float deltaTime_speed = 1.0;
static float (*original_deltaTime)(void) = NULL;

static float my_deltaTime(void) {
  float result = original_deltaTime();
  // 用真实帧时长(deltaTime/倍率)判断掉帧, 避免10倍速下正常0.167s被误报
  float real_dt = result / (timeScale_speed > 0.001f ? timeScale_speed : 1.0f);
  if (real_dt > 0.05f) {
    static double last_stutter_log = 0;
    double now = CFAbsoluteTimeGetCurrent();
    if (now - last_stutter_log > 2.0) {
      last_stutter_log = now;
    }
  }
  // 软上限: 按倍率折算真实10帧(0.1s), 截断异常大帧防止物理/逻辑大步进拖垮下一帧
  float max_dt = timeScale_speed * 0.1f;
  if (max_dt < 0.1f) max_dt = 0.1f;
  if (result > max_dt) result = max_dt;
  return result;  // deltaTime不二次乘倍, 避免游戏卡帧检测误判
}

int hook_time_deltaTime(void) {
  if (original_deltaTime) return 0;
  intptr_t unity_vmaddr_slide = 0;
  uint32_t image_count = _dyld_image_count();
  const char *image_name;
  for (uint32_t i = 0; i < image_count; ++i) {
    image_name = _dyld_get_image_name(i);
    if (strstr(image_name, "UnityFramework.framework/UnityFramework")) {
      unity_vmaddr_slide = _dyld_get_image_vmaddr_slide(i);
      break;
    }
  }
  if (!unity_vmaddr_slide) return -1;
  size_t size;
  uint8_t *cstring_section_data = (uint8_t *)getsectiondata((const struct mach_header_64 *)unity_vmaddr_slide, "__TEXT", "__cstring", &size);
  if (!cstring_section_data) return -1;
  const char *method_name = "UnityEngine.Time::get_deltaTime()";
  uint8_t *dt_function_address = (uint8_t *)memmem(cstring_section_data, size, method_name, strlen(method_name));
  if (!dt_function_address) return -1;
  uintptr_t il2cpp_section_base = 0;
  il2cpp_section_base = (uintptr_t)getsectiondata((const struct mach_header_64 *)unity_vmaddr_slide, "__TEXT", "il2cpp", &size);
  if (!il2cpp_section_base) {
    il2cpp_section_base = (uintptr_t)getsectiondata((const struct mach_header_64 *)unity_vmaddr_slide, "__TEXT", "__text", &size);
    if (!il2cpp_section_base) return -1;
  };
  uint8_t *il2cpp_end = (uint8_t *)(size + il2cpp_section_base);
  if (il2cpp_section_base + 4 >= size + il2cpp_section_base) return -1;
  uintptr_t first_instruction = *(uint32_t *)il2cpp_section_base;
  uintptr_t resolved_address, function_offset, second_instruction;
  while (1) {
    second_instruction = *(uint32_t *)(il2cpp_section_base + 4);
    if ((first_instruction & 0x9F000000) == 0x90000000 && (second_instruction & 0xFF800000) == 0x91000000) {
      resolved_address = (il2cpp_section_base & 0xFFFFFFFFFFFFF000LL) + (uint32_t)((((first_instruction >> 3) & 0xFFFFFFFC) | ((first_instruction >> 29) & 3)) << 12);
      function_offset = (second_instruction >> 10) & 0xFFF;
      if ((second_instruction & 0xC00000) != 0) function_offset <<= 12;
      if ((uint8_t *)(resolved_address + function_offset) == dt_function_address) break;
    }
    il2cpp_section_base += 4;
    first_instruction = second_instruction;
    if ((uint8_t *)(il2cpp_section_base + 8) >= il2cpp_end) return -1;
  }
  uintptr_t current_address = il2cpp_section_base;
  uintptr_t current_instruction, code_section_address;
  do {
    current_instruction = *(uint32_t *)(current_address - 4);
    current_address -= 4;
  } while ((current_instruction & 0x9F000000) != 0x90000000);
  code_section_address = (current_address & 0xFFFFFFFFFFFFF000LL) + (uint32_t)((((current_instruction >> 3) & 0xFFFFFFFC) | ((current_instruction >> 29) & 3)) << 12);
  uintptr_t method_data = *(uint32_t *)(current_address + 4);
  uintptr_t function_data_offset;
  if ((method_data & 0x1000000) != 0)
    function_data_offset = 8 * ((method_data >> 10) & 0xFFF);
  else
    function_data_offset = (method_data >> 10) & 0xFFF;
  if (*(uintptr_t *)(code_section_address + function_data_offset)) {
    original_deltaTime = *(float (**)(void))(function_data_offset + code_section_address);
  } else {
    typedef void *(*resolve_icall_t)(const char *);
    resolve_icall_t resolve_icall = (resolve_icall_t)dlsym(RTLD_DEFAULT, "il2cpp_resolve_icall");
    if (resolve_icall) {
      original_deltaTime = (float (*)(void))resolve_icall((const char *)dt_function_address);
    }
  }
  if (original_deltaTime) {
    *(uintptr_t *)(function_data_offset + code_section_address) = (uintptr_t)my_deltaTime;
    return 0;
  }
  return -1;
}

void set_deltaTime(float value) { deltaTime_speed = value; }
void reset_deltaTime(void) { deltaTime_speed = 1.0; }

#pragma mark - Unity Application.set_targetFrameRate (强制高帧率，突破游戏30fps限制导致的4倍clamp)
static int targetFrameRate_override = 60;
static void (*original_setTargetFrameRate)(int) = NULL;

static void my_setTargetFrameRate(int value) {
  original_setTargetFrameRate(targetFrameRate_override);
}

int hook_targetFrameRate(void) {
  if (original_setTargetFrameRate) return 0;
  intptr_t unity_vmaddr_slide = 0;
  uint32_t image_count = _dyld_image_count();
  const char *image_name;
  for (uint32_t i = 0; i < image_count; ++i) {
    image_name = _dyld_get_image_name(i);
    if (strstr(image_name, "UnityFramework.framework/UnityFramework")) {
      unity_vmaddr_slide = _dyld_get_image_vmaddr_slide(i);
      break;
    }
  }
  if (!unity_vmaddr_slide) return -1;
  size_t size;
  uint8_t *cstring_section_data = (uint8_t *)getsectiondata((const struct mach_header_64 *)unity_vmaddr_slide, "__TEXT", "__cstring", &size);
  if (!cstring_section_data) return -1;
  const char *method_name = "UnityEngine.Application::set_targetFrameRate(System.Int32)";
  uint8_t *tfr_function_address = (uint8_t *)memmem(cstring_section_data, size, method_name, strlen(method_name));
  if (!tfr_function_address) return -1;
  uintptr_t il2cpp_section_base = 0;
  il2cpp_section_base = (uintptr_t)getsectiondata((const struct mach_header_64 *)unity_vmaddr_slide, "__TEXT", "il2cpp", &size);
  if (!il2cpp_section_base) {
    il2cpp_section_base = (uintptr_t)getsectiondata((const struct mach_header_64 *)unity_vmaddr_slide, "__TEXT", "__text", &size);
    if (!il2cpp_section_base) return -1;
  };
  uint8_t *il2cpp_end = (uint8_t *)(size + il2cpp_section_base);
  if (il2cpp_section_base + 4 >= size + il2cpp_section_base) return -1;
  uintptr_t first_instruction = *(uint32_t *)il2cpp_section_base;
  uintptr_t resolved_address, function_offset, second_instruction;
  while (1) {
    second_instruction = *(uint32_t *)(il2cpp_section_base + 4);
    if ((first_instruction & 0x9F000000) == 0x90000000 && (second_instruction & 0xFF800000) == 0x91000000) {
      resolved_address = (il2cpp_section_base & 0xFFFFFFFFFFFFF000LL) + (uint32_t)((((first_instruction >> 3) & 0xFFFFFFFC) | ((first_instruction >> 29) & 3)) << 12);
      function_offset = (second_instruction >> 10) & 0xFFF;
      if ((second_instruction & 0xC00000) != 0) function_offset <<= 12;
      if ((uint8_t *)(resolved_address + function_offset) == tfr_function_address) break;
    }
    il2cpp_section_base += 4;
    first_instruction = second_instruction;
    if ((uint8_t *)(il2cpp_section_base + 8) >= il2cpp_end) return -1;
  }
  uintptr_t current_address = il2cpp_section_base;
  uintptr_t current_instruction, code_section_address;
  do {
    current_instruction = *(uint32_t *)(current_address - 4);
    current_address -= 4;
  } while ((current_instruction & 0x9F000000) != 0x90000000);
  code_section_address = (current_address & 0xFFFFFFFFFFFFF000LL) + (uint32_t)((((current_instruction >> 3) & 0xFFFFFFFC) | ((current_instruction >> 29) & 3)) << 12);
  uintptr_t method_data = *(uint32_t *)(current_address + 4);
  uintptr_t function_data_offset;
  if ((method_data & 0x1000000) != 0)
    function_data_offset = 8 * ((method_data >> 10) & 0xFFF);
  else
    function_data_offset = (method_data >> 10) & 0xFFF;
  if (*(uintptr_t *)(code_section_address + function_data_offset)) {
    original_setTargetFrameRate = *(void (**)(int))(function_data_offset + code_section_address);
  } else {
    typedef void *(*resolve_icall_t)(const char *);
    resolve_icall_t resolve_icall = (resolve_icall_t)dlsym(RTLD_DEFAULT, "il2cpp_resolve_icall");
    if (resolve_icall) {
      original_setTargetFrameRate = (void (*)(int))resolve_icall((const char *)tfr_function_address);
    }
  }
  if (original_setTargetFrameRate) {
    *(uintptr_t *)(function_data_offset + code_section_address) = (uintptr_t)my_setTargetFrameRate;
    return 0;
  }
  return -1;
}

void set_targetFrameRate(int value) {
  targetFrameRate_override = value;
  if (original_setTargetFrameRate) my_setTargetFrameRate(value);
}
void reset_targetFrameRate(void) { targetFrameRate_override = 60; }

#pragma mark - Unity QualitySettings.set_vSyncCount (关闭垂直同步，配合高帧率突破clamp)
static int vSyncCount_override = 0;
static void (*original_setVSyncCount)(int) = NULL;

static void my_setVSyncCount(int value) {
  original_setVSyncCount(vSyncCount_override);
}

int hook_vSyncCount(void) {
  if (original_setVSyncCount) return 0;
  intptr_t unity_vmaddr_slide = 0;
  uint32_t image_count = _dyld_image_count();
  const char *image_name;
  for (uint32_t i = 0; i < image_count; ++i) {
    image_name = _dyld_get_image_name(i);
    if (strstr(image_name, "UnityFramework.framework/UnityFramework")) {
      unity_vmaddr_slide = _dyld_get_image_vmaddr_slide(i);
      break;
    }
  }
  if (!unity_vmaddr_slide) return -1;
  size_t size;
  uint8_t *cstring_section_data = (uint8_t *)getsectiondata((const struct mach_header_64 *)unity_vmaddr_slide, "__TEXT", "__cstring", &size);
  if (!cstring_section_data) return -1;
  const char *method_name = "UnityEngine.QualitySettings::set_vSyncCount(System.Int32)";
  uint8_t *vsc_function_address = (uint8_t *)memmem(cstring_section_data, size, method_name, strlen(method_name));
  if (!vsc_function_address) return -1;
  uintptr_t il2cpp_section_base = 0;
  il2cpp_section_base = (uintptr_t)getsectiondata((const struct mach_header_64 *)unity_vmaddr_slide, "__TEXT", "il2cpp", &size);
  if (!il2cpp_section_base) {
    il2cpp_section_base = (uintptr_t)getsectiondata((const struct mach_header_64 *)unity_vmaddr_slide, "__TEXT", "__text", &size);
    if (!il2cpp_section_base) return -1;
  };
  uint8_t *il2cpp_end = (uint8_t *)(size + il2cpp_section_base);
  if (il2cpp_section_base + 4 >= size + il2cpp_section_base) return -1;
  uintptr_t first_instruction = *(uint32_t *)il2cpp_section_base;
  uintptr_t resolved_address, function_offset, second_instruction;
  while (1) {
    second_instruction = *(uint32_t *)(il2cpp_section_base + 4);
    if ((first_instruction & 0x9F000000) == 0x90000000 && (second_instruction & 0xFF800000) == 0x91000000) {
      resolved_address = (il2cpp_section_base & 0xFFFFFFFFFFFFF000LL) + (uint32_t)((((first_instruction >> 3) & 0xFFFFFFFC) | ((first_instruction >> 29) & 3)) << 12);
      function_offset = (second_instruction >> 10) & 0xFFF;
      if ((second_instruction & 0xC00000) != 0) function_offset <<= 12;
      if ((uint8_t *)(resolved_address + function_offset) == vsc_function_address) break;
    }
    il2cpp_section_base += 4;
    first_instruction = second_instruction;
    if ((uint8_t *)(il2cpp_section_base + 8) >= il2cpp_end) return -1;
  }
  uintptr_t current_address = il2cpp_section_base;
  uintptr_t current_instruction, code_section_address;
  do {
    current_instruction = *(uint32_t *)(current_address - 4);
    current_address -= 4;
  } while ((current_instruction & 0x9F000000) != 0x90000000);
  code_section_address = (current_address & 0xFFFFFFFFFFFFF000LL) + (uint32_t)((((current_instruction >> 3) & 0xFFFFFFFC) | ((current_instruction >> 29) & 3)) << 12);
  uintptr_t method_data = *(uint32_t *)(current_address + 4);
  uintptr_t function_data_offset;
  if ((method_data & 0x1000000) != 0)
    function_data_offset = 8 * ((method_data >> 10) & 0xFFF);
  else
    function_data_offset = (method_data >> 10) & 0xFFF;
  if (*(uintptr_t *)(code_section_address + function_data_offset)) {
    original_setVSyncCount = *(void (**)(int))(function_data_offset + code_section_address);
  } else {
    typedef void *(*resolve_icall_t)(const char *);
    resolve_icall_t resolve_icall = (resolve_icall_t)dlsym(RTLD_DEFAULT, "il2cpp_resolve_icall");
    if (resolve_icall) {
      original_setVSyncCount = (void (*)(int))resolve_icall((const char *)vsc_function_address);
    }
  }
  if (original_setVSyncCount) {
    *(uintptr_t *)(function_data_offset + code_section_address) = (uintptr_t)my_setVSyncCount;
    return 0;
  }
  return -1;
}

void set_vSyncCount(int value) {
  vSyncCount_override = value;
  if (original_setVSyncCount) my_setVSyncCount(value);
}
void reset_vSyncCount(void) { vSyncCount_override = 0; }

#pragma mark - Unity Time.set_maximumDeltaTime (关键！游戏把帧时间上限设成0.133秒导致4倍封顶，强制设为10秒解除)
static float maximumDeltaTime_override = 0.333f;  // Unity默认, 稳定60帧下10倍速deltaTime≈0.167<0.333, 不影响加速; 避免掉帧时3秒+大步进拖垮帧率
static void (*original_setMaximumDeltaTime)(float) = NULL;
static float (*original_getMaximumDeltaTime)(void) = NULL;

static void my_setMaximumDeltaTime(float value) {
  if (original_setMaximumDeltaTime) original_setMaximumDeltaTime(maximumDeltaTime_override);
}

static float my_getMaximumDeltaTime(void) {
  return maximumDeltaTime_override;
}

int hook_maximumDeltaTime_set(void) {
  if (original_setMaximumDeltaTime) return 0;
  intptr_t unity_vmaddr_slide = 0;
  uint32_t image_count = _dyld_image_count();
  const char *image_name;
  for (uint32_t i = 0; i < image_count; ++i) {
    image_name = _dyld_get_image_name(i);
    if (strstr(image_name, "UnityFramework.framework/UnityFramework")) {
      unity_vmaddr_slide = _dyld_get_image_vmaddr_slide(i);
      break;
    }
  }
  if (!unity_vmaddr_slide) return -1;
  size_t size;
  uint8_t *cstring_section_data = (uint8_t *)getsectiondata((const struct mach_header_64 *)unity_vmaddr_slide, "__TEXT", "__cstring", &size);
  if (!cstring_section_data) return -1;
  const char *method_name = "UnityEngine.Time::set_maximumDeltaTime(System.Single)";
  uint8_t *mdt_function_address = (uint8_t *)memmem(cstring_section_data, size, method_name, strlen(method_name));
  if (!mdt_function_address) return -1;
  uintptr_t il2cpp_section_base = 0;
  il2cpp_section_base = (uintptr_t)getsectiondata((const struct mach_header_64 *)unity_vmaddr_slide, "__TEXT", "il2cpp", &size);
  if (!il2cpp_section_base) {
    il2cpp_section_base = (uintptr_t)getsectiondata((const struct mach_header_64 *)unity_vmaddr_slide, "__TEXT", "__text", &size);
    if (!il2cpp_section_base) return -1;
  };
  uint8_t *il2cpp_end = (uint8_t *)(size + il2cpp_section_base);
  if (il2cpp_section_base + 4 >= size + il2cpp_section_base) return -1;
  uintptr_t first_instruction = *(uint32_t *)il2cpp_section_base;
  uintptr_t resolved_address, function_offset, second_instruction;
  while (1) {
    second_instruction = *(uint32_t *)(il2cpp_section_base + 4);
    if ((first_instruction & 0x9F000000) == 0x90000000 && (second_instruction & 0xFF800000) == 0x91000000) {
      resolved_address = (il2cpp_section_base & 0xFFFFFFFFFFFFF000LL) + (uint32_t)((((first_instruction >> 3) & 0xFFFFFFFC) | ((first_instruction >> 29) & 3)) << 12);
      function_offset = (second_instruction >> 10) & 0xFFF;
      if ((second_instruction & 0xC00000) != 0) function_offset <<= 12;
      if ((uint8_t *)(resolved_address + function_offset) == mdt_function_address) break;
    }
    il2cpp_section_base += 4;
    first_instruction = second_instruction;
    if ((uint8_t *)(il2cpp_section_base + 8) >= il2cpp_end) return -1;
  }
  uintptr_t current_address = il2cpp_section_base;
  uintptr_t current_instruction, code_section_address;
  do {
    current_instruction = *(uint32_t *)(current_address - 4);
    current_address -= 4;
  } while ((current_instruction & 0x9F000000) != 0x90000000);
  code_section_address = (current_address & 0xFFFFFFFFFFFFF000LL) + (uint32_t)((((current_instruction >> 3) & 0xFFFFFFFC) | ((current_instruction >> 29) & 3)) << 12);
  uintptr_t method_data = *(uint32_t *)(current_address + 4);
  uintptr_t function_data_offset;
  if ((method_data & 0x1000000) != 0)
    function_data_offset = 8 * ((method_data >> 10) & 0xFFF);
  else
    function_data_offset = (method_data >> 10) & 0xFFF;
  if (*(uintptr_t *)(code_section_address + function_data_offset)) {
    original_setMaximumDeltaTime = *(void (**)(float))(function_data_offset + code_section_address);
  } else {
    typedef void *(*resolve_icall_t)(const char *);
    resolve_icall_t resolve_icall = (resolve_icall_t)dlsym(RTLD_DEFAULT, "il2cpp_resolve_icall");
    if (resolve_icall) {
      original_setMaximumDeltaTime = (void (*)(float))resolve_icall((const char *)mdt_function_address);
    }
  }
  if (original_setMaximumDeltaTime) {
    *(uintptr_t *)(function_data_offset + code_section_address) = (uintptr_t)my_setMaximumDeltaTime;
    return 0;
  }
  return -1;
}

int hook_maximumDeltaTime_get(void) {
  if (original_getMaximumDeltaTime) return 0;
  intptr_t unity_vmaddr_slide = 0;
  uint32_t image_count = _dyld_image_count();
  const char *image_name;
  for (uint32_t i = 0; i < image_count; ++i) {
    image_name = _dyld_get_image_name(i);
    if (strstr(image_name, "UnityFramework.framework/UnityFramework")) {
      unity_vmaddr_slide = _dyld_get_image_vmaddr_slide(i);
      break;
    }
  }
  if (!unity_vmaddr_slide) return -1;
  size_t size;
  uint8_t *cstring_section_data = (uint8_t *)getsectiondata((const struct mach_header_64 *)unity_vmaddr_slide, "__TEXT", "__cstring", &size);
  if (!cstring_section_data) return -1;
  const char *method_name = "UnityEngine.Time::get_maximumDeltaTime()";
  uint8_t *mdt_function_address = (uint8_t *)memmem(cstring_section_data, size, method_name, strlen(method_name));
  if (!mdt_function_address) return -1;
  uintptr_t il2cpp_section_base = 0;
  il2cpp_section_base = (uintptr_t)getsectiondata((const struct mach_header_64 *)unity_vmaddr_slide, "__TEXT", "il2cpp", &size);
  if (!il2cpp_section_base) {
    il2cpp_section_base = (uintptr_t)getsectiondata((const struct mach_header_64 *)unity_vmaddr_slide, "__TEXT", "__text", &size);
    if (!il2cpp_section_base) return -1;
  };
  uint8_t *il2cpp_end = (uint8_t *)(size + il2cpp_section_base);
  if (il2cpp_section_base + 4 >= size + il2cpp_section_base) return -1;
  uintptr_t first_instruction = *(uint32_t *)il2cpp_section_base;
  uintptr_t resolved_address, function_offset, second_instruction;
  while (1) {
    second_instruction = *(uint32_t *)(il2cpp_section_base + 4);
    if ((first_instruction & 0x9F000000) == 0x90000000 && (second_instruction & 0xFF800000) == 0x91000000) {
      resolved_address = (il2cpp_section_base & 0xFFFFFFFFFFFFF000LL) + (uint32_t)((((first_instruction >> 3) & 0xFFFFFFFC) | ((first_instruction >> 29) & 3)) << 12);
      function_offset = (second_instruction >> 10) & 0xFFF;
      if ((second_instruction & 0xC00000) != 0) function_offset <<= 12;
      if ((uint8_t *)(resolved_address + function_offset) == mdt_function_address) break;
    }
    il2cpp_section_base += 4;
    first_instruction = second_instruction;
    if ((uint8_t *)(il2cpp_section_base + 8) >= il2cpp_end) return -1;
  }
  uintptr_t current_address = il2cpp_section_base;
  uintptr_t current_instruction, code_section_address;
  do {
    current_instruction = *(uint32_t *)(current_address - 4);
    current_address -= 4;
  } while ((current_instruction & 0x9F000000) != 0x90000000);
  code_section_address = (current_address & 0xFFFFFFFFFFFFF000LL) + (uint32_t)((((current_instruction >> 3) & 0xFFFFFFFC) | ((current_instruction >> 29) & 3)) << 12);
  uintptr_t method_data = *(uint32_t *)(current_address + 4);
  uintptr_t function_data_offset;
  if ((method_data & 0x1000000) != 0)
    function_data_offset = 8 * ((method_data >> 10) & 0xFFF);
  else
    function_data_offset = (method_data >> 10) & 0xFFF;
  if (*(uintptr_t *)(code_section_address + function_data_offset)) {
    original_getMaximumDeltaTime = *(float (**)(void))(function_data_offset + code_section_address);
  } else {
    typedef void *(*resolve_icall_t)(const char *);
    resolve_icall_t resolve_icall = (resolve_icall_t)dlsym(RTLD_DEFAULT, "il2cpp_resolve_icall");
    if (resolve_icall) {
      original_getMaximumDeltaTime = (float (*)(void))resolve_icall((const char *)mdt_function_address);
    }
  }
  if (original_getMaximumDeltaTime) {
    *(uintptr_t *)(function_data_offset + code_section_address) = (uintptr_t)my_getMaximumDeltaTime;
    return 0;
  }
  return -1;
}

int hook_maximumDeltaTime(void) {
  int ret1 = hook_maximumDeltaTime_set();
  int ret2 = hook_maximumDeltaTime_get();
  return (ret1 == 0 || ret2 == 0) ? 0 : -1;
}

void set_maximumDeltaTime(float value) {
  maximumDeltaTime_override = value;
  if (original_setMaximumDeltaTime) my_setMaximumDeltaTime(value);
}
void reset_maximumDeltaTime(void) { maximumDeltaTime_override = 0.333f; }

#pragma mark - Unity Time.get_smoothDeltaTime (平滑增量时间，游戏可能用它代替deltaTime)
static float smoothDeltaTime_speed = 1.0;
static float (*original_smoothDeltaTime)(void) = NULL;

static float my_smoothDeltaTime(void) {
  float result = original_smoothDeltaTime();
  return result;  // smoothDeltaTime不二次乘倍
}

int hook_smoothDeltaTime(void) {
  if (original_smoothDeltaTime) return 0;
  intptr_t unity_vmaddr_slide = 0;
  uint32_t image_count = _dyld_image_count();
  const char *image_name;
  for (uint32_t i = 0; i < image_count; ++i) {
    image_name = _dyld_get_image_name(i);
    if (strstr(image_name, "UnityFramework.framework/UnityFramework")) {
      unity_vmaddr_slide = _dyld_get_image_vmaddr_slide(i);
      break;
    }
  }
  if (!unity_vmaddr_slide) return -1;
  size_t size;
  uint8_t *cstring_section_data = (uint8_t *)getsectiondata((const struct mach_header_64 *)unity_vmaddr_slide, "__TEXT", "__cstring", &size);
  if (!cstring_section_data) return -1;
  const char *method_name = "UnityEngine.Time::get_smoothDeltaTime()";
  uint8_t *sdt_function_address = (uint8_t *)memmem(cstring_section_data, size, method_name, strlen(method_name));
  if (!sdt_function_address) return -1;
  uintptr_t il2cpp_section_base = 0;
  il2cpp_section_base = (uintptr_t)getsectiondata((const struct mach_header_64 *)unity_vmaddr_slide, "__TEXT", "il2cpp", &size);
  if (!il2cpp_section_base) {
    il2cpp_section_base = (uintptr_t)getsectiondata((const struct mach_header_64 *)unity_vmaddr_slide, "__TEXT", "__text", &size);
    if (!il2cpp_section_base) return -1;
  };
  uint8_t *il2cpp_end = (uint8_t *)(size + il2cpp_section_base);
  if (il2cpp_section_base + 4 >= size + il2cpp_section_base) return -1;
  uintptr_t first_instruction = *(uint32_t *)il2cpp_section_base;
  uintptr_t resolved_address, function_offset, second_instruction;
  while (1) {
    second_instruction = *(uint32_t *)(il2cpp_section_base + 4);
    if ((first_instruction & 0x9F000000) == 0x90000000 && (second_instruction & 0xFF800000) == 0x91000000) {
      resolved_address = (il2cpp_section_base & 0xFFFFFFFFFFFFF000LL) + (uint32_t)((((first_instruction >> 3) & 0xFFFFFFFC) | ((first_instruction >> 29) & 3)) << 12);
      function_offset = (second_instruction >> 10) & 0xFFF;
      if ((second_instruction & 0xC00000) != 0) function_offset <<= 12;
      if ((uint8_t *)(resolved_address + function_offset) == sdt_function_address) break;
    }
    il2cpp_section_base += 4;
    first_instruction = second_instruction;
    if ((uint8_t *)(il2cpp_section_base + 8) >= il2cpp_end) return -1;
  }
  uintptr_t current_address = il2cpp_section_base;
  uintptr_t current_instruction, code_section_address;
  do {
    current_instruction = *(uint32_t *)(current_address - 4);
    current_address -= 4;
  } while ((current_instruction & 0x9F000000) != 0x90000000);
  code_section_address = (current_address & 0xFFFFFFFFFFFFF000LL) + (uint32_t)((((current_instruction >> 3) & 0xFFFFFFFC) | ((current_instruction >> 29) & 3)) << 12);
  uintptr_t method_data = *(uint32_t *)(current_address + 4);
  uintptr_t function_data_offset;
  if ((method_data & 0x1000000) != 0)
    function_data_offset = 8 * ((method_data >> 10) & 0xFFF);
  else
    function_data_offset = (method_data >> 10) & 0xFFF;
  if (*(uintptr_t *)(code_section_address + function_data_offset)) {
    original_smoothDeltaTime = *(float (**)(void))(function_data_offset + code_section_address);
  } else {
    typedef void *(*resolve_icall_t)(const char *);
    resolve_icall_t resolve_icall = (resolve_icall_t)dlsym(RTLD_DEFAULT, "il2cpp_resolve_icall");
    if (resolve_icall) {
      original_smoothDeltaTime = (float (*)(void))resolve_icall((const char *)sdt_function_address);
    }
  }
  if (original_smoothDeltaTime) {
    *(uintptr_t *)(function_data_offset + code_section_address) = (uintptr_t)my_smoothDeltaTime;
    return 0;
  }
  return -1;
}

void set_smoothDeltaTime(float value) { smoothDeltaTime_speed = value; }
void reset_smoothDeltaTime(void) { smoothDeltaTime_speed = 1.0; }
