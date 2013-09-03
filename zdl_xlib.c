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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/time.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <GL/glx.h>
#include <GL/gl.h>

#include "zdl.h"

struct zdl_window {
	Display *display;
	int mapped;
	int eatpaste;
	int screen;
	int x, y;
	int width, height;
	struct {
		int x, y;
	} lastconfig;
	struct {
		int x, y;
		int width, height;
	} masked;
	zdl_flags_t flags;

	Window root;
	Window window;
	Colormap colormap;
	GLXContext context;

	struct { int x, y; } lastmotion;
	unsigned int modifiers;
	Atom wm_delete_window;
	struct zdl_clipboard_data clipboard;
};

#define MWM_HINTS_DECORATIONS   (1L << 1)
#define MWM_DECOR_ALL           (1L << 0)
#define MWM_DECOR_RESIZEH       (1L << 2)

static Bool wait_for_map_notify(Display *d, XEvent *e, char *arg)
{
	if ((e->type == MapNotify) && (e->xmap.window == (Window)arg))
		return True;
	return False;
}

static Bool wait_for_selection_notify(Display *d, XEvent *e, char *arg)
{
	if (e->type == SelectionNotify)
		return True;
	return False;
}

static void zdl_window_set_wm_state(zdl_window_t w, zdl_flags_t flags)
{
	Atom property;
	Atom value;

	property = XInternAtom(w->display, "_NET_WM_STATE", False);
	value = XInternAtom(w->display, "_NET_WM_STATE_FULLSCREEN", False);
	if (w->mapped) {
		XEvent xev;

		xev.xclient.type = ClientMessage;
		xev.xclient.serial = 0;
		xev.xclient.send_event = True;
		xev.xclient.message_type = property;
		xev.xclient.window = w->window;
		xev.xclient.format = 32;
		xev.xclient.data.l[0] = !!(flags & ZDL_FLAG_FULLSCREEN);
		xev.xclient.data.l[1] = value;
		xev.xclient.data.l[2] = 0;

		XSendEvent(w->display, w->root, False,
				SubstructureRedirectMask | SubstructureNotifyMask,
				&xev);
	} else if (flags & ZDL_FLAG_FULLSCREEN) {
		XChangeProperty(w->display, w->window, property,
			XA_ATOM,
			32, PropModeReplace,
			(unsigned char *)&value, 1);
	}
}

static void zdl_window_set_hints(zdl_window_t w, int width, int height, zdl_flags_t flags)
{
	XSizeHints *hints = XAllocSizeHints();
	struct {
		unsigned long flags;
		unsigned long functions;
		unsigned long decorations;
		long          inputMode;
		unsigned long status;
	} mwm_hints;
	Atom property;

	hints->flags = PMinSize | PMaxSize;

	if (flags & ZDL_FLAG_NORESIZE) {
		hints->min_width = width;
		hints->min_height = height;
		hints->max_width = width;
		hints->max_height = height;
		mwm_hints.decorations = ~MWM_DECOR_RESIZEH;
	} else {
		hints->min_width = 0;
		hints->min_height = 0;
		hints->max_width = XDisplayWidth(w->display, w->screen);
		hints->max_height = XDisplayHeight(w->display, w->screen);
		mwm_hints.decorations = MWM_DECOR_ALL;
	}

	hints->base_width = width;
	hints->base_height = height;

	if (flags & (ZDL_FLAG_FULLSCREEN | ZDL_FLAG_NODECOR)) {
		mwm_hints.flags = (1 << 1);
		mwm_hints.decorations = 0;
	} else {
		mwm_hints.flags = (1 << 1);
	}

	property = XInternAtom(w->display, "_MOTIF_WM_HINTS", False);

	if (flags & ZDL_FLAG_FULLSCREEN) {
		XChangeProperty(w->display, w->window, property, property,
				32, PropModeReplace, (unsigned char *)&mwm_hints,
				sizeof(mwm_hints) / sizeof(long));
		zdl_window_set_wm_state(w, flags);
		XSetWMNormalHints(w->display, w->window, hints);
	} else {
		XSetWMNormalHints(w->display, w->window, hints);
		zdl_window_set_wm_state(w, flags);
		XChangeProperty(w->display, w->window, property, property,
				32, PropModeReplace, (unsigned char *)&mwm_hints,
				sizeof(mwm_hints) / sizeof(long));
	}

	XFree(hints);
}

static int zdl_window_set_swap_interval(zdl_window_t w, int on)
{
	const char *ext = glXQueryExtensionsString(w->display, 0);
	void (*swap)(int) = NULL;

	if (strstr(ext, "GLX_SGI_swap_control") != NULL) {
		swap = (void (*)(int))glXGetProcAddress((const GLubyte*)"glXSwapIntervalSGI");
	} else if (strstr(ext, "GLX_MESA_swap_control") != NULL) {
		swap = (void (*)(int))glXGetProcAddress((const GLubyte*)"glXSwapIntervalMESA");
	}

	if (swap) {
		swap(on ? 1 : 0);
		return 0;
	}
	return -1;
}

