/*
 * Copyright (c) 2012, Courtney Cavin
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted without restriction for header files.
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

#pragma once

#ifdef __cplusplus
#define ZDL_EXTERN extern "C"
#else
#define ZDL_EXTERN
#endif

#ifdef _WIN32
#ifdef ZDL_INTERNAL
#define ZDL_EXPORT ZDL_EXTERN __declspec(dllexport)
#else
#define ZDL_EXPORT ZDL_EXTERN __declspec(dllimport)
#endif
#else
#define ZDL_EXPORT ZDL_EXTERN
#endif

#ifdef _WIN32
ZDL_EXPORT int zdl_win32_entry(int (* main)(int, char **));
#define ZDL_MAIN_FIXUP \
  int __stdcall WinMain(HINSTANCE i, HINSTANCE p, LPSTR txt, int cmd) \
  { return zdl_win32_entry(main); }
#else
#define ZDL_MAIN_FIXUP
#endif

/**< Window flags */
enum zdl_flag_enum {
	ZDL_FLAG_NONE       = 0,        /**< No flags */
	ZDL_FLAG_FULLSCREEN = (1 << 0), /**< Fullscreen window */
	ZDL_FLAG_NORESIZE   = (1 << 1), /**< Non-resizeable window */
	ZDL_FLAG_NOCURSOR   = (1 << 2), /**< Invisible cursor */
	ZDL_FLAG_NODECOR    = (1 << 3), /**< No window decoration */
	ZDL_FLAG_CLIPBOARD  = (1 << 4), /**< Enable clipboard operation */
	ZDL_FLAG_COPYONHL   = (1 << 5), /**< Copy on highlight (Read-Only) */
	ZDL_FLAG_FLIP_Y     = (1 << 6), /**< Y-axis is flipped (Read-Only) */
};
/**< Window flag bitmask */
typedef unsigned int zdl_flags_t;

#define zdl_bitmask_set(mask,bit) ((mask) |  (bit))
#define zdl_bitmask_clr(mask,bit) ((mask) & ~(bit))
#define zdl_bitmask_tgl(mask,bit) ((mask) ^  (bit))
#define zdl_bitmask_bool(mask,bit,bool) \
  ((bool)?((mask)|(bit)):((mask)&~(bit)))

/** Key modifier */
enum zdl_keymod_enum {
	ZDL_KEYMOD_NONE   = 0,         /**< No keys pressed */
	ZDL_KEYMOD_LSHIFT = (1 <<  0), /**< Left shift */
	ZDL_KEYMOD_RSHIFT = (1 <<  1), /**< Right shift */
	ZDL_KEYMOD_LCTRL  = (1 <<  6), /**< Left control */
	ZDL_KEYMOD_RCTRL  = (1 <<  7), /**< Right control */
	ZDL_KEYMOD_LALT   = (1 <<  8), /**< Left alt */
	ZDL_KEYMOD_RALT   = (1 <<  9), /**< Right alt */
	ZDL_KEYMOD_LSUPER = (1 << 10), /**< Left "super" (Usually Windows(r) key) */
	ZDL_KEYMOD_RSUPER = (1 << 11), /**< Right "super" */
	ZDL_KEYMOD_LHYPER = (1 << 12), /**< Left hyper */
	ZDL_KEYMOD_RHYPER = (1 << 13), /**< Right hyper */
	ZDL_KEYMOD_LMETA  = (1 << 14), /**< Left meta */
	ZDL_KEYMOD_RMETA  = (1 << 15), /**< Right meta */
	ZDL_KEYMOD_NUM    = (1 << 16), /**< Num Lock */
	ZDL_KEYMOD_CAPS   = (1 << 17), /**< Caps Lock */
	ZDL_KEYMOD_SCROLL = (1 << 18), /**< Scroll Lock */
	ZDL_KEYMOD_MODE   = (1 << 19), /**< Mode switch */

