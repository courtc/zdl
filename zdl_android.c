/*
 * Copyright (c) 2013, Sony Mobile Communications, AB.
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  - Redistributions of source code must retain the above copyright notice,
 *  this list of conditions and the following disclaimer.
 *
 *  - Redistributions in binary form must reproduce the above copyright notice,
 *  this list of conditions and the following disclaimer in the documentation
 *  and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <jni.h>
#include <poll.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>

#include <EGL/egl.h>
#include <sys/resource.h>

#include <android/native_activity.h>
#include <android/configuration.h>
#include <android/looper.h>
#include <android/log.h>

#define LAYOUTPARAMS_FULLSCREEN 0x00000400

#include "zdl.h"

#define LOG_TAG "zdl"
#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__))
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__))

#ifdef DEBUG
#  define LOGV(...) ((void)__android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__))
#  define LOGD(...) ((void)__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__))
#else
#  define LOGV(...) ((void)0)
#  define LOGD(...) ((void)0)
#endif

enum zdl_app_event_type {
	ZDL_APP_INVALID,
	ZDL_APP_DESTROY,
	ZDL_APP_START,
	ZDL_APP_RESUME,
	ZDL_APP_STATE_SAVE,
	ZDL_APP_PAUSE,
	ZDL_APP_STOP,
	ZDL_APP_CONFIG_CHANGED,
	ZDL_APP_CONTENT_RECT_CHANGED,
	ZDL_APP_LOW_MEMORY,
	ZDL_APP_WINDOW_FOCUS_LOST,
	ZDL_APP_WINDOW_FOCUS_GAINED,
	ZDL_APP_WINDOW_REDRAW_NEEDED,
	ZDL_APP_WINDOW_CREATED,
	ZDL_APP_WINDOW_DESTROYED,
	ZDL_APP_INPUT_QUEUE_CREATED,
	ZDL_APP_INPUT_QUEUE_DESTROYED,
};

struct zdl_app_event {
	enum zdl_app_event_type type;
	union {
		struct {
			void **ptr;
			size_t *size;
		} state;
		ARect rect;
		struct {
			ANativeWindow *window;
		} window;
		struct {
			AInputQueue *queue;
		} input;
		struct {
			sem_t *semaphore;
		} destroy;
	};
};

struct zdl_sem_stack_item {
	sem_t semaphore;
	struct zdl_sem_stack_item *next;
};

struct zdl_sem_stack {
	sem_t ready;
	pthread_mutex_t lock;
	struct zdl_sem_stack_item *head;
};

struct zdl_queue_item {
	struct zdl_event data;
	struct zdl_queue_item *next;
};

struct zdl_queue {
	struct zdl_queue_item *head;
	struct zdl_queue_item *tail;
};

struct zdl_wueue_item {
	void *data;
	sem_t *semaphore;
	struct zdl_wueue_item *next;
	int result;
};

struct zdl_wueue {
	pthread_mutex_t lock;
	struct zdl_wueue_item *head;
	struct zdl_wueue_item *tail;
	struct zdl_sem_stack stack;
	int wpipe[2];
};

struct zdl_window {
	ANativeWindow *native;
	zdl_flags_t flags;
	EGLDisplay display;
	EGLSurface surface;
	EGLContext context;
	int shutdown;
	int width;
	int height;
	struct zdl_queue queue;
};

struct zdl_app {
	struct zdl_wueue wueue;
	sem_t *destroyer;
	sem_t ready;
	pthread_t thread;
	int state;
	zdl_window_t window;
	struct {
		ANativeActivity *activity;
		ANativeWindow *window;
		AConfiguration *config;
		AInputQueue *queue;
		ALooper *looper;
	} internal;
};

static struct zdl_app *g_zdl_app;

#define container_of(ptr, type, member) \
  ((type *)((char *)(ptr) - ((unsigned long)&((type *)0)->member)))

struct zdl_jni {
	jobject   oActivity;
	jmethodID mGetWindow;

	jclass cWindow;
	jmethodID mSetTitle;

	jobject oClipboardManager;
	jmethodID mSetPrimaryClip;
	jmethodID mGetPrimaryClip;
	jmethodID mGetText;
	jmethodID mSetText;

	jclass cClipData;
	jmethodID mNewPlainText;
	jmethodID mNewRawURI;
	jmethodID mGetItemAt;

	jclass cClipData_Item;
	jmethodID mCoerceToText;

	jclass cKeyEvent;
	jmethodID mGetUnicodeChar;
	jmethodID mKeyEvent;
};

static struct zdl_jni g_zdl_jni;

#define _jassert(v) \
  do { \
    if ((v) == NULL) {\
      LOGE("assertion failed: '" #v " != NULL'"); \
      abort(); \
    } \
  } while (0)

static void zdl_jni_setup(ANativeActivity *act)
{
	struct zdl_jni *g = &g_zdl_jni;
	JNIEnv *env = act->env;
	JavaVM *vm = act->vm;

	jfieldID iCLIPBOARD_SERVICE;
	jobject oCLIPBOARD_SERVICE;
	jmethodID mGetSystemService;
	jobject oClipboardManager;
	jclass cClipboardManager;
	jclass cActivity;
	jclass cContext;

	if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) == JNI_EDETACHED)
		(*vm)->AttachCurrentThread(vm, &env, NULL);

	cActivity = (*env)->GetObjectClass(env, act->clazz);
	_jassert(cActivity);
	cContext = (*env)->FindClass(env, "android/content/Context");
	_jassert(cContext);
	g->oActivity = act->clazz;
	_jassert(g->oActivity);
	iCLIPBOARD_SERVICE = (*env)->GetStaticFieldID(env, cContext,
			"CLIPBOARD_SERVICE", "Ljava/lang/String;");
	oCLIPBOARD_SERVICE = (*env)->GetStaticObjectField(env,
			cContext, iCLIPBOARD_SERVICE);
	_jassert(oCLIPBOARD_SERVICE);
	mGetSystemService = (*env)->GetMethodID(env, cActivity,
			"getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
	_jassert(mGetSystemService);
	if (act->sdkVersion >= 11) {
		cClipboardManager = (*env)->FindClass(env, "android/content/ClipboardManager");
		_jassert(cClipboardManager);
		g->cClipData = (*env)->FindClass(env, "android/content/ClipData");
		_jassert(g->cClipData);
		g->cClipData_Item = (*env)->FindClass(env, "android/content/ClipData$Item");
		_jassert(g->cClipData_Item);

		g->mSetPrimaryClip = (*env)->GetMethodID(env, cClipboardManager,
				"setPrimaryClip", "(Landroid/content/ClipData;)V");
		_jassert(g->mSetPrimaryClip);
		g->mGetPrimaryClip = (*env)->GetMethodID(env, cClipboardManager,
				"getPrimaryClip", "()Landroid/content/ClipData;");
		_jassert(g->mGetPrimaryClip);
		g->mNewPlainText = (*env)->GetStaticMethodID(env, g->cClipData,
				"newPlainText", "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)Landroid/content/ClipData;");
		_jassert(g->mNewPlainText);
		g->mNewRawURI = (*env)->GetStaticMethodID(env, g->cClipData,
				"newRawUri", "(Ljava/lang/CharSequence;Landroid/net/Uri;)Landroid/content/ClipData;");
		_jassert(g->mNewRawURI);
		g->mGetItemAt = (*env)->GetMethodID(env, g->cClipData,
				"getItemAt", "(I)Landroid/content/ClipData$Item;");
		_jassert(g->mGetItemAt);
		g->mCoerceToText = (*env)->GetMethodID(env, g->cClipData_Item,
				"coerceToText", "(Landroid/content/Context;)Ljava/lang/CharSequence;");
		_jassert(g->mCoerceToText);
	} else {
		cClipboardManager = (*env)->FindClass(env, "android/text/ClipboardManager");
		_jassert(cClipboardManager);
		g->mSetText = (*env)->GetMethodID(env, cClipboardManager,
				"setText", "(Ljava/lang/CharSequence;)V");
		_jassert(g->mSetText);
		g->mGetText = (*env)->GetMethodID(env, cClipboardManager,
				"getText", "()Ljava/lang/CharSequence;");
		_jassert(g->mGetText);
	}
	oClipboardManager = (*env)->CallObjectMethod(env, g->oActivity,
			mGetSystemService, oCLIPBOARD_SERVICE);
	_jassert(oClipboardManager);
	g->oClipboardManager = (*env)->NewGlobalRef(env, oClipboardManager);
	_jassert(g->oClipboardManager);

	g->cKeyEvent = (*env)->FindClass(env, "android/view/KeyEvent");
	_jassert(g->cKeyEvent);
	g->mKeyEvent = (*env)->GetMethodID(env, g->cKeyEvent, "<init>", "(II)V");
	_jassert(g->mKeyEvent);
	g->mGetUnicodeChar = (*env)->GetMethodID(env, g->cKeyEvent, "getUnicodeChar", "(I)I");
	_jassert(g->mGetUnicodeChar);

	g->cWindow = (*env)->FindClass(env, "android/view/Window");
	_jassert(g->cWindow);
	g->mGetWindow = (*env)->GetMethodID(env, cActivity,
			"getWindow", "()Landroid/view/Window;");
	_jassert(g->mGetWindow);
	g->mSetTitle = (*env)->GetMethodID(env, g->cWindow,
			"setTitle", "(Ljava/lang/CharSequence;)V");
	_jassert(g->mSetTitle);

}

static void zdl_sem_stack_init(struct zdl_sem_stack *s, int count)
{
	int i;

	sem_init(&s->ready, 0, count);
	pthread_mutex_init(&s->lock, 0);

	for (i = 0; i < count; ++i) {
		struct zdl_sem_stack_item *it;

		it = calloc(1, sizeof(*it));
		sem_init(&it->semaphore, 0, 0);
		it->next = s->head;
		s->head = it;
	}
}

static void zdl_sem_stack_fini(struct zdl_sem_stack *s)
{
	while (s->head != NULL) {
		struct zdl_sem_stack_item *it;
		it = s->head;
		s->head = s->head->next;
		sem_destroy(&it->semaphore);
		free(it);
	}
	pthread_mutex_destroy(&s->lock);
	sem_destroy(&s->ready);
}

static sem_t *zdl_sem_stack_pop(struct zdl_sem_stack *s)
{
	struct zdl_sem_stack_item *it;
	sem_wait(&s->ready);

	pthread_mutex_lock(&s->lock);
	it = s->head;
	s->head = s->head->next;
	pthread_mutex_unlock(&s->lock);

	return &it->semaphore;
}

static void zdl_sem_stack_push(struct zdl_sem_stack *s, sem_t *sem)
{
	struct zdl_sem_stack_item *it;

	it = container_of(sem, struct zdl_sem_stack_item, semaphore);
	pthread_mutex_lock(&s->lock);
	it->next = s->head;
	s->head = it;
	pthread_mutex_unlock(&s->lock);

	sem_post(&s->ready);
}

static void zdl_wueue_init(struct zdl_wueue *w)
{
	if (pipe(w->wpipe))
		return;
	pthread_mutex_init(&w->lock, NULL);
	w->head = w->tail = NULL;

	zdl_sem_stack_init(&w->stack, 3);
}

static int zdl_wueue_post(struct zdl_wueue *w, void *data)
{
	struct zdl_wueue_item item;

	item.data = data;
	item.semaphore = zdl_sem_stack_pop(&w->stack);

	pthread_mutex_lock(&w->lock);

	item.next = 0;
	if (w->tail != 0) {
		w->tail->next = &item;
		w->tail = &item;
	} else {
		w->tail = w->head = &item;
	}
	pthread_mutex_unlock(&w->lock);

	write(w->wpipe[1], &data, 4);
	sem_wait(item.semaphore);

	zdl_sem_stack_push(&w->stack, item.semaphore);

	return item.result;
}

static struct zdl_wueue_item *zdl_wueue_read(struct zdl_wueue *w, unsigned int ms)
{
	struct zdl_wueue_item *item;
	int mtype;

	if (ms != (unsigned int)-1) {
		struct pollfd pfd;

		pfd.fd = w->wpipe[0];
		pfd.events = POLLIN;
		if (poll(&pfd, 1, ms) <= 0)
			return NULL;
	}

	read(w->wpipe[0], &mtype, 4);

	pthread_mutex_lock(&w->lock);
	item = w->head;
	w->head = w->head->next;
	if (w->head == 0)
		w->tail = 0;
	item->next = 0;
	pthread_mutex_unlock(&w->lock);

	return item;
}

static void zdl_wueue_release(struct zdl_wueue_item *item, int result)
{
	item->result = result;
	sem_post(item->semaphore);
}

static struct zdl_app_event *zdl_wueue_data(struct zdl_wueue_item *item)
{
	return item->data;
}

static void zdl_wueue_fini(struct zdl_wueue *w)
{
	struct zdl_wueue_item *item;

	while ((item = zdl_wueue_read(w, 0)) != NULL)
		zdl_wueue_release(item, -1);

	pthread_mutex_destroy(&w->lock);

	zdl_sem_stack_fini(&w->stack);
	close(w->wpipe[0]);
	close(w->wpipe[1]);
}

static void zdl_queue_init(struct zdl_queue *q)
{
	q->head = q->tail = NULL;
}

static void zdl_queue_push(struct zdl_queue *q, struct zdl_event *ev)
{
	struct zdl_queue_item *item;

	item = (struct zdl_queue_item *)calloc(1, sizeof(*item));
	item->data = *ev;

	if (q->tail != NULL) {
		q->tail->next = item;
		q->tail = item;
	} else {
		q->tail = q->head = item;
	}
}

static int zdl_queue_pop(struct zdl_queue *q, struct zdl_event *ev)
{
	struct zdl_queue_item *item;

	item = q->head;
	if (item != NULL) {
		q->head = q->head->next;
		if (q->head == NULL)
			q->tail = NULL;

		*ev = item->data;
		free(item);
		return 0;
	}
	return -1;
}

static void zdl_queue_destroy(struct zdl_queue *q)
{
	struct zdl_event ev;
	while (zdl_queue_pop(q, &ev) == 0);
}

static void zdl_window_queue_push(zdl_window_t w, struct zdl_event *ev)
{
	if (w == ZDL_WINDOW_INVALID)
		return;
	zdl_queue_push(&w->queue, ev);
}

static int zdl_display_init(zdl_window_t w)
{
	const EGLint attrs[] = {
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_DEPTH_SIZE, 24,
		EGL_BLUE_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_RED_SIZE, 8,
		EGL_NONE
	};
	const EGLint cattrs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};
	EGLConfig config;
	EGLint nconfig;
	EGLint format;

	w->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (w->display == EGL_NO_DISPLAY)
		goto err_displ;

	eglInitialize(w->display, 0, 0);
	eglChooseConfig(w->display, attrs, &config, 1, &nconfig);
	eglGetConfigAttrib(w->display, config, EGL_NATIVE_VISUAL_ID, &format);

	ANativeWindow_setBuffersGeometry(w->native, 0, 0, format);

	w->surface = eglCreateWindowSurface(w->display, config, w->native, NULL);
	if (w->surface == EGL_NO_SURFACE)
		goto err_srf;

	w->context = eglCreateContext(w->display, config, NULL, cattrs);
	if (w->context == EGL_NO_CONTEXT)
		goto err_ctx;

	if (eglMakeCurrent(w->display, w->surface, w->surface, w->context) == EGL_FALSE)
		goto err_make;

	eglQuerySurface(w->display, w->surface, EGL_WIDTH, &w->width);
	eglQuerySurface(w->display, w->surface, EGL_HEIGHT, &w->height);

	return 0;

err_make:
	eglDestroyContext(w->display, w->context);
err_ctx:
	eglDestroySurface(w->display, w->surface);
err_srf:
	eglTerminate(w->display);
err_displ:
	return -1;
}

static void zdl_display_fini(zdl_window_t w)
{
	if (w->display != EGL_NO_DISPLAY) {
		eglMakeCurrent(w->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		eglDestroyContext(w->display, w->context);
		eglDestroySurface(w->display, w->surface);
		eglTerminate(w->display);
	}
	w->display = EGL_NO_DISPLAY;
	w->context = EGL_NO_CONTEXT;
	w->surface = EGL_NO_SURFACE;
}

void zdl_window_swap(zdl_window_t w)
{
	if (w->display == EGL_NO_DISPLAY)
		return;

	eglSwapBuffers(w->display, w->surface);
}

void zdl_window_set_title(zdl_window_t w, const char *icon, const char *name)
{
	/* Java Analog:
	 *  Activity.getWindow().setTitle(<title>);
	 *  
	 * Should be:
	 *  Activity.runOnUIThread(new Runnable() { public void run() {
	 *  	Activity.getWindow().setTitle(<title>);
	 *  }});
	 *  Which cannot be done in pure C.
	 */
	ANativeActivity *act = g_zdl_app->internal.activity;
	struct zdl_jni *g = &g_zdl_jni;
	JNIEnv *env = act->env;
	JavaVM *vm = act->vm;
	jobject oWindow;
	jstring string;

	if (name == NULL && icon == NULL)
		return;
	if (name == NULL)
		name = icon;

	if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) == JNI_EDETACHED)
		(*vm)->AttachCurrentThread(vm, &env, NULL);

	string = (*env)->NewStringUTF(env, name);
	if (string == NULL)
		return;

	oWindow = (*env)->CallObjectMethod(env, g->oActivity, g->mGetWindow);
	if (oWindow == NULL)
		return;
	// TODO: Must be called in main activity thread
	// (*env)->CallVoidMethod(env, oWindow, g->mSetTitle, string);
	(*env)->DeleteLocalRef(env, string);
	(*env)->DeleteLocalRef(env, oWindow);
}

