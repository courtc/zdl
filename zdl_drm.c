/*
 * Copyright (c) 2012, Courtney Cavin
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

#define EGL_EGLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <string.h>
#include <xf86drmMode.h>
#include <xf86drm.h>
#include <gbm.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <termio.h>
#include <poll.h>
#include <unistd.h>
#include <dlfcn.h>

#include <linux/kd.h>
#include <linux/vt.h>
#include "zdl.h"

#ifndef POLLRDHUP
#define POLLRDHUP 0
#endif

#define fatal(s) \
do { \
	fprintf(stderr, s); \
} while (0)

struct drm_context {
	drmModeRes       *resources;
	drmModeConnector *connector;
	drmModeModeInfo  *info;
	drmModeEncoder   *encoder;
	int can_master;
	int dri_fd;
};

static struct drm_context *drm_create(int fd)
{
	struct drm_context *ctx;
	drmModeConnector *connector;
	drmModeModeInfo *info;
	drmModeEncoder *encoder;
	drmModeRes *resources;
	int can_master = 1;
	int area = 0;
	int i;

	if (drmSetMaster(fd) != 0) {
		if (errno == EINVAL) {
			perror("Failed to set DRM master");
			return NULL;
		}
		can_master = 0;
	}

	resources = drmModeGetResources(fd);
	if (!resources) {
		fatal("Failed to get DRM resources\n");
		return NULL;
	}

	for (i = 0; i < resources->count_connectors; ++i) {
		connector = drmModeGetConnector(fd, resources->connectors[i]);
		if (connector == NULL)
			continue;

		if (connector->connection == DRM_MODE_CONNECTED &&
				connector->count_modes > 0)
			break;

		drmModeFreeConnector(connector);
	}

	if (i == resources->count_connectors) {
		fatal("No currently active connector found.\n");
		drmModeFreeResources(resources);
		return NULL;
	}

	for (i = 0; i < resources->count_encoders; ++i) {
		encoder = drmModeGetEncoder(fd, resources->encoders[i]);
		if (encoder == NULL)
			continue;

		if (encoder->encoder_id == connector->encoder_id)
			break;

		drmModeFreeEncoder(encoder);
	}

	if (i == resources->count_encoders) {
		fatal("No encoder found for currently active connector.\n");
		drmModeFreeConnector(connector);
		drmModeFreeResources(resources);
		return NULL;
	}

	info = NULL;
	for (i = 0; i < connector->count_modes; ++i) {
		drmModeModeInfo *mode = &connector->modes[i];
		if (mode->vdisplay * mode->hdisplay > area) {
			info = mode;
			area = mode->vdisplay * mode->hdisplay;
		}
	}

	if (info == NULL) {
		fatal("No mode found for active connector.\n");
		drmModeFreeEncoder(encoder);
		drmModeFreeConnector(connector);
		drmModeFreeResources(resources);
		return NULL;
	}

	ctx = (struct drm_context *)calloc(1, sizeof(*ctx));
	if (ctx == NULL) {
		fatal("Failed to alloc DRM context\n");
		drmModeFreeEncoder(encoder);
		drmModeFreeConnector(connector);
		drmModeFreeResources(resources);
		return NULL;
	}

	ctx->can_master = can_master;
	ctx->resources = resources;
	ctx->connector = connector;
	ctx->encoder = encoder;
	ctx->info = info;
	ctx->dri_fd = fd;

	return ctx;

}

static void drm_destroy(struct drm_context *ctx)
{
	drmDropMaster(ctx->dri_fd);
	drmModeFreeEncoder(ctx->encoder);
	drmModeFreeConnector(ctx->connector);
	drmModeFreeResources(ctx->resources);
	free(ctx);
}

struct egl_context {
	struct gbm_surface *surface;
	struct drm_context *drm;
	struct gbm_device *gbm;
	EGLSurface egl_surface;
	EGLContext context;
	GLuint framebuffer;
	EGLDisplay display;
	int dri_fd;
	int frame;
	int vsync;
	int suspended;

	struct {
		GLuint color;
		GLuint depth;
		EGLImageKHR image;
		struct gbm_bo *bo;
		uint32_t fb_id;
	} rb[2];
};

static struct egl_context *egl_create(void)
{
	struct egl_context *egl;
	EGLint major, minor;
	static const EGLint config_attribs[] = {
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 0,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
		EGL_NONE
	};
	EGLConfig config;
	const char *ver;
	int i, n;

	egl = (struct egl_context *)calloc(1, sizeof(*egl));
	if (egl == NULL) {
		fatal("Failed to alloc EGL context\n");
		return NULL;
	}

	egl->dri_fd = open("/dev/dri/card0", O_RDWR);
	if (egl->dri_fd == -1) {
		fatal("Unable to open dri card\n");
		goto err_dri_open;
	}

	egl->gbm = gbm_create_device(egl->dri_fd);
	if (egl->gbm == NULL) {
		fatal("Unable to create gbm device\n");
		goto err_gbm_create;
	}

	egl->display = eglGetDisplay((EGLNativeDisplayType)egl->gbm);
	if (egl->display == EGL_NO_DISPLAY) {
		fatal("Unable to fetch EGL display\n");
		goto err_egl_getdisplay;
	}

	if (!eglInitialize(egl->display, &major, &minor)) {
		fatal("Unable to initialize EGL\n");
		goto err_egl_init;
	}

	ver = eglQueryString(egl->display, EGL_VERSION);

	egl->drm = drm_create(egl->dri_fd);
	if (egl->drm == NULL) {
		fatal("Failed to create DRM context\n");
		goto err_drm_create;
	}

	if (!eglBindAPI(EGL_OPENGL_API)) {
		fatal("Failed to create bind OpenGL API\n");
		goto err_egl_bind;
	}

	if (!eglChooseConfig(egl->display, config_attribs,
			     &config, 1, &n) || n != 1) {
		fatal("Failed to choose EGL config\n");
		goto err_egl_chooseconfig;
	}

	egl->context = eglCreateContext(egl->display, config,
			EGL_NO_CONTEXT, NULL);
	if (egl->context == EGL_NO_CONTEXT) {
		fatal("Failed to create context\n");
		goto err_egl_createctx;
	}

	egl->surface = gbm_surface_create(egl->gbm, 10, 10,
			GBM_FORMAT_XRGB8888, GBM_BO_USE_RENDERING);
	if (!egl->surface) {
		fatal("Failed to create dummy gbm surface\n");
		goto err_gbm_surface;
	}

	egl->egl_surface = eglCreateWindowSurface(egl->display,
			config, (EGLNativeWindowType)egl->surface, NULL);
	if (egl->egl_surface == EGL_NO_SURFACE) {
		fatal("Failed to create EGL surface\n");
		goto err_egl_surface;
	}

	if (!eglMakeCurrent(egl->display, egl->egl_surface,
			egl->egl_surface, egl->context)) {
		fatal("Failed to set EGL context\n");
		goto err_egl_current;
	}

	glGenFramebuffers(1, &egl->framebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER_EXT, egl->framebuffer);

	for (i = 0; i < 2; ++i) {
		uint32_t handle, stride;

		glGenRenderbuffers(1, &egl->rb[i].color);
		glGenRenderbuffers(1, &egl->rb[i].depth);

		egl->rb[i].bo = gbm_bo_create(egl->gbm,
				egl->drm->info->hdisplay,
				egl->drm->info->vdisplay,
				GBM_BO_FORMAT_XRGB8888,
				GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
		if (egl->rb[i].bo == NULL) {
			fatal("Failed to create gbm buffer object\n");
			break;
		}

		handle = gbm_bo_get_handle(egl->rb[i].bo).u32;
		stride = gbm_bo_get_stride(egl->rb[i].bo);

		drmModeAddFB(egl->dri_fd, egl->drm->info->hdisplay,
				egl->drm->info->vdisplay, 24, 32,
				stride, handle, &egl->rb[i].fb_id);

		egl->rb[i].image = eglCreateImageKHR(egl->display, NULL, EGL_NATIVE_PIXMAP_KHR, egl->rb[i].bo, NULL);
		if (egl->rb[i].image == EGL_NO_IMAGE_KHR) {
			fatal("Failed to create EGL image\n");
			break;
		}

		glBindRenderbuffer(GL_RENDERBUFFER_EXT, egl->rb[i].color);
		glEGLImageTargetRenderbufferStorageOES(GL_RENDERBUFFER, egl->rb[i].image);

		glBindRenderbuffer(GL_RENDERBUFFER_EXT, egl->rb[i].depth);
		glRenderbufferStorage(GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT,
				egl->drm->info->hdisplay, egl->drm->info->vdisplay);

		glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,
				GL_COLOR_ATTACHMENT0_EXT, GL_RENDERBUFFER_EXT,
				egl->rb[i].color);
		glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,
				GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT,
				egl->rb[i].depth);

		if (glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)
				!= GL_FRAMEBUFFER_COMPLETE) {
			fatal("Framebuffer status incomplete\n");
			break;
		}
	}
	if (i != 2)
		goto err_rb_create;


	drmModeSetCrtc(egl->dri_fd, egl->drm->encoder->crtc_id,
			egl->rb[0].fb_id, 0, 0,
			&egl->drm->connector->connector_id, 1,
			egl->drm->info);
	egl->frame = 0;

	return egl;

err_rb_create:
	glBindRenderbuffer(GL_RENDERBUFFER_EXT, 0);
	for (; i >= 0; --i) {
		glDeleteRenderbuffers(1, &egl->rb[i].color);
		glDeleteRenderbuffers(1, &egl->rb[i].depth);
		if (egl->rb[i].image != NULL)
			eglDestroyImageKHR(egl->display, egl->rb[i].image);
		if (egl->rb[i].bo != NULL) {
			drmModeRmFB(egl->dri_fd, egl->rb[i].fb_id);
			gbm_bo_destroy(egl->rb[i].bo);
		}
	}
	glBindFramebuffer(GL_FRAMEBUFFER_EXT, 0);
	glDeleteFramebuffers(1, &egl->framebuffer);
	eglMakeCurrent(egl->display, EGL_NO_SURFACE,
			EGL_NO_SURFACE, EGL_NO_CONTEXT);
err_egl_current:
	eglDestroySurface(egl->display, egl->egl_surface);
err_egl_surface:
	gbm_surface_destroy(egl->surface);
err_gbm_surface:
	eglDestroyContext(egl->display, egl->context);
err_egl_createctx:
err_egl_chooseconfig:
err_egl_bind:
	drm_destroy(egl->drm);
err_drm_create:
	eglTerminate(egl->display);
err_egl_init:
err_egl_getdisplay:
	gbm_device_destroy(egl->gbm);
err_gbm_create:
	close(egl->dri_fd);
err_dri_open:
	free(egl);
	return NULL;
}

static void egl_destroy(struct egl_context *egl)
{
	int i;
	glBindRenderbuffer(GL_RENDERBUFFER_EXT, 0);
	for (i = 0; i < 2; ++i) {
		glDeleteRenderbuffers(1, &egl->rb[i].color);
		glDeleteRenderbuffers(1, &egl->rb[i].color);
		eglDestroyImageKHR(egl->display, egl->rb[i].image);
		drmModeRmFB(egl->dri_fd, egl->rb[i].fb_id);
		gbm_bo_destroy(egl->rb[i].bo);
	}
	glBindFramebuffer(GL_FRAMEBUFFER_EXT, 0);
	glDeleteFramebuffers(1, &egl->framebuffer);
	eglMakeCurrent(egl->display, EGL_NO_SURFACE,
			EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroySurface(egl->display, egl->egl_surface);
	gbm_surface_destroy(egl->surface);
	eglDestroyContext(egl->display, egl->context);
	drm_destroy(egl->drm);
	eglTerminate(egl->display);
	gbm_device_destroy(egl->gbm);
	close(egl->dri_fd);
	free(egl);
}

static void egl_page_flip(int fd, unsigned int seq,
		unsigned int sec, unsigned int usec, void *data)
{
	struct egl_context *egl;
	egl = (struct egl_context *)data;
	egl->vsync = 0;
	egl->frame++;
	glFramebufferRenderbuffer(GL_FRAMEBUFFER_EXT,
			GL_COLOR_ATTACHMENT0_EXT, GL_RENDERBUFFER_EXT,
			egl->rb[egl->frame % 2].color);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER_EXT,
			GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT,
			egl->rb[egl->frame % 2].depth);
}

static int egl_handle_event(struct egl_context *egl, int ms)
{

	drmEventContext evctx = {
		.version = DRM_EVENT_CONTEXT_VERSION,
		.page_flip_handler = egl_page_flip,
	};
	struct pollfd fd;
	int rc;

	fd.fd = egl->dri_fd;
	fd.events = POLLIN;
	fd.revents = 0;

	rc = poll(&fd, 1, 0);
	if (rc < 0) {
		return -1;
	} else if (rc > 0) {
		drmHandleEvent(egl->dri_fd, &evctx);
		return 0;
	}
	return -1;
}

static int egl_suspend(struct egl_context *egl)
{
	if (egl->drm->can_master == 0)
		return -1;
	if (egl->suspended == 1)
		return 0;
	drmDropMaster(egl->dri_fd);
	egl->suspended = 1;
	return 0;
}

static int egl_resume(struct egl_context *egl)
{
	int rc;
	if (egl->suspended == 0)
		return 0;

	rc = drmSetMaster(egl->dri_fd);
	egl->suspended = (rc != 0 && errno == EINVAL);
	return -(egl->suspended != 0);
}

static int egl_suspended(struct egl_context *egl)
{
	return egl->suspended;
}

static void egl_swap(struct egl_context *egl)
{
	if (egl->suspended != 0)
		return;

	while (egl->vsync)
		egl_handle_event(egl, 16);

	glFlush();
	egl->vsync = 1;
	drmModePageFlip(egl->dri_fd, egl->drm->encoder->crtc_id,
			egl->rb[1 - (egl->frame % 2)].fb_id,
			DRM_MODE_PAGE_FLIP_EVENT, egl);
	egl_handle_event(egl, 0);
}

static void egl_size(struct egl_context *egl, int *w, int *h)
{
	if (w != NULL) *w = egl->drm->info->hdisplay;
	if (h != NULL) *h = egl->drm->info->vdisplay;
}

static const unsigned char kbd_us_ascii[] = {
	// Lower-case
	0x0, 0x0, '1', '2', '3', '4', '5', '6',
	'7', '8', '9', '0', '-', '=', '\b','\t',
	'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
	'o', 'p', '[', ']', '\n',0x0, 'a', 's',
	'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
	'\'','~', 0x0, '\\','z', 'x', 'c', 'v',
	'b', 'n', 'm', ',', '.', '/', 0x0, 0x0,
	0x0, ' ', 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,

	// Shifted
	0x0, 0x0, '!', '@', '#', '$', '%', '^',
	'&', '*', '(', ')', '_', '+', '\b','\t',
	'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',
	'O', 'P', '{', '}', '\n',0x0, 'A', 'S',
	'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',
	'"', '`', 0x0, '|', 'Z', 'X', 'C', 'V',
	'B', 'N', 'M', '<', '>', '?', 0x0, 0x0,
	0x0, ' ', 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
};

static const unsigned char kbd_us_keysym[] = {
	// Lower-case
	0x0, ZDL_KEYSYM_ESCAPE, ZDL_KEYSYM_1, ZDL_KEYSYM_2, ZDL_KEYSYM_3,
	ZDL_KEYSYM_4, ZDL_KEYSYM_5, ZDL_KEYSYM_6, ZDL_KEYSYM_7, ZDL_KEYSYM_8,
	ZDL_KEYSYM_9, ZDL_KEYSYM_0, ZDL_KEYSYM_MINUS, ZDL_KEYSYM_EQUALS,
	ZDL_KEYSYM_BACKSPACE, ZDL_KEYSYM_TAB, ZDL_KEYSYM_Q, ZDL_KEYSYM_W,
	ZDL_KEYSYM_E, ZDL_KEYSYM_R, ZDL_KEYSYM_T, ZDL_KEYSYM_Y, ZDL_KEYSYM_U,
	ZDL_KEYSYM_I, ZDL_KEYSYM_O, ZDL_KEYSYM_P, ZDL_KEYSYM_LEFTBRACKET,
	ZDL_KEYSYM_RIGHTBRACKET, ZDL_KEYSYM_RETURN, 0x0, ZDL_KEYSYM_A,
	ZDL_KEYSYM_S, ZDL_KEYSYM_D, ZDL_KEYSYM_F, ZDL_KEYSYM_G, ZDL_KEYSYM_H,
	ZDL_KEYSYM_J, ZDL_KEYSYM_K, ZDL_KEYSYM_L, ZDL_KEYSYM_SEMICOLON,
	ZDL_KEYSYM_QUOTE, ZDL_KEYSYM_BACKQUOTE, ZDL_KEYSYM_QUESTION,
	ZDL_KEYSYM_BACKSLASH,ZDL_KEYSYM_Z, ZDL_KEYSYM_X, ZDL_KEYSYM_C,
	ZDL_KEYSYM_V, ZDL_KEYSYM_B, ZDL_KEYSYM_N, ZDL_KEYSYM_M,
	ZDL_KEYSYM_COMMA, ZDL_KEYSYM_PERIOD, ZDL_KEYSYM_SLASH, 0x0, 0x0, 0x0,
	ZDL_KEYSYM_SPACE, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,

	// Shifted
	0x0, ZDL_KEYSYM_ESCAPE, ZDL_KEYSYM_EXCLAIM, ZDL_KEYSYM_AT,
	ZDL_KEYSYM_HASH, ZDL_KEYSYM_DOLLAR, ZDL_KEYSYM_PERCENT,
	ZDL_KEYSYM_CARET, ZDL_KEYSYM_AMPERSAND, ZDL_KEYSYM_ASTERISK,
	ZDL_KEYSYM_LEFTPAREN, ZDL_KEYSYM_RIGHTPAREN, ZDL_KEYSYM_UNDERSCORE,
	ZDL_KEYSYM_PLUS, ZDL_KEYSYM_BACKSPACE, ZDL_KEYSYM_TAB, ZDL_KEYSYM_Q,
	ZDL_KEYSYM_W, ZDL_KEYSYM_E, ZDL_KEYSYM_R, ZDL_KEYSYM_T, ZDL_KEYSYM_Y,
	ZDL_KEYSYM_U, ZDL_KEYSYM_I, ZDL_KEYSYM_O, ZDL_KEYSYM_P,
	ZDL_KEYSYM_LEFTBRACKET, ZDL_KEYSYM_RIGHTBRACKET, ZDL_KEYSYM_RETURN,
	0x0, ZDL_KEYSYM_A, ZDL_KEYSYM_S, ZDL_KEYSYM_D, ZDL_KEYSYM_F,
	ZDL_KEYSYM_G, ZDL_KEYSYM_H, ZDL_KEYSYM_J, ZDL_KEYSYM_K, ZDL_KEYSYM_L,
	ZDL_KEYSYM_COLON, ZDL_KEYSYM_QUOTEDBL, ZDL_KEYSYM_BACKQUOTE, 0x0,
	ZDL_KEYSYM_BACKSLASH, ZDL_KEYSYM_Z, ZDL_KEYSYM_X, ZDL_KEYSYM_C,
	ZDL_KEYSYM_V, ZDL_KEYSYM_B, ZDL_KEYSYM_N, ZDL_KEYSYM_M,
	ZDL_KEYSYM_LESS, ZDL_KEYSYM_GREATER, ZDL_KEYSYM_QUESTION, 0x0, 0x0,
	0x0, ZDL_KEYSYM_SPACE, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
};

struct tty {
	struct {
		struct termios termio;
		int            mode;
	} previous;

	zdl_keymod_t keymod;
	int fd;
	int error;
};

static struct tty *tty_create(void)
{
	struct termios termio;
	struct tty *tty;

	tty = (struct tty *)calloc(1, sizeof(*tty));
	if (tty == NULL)
		return NULL;

	tty->fd = open("/dev/tty", O_RDWR);
	if (tty->fd < 0) {
		fatal("Failed to open tty\n");
		free(tty);
		return NULL;
	}

	if (ioctl(tty->fd, KDGKBMODE, &tty->previous.mode) < 0) {
		fatal("Failed KDGKMODE call\n");
		close(tty->fd);
		free(tty);
		return NULL;
	}

	if (tcgetattr(tty->fd, &tty->previous.termio) < 0) {
		close(tty->fd);
		free(tty);
		return NULL;
	}

	termio = tty->previous.termio;
	termio.c_lflag &= ~(ICANON | ECHO | ISIG);
	termio.c_iflag &= ~(ISTRIP | IGNCR | ICRNL | INLCR | IXOFF | IXON | BRKINT);
	termio.c_cc[VMIN] = 0;
	termio.c_cc[VTIME] = 0;

	if (tcsetattr(tty->fd, TCSAFLUSH, &termio) < 0) {
		close(tty->fd);
		free(tty);
		return NULL;
	}

	if (ioctl(tty->fd, KDSKBMODE, K_MEDIUMRAW) < 0) {
		tcsetattr(tty->fd, TCSANOW, &tty->previous.termio);
		close(tty->fd);
		free(tty);
		return NULL;
	}

	return tty;
}

static int tty_current_vt(struct tty *tty)
{
	struct vt_stat st;
	ioctl(tty->fd, VT_GETSTATE, &st);
	return st.v_active;
}

static int tty_switch_vt(struct tty *tty, int vt)
{
	if (ioctl(tty->fd, VT_ACTIVATE, vt))
		return -1;
	return ioctl(tty->fd, VT_WAITACTIVE, vt);
}

static void tty_destroy(struct tty *tty)
{
	int vt;
	ioctl(tty->fd, KDSKBMODE, tty->previous.mode);
	tcsetattr(tty->fd, TCSANOW, &tty->previous.termio);

	vt = tty_current_vt(tty);
	tty_switch_vt(tty, vt + 1);
	tty_switch_vt(tty, vt);
	//write(tty->fd, "\033c", 2);
	close(tty->fd);
	free(tty);
}

static int tty_read_nb(struct tty *tty, unsigned char *ch)
{
	struct pollfd fd;
	int rc;

	fd.fd = tty->fd;
	fd.events = POLLIN | POLLRDHUP;
	fd.revents = 0;

	rc = poll(&fd, 1, 0);
	if (rc < 0) {
		tty->error = 1;
		return -1;
	} else if (rc == 0) {
		return -1;
	}

	if (fd.revents & POLLRDHUP) {
		tty->error = 1;
		return -1;
	}

	return read(tty->fd, ch, 1);
}

static int tty_kbd_translate(struct tty *tty,
		struct zdl_event *ev, unsigned char scancode)
{
	int shiftp = !!(tty->keymod & ZDL_KEYMOD_SHIFT);

	if (scancode < 64) {
		ev->key.unicode = kbd_us_ascii[scancode + (shiftp * 64)];
		ev->key.sym = kbd_us_keysym[scancode + (shiftp * 64)];
		return 0;
	}
	return -1;
}

/* TODO: optional libxkbcommon support */
/* TODO: optional evdev support if root */
static int tty_read_event(struct tty *tty, struct zdl_event *ev, int block)
{
	enum zdl_keymod_enum mod_e;
	unsigned char ch;
	int press;
	int rc;

	for (;;) {
		if (!block)
			rc = tty_read_nb(tty, &ch);
		else
			rc = read(tty->fd, &ch, 1);
		if (rc <= 0)
			return -1;

		press = !(ch & 0x80);

		if (press)
			ev->type = ZDL_EVENT_KEYPRESS;
		else
			ev->type = ZDL_EVENT_KEYRELEASE;
		ev->key.unicode = 0;
		ev->key.scancode = ch;

		ch &= ~0x80;
		switch (ch) {
		case 0xe0: /* special lead-characters */
		case 0xe1:
		case 0xe2:
			continue;
		case 0x1d: ev->key.sym = ZDL_KEYSYM_LCTRL; break;
		case 0x2a: ev->key.sym = ZDL_KEYSYM_LSHIFT; break;
		case 0x36: ev->key.sym = ZDL_KEYSYM_RSHIFT; break;
		case 0x38: ev->key.sym = ZDL_KEYSYM_LALT; break;
		case 0x3a: ev->key.sym = ZDL_KEYSYM_CAPSLOCK; break;
		case 0x3b: ev->key.sym = ZDL_KEYSYM_F1; break;
		case 0x3c: ev->key.sym = ZDL_KEYSYM_F2; break;
		case 0x3d: ev->key.sym = ZDL_KEYSYM_F3; break;
		case 0x3e: ev->key.sym = ZDL_KEYSYM_F4; break;
		case 0x3f: ev->key.sym = ZDL_KEYSYM_F5; break;
		case 0x40: ev->key.sym = ZDL_KEYSYM_F6; break;
		case 0x41: ev->key.sym = ZDL_KEYSYM_F7; break;
		case 0x42: ev->key.sym = ZDL_KEYSYM_F8; break;
		case 0x43: ev->key.sym = ZDL_KEYSYM_F9; break;
		case 0x44: ev->key.sym = ZDL_KEYSYM_F10; break;
		case 0x57: ev->key.sym = ZDL_KEYSYM_F11; break;
		case 0x58: ev->key.sym = ZDL_KEYSYM_F12; break;
		case 0x61: ev->key.sym = ZDL_KEYSYM_RCTRL; break;
		case 0x63: ev->key.sym = ZDL_KEYSYM_PRINT; break;
		case 0x64: ev->key.sym = ZDL_KEYSYM_RALT; break;
		case 0x66: ev->key.sym = ZDL_KEYSYM_HOME; break;
		case 0x67: ev->key.sym = ZDL_KEYSYM_UP; break;
		case 0x68: ev->key.sym = ZDL_KEYSYM_PAGEUP; break;
		case 0x69: ev->key.sym = ZDL_KEYSYM_LEFT; break;
		case 0x6a: ev->key.sym = ZDL_KEYSYM_RIGHT; break;
		case 0x6b: ev->key.sym = ZDL_KEYSYM_END; break;
		case 0x6c: ev->key.sym = ZDL_KEYSYM_DOWN; break;
		case 0x6d: ev->key.sym = ZDL_KEYSYM_PAGEDOWN; break;
		case 0x6e: ev->key.sym = ZDL_KEYSYM_INSERT; break;
		case 0x6f: ev->key.sym = ZDL_KEYSYM_DELETE; break;
		case 0x7d: ev->key.sym = ZDL_KEYSYM_LSUPER; break;
		default:
			ev->key.sym = (enum zdl_keysym)-1;
			break;
		}

		switch (ev->key.sym) {
		case ZDL_KEYSYM_LSUPER: mod_e = ZDL_KEYMOD_LSUPER; break;
		case ZDL_KEYSYM_RSUPER: mod_e = ZDL_KEYMOD_RSUPER; break;
		case ZDL_KEYSYM_LCTRL: mod_e = ZDL_KEYMOD_LCTRL; break;
		case ZDL_KEYSYM_RCTRL: mod_e = ZDL_KEYMOD_RCTRL; break;
		case ZDL_KEYSYM_LALT: mod_e = ZDL_KEYMOD_LALT; break;
		case ZDL_KEYSYM_RALT: mod_e = ZDL_KEYMOD_RALT; break;
		case ZDL_KEYSYM_LSHIFT: mod_e = ZDL_KEYMOD_LSHIFT; break;
		case ZDL_KEYSYM_RSHIFT: mod_e = ZDL_KEYMOD_RSHIFT; break;
		default: mod_e = ZDL_KEYMOD_NONE; break;
		}

		if (mod_e != ZDL_KEYMOD_NONE) {
			zdl_keymod_t lkmod = tty->keymod;
			if (press)
				tty->keymod |= mod_e;
			else
				tty->keymod &= ~mod_e;
			if (tty->keymod == lkmod)
				continue;
		}
		ev->key.modifiers = tty->keymod;

		if (ev->key.sym == (enum zdl_keysym)-1) {
			if (tty_kbd_translate(tty, ev, ch))
				continue;
		}
		break;
	}

	return 0;
}

