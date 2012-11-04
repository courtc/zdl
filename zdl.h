#pragma once

#ifdef _WIN32
#ifdef ZDL_INTERNAL
#define ZDL_EXPORT __declspec(dllexport)
#else
#define ZDL_EXPORT __declspec(dllimport)
#endif
#else
#define ZDL_EXPORT
#endif

enum zdl_keymod {
	ZDL_KEYMOD_NONE   = 0,
	ZDL_KEYMOD_LSHIFT = (1 <<  0),
	ZDL_KEYMOD_RSHIFT = (1 <<  1),
	ZDL_KEYMOD_LCTRL  = (1 <<  6),
	ZDL_KEYMOD_RCTRL  = (1 <<  7),
	ZDL_KEYMOD_LALT   = (1 <<  8),
	ZDL_KEYMOD_RALT   = (1 <<  9),
	ZDL_KEYMOD_LSUPER = (1 << 10),
	ZDL_KEYMOD_RSUPER = (1 << 11),
	ZDL_KEYMOD_LHYPER = (1 << 12),
	ZDL_KEYMOD_RHYPER = (1 << 13),
	ZDL_KEYMOD_LMETA  = (1 << 14),
	ZDL_KEYMOD_RMETA  = (1 << 15),
	ZDL_KEYMOD_NUM    = (1 << 16),
	ZDL_KEYMOD_CAPS   = (1 << 17),
	ZDL_KEYMOD_SCROLL = (1 << 18),
	ZDL_KEYMOD_MODE   = (1 << 19),

	ZDL_KEYMOD_CTRL  = (ZDL_KEYMOD_LCTRL  | ZDL_KEYMOD_RCTRL),
	ZDL_KEYMOD_SHIFT = (ZDL_KEYMOD_LSHIFT | ZDL_KEYMOD_RSHIFT),
	ZDL_KEYMOD_ALT   = (ZDL_KEYMOD_LALT   | ZDL_KEYMOD_RALT),
	ZDL_KEYMOD_META  = (ZDL_KEYMOD_LMETA  | ZDL_KEYMOD_RMETA),
	ZDL_KEYMOD_SUPER = (ZDL_KEYMOD_LSUPER | ZDL_KEYMOD_RSUPER),
	ZDL_KEYMOD_HYPER = (ZDL_KEYMOD_LHYPER | ZDL_KEYMOD_RHYPER),
};

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

enum zdl_event_type {
	ZDL_EVENT_KEYPRESS,
	ZDL_EVENT_KEYRELEASE,

	ZDL_EVENT_BUTTONPRESS,
	ZDL_EVENT_BUTTONRELEASE,

	ZDL_EVENT_MOTION,

	ZDL_EVENT_GAINFOCUS,
	ZDL_EVENT_LOSEFOCUS,

	ZDL_EVENT_RECONFIGURE,
	ZDL_EVENT_EXPOSE,
	ZDL_EVENT_ERROR,
	ZDL_EVENT_EXIT,
};

struct zdl_event {
	enum zdl_event_type type;

	union {
		struct {
			enum zdl_keysym sym;
			unsigned int  modifiers;
			unsigned short unicode;
			unsigned char scancode;
		} key;
		struct {
			int button;
			int x, y;
		} button;
		struct {
			int x, y;
			int d_x, d_y;
		} motion;
		struct {
			int width, height;
		} reconfigure;
	};
};

typedef struct zdl_window *zdl_window_t;
#define ZDL_WINDOW_INVALID ((zdl_window_t)0)

#ifdef __cplusplus
extern "C" {
#endif

ZDL_EXPORT zdl_window_t zdl_window_create(int width, int height, int fullscreen);
ZDL_EXPORT void zdl_window_destroy(zdl_window_t w);
ZDL_EXPORT void zdl_window_set_title(zdl_window_t w, const char *icon, const char *name);
ZDL_EXPORT void zdl_window_set_fullscreen(zdl_window_t w, int fullscreen);
ZDL_EXPORT int  zdl_window_get_fullscreen(const zdl_window_t w);
ZDL_EXPORT void zdl_window_set_size(zdl_window_t w, int width, int height);
ZDL_EXPORT void zdl_window_get_size(const zdl_window_t w, int *width, int *height);
ZDL_EXPORT void zdl_window_show_cursor(zdl_window_t w, int shown);
ZDL_EXPORT int  zdl_window_poll_event(zdl_window_t w, struct zdl_event *ev);
ZDL_EXPORT void zdl_window_swap(zdl_window_t w);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
namespace ZDL {

class Window {
public:
	Window(int width, int height, bool fullscreen)
	{
		m_win = zdl_window_create(width, height, fullscreen);
		if (m_win == NULL) throw 0;
	}
	~Window()
	{ zdl_window_destroy(m_win); }

	void setTitle(const char *icon, const char *name = NULL)
	{ zdl_window_set_title(m_win, icon, name); }

	void setFullscreen(bool fullscreen)
	{ zdl_window_set_fullscreen(m_win, fullscreen); }
	bool getFullscreen(void) const
	{ return !!zdl_window_get_fullscreen(m_win); }

	void setSize(int w, int h)
	{ zdl_window_set_size(m_win, w, h); }
	void getSize(int *w, int *h) const
	{ zdl_window_get_size(m_win, w, h); }

	void showCursor(bool show)
	{ return zdl_window_show_cursor(m_win, show); }

	int pollEvent(struct zdl_event *ev)
	{ return zdl_window_poll_event(m_win, ev); }

	void swap(void)
	{ zdl_window_swap(m_win); }

private:
	zdl_window_t m_win;
};

}
#endif