static const struct {
	zdl_flags_t flag;
	unsigned int wflag;
} zdl_flag_mapping[] = {
	{ ZDL_FLAG_FULLSCREEN, LAYOUTPARAMS_FULLSCREEN },
};

void zdl_window_set_flags(zdl_window_t w, zdl_flags_t flags)
{
	zdl_flags_t new = flags | ZDL_FLAG_FULLSCREEN | ZDL_FLAG_NORESIZE;
	zdl_flags_t chg = new ^ w->flags;
	unsigned int wflags_add = 0;
	unsigned int wflags_rem = 0;
	int i;

	for (i = 0; i < sizeof(zdl_flag_mapping)/sizeof(zdl_flag_mapping[0]); ++i) {
		if (!(chg & zdl_flag_mapping[i].flag))
			continue;
		if (new & zdl_flag_mapping[i].flag)
			wflags_add |= zdl_flag_mapping[i].wflag;
		else
			wflags_rem |= zdl_flag_mapping[i].wflag;
	}

	if (wflags_add | wflags_rem)
		ANativeActivity_setWindowFlags(g_zdl_app->internal.activity, wflags_add, wflags_rem);

	w->flags = flags;
}

zdl_flags_t zdl_window_get_flags(const zdl_window_t w)
{
	return w->flags;
}