static int zdl_window_reconfigure(zdl_window_t w, int width, int height, zdl_flags_t flags)
{
	unsigned int valuelist[6];
	unsigned int valuemask;
	XSetWindowAttributes swa;
	XVisualInfo *vi;
	XEvent event;

	valuelist[0] = GLX_RGBA;
	valuelist[1] = GLX_DOUBLEBUFFER;
	valuelist[2] = GLX_USE_GL;
	valuelist[3] = GLX_DEPTH_SIZE;
	valuelist[4] = 8;
	valuelist[5] = None;

	vi = glXChooseVisual(w->display, w->screen, (int *)valuelist);
	if (vi == NULL) {
		fprintf(stderr, "Unable to choose appropriate X visual\n");
		return -1;
	}

	w->context = glXCreateContext(w->display, vi, None, GL_TRUE);
	if (w->context == NULL) {
		fprintf(stderr, "Unable to create GLX context\n");
		return -1;
	}

	w->root = XRootWindow(w->display, vi->screen);
	w->colormap = XCreateColormap(w->display, w->root, vi->visual, AllocNone);

	swa.colormap = w->colormap;
	swa.border_pixel = 0;
	swa.background_pixel = 0;
	swa.override_redirect = 0;
	//swa.override_redirect = !!(flags & ZDL_FLAG_FULLSCREEN);
	swa.event_mask =	KeyPressMask        | KeyReleaseMask    |
				ButtonPressMask     | ButtonReleaseMask |
				EnterWindowMask     | LeaveWindowMask   |
				PointerMotionMask   | ExposureMask      |
				StructureNotifyMask;
	valuemask =	CWBackPixel |
			CWBorderPixel |
			CWOverrideRedirect |
			CWEventMask |
			CWColormap;

	w->window = XCreateWindow(w->display, w->root,
			0, 0, width, height, 0, vi->depth,
			InputOutput, vi->visual, valuemask, &swa);

	zdl_window_set_hints(w, width, height, flags);
	if (flags & ZDL_FLAG_FULLSCREEN)
		XMoveWindow(w->display, w->window, 0, 0);

	XFree(vi);

	if (!glXMakeCurrent(w->display, w->window, w->context)) {
		fprintf(stderr, "Unable to make context current\n");
		XFreeColormap(w->display, w->colormap);
		XDestroyWindow(w->display, w->window);
		glXDestroyContext(w->display, w->context);
		return -1;
	}
	zdl_window_set_swap_interval(w, 0);

	XMapWindow(w->display, w->window);
	XSetWMProtocols(w->display, w->window, &w->wm_delete_window, 1);
	XIfEvent(w->display, &event, wait_for_map_notify, (char *)w->window);

	w->mapped = 1;

	return 0;
}

zdl_window_t zdl_window_create(int width, int height, zdl_flags_t flags)
{
	zdl_window_t w;

	w = (zdl_window_t)calloc(1, sizeof(*w));
	if (w == NULL)
		return ZDL_WINDOW_INVALID;

	w->display = XOpenDisplay(0);
	if (w->display == NULL) {
		fprintf(stderr, "Unable to open X display\n");
		free(w);
		return ZDL_WINDOW_INVALID;
	}

	w->screen = XDefaultScreen(w->display);

	w->x = w->y = 0;
	w->width = width;
	w->height = height;
	w->flags = flags & ~ZDL_FLAG_NOCURSOR;

	w->wm_delete_window = XInternAtom(w->display, "WM_DELETE_WINDOW", False);

	if (flags & ZDL_FLAG_FULLSCREEN) {
		width = XDisplayWidth(w->display, w->screen);
		height = XDisplayHeight(w->display, w->screen);

		w->masked.width = w->width;
		w->masked.height = w->height;
		w->width = width;
		w->height = height;
	}

	if (zdl_window_reconfigure(w, width, height, flags)) {
		XCloseDisplay(w->display);
		free(w);
		return ZDL_WINDOW_INVALID;
	}

	zdl_window_set_flags(w, flags);

	return w;
}

void zdl_window_destroy(zdl_window_t w)
{
	XFreeColormap(w->display, w->colormap);
	glXMakeCurrent(w->display, None, NULL);
	XDestroyWindow(w->display, w->window);
	glXDestroyContext(w->display, w->context);
	XCloseDisplay(w->display);
	if (w->clipboard.text.text != NULL)
		free((void *)w->clipboard.text.text);
	free(w);
}

void zdl_window_set_flags(zdl_window_t w, zdl_flags_t flags)
{
	zdl_flags_t chg = flags ^ w->flags;

	if (chg & ZDL_FLAG_FULLSCREEN) {
		int x, y, width, height;
		chg &= ~(ZDL_FLAG_NORESIZE | ZDL_FLAG_NODECOR);

		if (flags & ZDL_FLAG_FULLSCREEN) {
			w->masked.x = w->x;
			w->masked.y = w->y;
			w->masked.width = w->width;
			w->masked.height = w->height;
			width = XDisplayWidth(w->display, w->screen);
			height = XDisplayHeight(w->display, w->screen);
			x = 0;
			y = 0;
		} else {
			x = w->masked.x;
			y = w->masked.y;
			width = w->masked.width;
			height = w->masked.height;
		}

		zdl_window_set_hints(w, width, height, flags);
		XMoveResizeWindow(w->display, w->window, x, y, width, height);
		w->width = width;
		w->height = height;
	}

	if (chg & (ZDL_FLAG_NORESIZE | ZDL_FLAG_NODECOR)) {
		zdl_window_set_hints(w, w->width, w->height, flags);
		XMoveWindow(w->display, w->window, w->x, w->y);
	}

	if (chg & ZDL_FLAG_NOCURSOR) {
		Cursor cursor;

		if (flags & ZDL_FLAG_NOCURSOR) {
			static char bm_no_data[] = {0, 0, 0, 0, 0, 0, 0, 0};
			XColor black;
			Pixmap bm_no;
			black.red = black.green = black.blue = 0;

			bm_no = XCreateBitmapFromData(w->display, w->window, bm_no_data, 8, 8);
			cursor = XCreatePixmapCursor(w->display, bm_no, bm_no, &black, &black, 0, 0);

			XDefineCursor(w->display, w->window, cursor);
			XFreeCursor(w->display, cursor);
			if (bm_no != None)
				XFreePixmap(w->display, bm_no);
		} else {
			cursor = XCreateFontCursor(w->display, XC_left_ptr);
			XDefineCursor(w->display, w->window, cursor);
			XFreeCursor(w->display, cursor);
		}
	}

	w->flags = flags | ZDL_FLAG_COPYONHL;
}

