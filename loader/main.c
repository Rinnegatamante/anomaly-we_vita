/* main.c -- Anomaly Warzone Earth HD .so loader
 *
 * Copyright (C) 2021 Andy Nguyen
 * Copyright (C) 2023 Rinnegatamante
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.	See the LICENSE file for details.
 */

#include <vitasdk.h>
#include <kubridge.h>
#include <vitashark.h>
#include <vitaGL.h>

#define AL_ALEXT_PROTOTYPES
#include <AL/alext.h>
#include <AL/efx.h>

#include <malloc.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <wchar.h>
#include <wctype.h>

#include <math.h>
#include <math_neon.h>

#include <errno.h>
#include <ctype.h>
#include <setjmp.h>
#include <sys/time.h>
#include <sys/stat.h>

#include "main.h"
#include "config.h"
#include "dialog.h"
#include "so_util.h"
#include "sha1.h"

extern const char *BIONIC_ctype_;
extern const short *BIONIC_tolower_tab_;
extern const short *BIONIC_toupper_tab_;

int pstv_mode = 0;
int enable_dlcs = 0;

int _newlib_heap_size_user = MEMORY_NEWLIB_MB * 1024 * 1024;

unsigned int _pthread_stack_default_user = 1 * 1024 * 1024;

unsigned int _oal_thread_priority = 64;
unsigned int _oal_thread_affinity = 0x40000;

so_module funky_mod;

void *__wrap_memcpy(void *dest, const void *src, size_t n) {
	return sceClibMemcpy(dest, src, n);
}

void *__wrap_memmove(void *dest, const void *src, size_t n) {
	return sceClibMemmove(dest, src, n);
}

void *__wrap_memset(void *s, int c, size_t n) {
	return sceClibMemset(s, c, n);
}

int debugPrintf(char *text, ...) {
#ifdef DEBUG
	va_list list;
	static char string[0x8000];

	va_start(list, text);
	vsprintf(string, text, list);
	va_end(list);

	printf(string);
#endif
	return 0;
}

int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
#ifdef DEBUG
	va_list list;
	static char string[0x8000];

	va_start(list, fmt);
	vsprintf(string, fmt, list);
	va_end(list);

	debugPrintf("[LOG] %s: %s\n", tag, string);
#endif
	return 0;
}

int __android_log_write(int prio, const char *tag, const char *text) {
#ifdef DEBUG
	 printf("[LOG] %s: %s\n", tag, text);
#endif
	return 0;
}

int ret0(void) {
	return 0;
}

int ret1(void) {
	return 1;
}

int clock_gettime(int clk_ik, struct timespec *t) {
	struct timeval now;
	int rv = gettimeofday(&now, NULL);
	if (rv)
		return rv;
	t->tv_sec = now.tv_sec;
	t->tv_nsec = now.tv_usec * 1000;
	return 0;
}

int pthread_mutexattr_init_fake(pthread_mutexattr_t **uid) {
	pthread_mutexattr_t *m = calloc(1, sizeof(pthread_mutexattr_t));
	if (!m)
		return -1;
	
	int ret = pthread_mutexattr_init(m);
	if (ret < 0) {
		free(m);
		return -1;
	}

	*uid = m;

	return 0;
}

int pthread_mutexattr_destroy_fake(pthread_mutexattr_t **m) {
	if (m && *m) {
		pthread_mutexattr_destroy(*m);
		free(*m);
		*m = NULL;
	}
	return 0;
}

int pthread_mutexattr_settype_fake(pthread_mutexattr_t **m, int type) {
	pthread_mutexattr_settype(*m, type);
	return 0;
}

int pthread_mutex_init_fake(pthread_mutex_t **uid, const pthread_mutexattr_t **mutexattr) {
	pthread_mutex_t *m = calloc(1, sizeof(pthread_mutex_t));
	if (!m)
		return -1;
	
	int ret = pthread_mutex_init(m, mutexattr ? *mutexattr : NULL);
	if (ret < 0) {
		free(m);
		return -1;
	}

	*uid = m;

	return 0;
}

int pthread_mutex_destroy_fake(pthread_mutex_t **uid) {
	if (uid && *uid && (uintptr_t)*uid > 0x8000) {
		pthread_mutex_destroy(*uid);
		free(*uid);
		*uid = NULL;
	}
	return 0;
}

int pthread_mutex_lock_fake(pthread_mutex_t **uid) {
	int ret = 0;
	if (!*uid) {
		ret = pthread_mutex_init_fake(uid, NULL);
	} else if ((uintptr_t)*uid == 0x4000) {
		pthread_mutexattr_t attr;
		pthread_mutexattr_init_fake(&attr);
		pthread_mutexattr_settype_fake(&attr, PTHREAD_MUTEX_RECURSIVE);
		ret = pthread_mutex_init_fake(uid, &attr);
		pthread_mutexattr_destroy_fake(&attr);
	} else if ((uintptr_t)*uid == 0x8000) {
		pthread_mutexattr_t attr;
		pthread_mutexattr_init_fake(&attr);
		pthread_mutexattr_settype_fake(&attr, PTHREAD_MUTEX_ERRORCHECK);
		ret = pthread_mutex_init_fake(uid, &attr);
		pthread_mutexattr_destroy_fake(&attr);
	}
	if (ret < 0)
		return ret;
	return pthread_mutex_lock(*uid);
}

int pthread_mutex_unlock_fake(pthread_mutex_t **uid) {
	int ret = 0;
	if (!*uid) {
		ret = pthread_mutex_init_fake(uid, NULL);
	} else if ((uintptr_t)*uid == 0x4000) {
		pthread_mutexattr_t attr;
		pthread_mutexattr_init_fake(&attr);
		pthread_mutexattr_settype_fake(&attr, PTHREAD_MUTEX_RECURSIVE);
		ret = pthread_mutex_init_fake(uid, &attr);
		pthread_mutexattr_destroy_fake(&attr);
	} else if ((uintptr_t)*uid == 0x8000) {
		pthread_mutexattr_t attr;
		pthread_mutexattr_init_fake(&attr);
		pthread_mutexattr_settype_fake(&attr, PTHREAD_MUTEX_ERRORCHECK);
		ret = pthread_mutex_init_fake(uid, &attr);
		pthread_mutexattr_destroy_fake(&attr);
	}
	if (ret < 0)
		return ret;
	return pthread_mutex_unlock(*uid);
}

int pthread_cond_init_fake(pthread_cond_t **cnd, const int *condattr) {
	pthread_cond_t *c = calloc(1, sizeof(pthread_cond_t));
	if (!c)
		return -1;

	*c = PTHREAD_COND_INITIALIZER;

	int ret = pthread_cond_init(c, NULL);
	if (ret < 0) {
		free(c);
		return -1;
	}

	*cnd = c;

	return 0;
}

int pthread_cond_broadcast_fake(pthread_cond_t **cnd) {
	if (!*cnd) {
		if (pthread_cond_init_fake(cnd, NULL) < 0)
			return -1;
	}
	return pthread_cond_broadcast(*cnd);
}

int pthread_cond_signal_fake(pthread_cond_t **cnd) {
	if (!*cnd) {
		if (pthread_cond_init_fake(cnd, NULL) < 0)
			return -1;
	}
	return pthread_cond_signal(*cnd);
}

int pthread_cond_destroy_fake(pthread_cond_t **cnd) {
	if (cnd && *cnd) {
		pthread_cond_destroy(*cnd);
		free(*cnd);
		*cnd = NULL;
	}
	return 0;
}

int pthread_cond_wait_fake(pthread_cond_t **cnd, pthread_mutex_t **mtx) {
	if (!*cnd) {
		if (pthread_cond_init_fake(cnd, NULL) < 0)
			return -1;
	}
	return pthread_cond_wait(*cnd, *mtx);
}

int pthread_cond_timedwait_fake(pthread_cond_t **cnd, pthread_mutex_t **mtx, const struct timespec *t) {
	if (!*cnd) {
		if (pthread_cond_init_fake(cnd, NULL) < 0)
			return -1;
	}
	return pthread_cond_timedwait(*cnd, *mtx, t);
}

int pthread_create_fake(pthread_t *thread, const void *unused, void *entry, void *arg) {
	return pthread_create(thread, NULL, entry, arg);
}

int pthread_mutex_trylock_fake(pthread_mutex_t **uid) {
	printf("pthread_mutex_trylock %x\n", uid);
	int ret = 0;
	if (!*uid) {
		ret = pthread_mutex_init_fake(uid, NULL);
	} else if ((uintptr_t)*uid == 0x4000) {
		pthread_mutexattr_t attr;
		pthread_mutexattr_init(&attr);
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
		ret = pthread_mutex_init_fake(uid, &attr);
		pthread_mutexattr_destroy(&attr);
	} else if ((uintptr_t)*uid == 0x8000) {
		pthread_mutexattr_t attr;
		pthread_mutexattr_init(&attr);
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
		ret = pthread_mutex_init_fake(uid, &attr);
		pthread_mutexattr_destroy(&attr);
	}
	if (ret < 0)
		return ret;
	return pthread_mutex_trylock(*uid);
}

int pthread_once_fake(volatile int *once_control, void (*init_routine)(void)) {
	if (!once_control || !init_routine)
		return -1;
	if (__sync_lock_test_and_set(once_control, 1) == 0)
		(*init_routine)();
	return 0;
}

int sem_init_fake(int *uid, int pshared, unsigned value) {
	*uid = sceKernelCreateSema("sema", 0, value, 0x7fffffff, NULL);
	if (*uid < 0)
		return -1;
	return 0;
}

int sem_post_fake(int *uid) {
	if (sceKernelSignalSema(*uid, 1) < 0)
		return -1;
	return 0;
}