	ZDL_KEYMOD_CTRL  = (ZDL_KEYMOD_LCTRL  | ZDL_KEYMOD_RCTRL),  /**< Control mask */
	ZDL_KEYMOD_SHIFT = (ZDL_KEYMOD_LSHIFT | ZDL_KEYMOD_RSHIFT), /**< Shift mask */
	ZDL_KEYMOD_ALT   = (ZDL_KEYMOD_LALT   | ZDL_KEYMOD_RALT),   /**< Alt mask */
	ZDL_KEYMOD_META  = (ZDL_KEYMOD_LMETA  | ZDL_KEYMOD_RMETA),  /**< Meta mask */
	ZDL_KEYMOD_SUPER = (ZDL_KEYMOD_LSUPER | ZDL_KEYMOD_RSUPER), /**< Super mask */
	ZDL_KEYMOD_HYPER = (ZDL_KEYMOD_LHYPER | ZDL_KEYMOD_RHYPER), /**< Hyper mask */
};
/**< Key modifier bitmask */
typedef unsigned int zdl_keymod_t;

/** Key symbol */
enum zdl_keysym {
	ZDL_KEYSYM_BACKSPACE,
	ZDL_KEYSYM_TAB,
	ZDL_KEYSYM_CLEAR,
	ZDL_KEYSYM_RETURN,
	ZDL_KEYSYM_PAUSE,
	ZDL_KEYSYM_ESCAPE,
	ZDL_KEYSYM_SPACE,
	ZDL_KEYSYM_EXCLAIM,
	ZDL_KEYSYM_QUOTEDBL,
	ZDL_KEYSYM_HASH,
	ZDL_KEYSYM_DOLLAR,
	ZDL_KEYSYM_PERCENT,
	ZDL_KEYSYM_AMPERSAND,
	ZDL_KEYSYM_QUOTE,
	ZDL_KEYSYM_LEFTPAREN,
	ZDL_KEYSYM_RIGHTPAREN,
	ZDL_KEYSYM_ASTERISK,
	ZDL_KEYSYM_PLUS,
	ZDL_KEYSYM_COMMA,
	ZDL_KEYSYM_MINUS,
	ZDL_KEYSYM_PERIOD,
	ZDL_KEYSYM_SLASH,
	ZDL_KEYSYM_0,
	ZDL_KEYSYM_1,
	ZDL_KEYSYM_2,
	ZDL_KEYSYM_3,
	ZDL_KEYSYM_4,
	ZDL_KEYSYM_5,
	ZDL_KEYSYM_6,
	ZDL_KEYSYM_7,
	ZDL_KEYSYM_8,
	ZDL_KEYSYM_9,
	ZDL_KEYSYM_COLON,
	ZDL_KEYSYM_SEMICOLON,
	ZDL_KEYSYM_LESS,
	ZDL_KEYSYM_EQUALS,
	ZDL_KEYSYM_GREATER,
	ZDL_KEYSYM_QUESTION,
	ZDL_KEYSYM_AT,
	ZDL_KEYSYM_LEFTBRACKET,
	ZDL_KEYSYM_BACKSLASH,
	ZDL_KEYSYM_RIGHTBRACKET,
	ZDL_KEYSYM_CARET,
	ZDL_KEYSYM_UNDERSCORE,
	ZDL_KEYSYM_BACKQUOTE,
	ZDL_KEYSYM_A,
	ZDL_KEYSYM_B,
	ZDL_KEYSYM_C,
	ZDL_KEYSYM_D,
	ZDL_KEYSYM_E,
	ZDL_KEYSYM_F,
	ZDL_KEYSYM_G,
	ZDL_KEYSYM_H,
	ZDL_KEYSYM_I,
	ZDL_KEYSYM_J,
	ZDL_KEYSYM_K,
	ZDL_KEYSYM_L,
	ZDL_KEYSYM_M,
	ZDL_KEYSYM_N,
	ZDL_KEYSYM_O,
	ZDL_KEYSYM_P,
	ZDL_KEYSYM_Q,
	ZDL_KEYSYM_R,
	ZDL_KEYSYM_S,
	ZDL_KEYSYM_T,
	ZDL_KEYSYM_U,
	ZDL_KEYSYM_V,
	ZDL_KEYSYM_W,
	ZDL_KEYSYM_X,
	ZDL_KEYSYM_Y,
	ZDL_KEYSYM_Z,
	ZDL_KEYSYM_DELETE,
	ZDL_KEYSYM_KEYPAD_0,
	ZDL_KEYSYM_KEYPAD_1,
	ZDL_KEYSYM_KEYPAD_2,
	ZDL_KEYSYM_KEYPAD_3,
	ZDL_KEYSYM_KEYPAD_4,
	ZDL_KEYSYM_KEYPAD_5,
	ZDL_KEYSYM_KEYPAD_6,
	ZDL_KEYSYM_KEYPAD_7,
	ZDL_KEYSYM_KEYPAD_8,
	ZDL_KEYSYM_KEYPAD_9,
	ZDL_KEYSYM_KEYPAD_PERIOD,
	ZDL_KEYSYM_KEYPAD_DIVIDE,
	ZDL_KEYSYM_KEYPAD_MULTIPLY,
	ZDL_KEYSYM_KEYPAD_MINUS,
	ZDL_KEYSYM_KEYPAD_PLUS,
	ZDL_KEYSYM_KEYPAD_ENTER,
	ZDL_KEYSYM_KEYPAD_EQUALS,
	ZDL_KEYSYM_UP,
	ZDL_KEYSYM_DOWN,
	ZDL_KEYSYM_RIGHT,
	ZDL_KEYSYM_LEFT,
	ZDL_KEYSYM_INSERT,
	ZDL_KEYSYM_HOME,
	ZDL_KEYSYM_END,
	ZDL_KEYSYM_PAGEUP,
	ZDL_KEYSYM_PAGEDOWN,
	ZDL_KEYSYM_F1,
	ZDL_KEYSYM_F2,
	ZDL_KEYSYM_F3,
	ZDL_KEYSYM_F4,
	ZDL_KEYSYM_F5,
	ZDL_KEYSYM_F6,
	ZDL_KEYSYM_F7,
	ZDL_KEYSYM_F8,
	ZDL_KEYSYM_F9,
	ZDL_KEYSYM_F10,
	ZDL_KEYSYM_F11,
	ZDL_KEYSYM_F12,
	ZDL_KEYSYM_F13,
	ZDL_KEYSYM_F14,
	ZDL_KEYSYM_F15,
	ZDL_KEYSYM_NUMLOCK,
	ZDL_KEYSYM_CAPSLOCK,
	ZDL_KEYSYM_SCROLLLOCK,
	ZDL_KEYSYM_RSHIFT,
	ZDL_KEYSYM_LSHIFT,
	ZDL_KEYSYM_RCTRL,
	ZDL_KEYSYM_LCTRL,
	ZDL_KEYSYM_RALT,
	ZDL_KEYSYM_LALT,
	ZDL_KEYSYM_RMETA,
	ZDL_KEYSYM_LMETA,
	ZDL_KEYSYM_LSUPER,
	ZDL_KEYSYM_RSUPER,
	ZDL_KEYSYM_MODE,
	ZDL_KEYSYM_COMPOSE,
	ZDL_KEYSYM_HELP,
	ZDL_KEYSYM_PRINT,
	ZDL_KEYSYM_SYSREQ,
	ZDL_KEYSYM_BREAK,
	ZDL_KEYSYM_MENU,
	ZDL_KEYSYM_POWER,
	ZDL_KEYSYM_EURO,
	ZDL_KEYSYM_UNDO,
};