struct zdl_window {
	int current_vt;

	int width;
	int height;

	zdl_flags_t flags;
	struct egl_context *context;
	struct tty *tty;
	struct zdl_clipboard_data clipboard;
};

zdl_window_t zdl_window_create(int width, int height, zdl_flags_t flags)
{
	zdl_window_t w;

	w = (zdl_window_t)calloc(1, sizeof(*w));
	if (w == NULL)
		return NULL;

	w->width = width;
	w->height = height;
	w->flags = flags | ZDL_FLAG_FLIP_Y;

	w->tty = tty_create();
	if (w->tty == NULL) {
		free(w);
		return NULL;
	}

	w->context = egl_create();
	if (w->context == NULL) {
		tty_destroy(w->tty);
		free(w);
		return NULL;
	}

	egl_size(w->context, &w->width, &w->height);

	w->current_vt = tty_current_vt(w->tty);

	return w;
}

void zdl_window_destroy(zdl_window_t w)
{
	if (w->clipboard.text.text != NULL)
		free((void *)w->clipboard.text.text);
	egl_destroy(w->context);
	tty_destroy(w->tty);
	free(w);
}

void zdl_window_set_title(zdl_window_t w, const char *icon, const char *name)
{
}

void zdl_window_set_flags(zdl_window_t w, zdl_flags_t flags)
{
	w->flags = flags | ZDL_FLAG_FLIP_Y;
}

