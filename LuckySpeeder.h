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

#ifndef LuckySpeeder_H
#define LuckySpeeder_H

#ifdef __cplusplus
extern "C" {
#endif

#define HOOK_SUCCESS 0

int hook_timeScale(void);
void set_popup_visible(int visible);  // 弹窗可见性: 1=弹窗显示(暂停), 0=弹窗消失(恢复)
void force_resume(void);  // 强制恢复加速(选完牌后调用)
int is_paused(void);  // 是否暂停状态(timeScale<=0或弹窗显示中)

void set_timeScale(float value);

void reset_timeScale(void);

int hook_time_realtimeSinceStartup(void);
void set_realtime(float value);
void reset_realtime(void);

int hook_time_unscaledTime(void);
void set_unscaledTime(float value);
void reset_unscaledTime(void);

int hook_time_timeSinceLevelLoad(void);
void set_timeSinceLevelLoad(float value);
void reset_timeSinceLevelLoad(void);

int hook_time_fixedUnscaledTime(void);
void set_fixedUnscaledTime(float value);
void reset_fixedUnscaledTime(void);

int hook_time_time(void);
void set_time_time(float value);
void reset_time_time(void);

int hook_time_deltaTime(void);
void set_deltaTime(float value);
void reset_deltaTime(void);
int hook_targetFrameRate(void);
void set_targetFrameRate(int value);
void reset_targetFrameRate(void);
int hook_vSyncCount(void);
void set_vSyncCount(int value);
void reset_vSyncCount(void);
int hook_maximumDeltaTime(void);
void set_maximumDeltaTime(float value);
void reset_maximumDeltaTime(void);
int hook_smoothDeltaTime(void);
void set_smoothDeltaTime(float value);
void reset_smoothDeltaTime(void);

int hook_gettimeofday(void);

void set_gettimeofday(float value);

void reset_gettimeofday(void);

int hook_clock_gettime(void);

void set_clock_gettime(float value);

void reset_clock_gettime(void);

int hook_mach_absolute_time(void);

void set_mach_absolute_time(float value);

void reset_mach_absolute_time(void);

int hook_SKScene_update(void);

void set_SKScene_update(float value);

void reset_SKScene_update(void);

#ifdef __cplusplus
}
#endif

#endif // LuckySpeeder_H