int sem_wait_fake(int *uid) {
	if (sceKernelWaitSema(*uid, 1, NULL) < 0)
		return -1;
	return 0;
}

int sem_timedwait_fake(int *uid, const struct timespec *abstime) {
	struct timespec now = {0};
	clock_gettime(0, &now);
	SceUInt timeout = (abstime->tv_sec * 1000 * 1000 + abstime->tv_nsec / 1000) - (now.tv_sec * 1000 * 1000 + now.tv_nsec / 1000);
	if (timeout < 0)
		timeout = 0;
	if (sceKernelWaitSema(*uid, 1, &timeout) < 0)
		return -1;
	return 0;
}

int sem_destroy_fake(int *uid) {
	if (sceKernelDeleteSema(*uid) < 0)
		return -1;
	return 0;
}

static int *TotalMemeorySizeInMB = NULL;

void DeteremineSystemMemory(void) {
	*TotalMemeorySizeInMB = MEMORY_NEWLIB_MB;
}

int FileSystem__IsAbsolutePath(void *this, const char *path) {
	return (strncmp(path, "ux0:", 4) == 0) || (strncmp(path, "app0:", 5) == 0);
}

char *ShaderManager__GetShaderPath(void) {
	return "app0:/Common/Shaders";
}

void PresentGLContext(void) {
	vglSwapBuffers(GL_FALSE);
}

void *(* _Znwj)(size_t size);
void *(* CountingSemaphore__Constructor)(void *this, int value);
void (* BaseThread__BeginMessage)(void *this, int msg, int size);
void (* BaseThread__EndMessage)(void *this);
void (* BaseThread___ThreadCode)(void *this);

int BaseThread__ThreadCodeDispatch(SceSize args, uintptr_t *argp) {
	void *this = (void *)argp[0];
	BaseThread___ThreadCode(this);
	return sceKernelExitDeleteThread(0);
}

void BaseThread__Init(void *this) {
	*(void **)(this + 0xC0) = _Znwj(4);
	*(void **)(this + 0xC4) = _Znwj(4);
	CountingSemaphore__Constructor(*(void **)(this + 0xC0), 0);
	CountingSemaphore__Constructor(*(void **)(this + 0xC4), 0);

	int priority;
	int affinity;

	char *name = *(char **)(this + 0xB4);

	if (strcmp(name, "This War of Mine") == 0) {
		priority = 64;
		affinity = 0x10000;
	} else if (strcmp(name, "Renderer") == 0) {
		priority = 64;
		affinity = 0x20000;
	} else if (strcmp(name, "SoundEngine") == 0) {
		priority = 65;
		affinity = 0x20000;
	} else if (strcmp(name, "PhysicalFileReader") == 0) {
		priority = 65;
		affinity = 0x40000;
	} else if (strcmp(name, "ResourceManager") == 0) {
		priority = 66;
		affinity = 0x40000;
	} else if (strcmp(name, "GameConsole") == 0) {
		priority = 127;
		affinity = 0x40000;
	} else {
		priority = 0x10000100;
		affinity = 0;
	}

	SceUID thid = sceKernelCreateThread(name, (SceKernelThreadEntry)BaseThread__ThreadCodeDispatch, priority, 128 * 1024, 0, affinity, NULL);
	if (thid >= 0) {
		uintptr_t args[1];
		args[0] = (uintptr_t)this;
		sceKernelStartThread(thid, sizeof(args), args);
	}

	*(uint32_t *)(this + 0xD0) = *(uint32_t *)(this + 0xCC) = thid;

	BaseThread__BeginMessage(this, 1, 0);
	BaseThread__EndMessage(this);
}

int GetCurrentThreadId(void) {
	return sceKernelGetThreadId();
}

extern void *__cxa_guard_acquire;
extern void *__cxa_guard_release;

uint32_t rumble_tick;
void VibrateController(void *this, float strength1, float strength2, uint32_t a4) {
	if (strength1 > 0.0f || strength2 > 0.0f) {
		SceCtrlActuator handle;
		handle.small = 100;
		handle.large = 100;
		sceCtrlSetActuator(1, &handle);
		rumble_tick = sceKernelGetProcessTimeWide();
	}
}

void StopRumble (void) {
	SceCtrlActuator handle;
	handle.small = 0;
	handle.large = 0;
	sceCtrlSetActuator(1, &handle);
	rumble_tick = 0;
}

void _sync_synchronize() {
	__sync_synchronize();
}

FILE *OpenJetFile(char *fname, char *mode) {
	FILE *f;
	char real_fname[256];
	printf("OpenJetFile(%s)\n", fname);
	if (strncmp(fname, "ux0:", 4)) {
		sprintf(real_fname, "%s/assets%s.jet", DATA_PATH, fname);
		printf("Resolved to %s\n", real_fname);
		f = fopen(real_fname, "rb");
	} else {
		f = fopen(fname, "rb");
	}
	return f;
}

int GameConsolePrint(void *this, int a1, int a2, char *text, ...) {
	//#ifdef DEBUG
	va_list list;
	static char string[0x8000];

	va_start(list, text);
	vsprintf(string, text, list);
	va_end(list);

	printf("GameConsolePrint: %s\n", string);
//#endif
	return 0;
}

void *SendRequestServer(const char *a1, int a2, int *res) {
	void *(*NameStringGen)(void *this, char *text) = so_symbol(&funky_mod, "_ZN10NameStringC1EPKc");
	int (*NameStringSet)(void *this, void *text) = so_symbol(&funky_mod, "_ZN10NameString3SetERKS_");
	NameStringGen(a1, 0);
	NameStringSet(a1, (uintptr_t)funky_mod.text_base + 0x442D0C);
	printf("SendRequestToServer\n");
	*res = 1;
	return a1;
}