zdl_flags_t zdl_window_get_flags(const zdl_window_t w)
{
	return w->flags;
}

void zdl_window_set_size(zdl_window_t w, int width, int height)
{
	if (width == w->width && height == w->height)
		return;

	if (!(w->flags & ZDL_FLAG_FULLSCREEN)) {
		w->width = width;
		w->height = height;
		XResizeWindow(w->display, w->window, w->width, w->height);
	} else {
		w->masked.width = width;
		w->masked.height = height;
	}
}

void zdl_window_get_size(const zdl_window_t w, int *width, int *height)
{
	if (width != NULL)  *width  = w->width;
	if (height != NULL) *height = w->height;
}

void zdl_window_set_position(zdl_window_t w, int x, int y)
{
	if (x == w->x && y == w->y)
		return;

	if (!(w->flags & ZDL_FLAG_FULLSCREEN)) {
		w->x = x;
		w->y = y;
		XMoveWindow(w->display, w->x, w->y, w->height);
	} else {
		w->masked.x = x;
		w->masked.y = y;
	}
}

void zdl_window_get_position(const zdl_window_t w, int *x, int *y)
{
	if (x != NULL) *x = w->x;
	if (y != NULL) *y = w->y;
}

static int zdl_window_translate(zdl_window_t w, int down, XKeyEvent *event, struct zdl_event *ev)
{
	unsigned int nmod = ZDL_KEYMOD_NONE;
	int drop = 0;
	KeySym ks;

	XLookupString(event, NULL, 0, &ks, NULL);

	switch (ks) {
	case XK_Shift_L:     nmod |= ZDL_KEYMOD_LSHIFT; break;
	case XK_Shift_R:     nmod |= ZDL_KEYMOD_RSHIFT; break;
	case XK_Control_L:   nmod |= ZDL_KEYMOD_LCTRL; break;
	case XK_Control_R:   nmod |= ZDL_KEYMOD_RCTRL; break;
	case XK_Alt_L:       nmod |= ZDL_KEYMOD_LALT; break;
	case XK_Alt_R:       nmod |= ZDL_KEYMOD_RALT; break;
	case XK_Super_L:     nmod |= ZDL_KEYMOD_LSUPER; break;
	case XK_Super_R:     nmod |= ZDL_KEYMOD_RSUPER; break;
	case XK_Hyper_L:     nmod |= ZDL_KEYMOD_LHYPER; break;
	case XK_Hyper_R:     nmod |= ZDL_KEYMOD_RHYPER; break;
	case XK_Meta_L:      nmod |= ZDL_KEYMOD_LMETA; break;
	case XK_Meta_R:      nmod |= ZDL_KEYMOD_RMETA; break;
	case XK_Num_Lock:    nmod |= ZDL_KEYMOD_NUM; break;
	case XK_Caps_Lock:   nmod |= ZDL_KEYMOD_CAPS; break;
	case XK_Scroll_Lock: nmod |= ZDL_KEYMOD_SCROLL; break;
	case XK_Mode_switch: nmod |= ZDL_KEYMOD_MODE; break;
	}

	if (down)
		w->modifiers |= nmod;
	else
		w->modifiers &= ~nmod;

	switch (ks) {
	case XK_BackSpace:    ev->key.sym = ZDL_KEYSYM_BACKSPACE; break;
	case XK_Tab:          ev->key.sym = ZDL_KEYSYM_TAB; break;
	case XK_Clear:        ev->key.sym = ZDL_KEYSYM_CLEAR; break;
	case XK_Return:       ev->key.sym = ZDL_KEYSYM_RETURN; break;
	case XK_Pause:        ev->key.sym = ZDL_KEYSYM_PAUSE; break;
	case XK_Escape:       ev->key.sym = ZDL_KEYSYM_ESCAPE; break;
	case XK_space:        ev->key.sym = ZDL_KEYSYM_SPACE; break;
	case XK_exclam:       ev->key.sym = ZDL_KEYSYM_EXCLAIM; break;
	case XK_quotedbl:     ev->key.sym = ZDL_KEYSYM_QUOTEDBL; break;
	case XK_numbersign:   ev->key.sym = ZDL_KEYSYM_HASH; break;
	case XK_dollar:       ev->key.sym = ZDL_KEYSYM_DOLLAR; break;
	case XK_ampersand:    ev->key.sym = ZDL_KEYSYM_AMPERSAND; break;
	case XK_quoteright:   ev->key.sym = ZDL_KEYSYM_QUOTE; break;
	case XK_parenleft:    ev->key.sym = ZDL_KEYSYM_LEFTPAREN; break;
	case XK_parenright:   ev->key.sym = ZDL_KEYSYM_RIGHTPAREN; break;
	case XK_asterisk:     ev->key.sym = ZDL_KEYSYM_ASTERISK; break;
	case XK_plus:         ev->key.sym = ZDL_KEYSYM_PLUS; break;
	case XK_comma:        ev->key.sym = ZDL_KEYSYM_COMMA; break;
	case XK_minus:        ev->key.sym = ZDL_KEYSYM_MINUS; break;
	case XK_period:       ev->key.sym = ZDL_KEYSYM_PERIOD; break;
	case XK_slash:        ev->key.sym = ZDL_KEYSYM_SLASH; break;
	case XK_0:            ev->key.sym = ZDL_KEYSYM_0; break;
	case XK_1:            ev->key.sym = ZDL_KEYSYM_1; break;
	case XK_2:            ev->key.sym = ZDL_KEYSYM_2; break;
	case XK_3:            ev->key.sym = ZDL_KEYSYM_3; break;
	case XK_4:            ev->key.sym = ZDL_KEYSYM_4; break;
	case XK_5:            ev->key.sym = ZDL_KEYSYM_5; break;
	case XK_6:            ev->key.sym = ZDL_KEYSYM_6; break;
	case XK_7:            ev->key.sym = ZDL_KEYSYM_7; break;
	case XK_8:            ev->key.sym = ZDL_KEYSYM_8; break;
	case XK_9:            ev->key.sym = ZDL_KEYSYM_9; break;
	case XK_colon:        ev->key.sym = ZDL_KEYSYM_COLON; break;
	case XK_semicolon:    ev->key.sym = ZDL_KEYSYM_SEMICOLON; break;
	case XK_less:         ev->key.sym = ZDL_KEYSYM_LESS; break;
	case XK_equal:        ev->key.sym = ZDL_KEYSYM_EQUALS; break;
	case XK_greater:      ev->key.sym = ZDL_KEYSYM_GREATER; break;
	case XK_question:     ev->key.sym = ZDL_KEYSYM_QUESTION; break;
	case XK_at:           ev->key.sym = ZDL_KEYSYM_AT; break;
	case XK_bracketleft:  ev->key.sym = ZDL_KEYSYM_LEFTBRACKET; break;
	case XK_backslash:    ev->key.sym = ZDL_KEYSYM_BACKSLASH; break;
	case XK_bracketright: ev->key.sym = ZDL_KEYSYM_RIGHTBRACKET; break;
	case XK_asciicircum:  ev->key.sym = ZDL_KEYSYM_CARET; break;
	case XK_underscore:   ev->key.sym = ZDL_KEYSYM_UNDERSCORE; break;
	case XK_grave:        ev->key.sym = ZDL_KEYSYM_BACKQUOTE; break;
	case XK_A: case XK_a: ev->key.sym = ZDL_KEYSYM_A; break;
	case XK_B: case XK_b: ev->key.sym = ZDL_KEYSYM_B; break;
	case XK_C: case XK_c: ev->key.sym = ZDL_KEYSYM_C; break;
	case XK_D: case XK_d: ev->key.sym = ZDL_KEYSYM_D; break;
	case XK_E: case XK_e: ev->key.sym = ZDL_KEYSYM_E; break;
	case XK_F: case XK_f: ev->key.sym = ZDL_KEYSYM_F; break;
	case XK_G: case XK_g: ev->key.sym = ZDL_KEYSYM_G; break;
	case XK_H: case XK_h: ev->key.sym = ZDL_KEYSYM_H; break;
	case XK_I: case XK_i: ev->key.sym = ZDL_KEYSYM_I; break;
	case XK_J: case XK_j: ev->key.sym = ZDL_KEYSYM_J; break;
	case XK_K: case XK_k: ev->key.sym = ZDL_KEYSYM_K; break;
	case XK_L: case XK_l: ev->key.sym = ZDL_KEYSYM_L; break;
	case XK_M: case XK_m: ev->key.sym = ZDL_KEYSYM_M; break;
	case XK_N: case XK_n: ev->key.sym = ZDL_KEYSYM_N; break;
	case XK_O: case XK_o: ev->key.sym = ZDL_KEYSYM_O; break;
	case XK_P: case XK_p: ev->key.sym = ZDL_KEYSYM_P; break;
	case XK_Q: case XK_q: ev->key.sym = ZDL_KEYSYM_Q; break;
	case XK_R: case XK_r: ev->key.sym = ZDL_KEYSYM_R; break;
	case XK_S: case XK_s: ev->key.sym = ZDL_KEYSYM_S; break;
	case XK_T: case XK_t: ev->key.sym = ZDL_KEYSYM_T; break;
	case XK_U: case XK_u: ev->key.sym = ZDL_KEYSYM_U; break;
	case XK_V: case XK_v: ev->key.sym = ZDL_KEYSYM_V; break;
	case XK_W: case XK_w: ev->key.sym = ZDL_KEYSYM_W; break;
	case XK_X: case XK_x: ev->key.sym = ZDL_KEYSYM_X; break;
	case XK_Y: case XK_y: ev->key.sym = ZDL_KEYSYM_Y; break;
	case XK_Z: case XK_z: ev->key.sym = ZDL_KEYSYM_Z; break;
	case XK_Delete:       ev->key.sym = ZDL_KEYSYM_DELETE; break;
	case XK_KP_0:         ev->key.sym = ZDL_KEYSYM_KEYPAD_0; break;
	case XK_KP_1:         ev->key.sym = ZDL_KEYSYM_KEYPAD_1; break;
	case XK_KP_2:         ev->key.sym = ZDL_KEYSYM_KEYPAD_2; break;
	case XK_KP_3:         ev->key.sym = ZDL_KEYSYM_KEYPAD_3; break;
	case XK_KP_4:         ev->key.sym = ZDL_KEYSYM_KEYPAD_4; break;
	case XK_KP_5:         ev->key.sym = ZDL_KEYSYM_KEYPAD_5; break;
	case XK_KP_6:         ev->key.sym = ZDL_KEYSYM_KEYPAD_6; break;
	case XK_KP_7:         ev->key.sym = ZDL_KEYSYM_KEYPAD_7; break;
	case XK_KP_8:         ev->key.sym = ZDL_KEYSYM_KEYPAD_8; break;
	case XK_KP_9:         ev->key.sym = ZDL_KEYSYM_KEYPAD_9; break;
	case XK_KP_Decimal:   ev->key.sym = ZDL_KEYSYM_KEYPAD_PERIOD; break;
	case XK_KP_Divide:    ev->key.sym = ZDL_KEYSYM_KEYPAD_DIVIDE; break;
	case XK_KP_Multiply:  ev->key.sym = ZDL_KEYSYM_KEYPAD_MULTIPLY; break;
	case XK_KP_Subtract:  ev->key.sym = ZDL_KEYSYM_KEYPAD_MINUS; break;
	case XK_KP_Add:       ev->key.sym = ZDL_KEYSYM_KEYPAD_PLUS; break;
	case XK_KP_Enter:     ev->key.sym = ZDL_KEYSYM_KEYPAD_ENTER; break;
	case XK_KP_Equal:     ev->key.sym = ZDL_KEYSYM_KEYPAD_EQUALS; break;
	case XK_Up:           ev->key.sym = ZDL_KEYSYM_UP; break;
	case XK_Down:         ev->key.sym = ZDL_KEYSYM_DOWN; break;
	case XK_Right:        ev->key.sym = ZDL_KEYSYM_RIGHT; break;
	case XK_Left:         ev->key.sym = ZDL_KEYSYM_LEFT; break;
	case XK_Insert:       ev->key.sym = ZDL_KEYSYM_INSERT; break;
	case XK_Home:         ev->key.sym = ZDL_KEYSYM_HOME; break;
	case XK_End:          ev->key.sym = ZDL_KEYSYM_END; break;
	case XK_Page_Up:      ev->key.sym = ZDL_KEYSYM_PAGEUP; break;
	case XK_Page_Down:    ev->key.sym = ZDL_KEYSYM_PAGEDOWN; break;
	case XK_F1:           ev->key.sym = ZDL_KEYSYM_F1; break;
	case XK_F2:           ev->key.sym = ZDL_KEYSYM_F2; break;
	case XK_F3:           ev->key.sym = ZDL_KEYSYM_F3; break;
	case XK_F4:           ev->key.sym = ZDL_KEYSYM_F4; break;
	case XK_F5:           ev->key.sym = ZDL_KEYSYM_F5; break;
	case XK_F6:           ev->key.sym = ZDL_KEYSYM_F6; break;
	case XK_F7:           ev->key.sym = ZDL_KEYSYM_F7; break;
	case XK_F8:           ev->key.sym = ZDL_KEYSYM_F8; break;
	case XK_F9:           ev->key.sym = ZDL_KEYSYM_F9; break;
	case XK_F10:          ev->key.sym = ZDL_KEYSYM_F10; break;
	case XK_F11:          ev->key.sym = ZDL_KEYSYM_F11; break;
	case XK_F12:          ev->key.sym = ZDL_KEYSYM_F12; break;
	case XK_F13:          ev->key.sym = ZDL_KEYSYM_F13; break;
	case XK_F14:          ev->key.sym = ZDL_KEYSYM_F14; break;
	case XK_F15:          ev->key.sym = ZDL_KEYSYM_F15; break;
	case XK_Num_Lock:     ev->key.sym = ZDL_KEYSYM_NUMLOCK; break;
	case XK_Caps_Lock:    ev->key.sym = ZDL_KEYSYM_CAPSLOCK; break;
	case XK_Scroll_Lock:  ev->key.sym = ZDL_KEYSYM_SCROLLLOCK; break;
	case XK_Shift_R:      ev->key.sym = ZDL_KEYSYM_RSHIFT; break;
	case XK_Shift_L:      ev->key.sym = ZDL_KEYSYM_LSHIFT; break;
	case XK_Control_R:    ev->key.sym = ZDL_KEYSYM_RCTRL; break;
	case XK_Control_L:    ev->key.sym = ZDL_KEYSYM_LCTRL; break;
	case XK_Alt_R:        ev->key.sym = ZDL_KEYSYM_RALT; break;
	case XK_Alt_L:        ev->key.sym = ZDL_KEYSYM_LALT; break;
	case XK_Meta_R:       ev->key.sym = ZDL_KEYSYM_RMETA; break;
	case XK_Meta_L:       ev->key.sym = ZDL_KEYSYM_LMETA; break;
	case XK_Super_L:      ev->key.sym = ZDL_KEYSYM_LSUPER; break;
	case XK_Super_R:      ev->key.sym = ZDL_KEYSYM_RSUPER; break;
	case XK_Mode_switch:  ev->key.sym = ZDL_KEYSYM_MODE; break;
	case XK_Help:         ev->key.sym = ZDL_KEYSYM_HELP; break;
	case XK_Print:        ev->key.sym = ZDL_KEYSYM_PRINT; break;
	case XK_Sys_Req:      ev->key.sym = ZDL_KEYSYM_SYSREQ; break;
	case XK_Break:        ev->key.sym = ZDL_KEYSYM_BREAK; break;
	case XK_Menu:         ev->key.sym = ZDL_KEYSYM_MENU; break;
	case XK_EuroSign:     ev->key.sym = ZDL_KEYSYM_EURO; break;
	case XK_Undo:         ev->key.sym = ZDL_KEYSYM_UNDO; break;
	default:
		ev->key.sym = (enum zdl_keysym)-1;
		drop = 1;
		break;
	}

	if (drop) {
		XSendEvent(w->display, w->root, False,
				KeyPressMask | KeyReleaseMask,
				(XEvent *)event);
		return -1;
	}

	if (ks >= 0xff00) {
		ev->key.scancode = 0;
		ev->key.unicode = 0;
	} else {
		ev->key.scancode = ks;
		ev->key.unicode = ks;
	}

	ev->key.modifiers = w->modifiers;

	return 0;
}