void zdl_window_set_size(zdl_window_t w, int width, int height)
{ }

void zdl_window_get_size(const zdl_window_t w, int *width, int *height)
{
	if (width != NULL) *width = w->width;
	if (height != NULL) *height = w->height;
}

void zdl_window_set_position(zdl_window_t w, int x, int y)
{ }

void zdl_window_get_position(const zdl_window_t w, int *x, int *y)
{
	if (x != NULL) *x = 0;
	if (y != NULL) *y = 0;
}

void zdl_window_warp_mouse(zdl_window_t w, int x, int y)
{ /* XXX: Android has no way to warp or hide the mouse cursor as of 2013-06-24 */ }

struct zdl_clipboard {
	struct zdl_jni *jni;
	void *data;
};

zdl_clipboard_t zdl_clipboard_open(zdl_window_t w)
{
	struct zdl_jni *g = &g_zdl_jni;
	zdl_clipboard_t b;

	b = calloc(1, sizeof(*b));
	if (b == NULL)
		return ZDL_CLIPBOARD_INVALID;

	b->jni = g;

	return b;
}

void zdl_clipboard_close(zdl_clipboard_t c)
{
	if (c->data != NULL)
		free(c->data);
	free(c);
}

/* TODO: add URI and Image support */
int zdl_clipboard_write(zdl_clipboard_t c, const struct zdl_clipboard_data *data)
{
	/* Java Analog:
	 *  SDK >= 11:
	 *    import android.content.ClipboardManager;
	 *    import android.content.ClipData;
	 *    ClipboardManager clipboard = (ClipboardManager)getSystemService(Context.CLIPBOARD_SERVICE);
	 *    ClipData clip = ClipData.newPlainText("label", <text>);
	 *    clipboard.setPrimaryClip(clip);
	 *  SDK < 11:
	 *    import android.text.ClipboardManager;
	 *    ClipboardManager clipboard = (ClipboardManager)getSystemService(Context.CLIPBOARD_SERVICE);
	 *    clipboard.setText(<text>);
	 */

	ANativeActivity *act = g_zdl_app->internal.activity;
	struct zdl_jni *g = c->jni;
	JNIEnv *env = act->env;
	JavaVM *vm = act->vm;

	if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) == JNI_EDETACHED)
		(*vm)->AttachCurrentThread(vm, &env, NULL);

	if (act->sdkVersion < 11) {
		if (data->format == ZDL_CLIPBOARD_TEXT) {
			jstring text;

			text = (*env)->NewStringUTF(env, data->text.text);
			(*env)->CallVoidMethod(env, g->oClipboardManager, g->mSetText, text);
			(*env)->DeleteLocalRef(env, text);

			return 0;
		} else {
			return -1;
		}
	} else {
		jobject oClipData;
		jstring label;

		label = (*env)->NewStringUTF(env, "label");
		if (data->format == ZDL_CLIPBOARD_TEXT) {
			jstring text;

			text = (*env)->NewStringUTF(env, data->text.text);
			oClipData = (*env)->CallStaticObjectMethod(env, g->cClipData,
					g->mNewPlainText, label, text);
			(*env)->DeleteLocalRef(env, text);
		} else {
			(*env)->DeleteLocalRef(env, label);
			return -1;
		}

		(*env)->DeleteLocalRef(env, label);
		(*env)->CallVoidMethod(env, g->oClipboardManager, g->mSetPrimaryClip, oClipData);
		(*env)->DeleteLocalRef(env, oClipData);
	}

	return 0;
}