void patch_game(void) {
	hook_addr(so_symbol(&funky_mod, "_ZNK19ShaderProgramObject21_SetDummyBoneMatricesEj"), (uintptr_t)ret0);
	//hook_addr(so_symbol(&funky_mod, "_ZN26XRayServerRequestInternals12_SendRequestEPKc"), (uintptr_t)ret0);
	hook_addr((uintptr_t)funky_mod.text_base + 0x31B0DC, (uintptr_t)_sync_synchronize);
	hook_addr(so_symbol(&funky_mod, "_Z11OpenJetFilePKcPj"), (uintptr_t)OpenJetFile);
	
	hook_addr(so_symbol(&funky_mod, "alAuxiliaryEffectSlotf"), (uintptr_t)alAuxiliaryEffectSlotf);
	hook_addr(so_symbol(&funky_mod, "alAuxiliaryEffectSlotfv"), (uintptr_t)alAuxiliaryEffectSlotfv);
	hook_addr(so_symbol(&funky_mod, "alAuxiliaryEffectSloti"), (uintptr_t)alAuxiliaryEffectSloti);
	hook_addr(so_symbol(&funky_mod, "alAuxiliaryEffectSlotiv"), (uintptr_t)alAuxiliaryEffectSlotiv);
	hook_addr(so_symbol(&funky_mod, "alBuffer3f"), (uintptr_t)alBuffer3f);
	hook_addr(so_symbol(&funky_mod, "alBuffer3i"), (uintptr_t)alBuffer3i);
	hook_addr(so_symbol(&funky_mod, "alBufferData"), (uintptr_t)alBufferData);
	hook_addr(so_symbol(&funky_mod, "alBufferSamplesSOFT"), (uintptr_t)alBufferSamplesSOFT);
	hook_addr(so_symbol(&funky_mod, "alBufferSubDataSOFT"), (uintptr_t)alBufferSubDataSOFT);
	hook_addr(so_symbol(&funky_mod, "alBufferSubSamplesSOFT"), (uintptr_t)alBufferSubSamplesSOFT);
	hook_addr(so_symbol(&funky_mod, "alBufferf"), (uintptr_t)alBufferf);
	hook_addr(so_symbol(&funky_mod, "alBufferfv"), (uintptr_t)alBufferfv);
	hook_addr(so_symbol(&funky_mod, "alBufferi"), (uintptr_t)alBufferi);
	hook_addr(so_symbol(&funky_mod, "alBufferiv"), (uintptr_t)alBufferiv);
	hook_addr(so_symbol(&funky_mod, "alDeferUpdatesSOFT"), (uintptr_t)alDeferUpdatesSOFT);
	hook_addr(so_symbol(&funky_mod, "alDeleteAuxiliaryEffectSlots"), (uintptr_t)alDeleteAuxiliaryEffectSlots);
	hook_addr(so_symbol(&funky_mod, "alDeleteBuffers"), (uintptr_t)alDeleteBuffers);
	hook_addr(so_symbol(&funky_mod, "alDeleteEffects"), (uintptr_t)alDeleteEffects);
	hook_addr(so_symbol(&funky_mod, "alDeleteFilters"), (uintptr_t)alDeleteFilters);
	hook_addr(so_symbol(&funky_mod, "alDeleteSources"), (uintptr_t)alDeleteSources);
	hook_addr(so_symbol(&funky_mod, "alDisable"), (uintptr_t)alDisable);
	hook_addr(so_symbol(&funky_mod, "alDistanceModel"), (uintptr_t)alDistanceModel);
	hook_addr(so_symbol(&funky_mod, "alDopplerFactor"), (uintptr_t)alDopplerFactor);
	hook_addr(so_symbol(&funky_mod, "alDopplerVelocity"), (uintptr_t)alDopplerVelocity);
	hook_addr(so_symbol(&funky_mod, "alEffectf"), (uintptr_t)alEffectf);
	hook_addr(so_symbol(&funky_mod, "alEffectfv"), (uintptr_t)alEffectfv);
	hook_addr(so_symbol(&funky_mod, "alEffecti"), (uintptr_t)alEffecti);
	hook_addr(so_symbol(&funky_mod, "alEffectiv"), (uintptr_t)alEffectiv);
	hook_addr(so_symbol(&funky_mod, "alEnable"), (uintptr_t)alEnable);
	hook_addr(so_symbol(&funky_mod, "alFilterf"), (uintptr_t)alFilterf);
	hook_addr(so_symbol(&funky_mod, "alFilterfv"), (uintptr_t)alFilterfv);
	hook_addr(so_symbol(&funky_mod, "alFilteri"), (uintptr_t)alFilteri);
	hook_addr(so_symbol(&funky_mod, "alFilteriv"), (uintptr_t)alFilteriv);
	hook_addr(so_symbol(&funky_mod, "alGenBuffers"), (uintptr_t)alGenBuffers);
	hook_addr(so_symbol(&funky_mod, "alGenEffects"), (uintptr_t)alGenEffects);
	hook_addr(so_symbol(&funky_mod, "alGenFilters"), (uintptr_t)alGenFilters);
	hook_addr(so_symbol(&funky_mod, "alGenSources"), (uintptr_t)alGenSources);
	hook_addr(so_symbol(&funky_mod, "alGetAuxiliaryEffectSlotf"), (uintptr_t)alGetAuxiliaryEffectSlotf);
	hook_addr(so_symbol(&funky_mod, "alGetAuxiliaryEffectSlotfv"), (uintptr_t)alGetAuxiliaryEffectSlotfv);
	hook_addr(so_symbol(&funky_mod, "alGetAuxiliaryEffectSloti"), (uintptr_t)alGetAuxiliaryEffectSloti);
	hook_addr(so_symbol(&funky_mod, "alGetAuxiliaryEffectSlotiv"), (uintptr_t)alGetAuxiliaryEffectSlotiv);
	hook_addr(so_symbol(&funky_mod, "alGetBoolean"), (uintptr_t)alGetBoolean);
	hook_addr(so_symbol(&funky_mod, "alGetBooleanv"), (uintptr_t)alGetBooleanv);
	hook_addr(so_symbol(&funky_mod, "alGetBuffer3f"), (uintptr_t)alGetBuffer3f);
	hook_addr(so_symbol(&funky_mod, "alGetBuffer3i"), (uintptr_t)alGetBuffer3i);
	hook_addr(so_symbol(&funky_mod, "alGetBufferSamplesSOFT"), (uintptr_t)alGetBufferSamplesSOFT);
	hook_addr(so_symbol(&funky_mod, "alGetBufferf"), (uintptr_t)alGetBufferf);
	hook_addr(so_symbol(&funky_mod, "alGetBufferfv"), (uintptr_t)alGetBufferfv);
	hook_addr(so_symbol(&funky_mod, "alGetBufferi"), (uintptr_t)alGetBufferi);
	hook_addr(so_symbol(&funky_mod, "alGetBufferiv"), (uintptr_t)alGetBufferiv);
	hook_addr(so_symbol(&funky_mod, "alGetDouble"), (uintptr_t)alGetDouble);
	hook_addr(so_symbol(&funky_mod, "alGetDoublev"), (uintptr_t)alGetDoublev);
	hook_addr(so_symbol(&funky_mod, "alGetEffectf"), (uintptr_t)alGetEffectf);
	hook_addr(so_symbol(&funky_mod, "alGetEffectfv"), (uintptr_t)alGetEffectfv);
	hook_addr(so_symbol(&funky_mod, "alGetEffecti"), (uintptr_t)alGetEffecti);
	hook_addr(so_symbol(&funky_mod, "alGetEffectiv"), (uintptr_t)alGetEffectiv);
	hook_addr(so_symbol(&funky_mod, "alGetEnumValue"), (uintptr_t)alGetEnumValue);
	hook_addr(so_symbol(&funky_mod, "alGetError"), (uintptr_t)alGetError);
	hook_addr(so_symbol(&funky_mod, "alGetFilterf"), (uintptr_t)alGetFilterf);
	hook_addr(so_symbol(&funky_mod, "alGetFilterfv"), (uintptr_t)alGetFilterfv);
	hook_addr(so_symbol(&funky_mod, "alGetFilteri"), (uintptr_t)alGetFilteri);
	hook_addr(so_symbol(&funky_mod, "alGetFilteriv"), (uintptr_t)alGetFilteriv);
	hook_addr(so_symbol(&funky_mod, "alGetFloat"), (uintptr_t)alGetFloat);
	hook_addr(so_symbol(&funky_mod, "alGetFloatv"), (uintptr_t)alGetFloatv);
	hook_addr(so_symbol(&funky_mod, "alGetInteger"), (uintptr_t)alGetInteger);
	hook_addr(so_symbol(&funky_mod, "alGetIntegerv"), (uintptr_t)alGetIntegerv);
	hook_addr(so_symbol(&funky_mod, "alGetListener3f"), (uintptr_t)alGetListener3f);
	hook_addr(so_symbol(&funky_mod, "alGetListener3i"), (uintptr_t)alGetListener3i);
	hook_addr(so_symbol(&funky_mod, "alGetListenerf"), (uintptr_t)alGetListenerf);
	hook_addr(so_symbol(&funky_mod, "alGetListenerfv"), (uintptr_t)alGetListenerfv);
	hook_addr(so_symbol(&funky_mod, "alGetListeneri"), (uintptr_t)alGetListeneri);
	hook_addr(so_symbol(&funky_mod, "alGetListeneriv"), (uintptr_t)alGetListeneriv);
	hook_addr(so_symbol(&funky_mod, "alGetProcAddress"), (uintptr_t)alGetProcAddress);
	hook_addr(so_symbol(&funky_mod, "alGetSource3dSOFT"), (uintptr_t)alGetSource3dSOFT);
	hook_addr(so_symbol(&funky_mod, "alGetSource3f"), (uintptr_t)alGetSource3f);
	hook_addr(so_symbol(&funky_mod, "alGetSource3i"), (uintptr_t)alGetSource3i);
	hook_addr(so_symbol(&funky_mod, "alGetSource3i64SOFT"), (uintptr_t)alGetSource3i64SOFT);
	hook_addr(so_symbol(&funky_mod, "alGetSourcedSOFT"), (uintptr_t)alGetSourcedSOFT);
	hook_addr(so_symbol(&funky_mod, "alGetSourcedvSOFT"), (uintptr_t)alGetSourcedvSOFT);
	hook_addr(so_symbol(&funky_mod, "alGetSourcef"), (uintptr_t)alGetSourcef);
	hook_addr(so_symbol(&funky_mod, "alGetSourcefv"), (uintptr_t)alGetSourcefv);
	hook_addr(so_symbol(&funky_mod, "alGetSourcei"), (uintptr_t)alGetSourcei);
	hook_addr(so_symbol(&funky_mod, "alGetSourcei64SOFT"), (uintptr_t)alGetSourcei64SOFT);
	hook_addr(so_symbol(&funky_mod, "alGetSourcei64vSOFT"), (uintptr_t)alGetSourcei64vSOFT);
	hook_addr(so_symbol(&funky_mod, "alGetSourceiv"), (uintptr_t)alGetSourceiv);
	hook_addr(so_symbol(&funky_mod, "alGetString"), (uintptr_t)alGetString);
	hook_addr(so_symbol(&funky_mod, "alIsAuxiliaryEffectSlot"), (uintptr_t)alIsAuxiliaryEffectSlot);
	hook_addr(so_symbol(&funky_mod, "alIsBuffer"), (uintptr_t)alIsBuffer);
	hook_addr(so_symbol(&funky_mod, "alIsBufferFormatSupportedSOFT"), (uintptr_t)alIsBufferFormatSupportedSOFT);
	hook_addr(so_symbol(&funky_mod, "alIsEffect"), (uintptr_t)alIsEffect);
	hook_addr(so_symbol(&funky_mod, "alIsEnabled"), (uintptr_t)alIsEnabled);
	hook_addr(so_symbol(&funky_mod, "alIsExtensionPresent"), (uintptr_t)alIsExtensionPresent);
	hook_addr(so_symbol(&funky_mod, "alIsFilter"), (uintptr_t)alIsFilter);
	hook_addr(so_symbol(&funky_mod, "alIsSource"), (uintptr_t)alIsSource);
	hook_addr(so_symbol(&funky_mod, "alListener3f"), (uintptr_t)alListener3f);
	hook_addr(so_symbol(&funky_mod, "alListener3i"), (uintptr_t)alListener3i);
	hook_addr(so_symbol(&funky_mod, "alListenerf"), (uintptr_t)alListenerf);
	hook_addr(so_symbol(&funky_mod, "alListenerfv"), (uintptr_t)alListenerfv);
	hook_addr(so_symbol(&funky_mod, "alListeneri"), (uintptr_t)alListeneri);
	hook_addr(so_symbol(&funky_mod, "alListeneriv"), (uintptr_t)alListeneriv);
	hook_addr(so_symbol(&funky_mod, "alProcessUpdatesSOFT"), (uintptr_t)alProcessUpdatesSOFT);
	hook_addr(so_symbol(&funky_mod, "alSetConfigMOB"), (uintptr_t)ret0);
	hook_addr(so_symbol(&funky_mod, "alSource3dSOFT"), (uintptr_t)alSource3dSOFT);
	hook_addr(so_symbol(&funky_mod, "alSource3f"), (uintptr_t)alSource3f);
	hook_addr(so_symbol(&funky_mod, "alSource3i"), (uintptr_t)alSource3i);
	hook_addr(so_symbol(&funky_mod, "alSource3i64SOFT"), (uintptr_t)alSource3i64SOFT);
	hook_addr(so_symbol(&funky_mod, "alSourcePause"), (uintptr_t)alSourcePause);
	hook_addr(so_symbol(&funky_mod, "alSourcePausev"), (uintptr_t)alSourcePausev);
	hook_addr(so_symbol(&funky_mod, "alSourcePlay"), (uintptr_t)alSourcePlay);
	hook_addr(so_symbol(&funky_mod, "alSourcePlayv"), (uintptr_t)alSourcePlayv);
	hook_addr(so_symbol(&funky_mod, "alSourceQueueBuffers"), (uintptr_t)alSourceQueueBuffers);
	hook_addr(so_symbol(&funky_mod, "alSourceRewind"), (uintptr_t)alSourceRewind);
	hook_addr(so_symbol(&funky_mod, "alSourceRewindv"), (uintptr_t)alSourceRewindv);
	hook_addr(so_symbol(&funky_mod, "alSourceStop"), (uintptr_t)alSourceStop);
	hook_addr(so_symbol(&funky_mod, "alSourceStopv"), (uintptr_t)alSourceStopv);
	hook_addr(so_symbol(&funky_mod, "alSourceUnqueueBuffers"), (uintptr_t)alSourceUnqueueBuffers);
	hook_addr(so_symbol(&funky_mod, "alSourcedSOFT"), (uintptr_t)alSourcedSOFT);
	hook_addr(so_symbol(&funky_mod, "alSourcedvSOFT"), (uintptr_t)alSourcedvSOFT);
	hook_addr(so_symbol(&funky_mod, "alSourcef"), (uintptr_t)alSourcef);
	hook_addr(so_symbol(&funky_mod, "alSourcefv"), (uintptr_t)alSourcefv);
	hook_addr(so_symbol(&funky_mod, "alSourcei"), (uintptr_t)alSourcei);
	hook_addr(so_symbol(&funky_mod, "alSourcei64SOFT"), (uintptr_t)alSourcei64SOFT);
	hook_addr(so_symbol(&funky_mod, "alSourcei64vSOFT"), (uintptr_t)alSourcei64vSOFT);
	hook_addr(so_symbol(&funky_mod, "alSourceiv"), (uintptr_t)alSourceiv);
	hook_addr(so_symbol(&funky_mod, "alSpeedOfSound"), (uintptr_t)alSpeedOfSound);
	hook_addr(so_symbol(&funky_mod, "alcCaptureCloseDevice"), (uintptr_t)alcCaptureCloseDevice);
	hook_addr(so_symbol(&funky_mod, "alcCaptureOpenDevice"), (uintptr_t)alcCaptureOpenDevice);
	hook_addr(so_symbol(&funky_mod, "alcCaptureSamples"), (uintptr_t)alcCaptureSamples);
	hook_addr(so_symbol(&funky_mod, "alcCaptureStart"), (uintptr_t)alcCaptureStart);
	hook_addr(so_symbol(&funky_mod, "alcCaptureStop"), (uintptr_t)alcCaptureStop);
	hook_addr(so_symbol(&funky_mod, "alcCloseDevice"), (uintptr_t)alcCloseDevice);
	hook_addr(so_symbol(&funky_mod, "alcCreateContext"), (uintptr_t)alcCreateContext);
	hook_addr(so_symbol(&funky_mod, "alcDestroyContext"), (uintptr_t)alcDestroyContext);
	hook_addr(so_symbol(&funky_mod, "alcDeviceEnableHrtfMOB"), (uintptr_t)ret0);
	hook_addr(so_symbol(&funky_mod, "alcGetContextsDevice"), (uintptr_t)alcGetContextsDevice);
	hook_addr(so_symbol(&funky_mod, "alcGetCurrentContext"), (uintptr_t)alcGetCurrentContext);
	hook_addr(so_symbol(&funky_mod, "alcGetEnumValue"), (uintptr_t)alcGetEnumValue);
	hook_addr(so_symbol(&funky_mod, "alcGetError"), (uintptr_t)alcGetError);
	hook_addr(so_symbol(&funky_mod, "alcGetIntegerv"), (uintptr_t)alcGetIntegerv);
	hook_addr(so_symbol(&funky_mod, "alcGetProcAddress"), (uintptr_t)alcGetProcAddress);
	hook_addr(so_symbol(&funky_mod, "alcGetString"), (uintptr_t)alcGetString);
	hook_addr(so_symbol(&funky_mod, "alcGetThreadContext"), (uintptr_t)alcGetThreadContext);
	hook_addr(so_symbol(&funky_mod, "alcIsExtensionPresent"), (uintptr_t)alcIsExtensionPresent);
	hook_addr(so_symbol(&funky_mod, "alcIsRenderFormatSupportedSOFT"), (uintptr_t)alcIsRenderFormatSupportedSOFT);
	hook_addr(so_symbol(&funky_mod, "alcLoopbackOpenDeviceSOFT"), (uintptr_t)alcLoopbackOpenDeviceSOFT);
	hook_addr(so_symbol(&funky_mod, "alcMakeContextCurrent"), (uintptr_t)alcMakeContextCurrent);
	hook_addr(so_symbol(&funky_mod, "alcOpenDevice"), (uintptr_t)alcOpenDevice);
	hook_addr(so_symbol(&funky_mod, "alcProcessContext"), (uintptr_t)alcProcessContext);
	hook_addr(so_symbol(&funky_mod, "alcRenderSamplesSOFT"), (uintptr_t)alcRenderSamplesSOFT);
	hook_addr(so_symbol(&funky_mod, "alcSetThreadContext"), (uintptr_t)alcSetThreadContext);
	hook_addr(so_symbol(&funky_mod, "alcSuspendContext"), (uintptr_t)alcSuspendContext);
	hook_addr(so_symbol(&funky_mod, "alBufferMarkNeedsFreed"), (uintptr_t)ret0);
	hook_addr(so_symbol(&funky_mod, "_Z22alBufferMarkNeedsFreedj"), (uintptr_t)ret0);
	hook_addr(so_symbol(&funky_mod, "_Z17alBufferDebugNamejPKc"), (uintptr_t)ret0);

	hook_addr(so_symbol(&funky_mod, "__cxa_guard_acquire"), (uintptr_t)&__cxa_guard_acquire);
	hook_addr(so_symbol(&funky_mod, "__cxa_guard_release"), (uintptr_t)&__cxa_guard_release);

	hook_addr(so_symbol(&funky_mod, "_ZN10FileSystem14IsAbsolutePathEPKc"), (uintptr_t)FileSystem__IsAbsolutePath);
	hook_addr(so_symbol(&funky_mod, "_ZN13ShaderManager13GetShaderPathEv"), (uintptr_t)ShaderManager__GetShaderPath);

	hook_addr(so_symbol(&funky_mod, "_Z17GetApkAssetOffsetPKcRj"), (uintptr_t)ret0);

	TotalMemeorySizeInMB = (int *)so_symbol(&funky_mod, "TotalMemeorySizeInMB");
	hook_addr(so_symbol(&funky_mod, "_Z22DeteremineSystemMemoryv"), (uintptr_t)DeteremineSystemMemory);

	hook_addr(so_symbol(&funky_mod, "_ZN14GoogleServices10IsSignedInEv"), (uintptr_t)ret0);
	hook_addr(so_symbol(&funky_mod, "_ZN26InAppStoreAndroidInterface24IsInAppPurchasePurchasedERK10NameString"), enable_dlcs ? (uintptr_t)ret1 : (uintptr_t)ret0);

	hook_addr(so_symbol(&funky_mod, "_Z12SetGLContextv"), (uintptr_t)ret0);
	hook_addr(so_symbol(&funky_mod, "_Z16PresentGLContextv"), (uintptr_t)PresentGLContext);

	hook_addr(so_symbol(&funky_mod, "_ZN11GameConsole5PrintEhhPKcz"), (uintptr_t)ret0);
	hook_addr(so_symbol(&funky_mod, "_ZN11GameConsole12PrintWarningEhPKcz"), (uintptr_t)ret0);
	hook_addr(so_symbol(&funky_mod, "_ZN11GameConsole10PrintErrorEhPKcz"), (uintptr_t)ret0);

	//_Znwj = (void *)so_symbol(&funky_mod, "_Znwj");
	//CountingSemaphore__Constructor = (void *)so_symbol(&funky_mod, "_ZN17CountingSemaphoreC2Ej");
	//BaseThread__BeginMessage = (void *)so_symbol(&funky_mod, "_ZN10BaseThread12BeginMessageEjj");
	//BaseThread__EndMessage = (void *)so_symbol(&funky_mod, "_ZN10BaseThread10EndMessageEv");
	//BaseThread___ThreadCode = (void *)so_symbol(&funky_mod, "_ZN10BaseThread11_ThreadCodeEv");
	//hook_addr(so_symbol(&funky_mod, "_ZN10BaseThread4InitEv"), (uintptr_t)BaseThread__Init);
	//hook_addr(so_symbol(&funky_mod, "_ZN10BaseThread11SetPriorityEi"), (uintptr_t)ret0);
	//hook_addr(so_symbol(&funky_mod, "_Z18GetCurrentThreadIdv"), (uintptr_t)GetCurrentThreadId);
}

