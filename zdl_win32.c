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
#include <windows.h>
#include <GL/gl.h>
#include <tchar.h>

#define ZDL_INTERNAL
#define ZDL_NO_WINMAIN
#include "zdl.h"

struct zdl_queue_item {
	struct zdl_event data;
	struct zdl_queue_item *next;
};

struct zdl_queue {
	struct zdl_queue_item *head;
	struct zdl_queue_item *tail;
	HANDLE lock;
};

void zdl_queue_create(struct zdl_queue *q)
{
	q->head = q->tail = NULL;
	q->lock = CreateMutex(NULL, FALSE, NULL);
}

void zdl_queue_push(struct zdl_queue *q, struct zdl_event *ev)
{
	struct zdl_queue_item *item;

	item = (struct zdl_queue_item *)calloc(1, sizeof(*item));
	item->data = *ev;

	WaitForSingleObject(q->lock, INFINITE);
	if (q->tail != NULL) {
		q->tail->next = item;
		q->tail = item;
	} else {
		q->tail = q->head = item;
	}
	ReleaseMutex(q->lock);
}

int zdl_queue_pop(struct zdl_queue *q, struct zdl_event *ev)
{
	struct zdl_queue_item *item;

	WaitForSingleObject(q->lock, INFINITE);
	item = q->head;
	if (item != NULL) {
		q->head = q->head->next;
		if (q->head == NULL)
			q->tail = NULL;

		*ev = item->data;
		free(item);
		ReleaseMutex(q->lock);
		return 0;
	}
	ReleaseMutex(q->lock);
	return -1;
}

void zdl_queue_smash_key(struct zdl_queue *q, struct zdl_event *ev)
{
	WaitForSingleObject(q->lock, INFINITE);
	if (q->tail != NULL && q->tail->data.type == ZDL_EVENT_KEYPRESS) {
		q->tail->data.key.unicode = ev->key.unicode;
	}
	ReleaseMutex(q->lock);
}

void zdl_queue_smash_reconfigure(struct zdl_queue *q, struct zdl_event *ev)
{
	WaitForSingleObject(q->lock, INFINITE);
	if (q->tail != NULL && q->tail->data.type == ZDL_EVENT_RECONFIGURE) {
		q->tail->data.reconfigure = ev->reconfigure;
		ReleaseMutex(q->lock);
	} else {
		ReleaseMutex(q->lock);
		zdl_queue_push(q, ev);
	}
}

void zdl_queue_destroy(struct zdl_queue *q)
{
	struct zdl_event ev;
	while (zdl_queue_pop(q, &ev) == 0);
	CloseHandle(q->lock);
}

struct zdl_window {
	int width;
	int height;
	int x, y;
	zdl_flags_t flags;
	struct {
		int x, y;
		int width, height;
	} masked;

	WNDCLASSEX wcex;
	HWND  window;
	DWORD style;

	HGLRC hRContext;
	HDC hDeviceContext;
	struct zdl_queue queue;
	struct { int x, y; } lastmotion[(ZDL_MOTION_HOVER_END - ZDL_MOTION_TOUCH_START) + 1];
};

#define MOUSEEVENTF_PENTOUCH_MASK 0xFFFFFF00
#define MOUSEEVENTF_PENTOUCH      0xFF515700
#define MOUSEEVENTF_TOUCH         0x00000080