static int zdl_window_read_event(zdl_window_t w, struct zdl_event *ev)
{
	static const enum zdl_button button_map[] = {
		[Button1] = ZDL_BUTTON_LEFT,
		[Button2] = ZDL_BUTTON_MIDDLE,
		[Button3] = ZDL_BUTTON_RIGHT,
		[Button4] = ZDL_BUTTON_MWUP,
		[Button5] = ZDL_BUTTON_MWDOWN,
	};
	XEvent resp;
	XEvent event;
	int rc;

	XNextEvent(w->display, &event);

	rc = 0;

	switch (event.type) {
	case KeyPress:
		ev->type = ZDL_EVENT_KEYPRESS;
		rc = zdl_window_translate(w, 1, &event.xkey, ev);
		break;
	case KeyRelease:
		ev->type = ZDL_EVENT_KEYRELEASE;
		if (XEventsQueued(w->display, QueuedAfterReading)) {
			XEvent nev;
			XPeekEvent(w->display, &nev);
			if (nev.type == KeyPress &&
			    nev.xkey.time == event.xkey.time &&
			    nev.xkey.keycode == event.xkey.keycode) {
				rc = -1;
				if (!(w->flags & ZDL_FLAG_KEYREPEAT))
					XNextEvent(w->display, &nev);
			}
		}
		if (rc != -1)
			rc = zdl_window_translate(w, 0, &event.xkey, ev);
		break;
	case ButtonPress:
		if (w->flags & ZDL_FLAG_CLIPBOARD && event.xbutton.button == 2) {
			ev->type = ZDL_EVENT_PASTE;
			w->eatpaste = 1;
		} else {
			ev->type = ZDL_EVENT_BUTTONPRESS;
			ev->button.x = event.xbutton.x;
			ev->button.y = event.xbutton.y;
			ev->button.modifiers = w->modifiers;
			ev->button.button = button_map[event.xbutton.button];
		}
		break;
	case ButtonRelease:
		if (w->eatpaste && event.xbutton.button == 2) {
			w->eatpaste = 0;
			rc = -1;
		} else {
			ev->type = ZDL_EVENT_BUTTONRELEASE;
			ev->button.x = event.xbutton.x;
			ev->button.y = event.xbutton.y;
			ev->button.modifiers = w->modifiers;
			ev->button.button = button_map[event.xbutton.button];
		}
		break;
	case MotionNotify:
		ev->type = ZDL_EVENT_MOTION;
		ev->motion.x = event.xmotion.x;
		ev->motion.y = event.xmotion.y;
		ev->motion.d_x = (ev->motion.x - w->lastmotion.x);
		ev->motion.d_y = (ev->motion.y - w->lastmotion.y);
		w->lastmotion.x = ev->motion.x;
		w->lastmotion.y = ev->motion.y;
		break;
	case EnterNotify:
		ev->type = ZDL_EVENT_GAINFOCUS;
		w->lastmotion.x = event.xcrossing.x;
		w->lastmotion.y = event.xcrossing.y;
		break;
	case LeaveNotify:
		ev->type = ZDL_EVENT_LOSEFOCUS;
		break;
	case ConfigureNotify:
		if (event.xconfigure.window != w->window) {
			rc = -1;
			break;
		}

		if ((event.xconfigure.width  == w->width) &&
		    (event.xconfigure.height == w->height) &&
		    (event.xconfigure.x == w->lastconfig.x) &&
		    (event.xconfigure.y == w->lastconfig.y)) {
			rc = -1;
			break;
		}

		if ((event.xconfigure.x != w->lastconfig.x) ||
		    (event.xconfigure.y != w->lastconfig.y)) {
			if (event.xconfigure.send_event == False) {
				Window ret;
				XTranslateCoordinates(w->display,
						w->window, w->root,
						0, 0, &w->x, &w->y, &ret);
			} else {
				w->x = event.xconfigure.x;
				w->y = event.xconfigure.y;
			}
		}

		w->lastconfig.x = event.xconfigure.x;
		w->lastconfig.y = event.xconfigure.y;

		ev->type = ZDL_EVENT_RECONFIGURE;
		ev->reconfigure.width =  event.xconfigure.width;
		ev->reconfigure.height = event.xconfigure.height;
		rc = -((ev->reconfigure.width  == w->width) &&
			       (ev->reconfigure.height == w->height));
		w->width = ev->reconfigure.width;
		w->height = ev->reconfigure.height;
		break;
	case Expose:
		ev->type = ZDL_EVENT_EXPOSE;
		break;
	case ClientMessage:
		if (event.xclient.data.l[0] == w->wm_delete_window) {
			ev->type = ZDL_EVENT_EXIT;
			break;
		} else {
			rc = -1;
		}
		break;
	case SelectionRequest:
		if (w->clipboard.text.text == NULL) {
			resp.xselection.property = None;
		} else if (event.xselectionrequest.target == XA_STRING) {
			XChangeProperty(w->display,
				event.xselectionrequest.requestor,
				event.xselectionrequest.property,
				XA_STRING, 8, PropModeReplace,
				(unsigned char *)w->clipboard.text.text,
				strlen(w->clipboard.text.text));
			resp.xselection.property = event.xselectionrequest.property;
		} else {
			resp.xselection.property = None;
		}
		resp.xselection.type = SelectionNotify;
		resp.xselection.display = event.xselectionrequest.display;
		resp.xselection.requestor = event.xselectionrequest.requestor;
		resp.xselection.selection = event.xselectionrequest.selection;
		resp.xselection.target = event.xselectionrequest.target;
		resp.xselection.time = event.xselectionrequest.time;
		XSendEvent(w->display, event.xselectionrequest.requestor,
				0, 0, &resp);
		rc = -1;
		break;
	default:
		rc = -1;
		break;
	}

	return rc;
}