extern void *__aeabi_atexit;
extern void *__aeabi_idiv;
extern void *__aeabi_idivmod;
extern void *__aeabi_ldivmod;
extern void *__aeabi_uidiv;
extern void *__aeabi_uidivmod;
extern void *__aeabi_uldivmod;
extern void *__cxa_atexit;
extern void *__cxa_finalize;
extern void *__gnu_unwind_frame;
extern void *__stack_chk_fail;

static int __stack_chk_guard_fake = 0x42424242;

static char *__ctype_ = (char *)&_ctype_;

static FILE __sF_fake[0x100][3];

int stat_hook(const char *pathname, void *statbuf) {
	struct stat st;
	int res = stat(pathname, &st);
	if (res == 0)
		*(uint64_t *)(statbuf + 0x30) = st.st_size;
	return res;
}

void glTexImage2DHook(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void * data) {
	if (level == 0)
		glTexImage2D(target, level, internalformat, width, height, border, format, type, data);
}

void glCompressedTexImage2DHook(GLenum target, GLint level, GLenum format, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void * data) {
	// mips for PVRTC textures break when they're under 1 block in size
	if (level == 0)
		glCompressedTexImage2D(target, level, format, width, height, border, imageSize, data);
}

FILE *fopen_hook(char *fname, char *mode) {
	FILE *f;
	char real_fname[256];
	//printf("fopen(%s,%s)\n", fname, mode);
	if (strncmp(fname, "ux0:", 4)) {
		sprintf(real_fname, "%s/assets/%s", DATA_PATH, fname);
		f = fopen(real_fname, mode);
	} else {
		f = fopen(fname, mode);
	}
	return f;
}