static int zdl_translate(WPARAM wParam, LPARAM lParam, struct zdl_event *ev)
{
	switch (wParam) {
	case VK_BACK:         ev->key.sym = ZDL_KEYSYM_BACKSPACE; break;
	case VK_TAB:          ev->key.sym = ZDL_KEYSYM_TAB; break;
	case VK_CLEAR:        ev->key.sym = ZDL_KEYSYM_CLEAR; break;
	case VK_RETURN:       ev->key.sym = ZDL_KEYSYM_RETURN; break;
	case VK_PAUSE:        ev->key.sym = ZDL_KEYSYM_PAUSE; break;
	case VK_ESCAPE:       ev->key.sym = ZDL_KEYSYM_ESCAPE; break;
	case VK_SPACE:        ev->key.sym = ZDL_KEYSYM_SPACE; break;
	case VK_OEM_7:        ev->key.sym = ZDL_KEYSYM_QUOTE; break;
	case VK_OEM_PLUS:     ev->key.sym = ZDL_KEYSYM_PLUS; break;
	case VK_OEM_COMMA:    ev->key.sym = ZDL_KEYSYM_COMMA; break;
	case VK_OEM_MINUS:    ev->key.sym = ZDL_KEYSYM_MINUS; break;
	case VK_OEM_PERIOD:   ev->key.sym = ZDL_KEYSYM_PERIOD; break;
	case VK_OEM_2:        ev->key.sym = ZDL_KEYSYM_SLASH; break;
	case 0x30:            ev->key.sym = ZDL_KEYSYM_0; break;
	case 0x31:            ev->key.sym = ZDL_KEYSYM_1; break;
	case 0x32:            ev->key.sym = ZDL_KEYSYM_2; break;
	case 0x33:            ev->key.sym = ZDL_KEYSYM_3; break;
	case 0x34:            ev->key.sym = ZDL_KEYSYM_4; break;
	case 0x35:            ev->key.sym = ZDL_KEYSYM_5; break;
	case 0x36:            ev->key.sym = ZDL_KEYSYM_6; break;
	case 0x37:            ev->key.sym = ZDL_KEYSYM_7; break;
	case 0x38:            ev->key.sym = ZDL_KEYSYM_8; break;
	case 0x39:            ev->key.sym = ZDL_KEYSYM_9; break;
	case VK_OEM_1:        ev->key.sym = ZDL_KEYSYM_COLON; break;
	case VK_OEM_4:        ev->key.sym = ZDL_KEYSYM_LEFTBRACKET; break;
	case VK_OEM_5:        ev->key.sym = ZDL_KEYSYM_BACKSLASH; break;
	case VK_OEM_6:        ev->key.sym = ZDL_KEYSYM_RIGHTBRACKET; break;
	case VK_OEM_3:        ev->key.sym = ZDL_KEYSYM_BACKQUOTE; break;
	case 0x41:            ev->key.sym = ZDL_KEYSYM_A; break;
	case 0x42:            ev->key.sym = ZDL_KEYSYM_B; break;
	case 0x43:            ev->key.sym = ZDL_KEYSYM_C; break;
	case 0x44:            ev->key.sym = ZDL_KEYSYM_D; break;
	case 0x45:            ev->key.sym = ZDL_KEYSYM_E; break;
	case 0x46:            ev->key.sym = ZDL_KEYSYM_F; break;
	case 0x47:            ev->key.sym = ZDL_KEYSYM_G; break;
	case 0x48:            ev->key.sym = ZDL_KEYSYM_H; break;
	case 0x49:            ev->key.sym = ZDL_KEYSYM_I; break;
	case 0x4a:            ev->key.sym = ZDL_KEYSYM_J; break;
	case 0x4b:            ev->key.sym = ZDL_KEYSYM_K; break;
	case 0x4c:            ev->key.sym = ZDL_KEYSYM_L; break;
	case 0x4d:            ev->key.sym = ZDL_KEYSYM_M; break;
	case 0x4e:            ev->key.sym = ZDL_KEYSYM_N; break;
	case 0x4f:            ev->key.sym = ZDL_KEYSYM_O; break;
	case 0x50:            ev->key.sym = ZDL_KEYSYM_P; break;
	case 0x51:            ev->key.sym = ZDL_KEYSYM_Q; break;
	case 0x52:            ev->key.sym = ZDL_KEYSYM_R; break;
	case 0x53:            ev->key.sym = ZDL_KEYSYM_S; break;
	case 0x54:            ev->key.sym = ZDL_KEYSYM_T; break;
	case 0x55:            ev->key.sym = ZDL_KEYSYM_U; break;
	case 0x56:            ev->key.sym = ZDL_KEYSYM_V; break;
	case 0x57:            ev->key.sym = ZDL_KEYSYM_W; break;
	case 0x58:            ev->key.sym = ZDL_KEYSYM_X; break;
	case 0x59:            ev->key.sym = ZDL_KEYSYM_Y; break;
	case 0x5a:            ev->key.sym = ZDL_KEYSYM_Z; break;
	case VK_DELETE:       ev->key.sym = ZDL_KEYSYM_DELETE; break;
	case VK_NUMPAD0:      ev->key.sym = ZDL_KEYSYM_KEYPAD_0; break;
	case VK_NUMPAD1:      ev->key.sym = ZDL_KEYSYM_KEYPAD_1; break;
	case VK_NUMPAD2:      ev->key.sym = ZDL_KEYSYM_KEYPAD_2; break;
	case VK_NUMPAD3:      ev->key.sym = ZDL_KEYSYM_KEYPAD_3; break;
	case VK_NUMPAD4:      ev->key.sym = ZDL_KEYSYM_KEYPAD_4; break;
	case VK_NUMPAD5:      ev->key.sym = ZDL_KEYSYM_KEYPAD_5; break;
	case VK_NUMPAD6:      ev->key.sym = ZDL_KEYSYM_KEYPAD_6; break;
	case VK_NUMPAD7:      ev->key.sym = ZDL_KEYSYM_KEYPAD_7; break;
	case VK_NUMPAD8:      ev->key.sym = ZDL_KEYSYM_KEYPAD_8; break;
	case VK_NUMPAD9:      ev->key.sym = ZDL_KEYSYM_KEYPAD_9; break;
	case VK_DECIMAL:      ev->key.sym = ZDL_KEYSYM_KEYPAD_PERIOD; break;
	case VK_DIVIDE:       ev->key.sym = ZDL_KEYSYM_KEYPAD_DIVIDE; break;
	case VK_MULTIPLY:     ev->key.sym = ZDL_KEYSYM_KEYPAD_MULTIPLY; break;
	case VK_SUBTRACT:     ev->key.sym = ZDL_KEYSYM_KEYPAD_MINUS; break;
	case VK_ADD:          ev->key.sym = ZDL_KEYSYM_KEYPAD_PLUS; break;
	case VK_UP:           ev->key.sym = ZDL_KEYSYM_UP; break;
	case VK_DOWN:         ev->key.sym = ZDL_KEYSYM_DOWN; break;
	case VK_RIGHT:        ev->key.sym = ZDL_KEYSYM_RIGHT; break;
	case VK_LEFT:         ev->key.sym = ZDL_KEYSYM_LEFT; break;
	case VK_INSERT:       ev->key.sym = ZDL_KEYSYM_INSERT; break;
	case VK_HOME:         ev->key.sym = ZDL_KEYSYM_HOME; break;
	case VK_END:          ev->key.sym = ZDL_KEYSYM_END; break;
	case VK_PRIOR:        ev->key.sym = ZDL_KEYSYM_PAGEUP; break;
	case VK_NEXT:         ev->key.sym = ZDL_KEYSYM_PAGEDOWN; break;
	case VK_F1:           ev->key.sym = ZDL_KEYSYM_F1; break;
	case VK_F2:           ev->key.sym = ZDL_KEYSYM_F2; break;
	case VK_F3:           ev->key.sym = ZDL_KEYSYM_F3; break;
	case VK_F4:           ev->key.sym = ZDL_KEYSYM_F4; break;
	case VK_F5:           ev->key.sym = ZDL_KEYSYM_F5; break;
	case VK_F6:           ev->key.sym = ZDL_KEYSYM_F6; break;
	case VK_F7:           ev->key.sym = ZDL_KEYSYM_F7; break;
	case VK_F8:           ev->key.sym = ZDL_KEYSYM_F8; break;
	case VK_F9:           ev->key.sym = ZDL_KEYSYM_F9; break;
	case VK_F10:          ev->key.sym = ZDL_KEYSYM_F10; break;
	case VK_F11:          ev->key.sym = ZDL_KEYSYM_F11; break;
	case VK_F12:          ev->key.sym = ZDL_KEYSYM_F12; break;
	case VK_F13:          ev->key.sym = ZDL_KEYSYM_F13; break;
	case VK_F14:          ev->key.sym = ZDL_KEYSYM_F14; break;
	case VK_F15:          ev->key.sym = ZDL_KEYSYM_F15; break;
	case VK_NUMLOCK:      ev->key.sym = ZDL_KEYSYM_NUMLOCK; break;
	case VK_CAPITAL:      ev->key.sym = ZDL_KEYSYM_CAPSLOCK; break;
	case VK_SCROLL:       ev->key.sym = ZDL_KEYSYM_SCROLLLOCK; break;
	case VK_RSHIFT:       ev->key.sym = ZDL_KEYSYM_RSHIFT; break;
	case VK_LSHIFT:       ev->key.sym = ZDL_KEYSYM_LSHIFT; break;
	case VK_RCONTROL:     ev->key.sym = ZDL_KEYSYM_RCTRL; break;
	case VK_LCONTROL:     ev->key.sym = ZDL_KEYSYM_LCTRL; break;
	case VK_MENU:         ev->key.sym = ZDL_KEYSYM_RALT; break;
	case VK_LWIN:         ev->key.sym = ZDL_KEYSYM_LSUPER; break;
	case VK_RWIN:         ev->key.sym = ZDL_KEYSYM_RSUPER; break;
	case VK_MODECHANGE:   ev->key.sym = ZDL_KEYSYM_MODE; break;
	case VK_HELP:         ev->key.sym = ZDL_KEYSYM_HELP; break;
	case VK_PRINT:        ev->key.sym = ZDL_KEYSYM_PRINT; break;
	case VK_CANCEL:       ev->key.sym = ZDL_KEYSYM_BREAK; break;
	default:
		ev->key.sym = (enum zdl_keysym)-1;
		break;
	}
	ev->key.scancode = (lParam >> 16) & 0x7f;
	ev->key.unicode = 0;
	ev->key.modifiers = 0;

	return 0;
}