int zdl_window_poll_event(zdl_window_t w, struct zdl_event *ev)
{
	while (XPending(w->display)) {
		if (zdl_window_read_event(w, ev) == 0)
			return 0;
	}
	return -1;
}

void zdl_window_wait_event(zdl_window_t w, struct zdl_event *ev)
{
	while (zdl_window_read_event(w, ev) != 0);
}

void zdl_window_warp_mouse(zdl_window_t w, int x, int y)
{
	XWarpPointer(w->display, None, w->window, 0, 0, 0, 0, x, y);
}

void zdl_window_swap(zdl_window_t w)
{
	glXSwapBuffers(w->display, w->window);
}

void zdl_window_set_title(zdl_window_t w, const char *icon, const char *name)
{
	if (name == NULL && icon == NULL)
		return;

	if (name == NULL)
		name = icon;
	else if (icon == NULL)
		icon = name;

	XStoreName(w->display, w->window, name);
	XSetIconName(w->display, w->window, icon);
}

union zdl_native_handle zdl_window_native_handle(zdl_window_t w)
{
	union zdl_native_handle ret;
	if (sizeof(w->window) == sizeof(unsigned int))
		ret.u32 = (unsigned int)w->window;
	else
		ret.u64 = (unsigned long long)w->window;
	return ret;
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