/* TODO: add URI and Image support */
int zdl_clipboard_read(zdl_clipboard_t c, struct zdl_clipboard_data *data)
{
	/* Java Analog:
	 *  SDK >= 11:
	 *    import android.content.ClipboardManager;
	 *    import android.content.ClipData;
	 *    ClipboardManager clipboard = (ClipboardManager)getSystemService(Context.CLIPBOARD_SERVICE);
	 *    ClipData clip = clipboard.getPrimaryClip(clip);
	 *    String text = clip.getItemAt(0).coerceToText(context);
	 *  SDK < 11:
	 *    import android.text.ClipboardManager;
	 *    ClipboardManager clipboard = (ClipboardManager)getSystemService(Context.CLIPBOARD_SERVICE);
	 *    String.text = clipboard.getText();
	 */

	ANativeActivity *act = g_zdl_app->internal.activity;
	struct zdl_jni *g = c->jni;
	JNIEnv *env = act->env;
	JavaVM *vm = act->vm;
	jobject oClipData_Item;
	jobject oClipData;
	const char *str;
	jstring text;

	if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) == JNI_EDETACHED)
		(*vm)->AttachCurrentThread(vm, &env, NULL);

	if (act->sdkVersion >= 11) {
		oClipData = (*env)->CallObjectMethod(env, g->oClipboardManager, g->mGetPrimaryClip);
		if (oClipData == NULL)
			return -1;

		oClipData_Item = (*env)->CallObjectMethod(env, oClipData, g->mGetItemAt, 0);
		if (oClipData_Item == NULL) {
			(*env)->DeleteLocalRef(env, oClipData);
			return -1;
		}

		text = (jstring)(*env)->CallObjectMethod(env, oClipData_Item, g->mCoerceToText, g->oActivity);
		if (text == NULL) {
			(*env)->DeleteLocalRef(env, oClipData_Item);
			(*env)->DeleteLocalRef(env, oClipData);
			return -1;
		}

	} else {
		text = (jstring)(*env)->CallObjectMethod(env, g->oClipboardManager, g->mGetText);
		if (text == NULL)
			return -1;
	}

	if (c->data != NULL)
		free(c->data);

	str = (*env)->GetStringUTFChars(env, text, 0);
	data->format = ZDL_CLIPBOARD_TEXT;
	data->text.text = strdup(str);
	c->data = (void *)data->text.text;
	(*env)->ReleaseStringUTFChars(env, text, str);
	(*env)->DeleteLocalRef(env, text);

	if (act->sdkVersion >= 11) {
		(*env)->DeleteLocalRef(env, oClipData_Item);
		(*env)->DeleteLocalRef(env, oClipData);
	}

	return 0;
}

zdl_window_t zdl_window_create(int width, int height, zdl_flags_t flags)
{
	struct zdl_event ev;
	zdl_window_t w;

	LOGD("+%s()", __func__);
	if (g_zdl_app->window != ZDL_WINDOW_INVALID)
		return ZDL_WINDOW_INVALID;

	w = calloc(1, sizeof(struct zdl_window));
	if (w == NULL)
		return ZDL_WINDOW_INVALID;

	zdl_queue_init(&w->queue);
	zdl_window_set_flags(w, flags);

	g_zdl_app->window = w;

	do {
		zdl_window_wait_event(w, &ev);
	} while (ev.type != ZDL_EVENT_EXPOSE);

	zdl_display_init(w);
	LOGD("-%s()", __func__);

	return w;
}

void zdl_window_destroy(zdl_window_t w)
{
	g_zdl_app->window = ZDL_WINDOW_INVALID;

	zdl_display_fini(w);
	zdl_queue_destroy(&w->queue);
	free(w);
}

static unsigned short zdl_key_uc(struct zdl_app *app, int meta, int keycode, int action)
{
	JNIEnv *env = app->internal.activity->env;
	JavaVM *vm = app->internal.activity->vm;
	struct zdl_jni *g = &g_zdl_jni;
	unsigned short ret;
	jobject oKeyEvent;

	switch (keycode) {
	case AKEYCODE_DEL: return 8;
	}

	if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) == JNI_EDETACHED)
		(*vm)->AttachCurrentThread(vm, &env, NULL);

	oKeyEvent = (*env)->NewObject(env, g->cKeyEvent, g->mKeyEvent, action, keycode);
	ret = (*env)->CallIntMethod(env, oKeyEvent, g->mGetUnicodeChar, meta);
	(*env)->DeleteLocalRef(env, oKeyEvent);

	return ret;
}