void glShaderSource_hook(GLuint shader, GLsizei count, const GLchar **string, const GLint *length) {
	uint32_t sha1[5];
	SHA1_CTX ctx;
	
	sha1_init(&ctx);
	for (int i = 0; i < count; i++) {
		sha1_update(&ctx, string[i], strlen(string[i]));
	}
	sha1_final(&ctx, (uint8_t *)sha1);

	char sha_name[64];
	snprintf(sha_name, sizeof(sha_name), "%08x%08x%08x%08x%08x", sha1[0], sha1[1], sha1[2], sha1[3], sha1[4]);

	char cg_path[128];
	snprintf(cg_path, sizeof(cg_path), "app0:/shaders/%s.cg.gxp", sha_name);

	FILE *file = fopen(cg_path, "rb");
	if (!file) {
		snprintf(cg_path, sizeof(cg_path), "ux0:data/anomaly/%s.glsl", sha_name);
		file = fopen(cg_path, "wb");
		for (int i = 0; i < count; i++) {
			fwrite(string[i], 1, strlen(string[i]), file);
		}
		fclose(file);
		if (strstr(string[1], "gl_FragColor"))
			file = fopen("app0:/shaders/024af586cd82eb776a818e96dc6c43e88acc6882.cg.gxp", "rb");
		else
			file = fopen("app0:/shaders/9f6320ad76145c22f135664c66a8ab4caeecbdbc.cg.gxp", "rb");
	}
		
	fseek(file, 0, SEEK_END);
	uint32_t size = ftell(file);
	fseek(file, 0, SEEK_SET);
	char *buf = (char *)malloc(size + 1);
	fread(buf, 1, size, file);
	fclose(file);
	buf[size] = 0;
	glShaderBinary(1, &shader, 0, buf, size);
	//glShaderSource(shader, 1, &buf, NULL);
	free(buf);
}

void glCompileShader_hook(GLuint prog) {	
}

int usleep_hook(useconds_t usec) {
	if (usec > 0)
		usleep(usec);
	return 0;
}

GLint glGetUniformLocation_hook(GLuint program, const GLchar *name) {
	GLint res = glGetUniformLocation(program, name);
	if (!strcmp(name, "BoneMatrices")) {
		float bones[4 * 32 * 3];
		for (int i = 0; i < 32 * 3; i++) {
			switch (i % 3) {
			case 0:
				bones[i * 4 + 0] = 1.0f;
				bones[i * 4 + 1] = 0.0f;
				bones[i * 4 + 2] = 0.0f;
				bones[i * 4 + 3] = 0.0f;
				break;
			case 1:
				bones[i * 4 + 0] = 0.0f;
				bones[i * 4 + 1] = 1.0f;
				bones[i * 4 + 2] = 0.0f;
				bones[i * 4 + 3] = 0.0f;
				break;
			case 2:
				bones[i * 4 + 0] = 0.0f;
				bones[i * 4 + 1] = 0.0f;
				bones[i * 4 + 2] = 1.0f;
				bones[i * 4 + 3] = 0.0f;
				break;
			}
		}
		glUniform4fv(res, 32 * 3, bones);
	}
	return res;
}