	return c;
}

void zdl_clipboard_close(zdl_clipboard_t c)
{
	if (c->data != NULL) {
		free(c->data);
		c->data = NULL;
	}
	free(c);
}

int zdl_clipboard_write(zdl_clipboard_t c, const struct zdl_clipboard_data *data)
{
	if (data->format != ZDL_CLIPBOARD_URI &&
	    data->format != ZDL_CLIPBOARD_TEXT)
		return -1;

	if (c->window->clipboard.text.text != NULL)
		free((void *)c->window->clipboard.text.text);

	if (data->format == ZDL_CLIPBOARD_URI)
		c->window->clipboard.text.text = strdup(data->uri.uri);
	else if (data->format == ZDL_CLIPBOARD_TEXT)
		c->window->clipboard.text.text = strdup(data->text.text);

	XSetSelectionOwner(c->window->display, XA_PRIMARY,
			c->window->window, CurrentTime);
	return 0;
}

static unsigned long long zdl_time_ms(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (unsigned long long)tv.tv_sec*1000 + tv.tv_usec/1000;
}

static int zdl_read_property(zdl_window_t w, Atom property, void **data, int *count)
{
	Atom actual_type;
	int actual_format;
	unsigned long nitems;
	unsigned long left;
	unsigned char *ret = 0;
	int read_bytes = 1024;
	int rc;

	do {
		if (ret != 0)
			XFree(ret);

		rc = XGetWindowProperty(w->display, w->window,
				property, 0, read_bytes, False,
				AnyPropertyType,
				&actual_type, &actual_format,
				&nitems, &left, &ret);
		if (rc != Success)
			return -1;
		read_bytes <<= 1;
	} while (left != 0);

	*count = nitems;
	*data = (void *)ret;

	return 0;
}