static void zdl_gl_setup(zdl_window_t w, HWND hwnd)
{
	PIXELFORMATDESCRIPTOR pfd = { 
		sizeof(PIXELFORMATDESCRIPTOR), 1,
		PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,   
		PFD_TYPE_RGBA, 24, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		32, 0, 0, PFD_MAIN_PLANE, 0, 0, 0, 0
	};
	int format;

	w->hDeviceContext = GetDC(hwnd);
	format = ChoosePixelFormat(w->hDeviceContext, &pfd);
	SetPixelFormat(w->hDeviceContext, format, &pfd);
	w->hRContext = wglCreateContext(w->hDeviceContext);
	if (!wglMakeCurrent(w->hDeviceContext, w->hRContext))
		fprintf(stderr, "Unable to make GL context (%d)\n", GetLastError());
}

static void zdl_gl_teardown(zdl_window_t w)
{
	wglMakeCurrent(w->hDeviceContext, NULL);
	wglDeleteContext(w->hRContext);
}

static void zdl_handle_touch(zdl_window_t w, HTOUCHINPUT touch, int count)
{
	struct zdl_event ev;
	TOUCHINPUT *ti;
	int i;

	ti = (TOUCHINPUT *)malloc(count * sizeof(*ti));
	if (ti == NULL)
		return;

	if (!GetTouchInputInfo(touch, count, ti, sizeof(TOUCHINPUT))) {
		free(ti);
		return;
	}

	for (i = 0;  i < count; ++i) {
		if (ti[i].dwFlags & TOUCHEVENTF_PALM)
			continue;
		ev.type = ZDL_EVENT_MOTION;
		if (ti[i].dwFlags & TOUCHEVENTF_INRANGE)
			ev.motion.id = (enum zdl_motion_id)(ZDL_MOTION_HOVER_START + ti[i].dwID);
		else
			ev.motion.id = (enum zdl_motion_id)(ZDL_MOTION_TOUCH_START + ti[i].dwID);

		ev.motion.x = ti[i].x;
		ev.motion.y = ti[i].y;
		ev.motion.d_x = (ev.motion.x - w->lastmotion[ev.motion.id].x);
		ev.motion.d_y = (ev.motion.y - w->lastmotion[ev.motion.id].y);
		w->lastmotion[ev.motion.id].x = ev.motion.x;
		w->lastmotion[ev.motion.id].y = ev.motion.y;
		ev.motion.flags = 0;
		if (ti[i].dwFlags & TOUCHEVENTF_DOWN) {
			ev.motion.flags |= ZDL_MOTION_FLAG_INITIAL;
		} else if (ti[i].dwFlags & TOUCHEVENTF_MOVE) {
		} else if (ti[i].dwFlags & TOUCHEVENTF_UP) {
			ev.motion.flags |= ZDL_MOTION_FLAG_INITIAL;
		}
		zdl_queue_push(&w->queue, &ev);
	}

	CloseTouchInputHandle(touch);
	free(ti);
}