static so_default_dynlib default_dynlib[] = {
	// { "ANativeWindow_release", (uintptr_t)&ANativeWindow_release },
	// { "ANativeWindow_setBuffersGeometry", (uintptr_t)&ANativeWindow_setBuffersGeometry },
	// { "_Unwind_Complete", (uintptr_t)&_Unwind_Complete },
	// { "_Unwind_DeleteException", (uintptr_t)&_Unwind_DeleteException },
	// { "_Unwind_GetDataRelBase", (uintptr_t)&_Unwind_GetDataRelBase },
	// { "_Unwind_GetLanguageSpecificData", (uintptr_t)&_Unwind_GetLanguageSpecificData },
	// { "_Unwind_GetRegionStart", (uintptr_t)&_Unwind_GetRegionStart },
	// { "_Unwind_GetTextRelBase", (uintptr_t)&_Unwind_GetTextRelBase },
	// { "_Unwind_RaiseException", (uintptr_t)&_Unwind_RaiseException },
	// { "_Unwind_Resume", (uintptr_t)&_Unwind_Resume },
	// { "_Unwind_Resume_or_Rethrow", (uintptr_t)&_Unwind_Resume_or_Rethrow },
	// { "_Unwind_VRS_Get", (uintptr_t)&_Unwind_VRS_Get },
	// { "_Unwind_VRS_Set", (uintptr_t)&_Unwind_VRS_Set },
	{ "__cxa_guard_acquire", (uintptr_t)&__cxa_guard_acquire },
	{ "__cxa_guard_release", (uintptr_t)&__cxa_guard_release },
	{ "__aeabi_atexit", (uintptr_t)&__aeabi_atexit },
	{ "__aeabi_idiv", (uintptr_t)&__aeabi_idiv },
	{ "__aeabi_idivmod", (uintptr_t)&__aeabi_idivmod },
	{ "__aeabi_ldivmod", (uintptr_t)&__aeabi_ldivmod },
	{ "__aeabi_uidiv", (uintptr_t)&__aeabi_uidiv },
	{ "__aeabi_uidivmod", (uintptr_t)&__aeabi_uidivmod },
	{ "__aeabi_uldivmod", (uintptr_t)&__aeabi_uldivmod },
	{ "__android_log_print", (uintptr_t)&__android_log_print },
	{ "__android_log_write", (uintptr_t)&__android_log_write },
	{ "__cxa_atexit", (uintptr_t)&__cxa_atexit },
	{ "__cxa_finalize", (uintptr_t)&__cxa_finalize },
	{ "__errno", (uintptr_t)&__errno },
	{ "__gnu_unwind_frame", (uintptr_t)&__gnu_unwind_frame },
	// { "__google_potentially_blocking_region_begin", (uintptr_t)&__google_potentially_blocking_region_begin },
	// { "__google_potentially_blocking_region_end", (uintptr_t)&__google_potentially_blocking_region_end },
	{ "__sF", (uintptr_t)&__sF_fake },
	{ "__stack_chk_fail", (uintptr_t)&__stack_chk_fail },
	{ "__stack_chk_guard", (uintptr_t)&__stack_chk_guard_fake },
	{ "_ctype_", (uintptr_t)&BIONIC_ctype_},
	{ "_tolower_tab_", (uintptr_t)&BIONIC_tolower_tab_},
	{ "_toupper_tab_", (uintptr_t)&BIONIC_toupper_tab_},
	{ "abort", (uintptr_t)&abort },
	// { "accept", (uintptr_t)&accept },
	{ "acos", (uintptr_t)&acos },
	{ "acosf", (uintptr_t)&acosf },
	{ "alBufferData", (uintptr_t)&alBufferData },
	{ "alDeleteBuffers", (uintptr_t)&alDeleteBuffers },
	{ "alDeleteSources", (uintptr_t)&alDeleteSources },
	{ "alDistanceModel", (uintptr_t)&alDistanceModel },
	{ "alGenBuffers", (uintptr_t)&alGenBuffers },
	{ "alGenSources", (uintptr_t)&alGenSources },
	{ "alGetError", (uintptr_t)&alGetError },
	{ "alGetSourcei", (uintptr_t)&alGetSourcei },
	{ "alGetString", (uintptr_t)&alGetString },
	// { "alHackPause", (uintptr_t)&alHackPause },
	// { "alHackResume", (uintptr_t)&alHackResume },
	{ "alListenerfv", (uintptr_t)&alListenerfv },
	{ "alSourcePlay", (uintptr_t)&alSourcePlay },
	{ "alSourceQueueBuffers", (uintptr_t)&alSourceQueueBuffers },
	{ "alSourceStop", (uintptr_t)&alSourceStop },
	{ "alSourceUnqueueBuffers", (uintptr_t)&alSourceUnqueueBuffers },
	{ "alSourcef", (uintptr_t)&alSourcef },
	{ "alSourcefv", (uintptr_t)&alSourcefv },
	{ "alSourcei", (uintptr_t)&alSourcei },
	{ "alcCloseDevice", (uintptr_t)&alcCloseDevice },
	{ "alcCreateContext", (uintptr_t)&alcCreateContext },
	{ "alcDestroyContext", (uintptr_t)&alcDestroyContext },
	{ "alcGetProcAddress", (uintptr_t)&alcGetProcAddress },
	{ "alcGetString", (uintptr_t)&alcGetString },
	{ "alcMakeContextCurrent", (uintptr_t)&alcMakeContextCurrent },
	{ "alcOpenDevice", (uintptr_t)&alcOpenDevice },
	{ "alcProcessContext", (uintptr_t)&alcProcessContext },
	{ "alcSuspendContext", (uintptr_t)&alcSuspendContext },
	{ "asin", (uintptr_t)&asin },
	{ "asinf", (uintptr_t)&asinf },
	{ "atan", (uintptr_t)&atan },
	{ "atan2", (uintptr_t)&atan2 },
	{ "atan2f", (uintptr_t)&atan2f },
	{ "atanf", (uintptr_t)&atanf },
	{ "atoi", (uintptr_t)&atoi },
	{ "atoll", (uintptr_t)&atoll },
	// { "bind", (uintptr_t)&bind },
	{ "bsearch", (uintptr_t)&bsearch },
	{ "btowc", (uintptr_t)&btowc },
	{ "calloc", (uintptr_t)&calloc },
	{ "ceil", (uintptr_t)&ceil },
	{ "ceilf", (uintptr_t)&ceilf },
	{ "clearerr", (uintptr_t)&clearerr },
	{ "clock_gettime", (uintptr_t)&clock_gettime },
	// { "close", (uintptr_t)&close },
	{ "cos", (uintptr_t)&cos },
	{ "cosf", (uintptr_t)&cosf },
	{ "cosh", (uintptr_t)&cosh },
	// { "eglChooseConfig", (uintptr_t)&eglChooseConfig },
	// { "eglCreateContext", (uintptr_t)&eglCreateContext },
	// { "eglCreateWindowSurface", (uintptr_t)&eglCreateWindowSurface },
	// { "eglDestroyContext", (uintptr_t)&eglDestroyContext },
	// { "eglDestroySurface", (uintptr_t)&eglDestroySurface },
	// { "eglGetConfigAttrib", (uintptr_t)&eglGetConfigAttrib },
	{ "eglGetDisplay", (uintptr_t)&ret0 },
	{ "eglGetProcAddress", (uintptr_t)&eglGetProcAddress },
	// { "eglInitialize", (uintptr_t)&eglInitialize },
	// { "eglMakeCurrent", (uintptr_t)&eglMakeCurrent },
	{ "eglQueryString", (uintptr_t)&ret0 },
	// { "eglQuerySurface", (uintptr_t)&eglQuerySurface },
	// { "eglSwapBuffers", (uintptr_t)&eglSwapBuffers },
	// { "eglTerminate", (uintptr_t)&eglTerminate },
	{ "exit", (uintptr_t)&exit },
	{ "exp", (uintptr_t)&exp },
	{ "expf", (uintptr_t)&expf },
	{ "fclose", (uintptr_t)&fclose },
	{ "fdopen", (uintptr_t)&fdopen },
	{ "ferror", (uintptr_t)&ferror },
	{ "fflush", (uintptr_t)&fflush },
	{ "fgets", (uintptr_t)&fgets },
	{ "floor", (uintptr_t)&floor },
	{ "floorf", (uintptr_t)&floorf },
	{ "fmod", (uintptr_t)&fmod },
	{ "fmodf", (uintptr_t)&fmodf },
	{ "fopen", (uintptr_t)&fopen_hook },
	{ "fprintf", (uintptr_t)&fprintf },
	{ "fputc", (uintptr_t)&fputc },
	{ "fputs", (uintptr_t)&fputs },
	{ "fread", (uintptr_t)&fread },
	{ "free", (uintptr_t)&free },
	{ "frexp", (uintptr_t)&frexp },
	{ "fseek", (uintptr_t)&fseek },
	{ "fstat", (uintptr_t)&fstat },
	{ "ftell", (uintptr_t)&ftell },
	{ "fwrite", (uintptr_t)&fwrite },
	{ "getc", (uintptr_t)&getc },
	{ "getenv", (uintptr_t)&getenv },
	{ "alarm", (uintptr_t)&ret0 },
	{ "sigsetjmp", (uintptr_t)&ret0 },
	{ "sigaction", (uintptr_t)&ret0 },
	{ "getwc", (uintptr_t)&getwc },
	{ "glActiveTexture", (uintptr_t)&glActiveTexture },
	{ "glAttachShader", (uintptr_t)&glAttachShader },
	{ "glBindAttribLocation", (uintptr_t)&glBindAttribLocation },
	{ "glBindBuffer", (uintptr_t)&glBindBuffer },
	{ "glBindFramebuffer", (uintptr_t)&glBindFramebuffer },
	{ "glBindRenderbuffer", (uintptr_t)&ret0 },
	{ "glBindTexture", (uintptr_t)&glBindTexture },
	{ "glBlendEquation", (uintptr_t)&glBlendEquation },
	{ "glBlendFunc", (uintptr_t)&glBlendFunc },
	{ "glBufferData", (uintptr_t)&glBufferData },
	{ "glBufferSubData", (uintptr_t)&glBufferSubData },
	{ "glCheckFramebufferStatus", (uintptr_t)&glCheckFramebufferStatus },
	{ "glClear", (uintptr_t)&glClear },
	{ "glClearColor", (uintptr_t)&glClearColor },
	{ "glClearDepthf", (uintptr_t)&glClearDepthf },
	{ "glClearStencil", (uintptr_t)&glClearStencil },
	{ "glColorMask", (uintptr_t)&glColorMask },
	{ "glCompileShader", (uintptr_t)&glCompileShader_hook },
	{ "glCompressedTexImage2D", (uintptr_t)&glCompressedTexImage2DHook },
	{ "glCreateProgram", (uintptr_t)&glCreateProgram },
	{ "glCreateShader", (uintptr_t)&glCreateShader },
	{ "glCullFace", (uintptr_t)&glCullFace },
	{ "glDeleteBuffers", (uintptr_t)&glDeleteBuffers },
	{ "glDeleteFramebuffers", (uintptr_t)&glDeleteFramebuffers },
	{ "glDeleteProgram", (uintptr_t)&glDeleteProgram },
	{ "glDeleteRenderbuffers", (uintptr_t)&ret0 },
	{ "glDeleteShader", (uintptr_t)&glDeleteShader },
	{ "glDeleteTextures", (uintptr_t)&glDeleteTextures },
	{ "glDepthFunc", (uintptr_t)&glDepthFunc },
	{ "glDepthMask", (uintptr_t)&glDepthMask },
	{ "glDisable", (uintptr_t)&glDisable },
	{ "glDisableVertexAttribArray", (uintptr_t)&glDisableVertexAttribArray },
	{ "glDrawArrays", (uintptr_t)&glDrawArrays },
	{ "glDrawElements", (uintptr_t)&glDrawElements },
	{ "glEnable", (uintptr_t)&glEnable },
	{ "glFlush", (uintptr_t)&ret0 },
	{ "glGetActiveUniform", (uintptr_t)&glGetActiveUniform },
	{ "glEnableVertexAttribArray", (uintptr_t)&glEnableVertexAttribArray },
	{ "glFinish", (uintptr_t)&glFinish },
	{ "glFramebufferRenderbuffer", (uintptr_t)&ret0 },
	{ "glFramebufferTexture2D", (uintptr_t)&glFramebufferTexture2D },
	{ "glGenBuffers", (uintptr_t)&glGenBuffers },
	{ "glGenFramebuffers", (uintptr_t)&glGenFramebuffers },
	{ "glGenRenderbuffers", (uintptr_t)&ret0 },
	{ "glGenTextures", (uintptr_t)&glGenTextures },
	{ "glGetError", (uintptr_t)&glGetError },
	{ "glGetIntegerv", (uintptr_t)&glGetIntegerv },
	{ "glGetProgramInfoLog", (uintptr_t)&glGetProgramInfoLog },
	{ "glGetProgramiv", (uintptr_t)&glGetProgramiv },
	{ "glGetShaderInfoLog", (uintptr_t)&glGetShaderInfoLog },
	{ "glGetShaderPrecisionFormat", (uintptr_t)&ret0 },
	{ "glGetShaderiv", (uintptr_t)&glGetShaderiv },
	{ "glGetString", (uintptr_t)&glGetString },
	{ "glGetUniformLocation", (uintptr_t)&glGetUniformLocation_hook },
	{ "glLinkProgram", (uintptr_t)&glLinkProgram },
	{ "glPolygonOffset", (uintptr_t)&glPolygonOffset },
	{ "glRenderbufferStorage", (uintptr_t)&ret0 },
	{ "glScissor", (uintptr_t)&glScissor },
	{ "glShaderSource", (uintptr_t)&glShaderSource_hook },
	{ "glStencilFunc", (uintptr_t)&glStencilFunc },
	{ "glStencilMask", (uintptr_t)&glStencilMask },
	{ "glStencilOp", (uintptr_t)&glStencilOp },
	{ "glTexImage2D", (uintptr_t)&glTexImage2DHook },
	{ "glTexParameteri", (uintptr_t)&glTexParameteri },
	{ "glUniform1i", (uintptr_t)&glUniform1i },
	{ "glUniform4fv", (uintptr_t)&glUniform4fv },
	{ "glUniformMatrix4fv", (uintptr_t)&glUniformMatrix4fv },
	{ "glUseProgram", (uintptr_t)&glUseProgram },
	{ "glValidateProgram", (uintptr_t)&ret0 },
	{ "glVertexAttribPointer", (uintptr_t)&glVertexAttribPointer },
	{ "glViewport", (uintptr_t)&glViewport },
	// { "inet_addr", (uintptr_t)&inet_addr },
	// { "ioctl", (uintptr_t)&ioctl },
	{ "isalnum", (uintptr_t)&isalnum },
	{ "isalpha", (uintptr_t)&isalpha },
	{ "iscntrl", (uintptr_t)&iscntrl },
	{ "islower", (uintptr_t)&islower },
	{ "ispunct", (uintptr_t)&ispunct },
	{ "isspace", (uintptr_t)&isspace },
	{ "isupper", (uintptr_t)&isupper },
	{ "iswctype", (uintptr_t)&iswctype },
	{ "isxdigit", (uintptr_t)&isxdigit },
	{ "ldexp", (uintptr_t)&ldexp },
	// { "listen", (uintptr_t)&listen },
	{ "localtime_r", (uintptr_t)&localtime_r },
	{ "localtime", (uintptr_t)&localtime },
	{ "log", (uintptr_t)&log },
	{ "log10", (uintptr_t)&log10 },
	{ "longjmp", (uintptr_t)&longjmp },
	{ "lrand48", (uintptr_t)&lrand48 },
	{ "lrint", (uintptr_t)&lrint },
	{ "lrintf", (uintptr_t)&lrintf },
	{ "lseek", (uintptr_t)&lseek },
	{ "strdup", (uintptr_t)&strdup },
	{ "malloc", (uintptr_t)&malloc },
	{ "mbrtowc", (uintptr_t)&mbrtowc },
	{ "memchr", (uintptr_t)&sceClibMemchr },
	{ "memcmp", (uintptr_t)&sceClibMemcmp },
	{ "memcpy", (uintptr_t)&sceClibMemcpy },
	{ "memmove", (uintptr_t)&sceClibMemmove },
	{ "memset", (uintptr_t)&sceClibMemset },
	{ "mkdir", (uintptr_t)&mkdir },
	{ "modf", (uintptr_t)&modf },
	// { "poll", (uintptr_t)&poll },
	{ "pow", (uintptr_t)&pow },
	{ "powf", (uintptr_t)&powf },
	{ "printf", (uintptr_t)&printf },
	{ "pthread_attr_destroy", (uintptr_t)&ret0 },
	{ "pthread_attr_init", (uintptr_t)&ret0 },
	{ "pthread_attr_setdetachstate", (uintptr_t)&ret0 },
	{ "pthread_create", (uintptr_t)&pthread_create_fake },
	{ "pthread_getschedparam", (uintptr_t)&pthread_getschedparam },
	{ "pthread_getspecific", (uintptr_t)&pthread_getspecific },
	{ "pthread_key_create", (uintptr_t)&pthread_key_create },
	{ "pthread_key_delete", (uintptr_t)&pthread_key_delete },
	{ "pthread_mutex_destroy", (uintptr_t)&pthread_mutex_destroy_fake },
	{ "pthread_mutex_init", (uintptr_t)&pthread_mutex_init_fake },
	{ "pthread_mutex_lock", (uintptr_t)&pthread_mutex_lock_fake },
	{ "pthread_mutex_trylock", (uintptr_t)&pthread_mutex_trylock_fake },
	{ "pthread_mutex_unlock", (uintptr_t)&pthread_mutex_unlock_fake },
	{ "pthread_mutexattr_destroy", (uintptr_t)&pthread_mutexattr_destroy_fake },
	{ "pthread_mutexattr_init", (uintptr_t)&pthread_mutexattr_init_fake },
	{ "pthread_mutexattr_settype", (uintptr_t)&pthread_mutexattr_settype_fake },
	{ "pthread_once", (uintptr_t)&pthread_once_fake },
	{ "pthread_self", (uintptr_t)&pthread_self },
	{ "pthread_setschedparam", (uintptr_t)&pthread_setschedparam },
	{ "pthread_setspecific", (uintptr_t)&pthread_setspecific },
	{ "putc", (uintptr_t)&putc },
	{ "putwc", (uintptr_t)&putwc },
	{ "qsort", (uintptr_t)&qsort },
	// { "read", (uintptr_t)&read },
	{ "realloc", (uintptr_t)&realloc },
	// { "recv", (uintptr_t)&recv },
	{ "rint", (uintptr_t)&rint },
	{ "sem_destroy", (uintptr_t)&sem_destroy_fake },
	{ "sem_init", (uintptr_t)&sem_init_fake },
	{ "sem_post", (uintptr_t)&sem_post_fake },
	{ "sem_timedwait", (uintptr_t)&sem_timedwait_fake },
	{ "sem_wait", (uintptr_t)&sem_wait_fake },
	// { "send", (uintptr_t)&send },
	// { "sendto", (uintptr_t)&sendto },
	{ "setjmp", (uintptr_t)&setjmp },
	// { "setlocale", (uintptr_t)&setlocale },
	// { "setsockopt", (uintptr_t)&setsockopt },
	{ "setvbuf", (uintptr_t)&setvbuf },
	{ "sin", (uintptr_t)&sin },
	{ "sinf", (uintptr_t)&sinf },
	{ "sinh", (uintptr_t)&sinh },
	{ "snprintf", (uintptr_t)&snprintf },
	// { "socket", (uintptr_t)&socket },
	{ "sprintf", (uintptr_t)&sprintf },
	{ "sqrt", (uintptr_t)&sqrt },
	{ "sqrtf", (uintptr_t)&sqrtf },
	{ "srand48", (uintptr_t)&srand48 },
	{ "sscanf", (uintptr_t)&sscanf },
	{ "stat", (uintptr_t)&stat_hook },
	{ "strcasecmp", (uintptr_t)&strcasecmp },
	{ "strcat", (uintptr_t)&strcat },
	{ "strchr", (uintptr_t)&strchr },
	{ "strcmp", (uintptr_t)&sceClibStrcmp },
	{ "strcoll", (uintptr_t)&strcoll },
	{ "strcpy", (uintptr_t)&strcpy },
	{ "strcspn", (uintptr_t)&strcspn },
	{ "strerror", (uintptr_t)&strerror },
	{ "strftime", (uintptr_t)&strftime },
	{ "strlen", (uintptr_t)&strlen },
	{ "strncasecmp", (uintptr_t)&sceClibStrncasecmp },
	{ "strncat", (uintptr_t)&sceClibStrncat },
	{ "strncmp", (uintptr_t)&sceClibStrncmp },
	{ "strncpy", (uintptr_t)&sceClibStrncpy },
	{ "strpbrk", (uintptr_t)&strpbrk },
	{ "strrchr", (uintptr_t)&sceClibStrrchr },
	{ "strstr", (uintptr_t)&sceClibStrstr },
	{ "strtod", (uintptr_t)&strtod },
	{ "strtol", (uintptr_t)&strtol },
	{ "strtoul", (uintptr_t)&strtoul },
	{ "strxfrm", (uintptr_t)&strxfrm },
	// { "syscall", (uintptr_t)&syscall },
	{ "tan", (uintptr_t)&tan },
	{ "tanf", (uintptr_t)&tanf },
	{ "tanh", (uintptr_t)&tanh },
	{ "time", (uintptr_t)&time },
	{ "tolower", (uintptr_t)&tolower },
	{ "toupper", (uintptr_t)&toupper },
	{ "towlower", (uintptr_t)&towlower },
	{ "towupper", (uintptr_t)&towupper },
	{ "ungetc", (uintptr_t)&ungetc },
	{ "ungetwc", (uintptr_t)&ungetwc },
	{ "usleep", (uintptr_t)&usleep_hook },
	{ "vsnprintf", (uintptr_t)&vsnprintf },
	{ "vsprintf", (uintptr_t)&vsprintf },
	{ "vswprintf", (uintptr_t)&vswprintf },
	{ "wcrtomb", (uintptr_t)&wcrtomb },
	{ "wcscoll", (uintptr_t)&wcscoll },
	{ "wcsftime", (uintptr_t)&wcsftime },
	{ "wcslen", (uintptr_t)&wcslen },
	{ "wcsxfrm", (uintptr_t)&wcsxfrm },
	{ "wctob", (uintptr_t)&wctob },
	{ "wctype", (uintptr_t)&wctype },
	{ "wmemchr", (uintptr_t)&wmemchr },
	{ "wmemcmp", (uintptr_t)&wmemcmp },
	{ "wmemcpy", (uintptr_t)&wmemcpy },
	{ "wmemmove", (uintptr_t)&wmemmove },
	{ "wmemset", (uintptr_t)&wmemset },
	// { "write", (uintptr_t)&write },
	// { "writev", (uintptr_t)&writev },
};

