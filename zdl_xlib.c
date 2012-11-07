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
#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <GL/glx.h>
#include <GL/gl.h>

#include "zdl.h"

struct zdl_window {
	char *title;
	Display *display;
	int screen;
	int width, height;
	int fullscreen;
	int noresize;
	struct { int width, height; } masked;
	int configured;
	int grabbed;
	int hidecursor;

	Window window;
	Colormap colormap;
	GLXContext context;

	struct { int x, y; } lastmotion;
	unsigned int modifiers;
	Atom wm_delete_window;
};

static Bool wait_for_map_notify(Display *d, XEvent *e, char *arg)
{
	if ((e->type == MapNotify) && (e->xmap.window == (Window)arg))
		return GL_TRUE;
	return GL_FALSE;
}

static void zdl_window_set_hints(zdl_window_t w, int width, int height)
{
	XSizeHints *hints = XAllocSizeHints();

	hints->flags = PMinSize | PMaxSize;

	if (w->noresize) {
		hints->min_width = width;
		hints->min_height = height;
		hints->max_width = width;
		hints->max_height = height;
	} else {
		hints->min_width = 0;
		hints->min_height = 0;
		hints->max_width = XDisplayWidth(w->display, w->screen);
		hints->max_height = XDisplayHeight(w->display, w->screen);
	}

	hints->base_width = width;
	hints->base_height = height;

	XSetWMNormalHints(w->display, w->window, hints);
	XFree(hints);
}

static int zdl_window_reconfigure(zdl_window_t w, int width, int height, zdl_flags_t flags)
{
	unsigned int valuelist[6];
	unsigned int valuemask;
	XSetWindowAttributes swa;
	XVisualInfo *vi;
	XEvent event;
	Window root;

	if (w->configured) {
		XFreeColormap(w->display, w->colormap);
		XDestroyWindow(w->display, w->window);
		glXDestroyContext(w->display, w->context);
		w->configured = 0;
	}

	valuelist[0] = GLX_RGBA;
	valuelist[1] = GLX_DOUBLEBUFFER;
	valuelist[2] = GLX_USE_GL;
	valuelist[3] = None;

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

	root = XRootWindow(w->display, vi->screen);
	w->colormap = XCreateColormap(w->display, root, vi->visual, AllocNone);

	swa.colormap = w->colormap;
	swa.border_pixel = 0;
	swa.background_pixel = 0;
	swa.override_redirect = !!(flags & ZDL_FLAG_FULLSCREEN);
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

	w->window = XCreateWindow(w->display, root,
			0, 0, width, height, 0, vi->depth,
			InputOutput, vi->visual, valuemask, &swa);

	zdl_window_set_hints(w, width, height);

	if (!glXMakeCurrent(w->display, w->window, w->context)) {
		fprintf(stderr, "Unable to make context current\n");
		XFreeColormap(w->display, w->colormap);
		XDestroyWindow(w->display, w->window);
		glXDestroyContext(w->display, w->context);
		return -1;
	}

	XMapWindow(w->display, w->window); 
	XSetWMProtocols(w->display, w->window, &w->wm_delete_window, 1);
	XIfEvent(w->display, &event, wait_for_map_notify, (char *)w->window);

	if (flags & ZDL_FLAG_FULLSCREEN) {
		XGrabKeyboard(w->display, root, 1, GrabModeAsync, GrabModeAsync, CurrentTime);
		w->grabbed = 1;
	} else if (w->grabbed) {
		XUngrabKeyboard(w->display, CurrentTime);
		w->grabbed = 0;
	}

	if (w->title != NULL)
		XStoreName(w->display, w->window, w->title);

	w->configured = 1;

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

	w->width = width;
	w->height = height;
	w->fullscreen = (flags & ZDL_FLAG_FULLSCREEN);
	w->noresize = (flags & ZDL_FLAG_NORESIZE);

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

	return w;
}

void zdl_window_destroy(zdl_window_t w)
{
	if (w->configured) {
		if (w->grabbed)
			XUngrabKeyboard(w->display, CurrentTime);
		XFreeColormap(w->display, w->colormap);
		XDestroyWindow(w->display, w->window);
		glXDestroyContext(w->display, w->context);
	}
	XCloseDisplay(w->display);
	if (w->title != NULL)
		free(w->title);
	free(w);
}