static LRESULT CALLBACK zdl_WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{	
	zdl_window_t w = (zdl_window_t)GetWindowLongPtr(hwnd, 0);
	struct zdl_event ev;

	if (message >= WM_MOUSEMOVE && message <= WM_MOUSELAST) {
		LPARAM info = GetMessageExtraInfo();
		if ((info & MOUSEEVENTF_PENTOUCH_MASK) == MOUSEEVENTF_PENTOUCH) {
			if (info & MOUSEEVENTF_TOUCH)
				return TRUE;
		}
	}

	switch (message) {
	case WM_CREATE:
		w = *(zdl_window_t *)lParam;
		SetWindowLongPtr(hwnd, 0, (LONG_PTR)w);
		zdl_gl_setup(w, hwnd);
		break;
	case WM_CHAR:
		ev.key.unicode = wParam;
		zdl_queue_smash_key(&w->queue, &ev);
		break;
	case WM_KEYDOWN:
		if (zdl_translate(wParam, lParam, &ev))
			break;
		ev.type = ZDL_EVENT_KEYPRESS;
		zdl_queue_push(&w->queue, &ev);
		break;
	case WM_KEYUP:
		if (zdl_translate(wParam, lParam, &ev))
			break;
		ev.type = ZDL_EVENT_KEYRELEASE;
		zdl_queue_push(&w->queue, &ev);
		break;
	case WM_TOUCH:
		zdl_handle_touch(w, (HTOUCHINPUT)lParam, (int)wParam);
		break;
	case WM_MOUSEMOVE:
		ev.type = ZDL_EVENT_MOTION;
		ev.motion.id = ZDL_MOTION_POINTER;
		ev.motion.x = (lParam >>  0) & 0xffff;
		ev.motion.y = (lParam >> 16) & 0xffff;
		ev.motion.d_x = (ev.motion.x - w->lastmotion[0].x);
		ev.motion.d_y = (ev.motion.y - w->lastmotion[0].y);
		w->lastmotion[0].x = ev.motion.x;
		w->lastmotion[0].y = ev.motion.y;
		zdl_queue_push(&w->queue, &ev);
		break;
	case WM_MOUSEWHEEL:
		ev.type = ZDL_EVENT_BUTTONPRESS;
		ev.button.button = ((short)(wParam >> 16) < 0) ?
				ZDL_BUTTON_MWDOWN : ZDL_BUTTON_MWUP;
		ev.button.x = (lParam >>  0) & 0xffff;
		ev.button.y = (lParam >> 16) & 0xffff;
		zdl_queue_push(&w->queue, &ev);
		break;
	case WM_LBUTTONDOWN:
		ev.type = ZDL_EVENT_BUTTONPRESS;
		ev.button.button = ZDL_BUTTON_LEFT;
		ev.button.x = (lParam >>  0) & 0xffff;
		ev.button.y = (lParam >> 16) & 0xffff;
		zdl_queue_push(&w->queue, &ev);
		break;
	case WM_MBUTTONDOWN:
		ev.type = ZDL_EVENT_BUTTONPRESS;
		ev.button.button = ZDL_BUTTON_MIDDLE;
		ev.button.x = (lParam >>  0) & 0xffff;
		ev.button.y = (lParam >> 16) & 0xffff;
		zdl_queue_push(&w->queue, &ev);
		break;
	case WM_RBUTTONDOWN:
		ev.type = ZDL_EVENT_BUTTONPRESS;
		ev.button.button = ZDL_BUTTON_RIGHT;
		ev.button.x = (lParam >>  0) & 0xffff;
		ev.button.y = (lParam >> 16) & 0xffff;
		zdl_queue_push(&w->queue, &ev);
		break;
	case WM_RBUTTONUP:
		ev.type = ZDL_EVENT_BUTTONRELEASE;
		ev.button.button = ZDL_BUTTON_RIGHT;
		ev.button.x = (lParam >>  0) & 0xffff;
		ev.button.y = (lParam >> 16) & 0xffff;
		zdl_queue_push(&w->queue, &ev);
		break;
	case WM_LBUTTONUP:
		ev.type = ZDL_EVENT_BUTTONRELEASE;
		ev.button.button = ZDL_BUTTON_LEFT;
		ev.button.x = (lParam >>  0) & 0xffff;
		ev.button.y = (lParam >> 16) & 0xffff;
		zdl_queue_push(&w->queue, &ev);
		break;
	case WM_MBUTTONUP:
		ev.type = ZDL_EVENT_BUTTONRELEASE;
		ev.button.button = ZDL_BUTTON_MIDDLE;
		ev.button.x = (lParam >>  0) & 0xffff;
		ev.button.y = (lParam >> 16) & 0xffff;
		zdl_queue_push(&w->queue, &ev);
		break;
	case WM_MOVE:
		if (w->style != WS_DISABLED) {
			RECT rect = {
				(lParam >>  0) & 0xffff,
				(lParam >> 16) & 0xffff,
				w->width, w->height
			};
			AdjustWindowRect(&rect, w->style, FALSE);
			w->x = rect.left;
			w->y = rect.top;
		} else {
			w->x = (lParam >>  0) & 0xffff;
			w->y = (lParam >> 16) & 0xffff;
		}
		break;
	case WM_SIZE:
		ev.type = ZDL_EVENT_RECONFIGURE;
		ev.reconfigure.width  = (lParam >>  0) & 0xffff;
		ev.reconfigure.height = (lParam >> 16) & 0xffff;
		if (ev.reconfigure.width == w->width && ev.reconfigure.height == w->height)
			break;
		zdl_queue_smash_reconfigure(&w->queue, &ev);
		w->width  = ev.reconfigure.width;
		w->height = ev.reconfigure.height;
		break;
	case WM_SHOWWINDOW:
		ev.type = (wParam == FALSE) ? ZDL_EVENT_EXPOSE : ZDL_EVENT_HIDE;
		zdl_queue_push(&w->queue, &ev);
		break;
	case WM_CLOSE:
		ev.type = ZDL_EVENT_EXIT;
		zdl_queue_push(&w->queue, &ev);
		DestroyWindow(hwnd);
		break;
	case WM_DESTROY:
		zdl_gl_teardown(w);
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hwnd, message, wParam, lParam);
	}
	return 0;
}