static const enum zdl_keysym zdl_keysyms[] = {
	[AKEYCODE_SOFT_LEFT] = ZDL_KEYSYM_LEFT,
	[AKEYCODE_SOFT_RIGHT] = ZDL_KEYSYM_RIGHT,
	[AKEYCODE_HOME] = ZDL_KEYSYM_HOME,
	[AKEYCODE_BACK] = ZDL_KEYSYM_ESCAPE,
	[AKEYCODE_CALL] = -1,
	[AKEYCODE_ENDCALL] = -1,
	[AKEYCODE_0] = ZDL_KEYSYM_0,
	[AKEYCODE_1] = ZDL_KEYSYM_1,
	[AKEYCODE_2] = ZDL_KEYSYM_2,
	[AKEYCODE_3] = ZDL_KEYSYM_3,
	[AKEYCODE_4] = ZDL_KEYSYM_4,
	[AKEYCODE_5] = ZDL_KEYSYM_5,
	[AKEYCODE_6] = ZDL_KEYSYM_6,
	[AKEYCODE_7] = ZDL_KEYSYM_7,
	[AKEYCODE_8] = ZDL_KEYSYM_8,
	[AKEYCODE_9] = ZDL_KEYSYM_9,
	[AKEYCODE_STAR] = ZDL_KEYSYM_ASTERISK,
	[AKEYCODE_POUND] = ZDL_KEYSYM_HASH,
	[AKEYCODE_DPAD_UP] = ZDL_KEYSYM_UP,
	[AKEYCODE_DPAD_DOWN] = ZDL_KEYSYM_DOWN,
	[AKEYCODE_DPAD_LEFT] = ZDL_KEYSYM_LEFT,
	[AKEYCODE_DPAD_RIGHT] = ZDL_KEYSYM_RIGHT,
	[AKEYCODE_DPAD_CENTER] = ZDL_KEYSYM_RETURN,
	[AKEYCODE_VOLUME_UP] = -1,
	[AKEYCODE_VOLUME_DOWN] = -1,
	[AKEYCODE_POWER] = ZDL_KEYSYM_POWER,
	[AKEYCODE_CAMERA] = ZDL_KEYSYM_PRINT,
	[AKEYCODE_CLEAR] = ZDL_KEYSYM_CLEAR,
	[AKEYCODE_A] = ZDL_KEYSYM_A,
	[AKEYCODE_B] = ZDL_KEYSYM_B,
	[AKEYCODE_C] = ZDL_KEYSYM_C,
	[AKEYCODE_D] = ZDL_KEYSYM_D,
	[AKEYCODE_E] = ZDL_KEYSYM_E,
	[AKEYCODE_F] = ZDL_KEYSYM_F,
	[AKEYCODE_G] = ZDL_KEYSYM_G,
	[AKEYCODE_H] = ZDL_KEYSYM_H,
	[AKEYCODE_I] = ZDL_KEYSYM_I,
	[AKEYCODE_J] = ZDL_KEYSYM_J,
	[AKEYCODE_K] = ZDL_KEYSYM_K,
	[AKEYCODE_L] = ZDL_KEYSYM_L,
	[AKEYCODE_M] = ZDL_KEYSYM_M,
	[AKEYCODE_N] = ZDL_KEYSYM_N,
	[AKEYCODE_O] = ZDL_KEYSYM_O,
	[AKEYCODE_P] = ZDL_KEYSYM_P,
	[AKEYCODE_Q] = ZDL_KEYSYM_Q,
	[AKEYCODE_R] = ZDL_KEYSYM_R,
	[AKEYCODE_S] = ZDL_KEYSYM_S,
	[AKEYCODE_T] = ZDL_KEYSYM_T,
	[AKEYCODE_U] = ZDL_KEYSYM_U,
	[AKEYCODE_V] = ZDL_KEYSYM_V,
	[AKEYCODE_W] = ZDL_KEYSYM_W,
	[AKEYCODE_X] = ZDL_KEYSYM_X,
	[AKEYCODE_Y] = ZDL_KEYSYM_Y,
	[AKEYCODE_Z] = ZDL_KEYSYM_Z,
	[AKEYCODE_COMMA] = ZDL_KEYSYM_COMMA,
	[AKEYCODE_PERIOD] = ZDL_KEYSYM_PERIOD,
	[AKEYCODE_ALT_LEFT] = ZDL_KEYSYM_LALT,
	[AKEYCODE_ALT_RIGHT] = ZDL_KEYSYM_RALT,
	[AKEYCODE_SHIFT_LEFT] = ZDL_KEYSYM_LSHIFT,
	[AKEYCODE_SHIFT_RIGHT] = ZDL_KEYSYM_RSHIFT,
	[AKEYCODE_TAB] = ZDL_KEYSYM_TAB,
	[AKEYCODE_SPACE] = ZDL_KEYSYM_SPACE,
	[AKEYCODE_SYM] = ZDL_KEYSYM_MODE,
	[AKEYCODE_EXPLORER] = -1,
	[AKEYCODE_ENVELOPE] = ZDL_KEYSYM_COMPOSE,
	[AKEYCODE_ENTER] = ZDL_KEYSYM_RETURN,
	[AKEYCODE_DEL] = ZDL_KEYSYM_DELETE,
	[AKEYCODE_GRAVE] = ZDL_KEYSYM_BACKQUOTE,
	[AKEYCODE_MINUS] = ZDL_KEYSYM_MINUS,
	[AKEYCODE_EQUALS] = ZDL_KEYSYM_EQUALS,
	[AKEYCODE_LEFT_BRACKET] = ZDL_KEYSYM_LEFTBRACKET,
	[AKEYCODE_RIGHT_BRACKET] = ZDL_KEYSYM_RIGHTBRACKET,
	[AKEYCODE_BACKSLASH] = ZDL_KEYSYM_BACKSLASH,
	[AKEYCODE_SEMICOLON] = ZDL_KEYSYM_SEMICOLON,
	[AKEYCODE_APOSTROPHE] = ZDL_KEYSYM_QUOTE,
	[AKEYCODE_SLASH] = ZDL_KEYSYM_SLASH,
	[AKEYCODE_AT] = ZDL_KEYSYM_AT,
	[AKEYCODE_NUM] = ZDL_KEYSYM_NUMLOCK,
	[AKEYCODE_HEADSETHOOK] = ZDL_KEYSYM_ESCAPE,
	[AKEYCODE_FOCUS] = -1,
	[AKEYCODE_PLUS] = ZDL_KEYSYM_PLUS,
	[AKEYCODE_MENU] = ZDL_KEYSYM_MENU,
	[AKEYCODE_NOTIFICATION] = -1,
	[AKEYCODE_SEARCH] = -1,
	[AKEYCODE_MEDIA_PLAY_PAUSE] = -1,
	[AKEYCODE_MEDIA_STOP] = -1,
	[AKEYCODE_MEDIA_NEXT] = -1,
	[AKEYCODE_MEDIA_PREVIOUS] = -1,
	[AKEYCODE_MEDIA_REWIND] = -1,
	[AKEYCODE_MEDIA_FAST_FORWARD] = -1,
	[AKEYCODE_MUTE] = -1,
	[AKEYCODE_PAGE_UP] = ZDL_KEYSYM_PAGEUP,
	[AKEYCODE_PAGE_DOWN] = ZDL_KEYSYM_PAGEDOWN,
	[AKEYCODE_PICTSYMBOLS] = -1,
	[AKEYCODE_SWITCH_CHARSET] = ZDL_KEYSYM_MODE,
	[AKEYCODE_BUTTON_A] = -1,
	[AKEYCODE_BUTTON_B] = -1,
	[AKEYCODE_BUTTON_C] = -1,
	[AKEYCODE_BUTTON_X] = -1,
	[AKEYCODE_BUTTON_Y] = -1,
	[AKEYCODE_BUTTON_Z] = -1,
	[AKEYCODE_BUTTON_L1] = -1,
	[AKEYCODE_BUTTON_R1] = -1,
	[AKEYCODE_BUTTON_L2] = -1,
	[AKEYCODE_BUTTON_R2] = -1,
	[AKEYCODE_BUTTON_THUMBL] = -1,
	[AKEYCODE_BUTTON_THUMBR] = -1,
	[AKEYCODE_BUTTON_START] = -1,
	[AKEYCODE_BUTTON_SELECT] = -1,
	[AKEYCODE_BUTTON_MODE] = -1,
};