/** Motion identifier */
enum zdl_motion_id {
	ZDL_MOTION_POINTER,           /**< Classic pointer device */

	ZDL_MOTION_TOUCH_START = 1,   /**< Touch finger #0 */
	/* ZDL_MOTION_TOUCH_FINGER<n> */
	ZDL_MOTION_TOUCH_END   = 255, /**< Touch finger #255 */

	ZDL_MOTION_HOVER_START = 256, /**< Hover finger #0 */
	/* ZDL_MOTION_HOVER_FINGER<n> */
	ZDL_MOTION_HOVER_END   = 511, /**< Hover finger #255 */
};

/** Motion flags */
enum zdl_motion_flag_enum {
	ZDL_MOTION_FLAG_NONE    = 0,        /**< Normal motion event */
	ZDL_MOTION_FLAG_INITIAL = (1 << 0), /**< Initial motion for id (eg finger down) */
	ZDL_MOTION_FLAG_FINAL   = (1 << 1), /**< Final motion for id (eg finger up) */
};
/** Motion flag bitmask */
typedef unsigned int zdl_motion_flags_t;

/** Button identifier */
enum zdl_button {
	ZDL_BUTTON_LEFT   = 1, /**< Left button */
	ZDL_BUTTON_RIGHT  = 2, /**< Right button */
	ZDL_BUTTON_MIDDLE = 3, /**< Middle */
	ZDL_BUTTON_MWDOWN = 4, /**< Mouse-wheel down */
	ZDL_BUTTON_MWUP   = 5, /**< Mouse-wheel up */
};