static void zdl_adjust_size(zdl_window_t w, int *width, int *height, DWORD *style, zdl_flags_t flags)
{
	RECT rect;

	if (flags & (ZDL_FLAG_FULLSCREEN | ZDL_FLAG_NODECOR))
		*style = WS_POPUP;
	else if (!(flags & ZDL_FLAG_NORESIZE))
		*style = WS_OVERLAPPEDWINDOW;
	else
		*style = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;

	rect.left = 0;
	rect.top = 0;

	if (flags & ZDL_FLAG_FULLSCREEN) {
		MONITORINFO minfo;
		HMONITOR mh;

		mh = MonitorFromWindow(w->window,
				w->window == NULL ?
					MONITOR_DEFAULTTOPRIMARY :
					MONITOR_DEFAULTTONEAREST);
		minfo.cbSize = sizeof(MONITORINFO);
		GetMonitorInfo(mh, &minfo);
		rect.right = minfo.rcMonitor.right - minfo.rcMonitor.left;
		rect.bottom = minfo.rcMonitor.bottom - minfo.rcMonitor.top;
	} else {
		rect.right = *width;
		rect.bottom = *height;
	}

	AdjustWindowRect(&rect, *style, FALSE);
	*width = rect.right - rect.left;
	*height = rect.bottom - rect.top;
}