static int zdl_app_handle_input(struct zdl_app *app, AInputEvent *event, int *handled)
{
	int type = AInputEvent_getType(event);
	int source = AInputEvent_getSource(event);
	zdl_window_t w = app->window;
	struct zdl_event ev;

	*handled = 1;
	if (type == AINPUT_EVENT_TYPE_MOTION) {
		int action = AMotionEvent_getAction(event);
		int count = AMotionEvent_getPointerCount(event);
		int code = action & AMOTION_EVENT_ACTION_MASK;
		int fid = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
				AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
		int id_off = (source == AINPUT_SOURCE_MOUSE) ?
			ZDL_MOTION_POINTER : ZDL_MOTION_TOUCH_START;
		int nold = AMotionEvent_getHistorySize(event);
		int i;

		ev.type = ZDL_EVENT_MOTION;
		for (i = 0; i < count; ++i) {
			ev.motion.x = AMotionEvent_getX(event, i);
			ev.motion.y = AMotionEvent_getY(event, i);
			ev.motion.id = AMotionEvent_getPointerId(event, i) + id_off;
			ev.motion.flags = ZDL_MOTION_FLAG_NONE;
			switch (code) {
			case AMOTION_EVENT_ACTION_DOWN:
			case AMOTION_EVENT_ACTION_POINTER_DOWN:
				if (i == fid)
					ev.motion.flags = ZDL_MOTION_FLAG_INITIAL;
				break;
			case AMOTION_EVENT_ACTION_UP:
			case AMOTION_EVENT_ACTION_CANCEL:
			case AMOTION_EVENT_ACTION_POINTER_UP:
				if (i == fid)
					ev.motion.flags = ZDL_MOTION_FLAG_FINAL;
				break;
			default:
				break;
			}

			if (nold > 0) {
				int oldx = AMotionEvent_getHistoricalX(event, i, nold - 1);
				int oldy = AMotionEvent_getHistoricalY(event, i, nold - 1);
				ev.motion.d_x = ev.motion.x - oldx;
				ev.motion.d_y = ev.motion.y - oldy;
			} else {
				ev.motion.d_x = 0;
				ev.motion.d_y = 0;
			}
			zdl_window_queue_push(w, &ev);
		}
	} else if (type == AINPUT_EVENT_TYPE_KEY) {
		int action = AKeyEvent_getAction(event);
		int code = AKeyEvent_getKeyCode(event);
		int scan = AKeyEvent_getScanCode(event);
		int state = AKeyEvent_getMetaState(event);
		zdl_keymod_t modifiers;
		int repl;

		modifiers = 0;
		modifiers |= ((state & AMETA_ALT_ON) ? ZDL_KEYMOD_LALT : 0);
		modifiers |= ((state & AMETA_ALT_LEFT_ON) ? ZDL_KEYMOD_LALT : 0);
		modifiers |= ((state & AMETA_ALT_RIGHT_ON) ? ZDL_KEYMOD_RALT : 0);
		modifiers |= ((state & AMETA_SHIFT_ON) ? ZDL_KEYMOD_LSHIFT : 0);
		modifiers |= ((state & AMETA_SHIFT_LEFT_ON) ? ZDL_KEYMOD_LSHIFT : 0);
		modifiers |= ((state & AMETA_SHIFT_RIGHT_ON) ? ZDL_KEYMOD_RSHIFT : 0);
		modifiers |= ((state & AMETA_SYM_ON) ? ZDL_KEYMOD_LMETA : 0);

		switch (action) {
		case AKEY_EVENT_ACTION_DOWN:
			repl = AKeyEvent_getRepeatCount(event);
			if (source == AINPUT_SOURCE_MOUSE) {
				ev.type = ZDL_EVENT_BUTTONPRESS;
				ev.button.modifiers = modifiers;
			} else {
				ev.type = ZDL_EVENT_KEYPRESS;
				ev.key.modifiers = modifiers;
				ev.key.scancode = scan;
				ev.key.unicode = zdl_key_uc(app, state, code, action);
				ev.key.sym = zdl_keysyms[code % (sizeof(zdl_keysyms)/4)];
			}
			break;
		case AKEY_EVENT_ACTION_UP:
			repl = AKeyEvent_getRepeatCount(event);
			if (source == AINPUT_SOURCE_MOUSE) {
				ev.type = ZDL_EVENT_BUTTONRELEASE;
				ev.button.modifiers = modifiers;
			} else {
				ev.type = ZDL_EVENT_KEYRELEASE;
				ev.key.modifiers = modifiers;
				ev.key.scancode = scan;
				ev.key.unicode = zdl_key_uc(app, state, code, action);
				ev.key.sym = zdl_keysyms[code % (sizeof(zdl_keysyms)/4)];
			}
			break;
		case AKEY_EVENT_ACTION_MULTIPLE:
			repl = AKeyEvent_getRepeatCount(event) * 2;
			break;
		}
		if (repl)
			LOGI("Key repeat not handled");
		zdl_window_queue_push(w, &ev);
	} else {
		*handled = 0;
	}

	return 0;
}

static int _zdl_app_handle_input(int _fd, int _events, void *data)
{
	struct zdl_app *app = (struct zdl_app *)data;
	AInputEvent *event;
	int handled;
	int rc;

	rc = AInputQueue_getEvent(app->internal.queue, &event);
	if (rc < 0)
		return 1;

	rc = AInputQueue_preDispatchEvent(app->internal.queue, event);
	if (rc)
		return 1;

	handled = 1;
	zdl_app_handle_input(app, event, &handled);

	AInputQueue_finishEvent(app->internal.queue, event, handled);

	return 1;
}

static void zdl_app_finish(struct zdl_app *app)
{
	if (app->state == ZDL_APP_DESTROY)
		return;

	ANativeActivity_finish(app->internal.activity);
	app->state = ZDL_APP_DESTROY;
}