zdl_flags_t zdl_window_get_flags(const zdl_window_t w)
{
	return w->flags;
}

void zdl_window_set_size(zdl_window_t w, int width, int height)
{
}

void zdl_window_get_size(const zdl_window_t w, int *width, int *height)
{
	if (width != NULL) *width = w->width;
	if (height != NULL) *height = w->height;
}

void zdl_window_set_position(zdl_window_t w, int x, int y)
{
}

void zdl_window_get_position(const zdl_window_t w, int *x, int *y)
{
	if (x != NULL) *x = 0;
	if (y != NULL) *y = 0;
}

static int zdl_handle_event(zdl_window_t w, struct zdl_event *ev)
{
	if (ev->type != ZDL_EVENT_KEYPRESS || !(ev->key.modifiers & ZDL_KEYMOD_ALT))
		return -1;

	if (ev->key.sym >= ZDL_KEYSYM_F1 && ev->key.sym <= ZDL_KEYSYM_F15) {
		int vt;
		vt = (ev->key.sym - ZDL_KEYSYM_F1) + 1;
		if (vt == tty_current_vt(w->tty))
			return -1;
		if (egl_suspend(w->context) == 0)
			tty_switch_vt(w->tty, vt);
		return 0;
	}

	return -1;
}

static int zdl_read_event(zdl_window_t w, struct zdl_event *ev, int block)
{
	int rc;

	if (egl_suspended(w->context)) {
		if (tty_current_vt(w->tty) == w->current_vt) {
			egl_resume(w->context);
			ev->type = ZDL_EVENT_EXPOSE;
			return 0;
		}
	}

	do {
		rc = tty_read_event(w->tty, ev, block);
		if (rc && !block)
			break;
	} while (zdl_handle_event(w, ev) == 0);

	return rc;
}