zdl_window_t zdl_window_create(int width, int height, zdl_flags_t flags)
{
	zdl_window_t w;
	HINSTANCE hInstance;

	w = (zdl_window_t)calloc(1, sizeof(*w));
	if (w == NULL)
		return ZDL_WINDOW_INVALID;

	zdl_queue_create(&w->queue);

	hInstance = GetModuleHandle(NULL);
	w->wcex.cbSize = sizeof(WNDCLASSEX);
	w->wcex.style          = CS_OWNDC;
	w->wcex.lpfnWndProc    = zdl_WndProc;
	w->wcex.cbClsExtra     = 0;
	w->wcex.cbWndExtra     = sizeof(PVOID);
	w->wcex.hInstance      = hInstance;
	w->wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPLICATION));
	w->wcex.hCursor        = LoadCursor(NULL, IDC_ARROW);
	w->wcex.hbrBackground  = (HBRUSH)GetStockObject(BLACK_BRUSH);
	w->wcex.lpszMenuName   = NULL;
	w->wcex.lpszClassName  = _T("win32app");
	w->wcex.hIconSm        = LoadIcon(w->wcex.hInstance, MAKEINTRESOURCE(IDI_APPLICATION));
	
	if (!RegisterClassEx(&w->wcex)) {
		fprintf(stderr, "Unable to register class\n");
		free(w);
		return ZDL_WINDOW_INVALID;
	}

	w->masked.width = w->width = width;
	w->masked.height = w->height = height;
	w->flags = flags & ~(ZDL_FLAG_NOCURSOR);

	zdl_adjust_size(w, &width, &height, &w->style, w->flags);
	w->window = CreateWindowEx(0,
			_T("win32app"),
			_T("win32app"),
			w->style,
			CW_USEDEFAULT, CW_USEDEFAULT,
			width, height,
			NULL,
			NULL,
			hInstance,
			w
	);

	if (!w->window) {
		fprintf(stderr, "Unable to create window (0x%08x)\n", GetLastError());
		free(w);
		return ZDL_WINDOW_INVALID;
	}

	if (((BYTE)GetSystemMetrics(SM_DIGITIZER) & 0xcf) != 0) {
		if (!RegisterTouchWindow(w->window, TWF_WANTPALM)) {
			fprintf(stderr, "Unable to register for touch events (0x%08x)\n", GetLastError());
		}
	}

	zdl_window_set_flags(w, flags);

	ShowWindow(w->window, SW_SHOWNORMAL);

	return w;
}