/** Event type */
enum zdl_event_type {
	ZDL_EVENT_KEYPRESS,      /**< Key was pressed */
	ZDL_EVENT_KEYRELEASE,    /**< Key was released */

	ZDL_EVENT_BUTTONPRESS,   /**< Button was pressed */
	ZDL_EVENT_BUTTONRELEASE, /**< Button was released */

	ZDL_EVENT_MOTION,        /**< Motion (touch/pointer) */

	ZDL_EVENT_GAINFOCUS,
	ZDL_EVENT_LOSEFOCUS,

	ZDL_EVENT_RECONFIGURE,   /**< Window was reconfigured */
	ZDL_EVENT_EXPOSE,        /**< Window should be redrawn */
	ZDL_EVENT_HIDE,          /**< Window was hidden */
	ZDL_EVENT_ERROR,         /**< Unrecoverable error happened */
	ZDL_EVENT_EXIT,          /**< Window manager requested exit */
	ZDL_EVENT_COPY,          /**< Window manager requested copy */
	ZDL_EVENT_PASTE,         /**< Window manager requested paste */
	ZDL_EVENT_CUT,           /**< Window manager requested cut */
};

/** Event */
struct zdl_event {
	enum zdl_event_type type; /**< Event type */

	union {
		/** Key event */
		struct {
			enum zdl_keysym sym;      /**< Key symbol */
			zdl_keymod_t modifiers;   /**< Key modifier mask */
			unsigned short unicode;   /**< UTF-8 keycode */
			unsigned char scancode;   /**< Device specific scancode */
		} key;

		/** Button event */
		struct {
			enum zdl_button button;   /**< Button identifier */
			int x, y;                 /**< Event position */
		} button;

		/** Motion event */
		struct {
			enum zdl_motion_id id;    /**< Motion identifier */
			int x, y;                 /**< Event position */
			int d_x, d_y;             /**< Delta position */
			zdl_motion_flags_t flags; /**< Flags */
		} motion;

		/** Reconfigure event */
		struct {
			int width, height; /**< New dimensions */
		} reconfigure;
	};
};

/** Window handle */
typedef struct zdl_window *zdl_window_t;
/** Invalid window handle */
#define ZDL_WINDOW_INVALID ((zdl_window_t)0)

/** Create a new window.
 * @param width Width of client area desired.
 * @param height Height of client area desired.
 * @param flags Flags describing window behavior.
 * @return Newly created window handle on success, ZDL_WINDOW_INVALID on failure.
 */
ZDL_EXPORT zdl_window_t zdl_window_create(int width, int height, zdl_flags_t flags);

/** Destroy existing window.
 * @param w Window handle.
 */
ZDL_EXPORT void zdl_window_destroy(zdl_window_t w);

/** Set window title.
 * @param w Window handle.
 * @param icon Window icon name.
 * @param name Window name.
 */