int check_kubridge(void) {
	int search_unk[2];
	return _vshKernelSearchModuleByName("kubridge", search_unk);
}

int file_exists(const char *path) {
	SceIoStat stat;
	return sceIoGetstat(path, &stat) >= 0;
}

static char fake_vm[0x1000];
static char fake_env[0x1000];

enum MethodIDs {
	UNKNOWN = 0,
	INIT,
	IS_JOYSTICK_PRESENT,
	IS_TOUCH_PRESENT,
} MethodIDs;

typedef struct {
	char *name;
	enum MethodIDs id;
} NameToMethodID;

static NameToMethodID name_to_method_ids[] = {
	{ "<init>", INIT },
	{ "IsJoystickPresent", IS_JOYSTICK_PRESENT },
	{ "IsTouchPresent", IS_TOUCH_PRESENT },
};

int GetMethodID(void *env, void *class, const char *name, const char *sig) {
	printf("%s\n", name);

	for (int i = 0; i < sizeof(name_to_method_ids) / sizeof(NameToMethodID); i++) {
		if (strcmp(name, name_to_method_ids[i].name) == 0) {
			return name_to_method_ids[i].id;
		}
	}

	return UNKNOWN;
}

int GetStaticMethodID(void *env, void *class, const char *name, const char *sig) {
	printf("%s\n", name);

	for (int i = 0; i < sizeof(name_to_method_ids) / sizeof(NameToMethodID); i++) {
		if (strcmp(name, name_to_method_ids[i].name) == 0)
			return name_to_method_ids[i].id;
	}

	return UNKNOWN;
}

void CallStaticVoidMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
}

int CallStaticBooleanMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
	switch (methodID) {
		case IS_JOYSTICK_PRESENT:
			return 1;
		case IS_TOUCH_PRESENT:
			return 1;
		default:
			return 0;
	}
	return 0;
}

void *FindClass(void) {
	return (void *)0x41414141;
}