void zdl_window_destroy(zdl_window_t w)
{
	DestroyWindow(w->window);

	zdl_queue_destroy(&w->queue);
	free(w);
}

void zdl_window_set_flags(zdl_window_t w, zdl_flags_t flags)
{
	zdl_flags_t chg = flags ^ w->flags;
	int width, height, x, y;

	if (chg == ZDL_FLAG_NONE)
		return;

	x = w->x;
	y = w->y;
	width = w->width;
	height = w->height;

	if (chg == ZDL_FLAG_NOCURSOR) {
		ShowCursor(!(flags & ZDL_FLAG_NOCURSOR));
		w->flags = flags;
		return;
	}

	if (chg & ZDL_FLAG_FULLSCREEN) {
		if (flags & ZDL_FLAG_FULLSCREEN) {
			w->masked.width = w->width;
			w->masked.height = w->height;
			w->masked.x = w->x;
			w->masked.y = w->y;
			x = 0;
			y = 0;
		} else {
			width = w->masked.width;
			height = w->masked.height;
			x = w->masked.x;
			y = w->masked.y;
		}
	}

	zdl_adjust_size(w, &width, &height, &w->style, flags);
	SetWindowLong(w->window, GWL_STYLE, w->style);
	SetWindowPos(w->window,0,x,y,width,height,SWP_NOZORDER|SWP_NOACTIVATE);

	if (chg & ZDL_FLAG_NOCURSOR)
		ShowCursor(!(flags & ZDL_FLAG_NOCURSOR));

	w->flags = flags;

	ShowWindow(w->window, SW_SHOW);
}