void zdl_window_set_flags(zdl_window_t w, zdl_flags_t flags)
{
	int fullscreen = (flags & ZDL_FLAG_FULLSCREEN);
	int noresize = (flags & ZDL_FLAG_NORESIZE);
	unsigned int width, height;

	if (fullscreen != w->fullscreen) {
		w->noresize = noresize;
		if (fullscreen) {
			w->masked.width = w->width;
			w->masked.height = w->height;
			width = XDisplayWidth(w->display, w->screen);
			height = XDisplayHeight(w->display, w->screen);
			if (zdl_window_reconfigure(w, width, height, flags))
				return;
			w->width = width;
			w->height = height;
		} else {
			width = w->masked.width;
			height = w->masked.height;
			if (zdl_window_reconfigure(w, width, height, flags))
				return;
			w->width = width;
			w->height = height;
		}
		w->fullscreen = fullscreen;
	}

	if (noresize != w->noresize) {
		w->noresize = noresize;
		zdl_window_set_hints(w, w->width, w->height);
	}
}

zdl_flags_t zdl_window_get_flags(zdl_window_t w)
{
	return (w->fullscreen ? ZDL_FLAG_FULLSCREEN : 0) |
		(w->noresize  ? ZDL_FLAG_NORESIZE : 0);
}

void zdl_window_set_size(zdl_window_t w, int width, int height)
{
	if (width == w->width && height == w->height)
		return;

	if (!w->fullscreen) {
		w->width = width;
		w->height = height;
		XResizeWindow(w->display, w->window, w->width, w->height);
	} else {
		w->masked.width = width;
		w->masked.height = height;
	}
}

void zdl_window_get_size(zdl_window_t w, int *width, int *height)
{
	if (width != NULL)  *width  = w->width;
	if (height != NULL) *height = w->height;
}

static const struct {
	KeySym sym;
	unsigned int mask;
} drop_keys[] = {
	{ XK_Tab, Mod1Mask | Mod4Mask },
	{ XK_F1,  Mod1Mask | Mod4Mask },
	{ XK_F1,  Mod1Mask | Mod4Mask },
	{ XK_F2,  Mod1Mask | Mod4Mask },
	{ XK_F3,  Mod1Mask | Mod4Mask },
	{ XK_F4,  Mod1Mask | Mod4Mask },
	{ XK_F5,  Mod1Mask | Mod4Mask },
	{ XK_F6,  Mod1Mask | Mod4Mask },
	{ XK_F7,  Mod1Mask | Mod4Mask },
	{ XK_F8,  Mod1Mask | Mod4Mask },
	{ XK_F9,  Mod1Mask | Mod4Mask },
	{ XK_F10, Mod1Mask | Mod4Mask },
	{ XK_F11, Mod1Mask | Mod4Mask },
	{ XK_F12, Mod1Mask | Mod4Mask },
	{ XK_Super_R, 0 },
	{ XK_Super_L, 0 },
	{ (KeySym)-1, Mod4Mask },
};

static int zdl_window_translate(zdl_window_t w, int down, XKeyEvent *event, struct zdl_event *ev)
{
	unsigned int nmod = ZDL_KEYMOD_NONE;
	int drop = 0, i;
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

	if (w->grabbed) {
		for (i = 0; i < sizeof(drop_keys) / sizeof(drop_keys[0]); ++i) {
			if (drop_keys[i].sym == (KeySym)-1) {
				drop = (drop_keys[i].mask & event->state);
			} else if (drop_keys[i].sym == ks) {
				if (drop_keys[i].mask)
					drop = (drop_keys[i].mask & event->state);
				else
					drop = 1;
			}
			if (drop)
				break;
		}
	}

	if (drop) {
		Window root;
		XEvent xevent;
		root = XRootWindow(w->display, w->screen);
		xevent.xkey = *event;
		XSendEvent(w->display, root, False,
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
		rc = zdl_window_translate(w, 0, &event.xkey, ev);
		break;
	case ButtonPress:
		ev->type = ZDL_EVENT_BUTTONPRESS;
		ev->button.x = event.xbutton.x;
		ev->button.y = event.xbutton.y;
		ev->button.button = event.xbutton.button;
		break;
	case ButtonRelease:
		ev->type = ZDL_EVENT_BUTTONRELEASE;
		ev->button.x = event.xbutton.x;
		ev->button.y = event.xbutton.y;
		ev->button.button = event.xbutton.button;
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
	default:
		rc = -1;
		break;
	}

	return rc;
}

int zdl_window_poll_event(zdl_window_t w, struct zdl_event *ev)
{
	if (!XPending(w->display))
		return -1;

	return zdl_window_read_event(w, ev);
}

void zdl_window_wait_event(zdl_window_t w, struct zdl_event *ev)
{
	while (zdl_window_read_event(w, ev) != 0);
}

void zdl_window_show_cursor(zdl_window_t w, int shown)
{
	Cursor cursor;

	if (shown == !w->hidecursor)
		return;

	if (!shown) {
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
	w->hidecursor = !shown;
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

	if (w->title != NULL)
		free(w->title);

	w->title = strdup(name);

	XStoreName(w->display, w->window, name);
}