void *NewGlobalRef(void) {
	return (void *)0x42424242;
}

void *NewObjectV(void *env, void *clazz, int methodID, uintptr_t args) {
	return (void *)0x43434343;
}

void *GetObjectClass(void *env, void *obj) {
	return (void *)0x44444444;
}

char *NewStringUTF(void *env, char *bytes) {
	return bytes;
}

char *GetStringUTFChars(void *env, char *string, int *isCopy) {
	return string;
}

int GetJavaVM(void *env, void **vm) {
	*vm = fake_vm;
	return 0;
}

int GetEnv(void *vm, void **env, int r2) {
	*env = fake_env;
	return 0;
}

enum {
	AKEYCODE_BUTTON_A = 96,
	AKEYCODE_BUTTON_B = 97,
	AKEYCODE_BUTTON_X = 99,
	AKEYCODE_BUTTON_Y = 100,
	AKEYCODE_BUTTON_L1 = 102,
	AKEYCODE_BUTTON_R1 = 103,
	AKEYCODE_BUTTON_THUMBL = 106,
	AKEYCODE_BUTTON_THUMBR = 107,
	AKEYCODE_BUTTON_START = 108,
};

typedef struct {
	uint32_t sce_button;
	uint32_t android_button;
} ButtonMapping;

static ButtonMapping mapping[] = {
	{ SCE_CTRL_CROSS,		 AKEYCODE_BUTTON_A },
	{ SCE_CTRL_CIRCLE,		AKEYCODE_BUTTON_B },
	{ SCE_CTRL_SQUARE,		AKEYCODE_BUTTON_X },
	{ SCE_CTRL_TRIANGLE,	AKEYCODE_BUTTON_Y },
	{ SCE_CTRL_L2,				AKEYCODE_BUTTON_L1 },
	{ SCE_CTRL_R2,				AKEYCODE_BUTTON_R1 },
	{ SCE_CTRL_L3,				AKEYCODE_BUTTON_THUMBL },
	{ SCE_CTRL_R3,				AKEYCODE_BUTTON_THUMBR },
	{ SCE_CTRL_START,		 AKEYCODE_BUTTON_START },
};

static int rear_mapping[] = {
	AKEYCODE_BUTTON_THUMBR,
	AKEYCODE_BUTTON_R1,
	AKEYCODE_BUTTON_THUMBL,
	AKEYCODE_BUTTON_L1
};

int ctrl_thread(SceSize args, void *argp) {
	int (* Java_com_android_Game11Bits_GameLib_touchDown)(void *env, void *obj, int id, float x, float y) = (void *)so_symbol(&funky_mod, "Java_com_android_Game11Bits_GameLib_touchDown");
	int (* Java_com_android_Game11Bits_GameLib_touchUp)(void *env, void *obj, int id, float x, float y) = (void *)so_symbol(&funky_mod, "Java_com_android_Game11Bits_GameLib_touchUp");
	int (* Java_com_android_Game11Bits_GameLib_touchMove)(void *env, void *obj, int id, float x, float y) = (void *)so_symbol(&funky_mod, "Java_com_android_Game11Bits_GameLib_touchMove");

	int lastX[2] = { -1, -1 };
	int lastY[2] = { -1, -1 };

	while (1) {
		SceTouchData touch;
		sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);

		for (int i = 0; i < 2; i++) {
			if (i < touch.reportNum) {
				int x = (int)((float)touch.report[i].x * (float)SCREEN_W / 1920.0f);
				int y = (int)((float)touch.report[i].y * (float)SCREEN_H / 1088.0f);

				if (lastX[i] == -1 || lastY[i] == -1)
					Java_com_android_Game11Bits_GameLib_touchDown(fake_env, NULL, i, x, y);
				else if (lastX[i] != x || lastY[i] != y)
					Java_com_android_Game11Bits_GameLib_touchMove(fake_env, NULL, i, x, y);
				lastX[i] = x;
				lastY[i] = y;
			} else {
				if (lastX[i] != -1 || lastY[i] != -1)
					Java_com_android_Game11Bits_GameLib_touchUp(fake_env, NULL, i, lastX[i], lastY[i]);
				lastX[i] = -1;
				lastY[i] = -1;
			}
		}

		sceKernelDelayThread(1000);
	}

	return 0;
}

extern int YYVideoOpen(const char *path);
extern int YYVideoDraw();
extern void YYVideoStop();

int main(int argc, char *argv[]) {
	//sceSysmoduleLoadModule(SCE_SYSMODULE_RAZOR_CAPTURE);
	sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);

	scePowerSetArmClockFrequency(444);
	scePowerSetBusClockFrequency(222);
	scePowerSetGpuClockFrequency(222);
	scePowerSetGpuXbarClockFrequency(166);

	pstv_mode = sceCtrlIsMultiControllerSupported() ? 1 : 0;

	if (check_kubridge() < 0)
		fatal_error("Error kubridge.skprx is not installed.");

	if (!file_exists("ur0:/data/libshacccg.suprx") && !file_exists("ur0:/data/external/libshacccg.suprx"))
		fatal_error("Error libshacccg.suprx is not installed.");

	if (so_file_load(&funky_mod, SO_PATH, LOAD_ADDRESS) < 0)
		fatal_error("Error could not load %s.", SO_PATH);

	so_relocate(&funky_mod);
	so_resolve(&funky_mod, default_dynlib, sizeof(default_dynlib), 0);

	patch_game();
	so_flush_caches(&funky_mod);

	so_initialize(&funky_mod);

	vglSetupRuntimeShaderCompiler(SHARK_OPT_UNSAFE, SHARK_ENABLE, SHARK_ENABLE, SHARK_ENABLE);
	vglSetupGarbageCollector(127, 0x20000);
	vglInitExtended(0, SCREEN_W, SCREEN_H, MEMORY_VITAGL_THRESHOLD_MB * 1024 * 1024, SCE_GXM_MULTISAMPLE_4X);

	// Playing the intro video if present)
	SceIoStat st1;
	if (sceIoGetstat("ux0:data/anomaly/assets/start.mp4", &st1) >= 0) {
		YYVideoOpen("start.mp4");
		while (!YYVideoDraw()) {
			vglSwapBuffers(GL_FALSE);
			SceCtrlData pad;
			SceTouchData touch;
			sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
			sceCtrlPeekBufferPositive(0, &pad, 1);
			if (pad.buttons || touch.reportNum) {
				YYVideoStop();
				break;
			}
		}
	}
	
	int (* Java_com_android_Game11Bits_GameLib_initOBBFile)(void *env, void *obj, const char *file, int filesize) = (void *)so_symbol(&funky_mod, "Java_com_android_Game11Bits_GameLib_initOBBFile");
	int (* Java_com_android_Game11Bits_GameLib_init)(void *env, void *obj, const char *ApkFilePath, const char *StorageFilePath, const char *CacheFilePath, int resX, int resY, int sdkVersion) = (void *)so_symbol(&funky_mod, "Java_com_android_Game11Bits_GameLib_init");

	memset(fake_vm, 'A', sizeof(fake_vm));
	*(uintptr_t *)(fake_vm + 0x00) = (uintptr_t)fake_vm; // just point to itself...
	*(uintptr_t *)(fake_vm + 0x10) = (uintptr_t)ret0;
	*(uintptr_t *)(fake_vm + 0x18) = (uintptr_t)GetEnv;

	memset(fake_env, 'A', sizeof(fake_env));
	*(uintptr_t *)(fake_env + 0x00) = (uintptr_t)fake_env; // just point to itself...
	*(uintptr_t *)(fake_env + 0x18) = (uintptr_t)FindClass;
	*(uintptr_t *)(fake_env + 0x54) = (uintptr_t)NewGlobalRef;
	*(uintptr_t *)(fake_env + 0x5C) = (uintptr_t)ret0; // DeleteLocalRef
	*(uintptr_t *)(fake_env + 0x74) = (uintptr_t)NewObjectV;
	*(uintptr_t *)(fake_env + 0x7C) = (uintptr_t)GetObjectClass;
	*(uintptr_t *)(fake_env + 0x84) = (uintptr_t)GetMethodID;
	*(uintptr_t *)(fake_env + 0x1C4) = (uintptr_t)GetStaticMethodID;
	*(uintptr_t *)(fake_env + 0x238) = (uintptr_t)CallStaticVoidMethodV;
	*(uintptr_t *)(fake_env + 0x1D8) = (uintptr_t)CallStaticBooleanMethodV;
	*(uintptr_t *)(fake_env + 0x29C) = (uintptr_t)NewStringUTF;
	*(uintptr_t *)(fake_env + 0x2A4) = (uintptr_t)GetStringUTFChars;
	*(uintptr_t *)(fake_env + 0x2A8) = (uintptr_t)ret0; // ReleaseStringUTFChars
	*(uintptr_t *)(fake_env + 0x36C) = (uintptr_t)GetJavaVM;

	struct stat st;
	stat(DATA_PATH "/main.obb", &st);
	printf("%x %x\n", Java_com_android_Game11Bits_GameLib_initOBBFile, Java_com_android_Game11Bits_GameLib_init);
	//Java_com_android_Game11Bits_GameLib_initOBBFile(fake_env, NULL, DATA_PATH "/main.obb", st.st_size);
	Java_com_android_Game11Bits_GameLib_init(fake_env, (void *)0x41414141, "apk", DATA_PATH, NULL, SCREEN_W, SCREEN_H, 0);

	SceUID ctrl_thid = sceKernelCreateThread("ctrl_thread", (SceKernelThreadEntry)ctrl_thread, 0x10000100, 128 * 1024, 0, 0, NULL);
	sceKernelStartThread(ctrl_thid, 0, NULL);

	return sceKernelExitDeleteThread(0);
}