static int zdl_app_handle_event(struct zdl_app *app, struct zdl_app_event *aev)
{
	zdl_window_t w = app->window;
	struct zdl_event ev;

	switch (aev->type) {
	case ZDL_APP_INVALID:
		break;
	case ZDL_APP_STOP:
		app->state = aev->type;
		ev.type = ZDL_EVENT_EXIT;
		zdl_window_queue_push(w, &ev);
		break;
	case ZDL_APP_START:
	case ZDL_APP_PAUSE:
	case ZDL_APP_RESUME:
		app->state = aev->type;
		break;
	case ZDL_APP_DESTROY:
		ev.type = ZDL_EVENT_EXIT;
		app->destroyer = aev->destroy.semaphore;
		app->state = aev->type;
		zdl_window_queue_push(w, &ev);
		break;
	case ZDL_APP_LOW_MEMORY:
		break;
	case ZDL_APP_STATE_SAVE:
		*aev->state.ptr = NULL;
		*aev->state.size = 0;
		break;
	case ZDL_APP_CONFIG_CHANGED:
		AConfiguration_fromAssetManager(app->internal.config, app->internal.activity->assetManager);
		break;
	case ZDL_APP_CONTENT_RECT_CHANGED:
		ev.type = ZDL_EVENT_RECONFIGURE;
		//ev.reconfigure.width = aev->rect.right  - aev->rect.left;
		//ev.reconfigure.height = aev->rect.bottom - aev->rect.top;
		ev.reconfigure.width = ANativeWindow_getWidth(w->native);
		ev.reconfigure.height = ANativeWindow_getHeight(w->native);
		if (ev.reconfigure.width != w->width ||
				ev.reconfigure.height != w->height) {
			w->width = ev.reconfigure.width;
			w->height = ev.reconfigure.height;
			zdl_window_queue_push(w, &ev);
		}
		break;
	case ZDL_APP_WINDOW_CREATED:
		app->internal.window = aev->window.window;
		if (w != ZDL_WINDOW_INVALID)
			w->native = aev->window.window;
		ev.type = ZDL_EVENT_EXPOSE;
		zdl_window_queue_push(w, &ev);
		break;
	case ZDL_APP_WINDOW_DESTROYED:
		app->internal.window = NULL;
		if (w != ZDL_WINDOW_INVALID)
			w->native = NULL;
		break;
	case ZDL_APP_WINDOW_FOCUS_LOST:
		ev.type = ZDL_EVENT_LOSEFOCUS;
		zdl_window_queue_push(w, &ev);
		break;
	case ZDL_APP_WINDOW_FOCUS_GAINED:
		ev.type = ZDL_EVENT_GAINFOCUS;
		zdl_window_queue_push(w, &ev);
		break;
	case ZDL_APP_WINDOW_REDRAW_NEEDED:
		ev.type = ZDL_EVENT_EXPOSE;
		zdl_window_queue_push(w, &ev);
		break;
	case ZDL_APP_INPUT_QUEUE_DESTROYED:
		if (app->internal.queue != NULL)
			AInputQueue_detachLooper(app->internal.queue);
		app->internal.queue = NULL;
		break;
	case ZDL_APP_INPUT_QUEUE_CREATED:
		if (app->internal.queue != NULL)
			AInputQueue_detachLooper(app->internal.queue);
		app->internal.queue = aev->input.queue;
		AInputQueue_attachLooper(app->internal.queue, app->internal.looper,
				1, _zdl_app_handle_input, app);
		break;
	}

	return 0;
}

static int _zdl_app_handle_event(int _fd, int _events, void *data)
{
	struct zdl_app *app = (struct zdl_app *)data;
	struct zdl_wueue_item *item;
	struct zdl_app_event *ev;
	int rc;

	item = zdl_wueue_read(&app->wueue, -1);
	if (item == NULL)
		return 1;

	ev = (struct zdl_app_event *)zdl_wueue_data(item);
	rc = zdl_app_handle_event(app, ev);
	zdl_wueue_release(item, rc);

	return 1;
}

static void zdl_app_destroy(struct zdl_app *app)
{
	JNIEnv *env = app->internal.activity->env;
	JavaVM *vm = app->internal.activity->vm;
	sem_t *destroyer;

	sem_wait(&app->ready);
	destroyer = app->destroyer;

	if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_EDETACHED)
		(*vm)->DetachCurrentThread(vm);

	if (app->window != ZDL_WINDOW_INVALID)
		zdl_window_destroy(app->window);
	g_zdl_app = NULL;
	if (app->internal.queue != NULL)
		AInputQueue_detachLooper(app->internal.queue);
	AConfiguration_delete(app->internal.config);

	zdl_wueue_fini(&app->wueue);
	sem_destroy(&app->ready);
	free(app);

	if (destroyer)
		sem_post(destroyer);
}

static void zdl_call_main(int (* main)(int, char **))
{
	char *argv[] = {
		"zdl_application",
		NULL
	};
	LOGD("+%s()", __func__);
	(*main)(1, argv);
	LOGD("-%s()", __func__);
}

int (* zdl_main)(int, char **) = 0;

static void *zdl_app_thread(void *param)
{
	struct zdl_app *app = (struct zdl_app *)param;

	LOGD("+%s()", __func__);
	app->internal.looper = ALooper_prepare(0);
	ALooper_addFd(app->internal.looper, app->wueue.wpipe[0],
			0, ALOOPER_EVENT_INPUT, _zdl_app_handle_event, app);

	zdl_wueue_release(zdl_wueue_read(&app->wueue, -1), 0);
	zdl_call_main(zdl_main);

	zdl_app_finish(app);

	while (app->destroyer == NULL) {
		void *data;
		int events;
		ALooper_pollOnce(-1, NULL, &events, &data);
	}
	ALooper_removeFd(app->internal.looper, app->wueue.wpipe[0]);

	zdl_app_destroy(app);

	LOGD("-%s()", __func__);

	return NULL;
}

void zdl_window_wait_event(zdl_window_t w, struct zdl_event *ev)
{
	void *data;
	int events;

	if (w->shutdown) {
		ev->type = ZDL_EVENT_ERROR;
		return;
	}

	for (;;) {
		ALooper_pollOnce(-1, NULL, &events, &data);

		if (zdl_queue_pop(&w->queue, ev) == 0) {
			if (ev->type == ZDL_EVENT_EXIT)
				w->shutdown = 1;
			return;
		}
	}
}

int zdl_window_poll_event(zdl_window_t w, struct zdl_event *ev)
{
	void *data;
	int events;

	if (w->shutdown)
		return -1;

	while (ALooper_pollOnce(0, NULL, &events, &data) == ALOOPER_POLL_CALLBACK) {
		if (zdl_queue_pop(&w->queue, ev) == 0) {
			if (ev->type == ZDL_EVENT_EXIT)
				w->shutdown = 1;
			return 0;
		}
	}

	return -1;
}

static struct zdl_app *zdl_app_create(ANativeActivity *act,
		void *savedState, size_t savedStateSize)
{
	pthread_attr_t attr;
	struct zdl_app *app;

	LOGD("+%s()", __func__);
	app = calloc(1, sizeof(*app));
	if (app == NULL)
		return NULL;

	sem_init(&app->ready, 0, 0);
	app->internal.activity = act;
	zdl_wueue_init(&app->wueue);

	app->internal.config = AConfiguration_new();
	AConfiguration_fromAssetManager(app->internal.config, act->assetManager);

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_create(&app->thread, &attr, zdl_app_thread, app);
	pthread_attr_destroy(&attr);

	g_zdl_app = app;
	zdl_wueue_post(&app->wueue, app);
	LOGD("-%s()", __func__);

	return app;
}

static void zdl_app_onDestroy(ANativeActivity *act)
{
	struct zdl_app *app = (struct zdl_app *)act->instance;
	struct zdl_app_event ev;
	sem_t sem;

	sem_init(&sem, 0, 0);
	ev.type = ZDL_APP_DESTROY;
	ev.destroy.semaphore = &sem;
	LOGD("+%s()", __func__);
	zdl_wueue_post(&app->wueue, &ev);
	sem_post(&app->ready);
	sem_wait(&sem);
	sem_destroy(&sem);
	LOGD("-%s()", __func__);
}

static void zdl_app_onStart(ANativeActivity *act)
{
	struct zdl_app *app = (struct zdl_app *)act->instance;
	struct zdl_app_event ev;

	ev.type = ZDL_APP_START;
	LOGD("+%s()", __func__);
	zdl_wueue_post(&app->wueue, &ev);
	LOGD("-%s()", __func__);
}