ZDL_EXPORT void zdl_window_set_title(zdl_window_t w, const char *icon, const char *name);

/** Set window flags.
 * @param w Window handle.
 * @param flags Desired behavioral flags.
 */
ZDL_EXPORT void zdl_window_set_flags(zdl_window_t w, zdl_flags_t flags);

/** Get window flags.
 * @param w Window handle.
 * @return Behavioral flags.
 */
ZDL_EXPORT zdl_flags_t zdl_window_get_flags(const zdl_window_t w);

/** Set window size.
 * @param w Window handle.
 * @param width Desired window width.
 * @param height Desired window height.
 */
ZDL_EXPORT void zdl_window_set_size(zdl_window_t w, int width, int height);

/** Get window size.
 * @param w Window handle.
 * @param width Pointer to where window width should be stored.
 * @param height Pointer to where window height should be stored.
 */
ZDL_EXPORT void zdl_window_get_size(const zdl_window_t w, int *width, int *height);

/** Set window position.
 * @param w Window handle.
 * @param width Desired window x position.
 * @param height Desired window y position.
 */
ZDL_EXPORT void zdl_window_set_position(zdl_window_t w, int x, int y);

/** Get window position.
 * @param w Window handle.
 * @param width Pointer to where window x position should be stored.
 * @param height Pointer to where window y position should be stored.
 */
ZDL_EXPORT void zdl_window_get_position(const zdl_window_t w, int *x, int *y);

/** Set window cursor visibility.
 * @param w Window handle.
 * @param shown Whether to show the cursor in the window.
 */
#define zdl_window_show_cursor(w, shown) \
  zdl_window_set_flags(w, zdl_bitmask_bool(zdl_window_get_flags(w),ZDL_FLAG_NOCURSOR,!(shown)))

/** Set window fullscreen state.
 * @param w Window handle.
 * @param fullscreen Whether to the window should be fullscreen.
 */
#define zdl_window_set_fullscreen(w, fullscreen) \
  zdl_window_set_flags(w, zdl_bitmask_bool(zdl_window_get_flags(w),ZDL_FLAG_FULLSCREEN,fullscreen))

/** Set window decoration state.
 * @param w Window handle.
 * @param enabled Whether to the window should be decorated.
 */
#define zdl_window_set_decor(w, enabled) \
  zdl_window_set_flags(w, zdl_bitmask_bool(zdl_window_get_flags(w),ZDL_FLAG_NODECOR,!(enabled)))

/** Set window resize capability.
 * @param w Window handle.
 * @param enabled Whether to the window should be resizeable.
 */
#define zdl_window_set_resize(w, enabled) \
  zdl_window_set_flags(w, zdl_bitmask_bool(zdl_window_get_flags(w),ZDL_FLAG_NORESIZE,!(enabled)))

/** Set window clipboard capability.
 * @param w Window handle.
 * @param enabled Whether the window should handle clipboard operations.
 */
#define zdl_window_set_clipboard(w, enabled) \
  zdl_window_set_flags(w, zdl_bitmask_bool(zdl_window_get_flags(w),ZDL_FLAG_CLIPBOARD,enabled))


/** Poll for window events.
 * @param w Window handle.
 * @param ev Pointer to event structure to fill-out
 * @return 0 on event, !0 if no event.
 */
ZDL_EXPORT int  zdl_window_poll_event(zdl_window_t w, struct zdl_event *ev);

/** Wait for window events.
 * @param w Window handle.
 * @param ev Pointer to event structure to fill-out
 */
ZDL_EXPORT void zdl_window_wait_event(zdl_window_t w, struct zdl_event *ev);

/** Swap window buffers.
 * @param w Window handle.
 */
ZDL_EXPORT void zdl_window_swap(zdl_window_t w);

/** Clipboard handle. */
typedef struct zdl_clipboard *zdl_clipboard_t;

/** Invalid clipboard handle. */
#define ZDL_CLIPBOARD_INVALID ((zdl_clipboard_t)0)