zdl_flags_t zdl_window_get_flags(const zdl_window_t w)
{
	return w->flags;
}

void zdl_window_set_size(zdl_window_t w, int width, int height)
{
	if (w->flags & ZDL_FLAG_FULLSCREEN) {
		w->masked.width = width;
		w->masked.height = height;
	} else {
		if (width == w->width && height == w->height)
			return;
		w->width = width;
		w->height = height;
		SetWindowPos(w->window,0,0,0,w->width,w->height,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
	}
}

void zdl_window_get_size(const zdl_window_t w, int *width, int *height)
{
	if (width != NULL)  *width  = w->width;
	if (height != NULL) *height = w->height;
}

void zdl_window_set_position(zdl_window_t w, int x, int y)
{
	if (w->flags & ZDL_FLAG_FULLSCREEN) {
		w->masked.x = x;
		w->masked.y = y;
	} else {
		if (x == w->x && y == w->y)
			return;
		w->x = x;
		w->y = y;
		SetWindowPos(w->window,0,x,y,w->width,w->height,SWP_NOZORDER|SWP_NOACTIVATE);
	}
}

void zdl_window_get_position(const zdl_window_t w, int *x, int *y)
{
	if (x != NULL) *x = w->x;
	if (y != NULL) *y = w->y;
}

int zdl_window_poll_event(zdl_window_t w, struct zdl_event *ev)
{
	MSG msg;

	if (zdl_queue_pop(&w->queue, ev) == 0)
		return 0;

	while (PeekMessage(&msg, w->window, 0, 0, PM_REMOVE) != 0) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return zdl_queue_pop(&w->queue, ev);
}

void zdl_window_wait_event(zdl_window_t w, struct zdl_event *ev)
{
	while (zdl_queue_pop(&w->queue, ev) != 0) {
		MSG msg;

		if (GetMessage(&msg, w->window, 0, 0) > 0) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
}

void zdl_window_warp_mouse(zdl_window_t w, int x, int y)
{
	RECT rect = {
			w->x, w->y,
			w->width, w->height
	};

	AdjustWindowRect(&rect, w->style, FALSE);
	rect.left = w->x + (w->x - rect.left);
	rect.top  = w->y + (w->y - rect.top);
	SetCursorPos(rect.left + x, rect.top + y);
}

void zdl_window_swap(zdl_window_t w)
{
	SwapBuffers(w->hDeviceContext);
}

void zdl_window_set_title(zdl_window_t w, const char *icon, const char *name)
{
	if (name == NULL && icon == NULL)
		return;

	if (name == NULL)
		name = icon;
	else if (icon == NULL)
		icon = name;

	SetWindowText(w->window, name);
}

int zdl_win32_entry(int (* main)(int, char **))
{
	char *argv[256];
	const char *cmdline = (const char *)GetCommandLineA();
	char *dup = _strdup(cmdline);
	char *tok;
	int rc, i;
	int len;

	len = strlen(dup);
	while (len > 0 && dup[len - 1] != '"') --len;
	if (len == 0)
		return 127;
	dup[len - 1] = 0;
	argv[0] = strtok_s(dup + 1, " \t", &tok);
	for (i = 0; argv[i] != NULL; ++i)
		argv[i + 1] = strtok_s(NULL, " \t", &tok);

	rc = main(i, argv);

	free(dup);
	return rc;
}