static void zdl_app_onResume(ANativeActivity *act)
{
	struct zdl_app *app = (struct zdl_app *)act->instance;
	struct zdl_app_event ev;

	ev.type = ZDL_APP_RESUME;
	LOGD("+%s()", __func__);
	zdl_wueue_post(&app->wueue, &ev);
	LOGD("-%s()", __func__);
}

static void *zdl_app_onSaveInstanceState(ANativeActivity *act, size_t *outlen)
{
	struct zdl_app *app = (struct zdl_app *)act->instance;
	struct zdl_app_event ev;
	void *ret;

	ev.type = ZDL_APP_STATE_SAVE;
	ev.state.ptr = &ret;
	ev.state.size = outlen;
	LOGD("+%s()", __func__);
	zdl_wueue_post(&app->wueue, &ev);
	LOGD("-%s()", __func__);

	return ret;
}

static void zdl_app_onPause(ANativeActivity *act)
{
	struct zdl_app *app = (struct zdl_app *)act->instance;
	struct zdl_app_event ev;

	ev.type = ZDL_APP_PAUSE;
	LOGD("+%s()", __func__);
	zdl_wueue_post(&app->wueue, &ev);
	LOGD("-%s()", __func__);
}

static void zdl_app_onStop(ANativeActivity *act)
{
	struct zdl_app *app = (struct zdl_app *)act->instance;
	struct zdl_app_event ev;

	ev.type = ZDL_APP_STOP;
	LOGD("+%s()", __func__);
	zdl_wueue_post(&app->wueue, &ev);
	LOGD("-%s()", __func__);
}

static void zdl_app_onConfigurationChanged(ANativeActivity *act)
{
	struct zdl_app *app = (struct zdl_app *)act->instance;
	struct zdl_app_event ev;

	ev.type = ZDL_APP_CONFIG_CHANGED;
	LOGD("+%s()", __func__);
	zdl_wueue_post(&app->wueue, &ev);
	LOGD("-%s()", __func__);
}

static void zdl_app_onContentRectChanged(ANativeActivity *act, const ARect* rect)
{
	struct zdl_app *app = (struct zdl_app *)act->instance;
	struct zdl_app_event ev;

	ev.type = ZDL_APP_CONTENT_RECT_CHANGED;
	ev.rect = *rect;

	LOGD("+%s()", __func__);
	zdl_wueue_post(&app->wueue, &ev);
	LOGD("-%s()", __func__);
}

static void zdl_app_onLowMemory(ANativeActivity *act)
{
	struct zdl_app *app = (struct zdl_app *)act->instance;
	struct zdl_app_event ev;

	ev.type = ZDL_APP_LOW_MEMORY;

	LOGD("+%s()", __func__);
	zdl_wueue_post(&app->wueue, &ev);
	LOGD("-%s()", __func__);
}

static void zdl_app_onWindowFocusChanged(ANativeActivity *act, int focused)
{
	struct zdl_app *app = (struct zdl_app *)act->instance;
	struct zdl_app_event ev;

	ev.type = focused ? ZDL_APP_WINDOW_FOCUS_LOST : ZDL_APP_WINDOW_FOCUS_GAINED;

	LOGD("+%s()", __func__);
	zdl_wueue_post(&app->wueue, &ev);
	LOGD("-%s()", __func__);
}

static void zdl_app_onNativeWindowRedrawNeeded(ANativeActivity *act, ANativeWindow *window)
{
	struct zdl_app *app = (struct zdl_app *)act->instance;
	struct zdl_app_event ev;

	ev.type = ZDL_APP_WINDOW_REDRAW_NEEDED;
	ev.window.window = window;

	LOGD("+%s()", __func__);
	zdl_wueue_post(&app->wueue, &ev);
	LOGD("-%s()", __func__);
}

static void zdl_app_onNativeWindowCreated(ANativeActivity *act, ANativeWindow *window)
{
	struct zdl_app *app = (struct zdl_app *)act->instance;
	struct zdl_app_event ev;

	ev.type = ZDL_APP_WINDOW_CREATED;
	ev.window.window = window;

	LOGD("+%s()", __func__);
	zdl_wueue_post(&app->wueue, &ev);
	LOGD("-%s()", __func__);
}

static void zdl_app_onNativeWindowDestroyed(ANativeActivity *act, ANativeWindow *window)
{
	struct zdl_app *app = (struct zdl_app *)act->instance;
	struct zdl_app_event ev;

	ev.type = ZDL_APP_WINDOW_DESTROYED;
	ev.window.window = window;

	LOGD("+%s()", __func__);
	zdl_wueue_post(&app->wueue, &ev);
	LOGD("-%s()", __func__);
}

static void zdl_app_onInputQueueCreated(ANativeActivity *act, AInputQueue *queue)
{
	struct zdl_app *app = (struct zdl_app *)act->instance;
	struct zdl_app_event ev;

	ev.type = ZDL_APP_INPUT_QUEUE_CREATED;
	ev.input.queue = queue;

	LOGD("+%s()", __func__);
	zdl_wueue_post(&app->wueue, &ev);
	LOGD("-%s()", __func__);
}

static void zdl_app_onInputQueueDestroyed(ANativeActivity *act, AInputQueue *queue)
{
	struct zdl_app *app = (struct zdl_app *)act->instance;
	struct zdl_app_event ev;

	ev.type = ZDL_APP_INPUT_QUEUE_DESTROYED;
	ev.input.queue = queue;

	LOGD("+%s()", __func__);
	zdl_wueue_post(&app->wueue, &ev);
	LOGD("-%s()", __func__);
}

void ANativeActivity_onCreate(ANativeActivity* act,
		void *savedState, size_t savedStateSize)
{
	act->callbacks->onDestroy = zdl_app_onDestroy;
	act->callbacks->onStart = zdl_app_onStart;
	act->callbacks->onResume = zdl_app_onResume;
	act->callbacks->onSaveInstanceState = zdl_app_onSaveInstanceState;
	act->callbacks->onPause = zdl_app_onPause;
	act->callbacks->onStop = zdl_app_onStop;
	act->callbacks->onConfigurationChanged = zdl_app_onConfigurationChanged;
	act->callbacks->onContentRectChanged = zdl_app_onContentRectChanged;
	act->callbacks->onLowMemory = zdl_app_onLowMemory;
	act->callbacks->onWindowFocusChanged = zdl_app_onWindowFocusChanged;
	act->callbacks->onNativeWindowRedrawNeeded = zdl_app_onNativeWindowRedrawNeeded;
	act->callbacks->onNativeWindowCreated = zdl_app_onNativeWindowCreated;
	act->callbacks->onNativeWindowDestroyed = zdl_app_onNativeWindowDestroyed;
	act->callbacks->onInputQueueCreated = zdl_app_onInputQueueCreated;
	act->callbacks->onInputQueueDestroyed = zdl_app_onInputQueueDestroyed;
	act->instance = zdl_app_create(act, savedState, savedStateSize);
	zdl_jni_setup(act);
}