int zdl_clipboard_read(zdl_clipboard_t c, struct zdl_clipboard_data *data)
{
	Window owner;
	XEvent event;
	Atom board_atoms[] = {
		XA_PRIMARY,
		XA_SECONDARY,
		XInternAtom(c->window->display, "CLIPBOARD", False),
	};
	Atom xa_targets = XInternAtom(c->window->display, "TARGETS", False);
	Atom targets[] = {
		/* text formats */
		XInternAtom(c->window->display, "UTF8_STRING", False),
		XInternAtom(c->window->display, "C_STRING", False),
		XInternAtom(c->window->display, "text/unicode", False),
		XA_STRING,
		XInternAtom(c->window->display, "COMPOUND_TEXT", False),
		XInternAtom(c->window->display, "TEXT", False),
		/* image formats */
		XA_PIXMAP,
		//XA_BITMAP,
		//XA_DRAWABLE,
		//XA_WINDOW,
	};

	unsigned long long start;
	void *pdata;
	Atom board;
	Atom type;
	int count;
	int i, j, rc;

	if (c->data != NULL) {
		free(c->data);
		c->data = NULL;
	}

	for (i = 0; i < sizeof(board_atoms)/sizeof(board_atoms[0]); ++i) {
		board = board_atoms[i];
		owner = XGetSelectionOwner(c->window->display, board);
		if (owner != None)
			break;
	}
	if (owner == None)
		return -1;

	start = zdl_time_ms();
	/* request possible conversion targets */
	XConvertSelection(c->window->display,
			board, xa_targets, board,
			c->window->window, CurrentTime);

	for (;;) {
		count = 0;
		pdata = NULL;
		XFlush(c->window->display);

		do {
			rc = XCheckIfEvent(c->window->display, &event, wait_for_selection_notify, NULL);
		} while (rc == False && zdl_time_ms() - start < 100);

		if (rc == False)
			return -1;

		if (event.xselection.property == None)
			return -1;

		if (zdl_read_property(c->window, board, (void **)&pdata, &count))
			return -1;

		if (event.xselection.target != xa_targets)
			break;

		for (i = 0; i < sizeof(targets)/sizeof(targets[0]); ++i) {
			for (j = 0; j < count; ++j) {
				if (((Atom *)pdata)[j] == targets[i])
					break;
			}
			if (j != count)
				break;
		}

		XFree(pdata);
		if (j == count)
			return -1;

		type = targets[i];
		/* request actual data */
		XConvertSelection(c->window->display,
				board, type, board,
				c->window->window, CurrentTime);
	}

	switch (type) {
	case XA_PIXMAP: {
		unsigned int d0;
		unsigned int w, h;
		XImage *image;
		Window root;
		int x, y;
		int d1;

		XGetGeometry(c->window->display, ((Pixmap *)pdata)[0],
				&root, &d1, &d1, &w, &h,
				&d0, &d0);
		image = XGetImage(c->window->display, ((Pixmap *)pdata)[0], 0, 0,
				w, h, AllPlanes, ZPixmap);
		XFree(pdata);
		data->format = ZDL_CLIPBOARD_IMAGE;
		data->image.pixels = (unsigned int *)calloc(4, w * h);
		if (data->image.pixels == NULL)
			return -1;
		data->image.width = w;
		data->image.height = h;
		for (i = y = 0; y < h; ++y) {
			char *line = image->data + y * image->bytes_per_line;
			for (x = 0; x < w; ++x) {
				void *pin = line + (x * image->bits_per_pixel) / 8;
				unsigned int *pixel = (unsigned int *)&data->image.pixels[i++];
				unsigned int in;
				switch (image->bits_per_pixel) {
				case 32:
				case 24:
					in = (*(unsigned int *)pin) & 0x00ffffff;
					*pixel = (0xff << 24) | in;
					break;
				case 16:
					in = *(unsigned short *)pin;
					*pixel = (0xff << 24) |
						((in & 0xf100) << 8) |
						((in & 0x03f0) << 6) |
						 (in & 0x001f);
					break;
				case  8:
					in = *(unsigned char *)pin;
					*pixel = (0xff << 24) |
						 (in << 16) |
						 (in << 8) |
						  in;
					break;
				default:
					*pixel = 0;
					break;
				}
				/* bgr -> rgb swap */
				if (image->blue_mask > image->red_mask) {
					unsigned int tmp = *pixel;
					*pixel =  (tmp & (0xff << 24)) |
						  (tmp & (0xff <<  8)) |
						 ((tmp & (0xff << 16)) >> 16) |
						 ((tmp & (0xff <<  0)) << 16);
				}
			}
		}
		XDestroyImage(image);
		pdata = (void *)data->image.pixels;
		} break;
	case XA_BITMAP:
	case XA_DRAWABLE:
	case XA_WINDOW:
		XFree(pdata);
		return -1;
	default:
		data->format = ZDL_CLIPBOARD_TEXT;
		data->text.text = strdup((char *)pdata);
		XFree(pdata);
		pdata = (void *)data->text.text;
		break;
	}

	c->data = pdata;
	return 0;
}