/** Clipboard format */
enum zdl_clipboard_format {
	ZDL_CLIPBOARD_TEXT,  /**< Plain UTF-8 text. */
	ZDL_CLIPBOARD_IMAGE, /**< ARGB8888 pixels. */
	ZDL_CLIPBOARD_URI,   /**< ASCII URI. */
};

/** Clipboard data */
struct zdl_clipboard_data {
	enum zdl_clipboard_format format; /**< Clipboard format. */
	union {
		struct {
			const char *text; /**< NULL terminated UTF-8. */
		} text;
		struct {
			const unsigned int *pixels; /**< Image pixels. */
			int width; /**< Image width. */
			int height; /**< Image height. */
		} image;
		struct {
			const char *uri; /**< NULL terminated ASCII uri */
		} uri;
	};
};

/** Open the window manager's clipboard.
 * @param w Window handle.
 * @return Clipboard handle on success, ZDL_CLIPBOARD_INVALID on failure.
 */
ZDL_EXPORT zdl_clipboard_t zdl_clipboard_open(zdl_window_t w);

/** Close clipboard.
 * @param c Clipboard handle.
 */
ZDL_EXPORT void zdl_clipboard_close(zdl_clipboard_t c);

/** Write to clipboard.
 * @param c Clipboard handle.
 * @param data Pointer to clipboard data.
 * @return 0 on success, !0 on failure.
 */
ZDL_EXPORT int zdl_clipboard_write(zdl_clipboard_t c, const struct zdl_clipboard_data *data);

/** Read from clipboard.
 * @param c Clipboard handle.
 * @param data Pointer where clipboard data should be written.
 * @return 0 on success, !0 on failure.
 */
ZDL_EXPORT int zdl_clipboard_read(zdl_clipboard_t c, struct zdl_clipboard_data *data);

#ifdef __cplusplus
namespace ZDL {

class Clipboard {
public:
	Clipboard(zdl_window_t w)
	{
		m_clip = zdl_clipboard_open(w);
		if (m_clip == 0) throw 0;
	}
	~Clipboard()
	{ zdl_clipboard_close(m_clip); }

	int read(struct zdl_clipboard_data *data)
	{ return zdl_clipboard_read(m_clip, data); }

	int write(const struct zdl_clipboard_data *data)
	{ return zdl_clipboard_write(m_clip, data); }

private:
	zdl_clipboard m_clip;
};

class Window {
public:
	Window(int width, int height, int flags)
	{
		m_win = zdl_window_create(width, height, flags);
		if (m_win == 0) throw 0;
	}
	~Window()
	{ zdl_window_destroy(m_win); }

	void setTitle(const char *icon, const char *name = 0)
	{ zdl_window_set_title(m_win, icon, name); }

	void setFlags(zdl_flags_t flags)
	{ zdl_window_set_flags(m_win, flags); }
	zdl_flags_t getFlags(void) const
	{ return zdl_window_get_flags(m_win); }

	void setSize(int w, int h)
	{ zdl_window_set_size(m_win, w, h); }
	void getSize(int *w, int *h) const
	{ zdl_window_get_size(m_win, w, h); }

	void showCursor(bool show)
	{ return zdl_window_show_cursor(m_win, show); }
	void setFullscreen(bool fullscreen)
	{ return zdl_window_set_fullscreen(m_win, fullscreen); }
	void setDecor(bool enabled)
	{ return zdl_window_set_decor(m_win, enabled); }
	void setResize(bool enabled)
	{ return zdl_window_set_resize(m_win, enabled); }
	void setClipboard(bool enabled)
	{ return zdl_window_set_clipboard(m_win, enabled); }

	int pollEvent(struct zdl_event *ev)
	{ return zdl_window_poll_event(m_win, ev); }

	void waitEvent(struct zdl_event *ev)
	{ zdl_window_wait_event(m_win, ev); }

	void swap(void)
	{ zdl_window_swap(m_win); }

	Clipboard *getClipboard(void)
	{
		return new Clipboard(m_win);
	}

private:
	zdl_window_t m_win;
};

}
#endif