int  zdl_window_poll_event(zdl_window_t w, struct zdl_event *ev)
{
	return zdl_read_event(w, ev, 0);
}

void zdl_window_wait_event(zdl_window_t w, struct zdl_event *ev)
{
	zdl_read_event(w, ev, 1);
}

void zdl_window_swap(zdl_window_t w)
{
	egl_swap(w->context);
}

struct zdl_clipboard {
	zdl_window_t window;
	void *data;
};

zdl_clipboard_t zdl_clipboard_open(zdl_window_t w)
{
	zdl_clipboard_t c;

	c = (zdl_clipboard_t)calloc(1, sizeof(*c));
	if (c == NULL)
		return ZDL_CLIPBOARD_INVALID;
	c->window = w;
	c->data = (void *)w->clipboard.text.text;

	return c;
}

void zdl_clipboard_close(zdl_clipboard_t c)
{
	free(c);
}

static int zdl_clipboard_copy(const struct zdl_clipboard_data *from,
		struct zdl_clipboard_data *to, void **memory)
{
	memset(to, 0, sizeof(*to));

	to->format = from->format;
	switch (from->format) {
	case ZDL_CLIPBOARD_TEXT:
		if (from->text.text == NULL)
			return -1;
		to->text.text = strdup(from->text.text);
		*memory = (void *)to->text.text;
		break;
	case ZDL_CLIPBOARD_URI:
		if (from->uri.uri == NULL)
			return -1;
		to->uri.uri = strdup(from->uri.uri);
		*memory = (void *)to->uri.uri;
		break;
	case ZDL_CLIPBOARD_IMAGE:
		if (from->image.pixels == NULL)
			return -1;
		to->image.pixels = (unsigned int *)calloc(4, from->image.width * from->image.height);
		if (to->image.pixels == NULL)
			return -1;
		to->image.width = from->image.width;
		to->image.height = from->image.height;
		memcpy((void *)to->image.pixels, from->image.pixels,
				4 * from->image.width * from->image.height);
		*memory = (void *)to->image.pixels;
		break;
	}

	return 0;
}

int zdl_clipboard_write(zdl_clipboard_t c, const struct zdl_clipboard_data *data)
{
	if (c->data != NULL) {
		free(c->data);
		c->data = NULL;
	}

	return zdl_clipboard_copy(data, &c->window->clipboard, &c->data);
}

int zdl_clipboard_read(zdl_clipboard_t c, struct zdl_clipboard_data *data)
{
	if (c->data == NULL)
		return -1;
	*data = c->window->clipboard;
	return 0;
}
