/*  $Id$

    Part of SWI-Prolog

    Author:        Jan Wielemaker
    E-mail:        jan@swi.psy.uva.nl
    WWW:           http://www.swi-prolog.org
    Copyright (C): 1985-2002, University of Amsterdam

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
This file defines a console for porting (unix) stream-based applications
to MS-Windows. It has been developed for  SWI-Prolog. The main source is
part of SWI-Prolog.

The SWI-Prolog source is at http://www.swi-prolog.org
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Thread design:

<written as a mail to Lutz Wohlrab>

There are two threads. The Prolog engine   runs  in the main thread. The
other thread deals with the window.   Basically, it processes events and
if anything is typed it puts it into a queue.

The main thread  at  some  stage   forks  the  display  thread,  running
window_loop().  This  thread  initialises  the   input  and  then  sends
WM_RLC_READY to the main thread to indicate it is ready to accept data.

If data is to be written,  Prolog   calls  rlc_write(),  which posts the
WM_RLC_WRITE to the display thread, waiting  on the termination. If data
is to be read, rlc_read() posts  a   WM_RLC_FLUSH,  and then waits while
dispatching events, for the display-thread to   fill the buffer and send
WM_RLC_INPUT (which is just sent  to   make  GetMessage()  in rlc_read()
return).

Towards an MT version on Windows
--------------------------------

If we want to move towards a  multi-threaded version for MS-Windows, the
console code needs to be changed significantly, as we need to be able to
create multiple consoles to support thread_attach_console/0.

The most logical solution seems to   be to reverse the thread-structure,
Prolog starting and running in the   main-thread  and creating a console
creates a new thread for this console. There  are two ways to keep track
of the console to use. Cleanest might be to add an argument denoting the
allocated console and alternatively we could   use thread-local data. We
can also combine the two: add an  additional argument, but allow passing
NULL to use the default console for this thread.

Menus
-----

The current console provides a menu that can be extended from Prolog.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#ifdef _DEBUG
#define O_DEBUG 1
static void initHeapDebug(void);
#include <crtdbg.h>
#else
#define initHeapDebug()
#endif

#include <windows.h>
#ifndef WM_MOUSEWHEEL			/* sometimes not defined */
#define WM_MOUSEWHEEL 0x020A
#endif

#include <stdlib.h>
#include <io.h>
#include <string.h>
#include <malloc.h>
#define _MAKE_DLL 1
#undef _export
#include "console.h"
#include "menu.h"
#include "common.h"
#include <signal.h>
#include <ctype.h>
#include <stdio.h>

#ifndef isletter
#define isletter(c) (isalpha(c) || (c) == '_')
#endif

#ifndef MAXPATHLEN
#define MAXPATHLEN 256
#endif

#ifndef CHAR_MAX
#define CHAR_MAX 256
#endif

#define MAXLINE	     1024		/* max chars per line */

#define CMD_INITIAL	0
#define CMD_ESC		1
#define CMD_ANSI	2

#define GWL_DATA	0		/* offset for client data */

#define CHG_RESET	0		/* unchenged */
#define CHG_CHANGED	1		/* changed, but no clear */
#define CHG_CLEAR	2		/* clear */
#define CHG_CARET	4		/* caret has moved */

#define SEL_CHAR	0		/* character-unit selection */
#define SEL_WORD	1		/* word-unit selection */
#define SEL_LINE	2		/* line-unit selection */

#ifndef EOS
#define EOS 0
#endif

#define ESC 27				/* the escape character */

#define WM_RLC_INPUT	 WM_USER+10	/* Just somewhere ... */
#define WM_RLC_WRITE	 WM_USER+11	/* write data */
#define WM_RLC_FLUSH	 WM_USER+12	/* flush buffered data */
#define WM_RLC_READY	 WM_USER+13	/* Window thread is ready */
#define WM_RLC_CLOSEWIN  WM_USER+14	/* Close the window */
/*#define WM_RLC_MENU	 WM_USER+15	   Insert a menu (defined in menu.h) */

#define IMODE_RAW	1		/* char-by-char */
#define IMODE_COOKED	2		/* line-by-line */

#define NextLine(b, i) ((i) < (b)->height-1 ? (i)+1 : 0)
#define PrevLine(b, i) ((i) > 0 ? (i)-1 : (b)->height-1)
#define Bounds(v, mn, mx) ((v) < (mn) ? (mn) : (v) > (mx) ? (mx) : (v))

#define Control(x) ((x) - '@')

#define streq(s, q) (strcmp((s), (q)) == 0)

#include "console_i.h"			/* internal package stuff */

#define OPT_SIZE	0x01
#define OPT_POSITION	0x02

		 /*******************************
		 *	       DATA		*
		 *******************************/

       RlcData  _rlc_stdio = NULL;	/* the main buffer */
static int      _rlc_show;		/* initial show */
static char	_rlc_word_chars[CHAR_MAX]; /* word-characters (selection) */
static const char *	_rlc_program;		/* name of the program */
static HANDLE   _rlc_hinstance;		/* Global instance */
static HICON    _rlc_hicon;		/* Global icon */



		 /*******************************
		 *	     FUNCTIONS		*
		 *******************************/

static WINAPI	rlc_wnd_proc(HWND win, UINT msg, UINT wP, LONG lP);

static void	rlc_place_caret(RlcData b);
static void	rlc_resize_pixel_units(RlcData b, int w, int h);
static RlcData	rlc_make_buffer(int w, int h);
static int	rlc_count_lines(RlcData b, int from, int to);
static void	rlc_add_line(RlcData b);
static void	rlc_open_line(RlcData b);
static void	rlc_update_scrollbar(RlcData b);
static void	rlc_paste(RlcData b);
static void	rlc_init_text_dimensions(RlcData b, HFONT f);
static void	rlc_save_font_options(HFONT f, rlc_console_attr *attr);
static void	rlc_get_options(rlc_console_attr *attr);
static HKEY	rlc_option_key(rlc_console_attr *attr, int create);
static void	rlc_progbase(char *path, char *base);
static int	rlc_add_queue(RlcData b, RlcQueue q, int chr);
static int	rlc_add_lines(RlcData b, int here, int add);
static void	rlc_start_selection(RlcData b, int x, int y);
static void	rlc_extend_selection(RlcData b, int x, int y);
static void	rlc_word_selection(RlcData b, int x, int y);
static void	rlc_copy(RlcData b);
static void	rlc_destroy(RlcData b);
static void	rlc_request_redraw(RlcData b);
static void	rlc_redraw(RlcData b);
static int	rlc_breakargs(char *program, char *line, char **argv);
static void	rlc_resize(RlcData b, int w, int h);
static void	rlc_adjust_line(RlcData b, int line);
static int	text_width(RlcData b, HDC hdc, const char *text, int len);
static void	rlc_queryfont(RlcData b);
static void     rlc_do_write(RlcData b, char *buf, int count);
static void     rlc_reinit_line(RlcData b, int line);
static void	rlc_free_line(RlcData b, int line);
static int	rlc_between(RlcData b, int f, int t, int v);
static void	free_user_data(RlcData b);

static RlcQueue	rlc_make_queue(int size);
static void	rlc_free_queue(RlcQueue q);
static int	rlc_from_queue(RlcQueue q);
static int	rlc_is_empty_queue(RlcQueue q);
static void	rlc_empty_queue(RlcQueue q);

extern int	main();

static RlcUpdateHook	_rlc_update_hook;
static RlcTimerHook	_rlc_timer_hook;
static RlcRenderHook	_rlc_render_hook;
static RlcRenderAllHook _rlc_render_all_hook;
static RlcInterruptHook _rlc_interrupt_hook;
static RlcResizeHook    _rlc_resize_hook;
static RlcMenuHook	_rlc_menu_hook;
static RlcMessageHook	_rlc_message_hook;
static int _rlc_copy_output_to_debug_output=0;	/* != 0: copy to debugger */
static int	emulate_three_buttons;
static HWND	emu_hwnd;		/* Emulating for this window */

static void _rlc_create_kill_window(RlcData b);
static DWORD WINAPI window_loop(LPVOID arg);	/* console window proc */

#ifdef O_DEBUG
#include <stdarg.h>
static void Dprintf(const char *fmt, ...);
static void Dprint_lines(RlcData b, int from, int to);
#define DEBUG(Code) Code

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
It might look a bit weird not to  use <assert.h>, but for some reason it
looks as if the application thread continues if the asserting is trapped
using  the  normal  assert()!?  Just  but    a  debugger  breakpoint  on
rlc_assert() and all functions normally.

rlc_check_assertions() is a (very) incomplete   check that everything we
expect to be true about the data is indeed the case.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

void
rlc_assert(const char *msg)
{ MessageBox(NULL, msg, "Console assertion failed", MB_OK|MB_TASKMODAL);
}

void
rlc_check_assertions(RlcData b)
{ int window_last = rlc_add_lines(b, b->window_start, b->window_size-1);
  int y;

  assert(b->last != b->first || b->first == 0);
  assert(b->caret_x >= 0 && b->caret_x < b->width);
					/* TBD: debug properly */
/*assert(rlc_between(b, b->window_start, window_last, b->caret_y));*/
  
  for(y=0; y<b->height; y++)
  { TextLine tl = &b->lines[y];

    assert(tl->size >= 0 && tl->size <= b->width);
  }
}

#else

#define DEBUG(Code) ((void)0)
#define rlc_check_assertions(b)
#endif

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
rlc_long_name(char *buffer)
	Translate a filename, possibly holding 8+3 abbreviated parts into
	the `real' filename.  I couldn't find a direct call for this.  If
	you have it, I'd be glad to receive a better implementation.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static void
rlc_long_name(char *file)
{ char buf[MAXPATHLEN];
  char *i = file;
  char *o = buf;
  char *ok = buf;
  int changed = 0;

  while(*i)
  { int dirty = FALSE;

    while(*i && *i != '\\')
    { if ( *i == '~' )
	dirty++;
      *o++ = *i++;
    }
    if ( dirty )
    { WIN32_FIND_DATA data;
      HANDLE h;

      *o = '\0';
      if ( (h=FindFirstFile(buf, &data)) != INVALID_HANDLE_VALUE )
      { strcpy(ok, data.cFileName);
	FindClose(h);
	o = ok + strlen(ok);
	changed++;
      }
    }
    if ( *i )
      *o++ = *i++;
    ok = o;
  }

  if ( changed )
  { *o = '\0';
    strcpy(file, buf);
  }
}


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
If %PLTERM_CLASS% is in the environment, this   value is used as Windows
class identifier for the console window.   This allows external programs
to start PLWIN.EXE and find the window it  has started in order to embed
it.

In old versions this was fixed to "RlcConsole"
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static char *
rlc_window_class(HICON icon)
{ static char winclassname[32];
  static WNDCLASS wndClass;
  HINSTANCE instance = _rlc_hinstance;

  if ( !winclassname[0] )
  { if ( !GetEnvironmentVariable("PLTERM_CLASS",
				 winclassname, sizeof(winclassname)) )
      sprintf(winclassname, "PlTerm-%d", instance);

    wndClass.lpszClassName	= winclassname;
    wndClass.style		= CS_HREDRAW|CS_VREDRAW|CS_DBLCLKS;
    wndClass.lpfnWndProc	= (LPVOID) rlc_wnd_proc;
    wndClass.cbClsExtra		= 0;
    wndClass.cbWndExtra		= sizeof(long);
    wndClass.hInstance		= instance;
    if ( icon )
      wndClass.hIcon		= icon;
    else
      wndClass.hIcon		= LoadIcon(NULL, IDI_APPLICATION);
    wndClass.hCursor		= LoadCursor(NULL, IDC_IBEAM);
    wndClass.hbrBackground	= (HBRUSH) NULL;
    wndClass.lpszMenuName	= NULL;

    RegisterClass(&wndClass);
  }

  return winclassname;
}



int
rlc_main(HANDLE hInstance, HANDLE hPrevInstance,
	 LPSTR lpszCmdLine, int nCmdShow,
	 RlcMain mainfunc, HICON icon)
{ char *	    argv[100];
  int		    argc;
  char		    program[MAXPATHLEN];
  char	 	    progbase[100];
  RlcData           b;
  rlc_console_attr  attr;

  initHeapDebug();

  _rlc_hinstance = hInstance;
  _rlc_show = nCmdShow;
  _rlc_hicon = icon;

  GetModuleFileName(hInstance, program, sizeof(program));
  rlc_long_name(program);
  rlc_progbase(program, progbase);

  memset(&attr, 0, sizeof(attr));
  _rlc_program = attr.title = progbase;
  _rlc_stdio = b = rlc_create_console(&attr);

  argc = rlc_breakargs(program, lpszCmdLine, argv);

  if ( mainfunc )
    return (*mainfunc)(b, argc, argv);
  else
    return 0;
}


rlc_console
rlc_create_console(rlc_console_attr *attr)
{ RlcData b;
  MSG msg;
  const char *title;

  rlc_get_options(attr);

  if ( attr->title )
    title = attr->title;
  else
    title = "Untitled";

  b = rlc_make_buffer(attr->width, attr->savelines);
  b->create_attributes = attr;
  strcpy(b->current_title, title);
  if ( attr->key )
  { b->regkey_name = strdup(attr->key);
  }

  rlc_init_text_dimensions(b, NULL);
  _rlc_create_kill_window(b);

  DuplicateHandle(GetCurrentProcess(),
		  GetCurrentThread(),
		  GetCurrentProcess(),
		  &b->application_thread,
		  0,
		  FALSE,
		  DUPLICATE_SAME_ACCESS);
  b->application_thread_id = GetCurrentThreadId();
  b->console_thread = CreateThread(NULL,			/* security */
				   2048,			/* stack */
				   window_loop, b,		/* proc+arg */
				   0,				/* flags */
				   &b->console_thread_id);	/* id */
					/* wait till the window is created */
  GetMessage(&msg, NULL, WM_RLC_READY, WM_RLC_READY);
  b->create_attributes = NULL;		/* release this data */

  return b;
}


static void
rlc_create_window(RlcData b)
{ HWND hwnd;
  rlc_console_attr *a = b->create_attributes;
  RECT rect;
  DWORD style = (WS_OVERLAPPEDWINDOW|WS_VSCROLL);

/* One would assume AdjustWindowRect() uses WS_VSCROLL to add the width of
   the scrollbar.  I think this isn't true, but maybe there is another reason
   for getting 2 characters shorter each invocation ...
*/

  rect.left   = a->x;
  rect.top    = a->y;
  rect.right  = a->x + (a->width+2) * b->cw + GetSystemMetrics(SM_CXVSCROLL);
  rect.bottom = a->y + a->height * b->ch;

  AdjustWindowRect(&rect, style, TRUE);
  hwnd = CreateWindow(rlc_window_class(_rlc_hicon), b->current_title,
		      style,
		      a->x, a->y,
		      rect.right - rect.left,
		      rect.bottom - rect.top,
		      NULL, NULL, _rlc_hinstance, NULL);

  b->window = hwnd;
  SetWindowLong(hwnd, GWL_DATA, (LONG) b);
  SetScrollRange(hwnd, SB_VERT, 0, b->sb_lines, FALSE);
  SetScrollPos(hwnd, SB_VERT, b->sb_start, TRUE);

  b->queue    = rlc_make_queue(256);
  b->sb_lines = rlc_count_lines(b, b->first, b->last); 
  b->sb_start = rlc_count_lines(b, b->first, b->window_start); 

  b->foreground = GetSysColor(COLOR_WINDOWTEXT);
  b->background = GetSysColor(COLOR_WINDOW);
  b->sel_foreground = GetSysColor(COLOR_HIGHLIGHTTEXT);
  b->sel_background = GetSysColor(COLOR_HIGHLIGHT);
  if ( GetSystemMetrics(SM_CMOUSEBUTTONS) == 2 )
    emulate_three_buttons = 120;

  rlc_add_menu_bar(b->window);

  ShowWindow(hwnd, _rlc_show);
  UpdateWindow(hwnd);
}


int
rlc_iswin32s()
{ if( GetVersion() & 0x80000000 && (GetVersion() & 0xFF) ==3)
    return TRUE;
  else
    return FALSE;
}


static void
rlc_progbase(char *path, char *base)
{ char *s;
  char *e;

  if ( !(s=strrchr(path, '\\')) )
    s = path;				/* takes the filename part */
  else
    s++;
  if ( !(e = strchr(s, '.')) )
    strcpy(base, s);
  else
  { memmove(base, s, e-s);
    base[e-s] = '\0';
  }
}

		 /*******************************
		 *	  HIDDEN WINDOW		*
		 *******************************/

static WINAPI
rlc_kill_wnd_proc(HWND hwnd, UINT message, UINT wParam, LONG lParam)
{ switch(message)
  { case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }

  return DefWindowProc(hwnd, message, wParam, lParam);
}

static char *
rlc_kill_window_class()
{ static char winclassname[32];
  static WNDCLASS wndClass;
  HINSTANCE instance = _rlc_hinstance;

  if ( !winclassname[0] )
  { sprintf(winclassname, "Console-hidden-win%d", instance);

    wndClass.style		= 0;
    wndClass.lpfnWndProc	= (LPVOID) rlc_kill_wnd_proc;
    wndClass.cbClsExtra		= 0;
    wndClass.cbWndExtra		= 0;
    wndClass.hInstance		= instance;
    wndClass.hIcon		= NULL;
    wndClass.hCursor		= NULL;
    wndClass.hbrBackground	= GetStockObject(WHITE_BRUSH);
    wndClass.lpszMenuName	= NULL;
    wndClass.lpszClassName	= winclassname;

    RegisterClass(&wndClass);
  }

  return winclassname;
}


static void
_rlc_create_kill_window(RlcData b)
{ b->kill_window = CreateWindow(rlc_kill_window_class(),
				"Console hidden window",
				0,
				0, 0, 32, 32,
				NULL, NULL, _rlc_hinstance, NULL);
}


		 /*******************************
		 *     REGISTRY COMMUNICATION	*
		 *******************************/

#define MAXREGSTRLEN 1024

static void
reg_save_int(HKEY key, const char *name, int value)
{ DWORD val = value;

  RegSetValueEx(key, name, 0,
		REG_DWORD_LITTLE_ENDIAN, (LPBYTE)&val, sizeof(val));
}

static void
reg_save_str(HKEY key, const char *name, char *value)
{ RegSetValueEx(key, name, 0, REG_SZ, (LPBYTE)value, strlen(value)+1);
}


static void
rlc_save_options(RlcData b)
{ HKEY key;
  rlc_console_attr attr;

  memset(&attr, 0, sizeof(attr));
  attr.key = b->regkey_name;

  if ( !(key = rlc_option_key(&attr, TRUE)) )
    return;

  reg_save_int(key, "SaveLines",  b->height);
  if ( b->modified_options & OPT_SIZE )
  { reg_save_int(key, "Width",    b->width);
    reg_save_int(key, "Height",   b->window_size);
  }
  if ( b->modified_options & OPT_POSITION )
  { reg_save_int(key, "X",	  b->win_x);
    reg_save_int(key, "Y",	  b->win_y);
  }

  rlc_save_font_options(b->hfont, &attr);
  if ( attr.face_name[0] )
  { reg_save_str(key, "FaceName",    attr.face_name);
    reg_save_int(key, "FontFamily",  attr.font_family);
    reg_save_int(key, "FontSize",    attr.font_size);
    reg_save_int(key, "FontWeight",  attr.font_weight);
    reg_save_int(key, "FontCharSet", attr.font_char_set);
  }

  RegCloseKey(key);
}


static void
reg_get_int(HKEY key, const char *name, int mn, int def, int mx, int *value)
{ DWORD type;
  BYTE  data[8];
  DWORD len = sizeof(data);

  if ( *value )
    return;				/* use default */

  if ( RegQueryValueEx(key, name, NULL, &type, data, &len) == ERROR_SUCCESS )
  { switch(type)
    { /*case REG_DWORD:*/		/* Same case !? */
      case REG_DWORD_LITTLE_ENDIAN:
      { DWORD *valp = (DWORD *)data;
	int v = *valp;

	if ( mn < mx )
	{ if ( v < mn )
	    v = mn;
	  else if ( v > mx )
	    v = mx;
	}
	  
	*value = v;
      }
    }
  } else
    *value = def;
}


static void
reg_get_str(HKEY key, const char *name, char *value, int length)
{ DWORD type;
  BYTE  data[MAXREGSTRLEN];
  DWORD len = sizeof(data);

  if ( *value )
    return;				/* use default */

  if ( RegQueryValueEx(key, name, NULL, &type, data, &len) == ERROR_SUCCESS )
  { switch(type)
    { case REG_SZ:
      { char *val = data;
	strncpy(value, val, length-1);
	value[length-1] = '\0';
      }
    }
  }
}


HKEY
reg_open_key(char **which, int create)
{ HKEY key = HKEY_CURRENT_USER;
  DWORD disp;
  LONG rval;

  for( ; *which; which++)
  { HKEY tmp;

    if ( which[1] )
    { if ( RegOpenKeyEx(key, which[0], 0L, KEY_READ, &tmp) == ERROR_SUCCESS )
      { key = tmp;
	continue;
      }

      if ( !create )
	return NULL;
    }

    rval = RegCreateKeyEx(key, which[0], 0, "", 0,
			  KEY_ALL_ACCESS, NULL, &tmp, &disp);
    RegCloseKey(key);
    if ( rval == ERROR_SUCCESS )
      key = tmp;
    else
      return NULL;
  }

  return key;
}


static HKEY
rlc_option_key(rlc_console_attr *attr, int create)
{ char Prog[256];
  char *address[] = { "Software",
  		      RLC_VENDOR,
		      Prog,
		      "Console",
		      (char *)attr->key,	/* possible secondary key */
		      NULL
		    };
  const char *s;
  char *q;

  for(s=_rlc_program, q=Prog; *s; s++, q++) /* capitalise the key */
  { *q = (s==_rlc_program ? toupper(*s) : tolower(*s));
  }
  *q = EOS;

  return reg_open_key(address, create);
}


static void
rlc_get_options(rlc_console_attr *attr)
{ HKEY key;

  if ( !(key = rlc_option_key(attr, FALSE)) )
  { if ( !attr->width  )    attr->width = 80;
    if ( !attr->height )    attr->height = 24;
    if ( !attr->savelines ) attr->savelines = 200;

    return;
  }

{ int minx, miny, maxx, maxy;
  RECT rect;

  SystemParametersInfo(SPI_GETWORKAREA, 0, &rect, 0);
  minx = rect.top;
  miny = rect.left;
  maxx = rect.right  - 40;
  maxy = rect.bottom - 40;

  reg_get_int(key, "SaveLines",   200,  200, 100000, &attr->savelines);
  reg_get_int(key, "Width",        20,	 80,    300, &attr->width);
  reg_get_int(key, "Height",        5,	 24,    100, &attr->height);
  reg_get_int(key, "X",		 minx, minx,   maxx, &attr->x);
  reg_get_int(key, "Y",	         miny, miny,   maxy, &attr->y);
}

  reg_get_str(key, "FaceName", attr->face_name, sizeof(attr->face_name));
  reg_get_int(key, "FontFamily",    0,  0,  0, &attr->font_family);
  reg_get_int(key, "FontSize",      0,  0,  0, &attr->font_size);
  reg_get_int(key, "FontWeight",    0,  0,  0, &attr->font_weight);
  reg_get_int(key, "FontCharSet",   0,  0,  0, &attr->font_char_set);

  RegCloseKey(key);
}



/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Windows-'95 appears to quote names of files   (I guess because files may
hold spaces).  rlc_breakargs()  will  pass  a   quoted  strings  as  one
argument.  If it can't find the closing   quote, it will tread the quote
as a normal character.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static int
rlc_breakargs(char *program, char *line, char **argv)
{ int argc = 1;

  argv[0] = program;

  while(*line)
  { int q;

    while(*line && isspace(*line))
      line++;

    if ( (q = *line) == '"' || q == '\'' )	/* quoted arguments */
    { char *start = line+1;
      char *end = start;

      while( *end && *end != q )
	end++;
      if ( *end == q )
      { *end = '\0';
        argv[argc++] = start;
	line = end+1;
	continue;
      }
    }

    if ( *line )
    { argv[argc++] = line;
      while(*line && !isspace(*line))
	line++;
      if ( *line )
	*line++ = '\0';
    }
  }
  argv[argc] = NULL;			/* add trailing NULL pointer to argv */

  return argc;
}      
    

		 /*******************************
		 *	     ATTRIBUTES		*
		 *******************************/

COLORREF
rlc_color(rlc_console con, int which, COLORREF c)
{ HDC hdc;
  COLORREF old;
  RlcData b = rlc_get_data(con);

  hdc = GetDC(NULL);
  c = GetNearestColor(hdc, c);
  ReleaseDC(NULL, hdc);

  switch(which)
  { case RLC_WINDOW:
      old = b->background;
      b->background = c;
      break;
    case RLC_TEXT:
      old = b->foreground;
      b->foreground = c;
      break;
    case RLC_HIGHLIGHT:
      old = b->sel_background;
      b->sel_background = c;
      break;
    case RLC_HIGHLIGHTTEXT:
      old = b->sel_foreground;
      b->sel_foreground = c;
      break;
    default:
      return (COLORREF)-1;
  }

  if ( b->window )
    InvalidateRect(b->window, NULL, TRUE);

  return old;
}


static int
rlc_kill(RlcData b)
{ DWORD result;

  switch(b->closing++)
  { case 0:
      b->queue->flags |= RLC_EOF;
      PostThreadMessage(b->application_thread_id, WM_RLC_INPUT, 0, 0);
      return TRUE;
    case 1:
      if ( _rlc_interrupt_hook )
      { (*_rlc_interrupt_hook)(b, SIGINT);
	return TRUE;
      }
    default:
      if ( !SendMessageTimeout(b->kill_window,
			       WM_DESTROY,
			       0, 0,
			       SMTO_ABORTIFHUNG,
			       5000,
			       &result) )
      { if ( b->window )
	{ switch( MessageBox(b->window,
			     "Main task is not responding."
			     "Click \"OK\" to terminate it",
			     "Error",
			     MB_OKCANCEL|MB_ICONEXCLAMATION|MB_APPLMODAL) )
	  { case IDCANCEL:
	      return FALSE;
	  }
	  TerminateThread(b->application_thread, 1);
    
	  return TRUE;
	}
      }
  }

  return FALSE;
}


static void
rlc_interrupt(RlcData b)
{ if ( _rlc_interrupt_hook )
    (*_rlc_interrupt_hook)((rlc_console)b, SIGINT);
  else
    raise(SIGINT);
}


static void
typed_char(RlcData b, int chr)
{ if ( chr == Control('C') )
    rlc_interrupt(b);
  else if ( chr == Control('V') || chr == Control('Y') )
    rlc_paste(b);
  else if ( b->queue )
    rlc_add_queue(b, b->queue, chr);
}


		 /*******************************
		 *	 WINDOW PROCEDURE	*
		 *******************************/

static void
rlc_destroy(RlcData b)
{ if ( b && b->window )
  { DestroyWindow(b->window);
    b->window = NULL;
    b->closing = 3;
  }
}


static int
IsDownKey(code)
{ short mask = GetKeyState(code);

  return mask & 0x8000;
}


static WINAPI
rlc_wnd_proc(HWND hwnd, UINT message, UINT wParam, LONG lParam)
{ RlcData b = (RlcData) GetWindowLong(hwnd, GWL_DATA);

  switch(message)
  { case WM_CREATE:
      return 0;

    case WM_SIZE:
      if ( wParam != SIZE_MINIMIZED )
      { rlc_resize_pixel_units(b, LOWORD(lParam), HIWORD(lParam));
	b->modified_options |= OPT_SIZE;
      }
      return 0;

    case WM_MOVE:
    { WINDOWPLACEMENT placement;

      placement.length = sizeof(placement);
      GetWindowPlacement(hwnd, &placement);
      
      if ( placement.showCmd == SW_SHOWNORMAL )
      { b->win_x = placement.rcNormalPosition.left;
  	b->win_y = placement.rcNormalPosition.top;

	b->modified_options |= OPT_POSITION;
      }

      return 0;
    }

    case WM_SETFOCUS:
      b->has_focus = TRUE;
      CreateCaret(hwnd, NULL, b->fixedfont ? b->cw : 3, b->ch-1);
      rlc_place_caret(b);
      return 0;

    case WM_KILLFOCUS:
      b->has_focus = FALSE;
      b->caret_is_shown = FALSE;
      HideCaret(hwnd);
      DestroyCaret();
      return 0;

    case WM_PAINT:
      rlc_redraw(b);
      return 0;

    case WM_COMMAND:
    { UINT  item  = (UINT) LOWORD(wParam);
      const char *name;

      switch( item )
      { case IDM_PASTE:
	  rlc_paste(b);
	  return 0;
	case IDM_COPY:
	  return 0;			/* no op: already done */
	case IDM_CUT:
	  break;			/* TBD: cut */
	case IDM_BREAK:
	  rlc_interrupt(b);
	  break;
	case IDM_FONT:
	  rlc_queryfont(b);
	  return 0;
	case IDM_EXIT:
	  if ( rlc_kill(b) )
	    return 0;
	  break;
      }

      if ( (name = lookupMenuId(item)) )
      { if ( _rlc_menu_hook )
	{ (*_rlc_menu_hook)(b, name);
	}

	return 0;
      }

      break;
    }

  { int chr; 

    case WM_KEYDOWN:			/* up is sent only once */
    { switch((int) wParam)
      { case VK_DELETE:	chr = 127;		break;
	case VK_LEFT:	chr = Control('B');	break;
	case VK_RIGHT:	chr = Control('F');	break;
	case VK_UP:	chr = Control('P');	break;
	case VK_DOWN:	chr = Control('N');	break;
	case VK_HOME:	chr = Control('A');	break;
	case VK_END:	chr = Control('E');	break;

        case VK_PRIOR:			/* page up */
	{ int maxdo = rlc_count_lines(b, b->first, b->window_start);
	  int pagdo = b->window_size - 1;
	  b->window_start = rlc_add_lines(b, b->window_start,
					  -min(maxdo, pagdo));

	scrolledbykey:
	  rlc_update_scrollbar(b);
	  InvalidateRect(hwnd, NULL, FALSE);

	  return 0;
	}
	case VK_NEXT:			/* page down */
	{ int maxup = rlc_count_lines(b, b->window_start, b->last);
	  int pagup = b->window_size - 1;
	  b->window_start = rlc_add_lines(b, b->window_start,
					  min(maxup, pagup));
	  goto scrolledbykey;
	}
	default:
	  goto break2;
      }
      if ( chr > 0 )
      { if ( IsDownKey(VK_CONTROL) )
	  typed_char(b, ESC);

	typed_char(b, chr);

	return 0;
      }
    break2:
      break;
    }
    case WM_SYSCHAR:	typed_char(b, ESC); /* Play escape-something */
    case WM_CHAR:	chr = wParam;

      typed_char(b, chr);

      return 0;
  }

					/* selection handling */
    case WM_MBUTTONDOWN:
    middle_down:
      return 0;

    case WM_MBUTTONUP:
    middle_up:
      rlc_paste(b);

      return 0;

    case WM_LBUTTONDOWN:
    { POINTS pt;

      if ( emulate_three_buttons )
      { MSG msg;

	Sleep(emulate_three_buttons);
	if ( PeekMessage(&msg, hwnd,
			 WM_RBUTTONDOWN, WM_RBUTTONDOWN, PM_REMOVE) )
	{ emu_hwnd = hwnd;
	  goto middle_down;
	}
      }

      pt = MAKEPOINTS(lParam);
      rlc_start_selection(b, pt.x, pt.y);

      return 0;
    }
    
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    if ( emu_hwnd == hwnd )
    { if ( wParam & (MK_RBUTTON|MK_LBUTTON) )
	goto middle_up;
      else
      { emu_hwnd = 0;
	return 0;
      }
    } else
    { rlc_copy(b);

      return 0;
    }

    case WM_LBUTTONDBLCLK:
    { POINTS pt = MAKEPOINTS(lParam);

      rlc_word_selection(b, pt.x, pt.y);

      return 0;
    }

    case WM_RBUTTONDOWN:
    { POINTS pt;

      if ( emulate_three_buttons )
      { MSG msg;

	Sleep(emulate_three_buttons);
	if ( PeekMessage(&msg, hwnd,
			 WM_LBUTTONDOWN, WM_LBUTTONDOWN, PM_REMOVE) )
	{ emu_hwnd = hwnd;
	  goto middle_down;
	}
      }

      pt = MAKEPOINTS(lParam);
      rlc_extend_selection(b, pt.x, pt.y);

      return 0;
    }

    case WM_MOUSEMOVE:
    { POINTS pt = MAKEPOINTS(lParam);

      if ( (wParam & (MK_LBUTTON|MK_RBUTTON)) &&
	   (wParam & (MK_LBUTTON|MK_RBUTTON)) != (MK_LBUTTON|MK_RBUTTON) )
      { rlc_extend_selection(b, pt.x, pt.y);

	return 0;
      }

      break;
    }

    case WM_MOUSEWHEEL:
    { short angle = (short)HIWORD(wParam);

      if ( angle < 0 )
      { if ( b->window_start != b->last )
	  b->window_start = NextLine(b, b->window_start);
      } else
      { if ( b->window_start != b->first )
	  b->window_start = PrevLine(b, b->window_start);
      }

      rlc_update_scrollbar(b);
      InvalidateRect(hwnd, NULL, FALSE);

      return 0;
    }
					/* scrolling */
    case WM_VSCROLL:
    { switch( LOWORD(wParam) )
      { case SB_LINEUP:
	  if ( b->window_start != b->first )
	    b->window_start = PrevLine(b, b->window_start);
	  break;
	case SB_LINEDOWN:
	  if ( b->window_start != b->last )
	    b->window_start = NextLine(b, b->window_start);
	  break;
	case SB_PAGEUP:
	{ int maxdo = rlc_count_lines(b, b->first, b->window_start);
	  int pagdo = b->window_size - 1;
	  b->window_start = rlc_add_lines(b, b->window_start,
					  -min(maxdo, pagdo));
	  break;
	}
	case SB_PAGEDOWN:
	{ int maxup = rlc_count_lines(b, b->window_start, b->last);
	  int pagup = b->window_size - 1;
	  b->window_start = rlc_add_lines(b, b->window_start,
					  min(maxup, pagup));
	  break;
	}
	case SB_THUMBTRACK:
	  b->window_start = rlc_add_lines(b, b->first, HIWORD(wParam));
	  break;
      }

      rlc_update_scrollbar(b);
      InvalidateRect(hwnd, NULL, FALSE);

      return 0;
    }

    case WM_TIMER:
      if ( _rlc_timer_hook && wParam >= RLC_APPTIMER_ID )
      { (*_rlc_timer_hook)((int) wParam);

	return 0;
      }
      break;

    case WM_RENDERALLFORMATS:
      if ( _rlc_render_all_hook )
      { (*_rlc_render_all_hook)();

        return 0;
      }
      break;

    case WM_RENDERFORMAT:
      if ( _rlc_render_hook && (*_rlc_render_hook)(wParam) )
        return 0;

      break;

    case WM_ERASEBKGND:
    { HDC hdc = (HDC) wParam;
      RECT rect;
      HBRUSH hbrush;
      COLORREF rgb = b->background;

      hbrush = CreateSolidBrush(rgb);
      GetClipBox(hdc, &rect);
      FillRect(hdc, &rect, hbrush);
      DeleteObject(hbrush);

      return 1;				/* non-zero: I've erased it */
    }

    case WM_SYSCOLORCHANGE:
      b->foreground     = GetSysColor(COLOR_WINDOWTEXT);
      b->background     = GetSysColor(COLOR_WINDOW);
      b->sel_foreground = GetSysColor(COLOR_HIGHLIGHTTEXT);
      b->sel_background = GetSysColor(COLOR_HIGHLIGHT);
      return 0;

    case WM_RLC_WRITE:
    { int count = (int)wParam;
      char *buf = (char *)lParam;

      if ( OQSIZE - b->output_queued > count )
      { memcpy(&b->output_queue[b->output_queued], buf, count);
	b->output_queued += count;
      } else
      { if ( b->output_queued > 0 )
	  rlc_flush_output(b);

	if ( count <= OQSIZE )
	{ memcpy(b->output_queue, buf, count);
	  b->output_queued = count;
	} else
	  rlc_do_write(b, buf, count);
      }

      return 0;
    }

    case WM_RLC_FLUSH:
    { rlc_flush_output(b);
      return 0;
    }

    case WM_RLC_MENU:
    { rlc_menu_action((rlc_console) b, (struct menu_data*)lParam);

      return 0;
    }

    case WM_RLC_CLOSEWIN:
      return 0;

    case WM_CLOSE:
      if ( rlc_kill(b) )
        return 0;
      break;

    case WM_DESTROY:
      b->window = NULL;
      PostQuitMessage(0);
      return 0;
  }

  return DefWindowProc(hwnd, message, wParam, lParam);
}

static int
rlc_get_message(MSG *msg, HWND hwnd, UINT low, UINT high)
{ int rc;
again:
  if ( (rc=GetMessage(msg, hwnd, low, high)) )
  { if ( _rlc_message_hook &&
	 (*_rlc_message_hook)(msg->hwnd, msg->message,
			      msg->wParam, msg->lParam) )
      goto again;
  }

  return rc;
}


static void
rlc_dispatch(RlcData b)
{ MSG msg;

  if ( rlc_get_message(&msg, NULL, 0, 0) && msg.message != WM_RLC_CLOSEWIN )
  { /* DEBUG(Dprintf("Thread %x got message 0x%04x\n",
		     GetCurrentThreadId(), msg.message));
    */
    TranslateMessage(&msg);
    DispatchMessage(&msg);
    rlc_flush_output(b);
    return;
  } else
  { DEBUG(Dprintf("Thread %x got WM_RLC_CLOSEWIN\n",
		  GetCurrentThreadId()));
    b->queue->flags |= RLC_EOF;
  }
}


void
rlc_yield()
{ MSG msg;

  while ( PeekMessage(&msg, NULL, 0, 0, PM_REMOVE) )
  { TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
}

		 /*******************************
		 *	 CHARACTER TYPES	*
		 *******************************/

#define iswordchar(c) (_rlc_word_chars[(int)(c)])

static void
rlc_init_word_chars()
{ int i;

  for(i=0; i<CHAR_MAX; i++)
    _rlc_word_chars[i] = (isalnum(i) || i == '_') ? TRUE : FALSE;
}


void
rlc_word_char(int chr, int isword)
{ if ( chr > 0 && chr < CHAR_MAX )
    _rlc_word_chars[chr] = isword;
}


int
rlc_is_word_char(int chr)
{ if ( chr > 0 && chr < CHAR_MAX )
    return _rlc_word_chars[chr];

  return FALSE;
}


		 /*******************************
		 *	    SELECTION		*
		 *******************************/

#define SelLT(l1, c1, l2, c2) ((l1) < (l2) || (l1) == (l2) && (c1) < (c2))
#define SelEQ(l1, c1, l2, c2) ((l1) == (l2) && (c1) == (c2))

static int
rlc_min(RlcData b, int x, int y)
{ if ( rlc_count_lines(b, b->first, x) < rlc_count_lines(b, b->first, y) )
    return x;

  return y;
}


static int
rlc_max(RlcData b, int x, int y)
{ if ( rlc_count_lines(b, b->first, x) > rlc_count_lines(b, b->first, y) )
    return x;

  return y;
}


static void
rlc_changed_line(RlcData b, int i, int mask)
{ b->lines[i].changed |= mask;
}


static void
rlc_set_selection(RlcData b, int sl, int sc, int el, int ec)
{ int sch = rlc_min(b, sl, b->sel_start_line);
  int ech = rlc_max(b, el, b->sel_end_line);
  int nel = NextLine(b, el);
  int nsel= NextLine(b, b->sel_end_line);
  int i;
  int innow  = FALSE;
  int insoon = FALSE;

					/* find the lines that changed */
  for(i=sch; ; i = NextLine(b, i))
  { if ( i == sl )
    { insoon = TRUE;
      if ( i == b->sel_start_line )
      { innow = TRUE;
	if ( sc != b->sel_start_char ||
	     (i == el && i != b->sel_end_line) ||
	     (i == b->sel_end_line && i != el) )
	  rlc_changed_line(b, i, CHG_CHANGED);
      } else
	rlc_changed_line(b, i, CHG_CHANGED);
    } else if ( i == b->sel_start_line )
    { innow = TRUE;
      rlc_changed_line(b, i, CHG_CHANGED);
    }

    if ( i == b->sel_end_line )
    { if ( (i == el && ec != b->sel_end_char) || el != i )
	rlc_changed_line(b, i, CHG_CHANGED);
    }

    if ( innow != insoon )
      rlc_changed_line(b, i, CHG_CHANGED);

    if ( i == nel )
    { insoon = FALSE;
      if ( i == nsel )
	innow = FALSE;
      else
	rlc_changed_line(b, i, CHG_CHANGED);
    } else if ( i == nsel )
    { innow = FALSE;
      rlc_changed_line(b, i, CHG_CHANGED);
    }

    if ( i == ech )
      break;
  }

					/* update the attributes */
  b->sel_start_line = sl;
  b->sel_start_char = sc;
  b->sel_end_line   = el;
  b->sel_end_char   = ec;

					/* ... and request a repaint */
  rlc_request_redraw(b);
}


void
rlc_translate_mouse(RlcData b, int x, int y, int *line, int *chr)
{ int ln = b->window_start;
  int n = b->window_size;		/* # lines */
  TextLine tl;
  x-= b->cw;				/* margin */

  if ( !b->window )
    return;

  while( y > b->ch && ln != b->last && n-- > 0 )
  { ln = NextLine(b, ln);
    y -= b->ch;
  }
  *line = ln;
  tl = &b->lines[ln];

  if ( b->fixedfont )
  { *chr = min(x/b->cw, tl->size);
  } else if ( tl->size == 0 )
  { *chr = 0;
  } else
  { char *s = tl->text;
    HDC hdc = GetDC(b->window);
    int f = 0;
    int t = tl->size;
    int m = (f+t)/2;
    int i;

    SelectObject(hdc, b->hfont);

    for(i=10; --i > 0; m=(f+t)/2)
    { int w;
     
      w = text_width(b, hdc, s, m);
      if ( x > w )
      { int cw;

	GetCharWidth32(hdc, s[m], s[m], &cw);
	if ( x < w+cw )
	{ *chr = m;
	  return;
	}
	f = m+1;
      } else
      { t = m;
      }
    }
	
    *chr = m;
  }
}


static void
rlc_start_selection(RlcData b, int x, int y)
{ int l, c;

  rlc_translate_mouse(b, x, y, &l, &c);
  b->sel_unit = SEL_CHAR;
  b->sel_org_line = l;
  b->sel_org_char = c;
  rlc_set_selection(b, l, c, l, c);
}


static void
rlc_end_selection(RlcData b, int x, int y)
{ int l, c;

  rlc_translate_mouse(b, x, y, &l, &c);
  if ( SelLT(l, c, b->sel_org_line, b->sel_org_char) )
    rlc_set_selection(b, l, c, b->sel_org_line, b->sel_org_char);
  else if ( SelLT(b->sel_org_line, b->sel_org_char, l, c) )
    rlc_set_selection(b, b->sel_org_line, b->sel_org_char, l, c);
  rlc_set_selection(b, l, c, l, c);
}


static int				/* v >= f && v <= t */
rlc_between(RlcData b, int f, int t, int v) 
{ int h = rlc_count_lines(b, b->first, v);

  if ( h >= rlc_count_lines(b, b->first, f) &&
       h <= rlc_count_lines(b, b->first, t) )
    return TRUE;

  return FALSE;
}


static void
rlc_word_selection(RlcData b, int x, int y)
{ int l, c;

  rlc_translate_mouse(b, x, y, &l, &c);
  if ( rlc_between(b, b->first, b->last, l) )
  { TextLine tl = &b->lines[l];

    if ( c < tl->size && iswordchar(tl->text[c]) )
    { int f, t;

      for(f=c; f>0 && iswordchar(tl->text[f-1]); f--)
	;
      for(t=c; t<tl->size && iswordchar(tl->text[t]); t++)
	;
      rlc_set_selection(b, l, f, l, t);
    }
  }

  b->sel_unit = SEL_WORD;
}


static void
rlc_extend_selection(RlcData b, int x, int y)
{ int l, c;

  rlc_translate_mouse(b, x, y, &l, &c);
  if ( SelLT(l, c, b->sel_org_line, b->sel_org_char) )
  { if ( b->sel_unit == SEL_WORD )
    { if ( rlc_between(b, b->first, b->last, l) )
      { TextLine tl = &b->lines[l];

	if ( c < tl->size && iswordchar(tl->text[c]) )
	  for(; c > 0 && iswordchar(tl->text[c-1]); c--)
	    ;
      }
    } else if ( b->sel_unit == SEL_LINE )
      c = 0;
    rlc_set_selection(b, l, c, b->sel_end_line, b->sel_end_char);
  } else if ( SelLT(b->sel_org_line, b->sel_org_char, l, c) )
  { if ( b->sel_unit == SEL_WORD )
    { if ( rlc_between(b, b->first, b->last, l) )
      { TextLine tl = &b->lines[l];

	if ( c < tl->size && iswordchar(tl->text[c]) )
	  for(; c < tl->size && iswordchar(tl->text[c]); c++)
	    ;
      }
    } else if ( b->sel_unit == SEL_LINE )
      c = b->width;
    rlc_set_selection(b, b->sel_start_line, b->sel_start_char, l, c);
  }
}


static char *
rlc_read_from_window(RlcData b, int sl, int sc, int el, int ec)
{ int bufsize = 256;
  char *buf;
  int i = 0;

  if ( el < sl || el == sl && ec < sc )
    return NULL;			/* invalid region */
  if ( !(buf = rlc_malloc(bufsize)) )
    return NULL;			/* not enough memory */

  for( ; ; sc = 0, sl = NextLine(b, sl))
  { TextLine tl = &b->lines[sl];
    if ( tl )
    { int e = (sl == el ? ec : tl->size);

      if ( e > tl->size )
	e = tl->size;

      while(sc < e)
      { if ( i >= bufsize )
	{ bufsize *= 2;
	  if ( !(buf = rlc_realloc(buf, bufsize)) )
	    return NULL;		/* not enough memory */
	}
	buf[i++] = tl->text[sc++];
      }
    }
      
    if ( sl == el || sl == b->last )
    { buf[i++] = '\0';
      return buf;
    }

    if ( tl && !tl->softreturn )
    { if ( i+1 >= bufsize )
      { bufsize *= 2;
	if ( !(buf = rlc_realloc(buf, bufsize)) )
	  return NULL;			/* not enough memory */
      }
      buf[i++] = '\r';			/* Bill ... */
      buf[i++] = '\n';
    }
  }
}


static char *
rlc_selection(RlcData b)
{ if ( SelEQ(b->sel_start_line, b->sel_start_char,
	     b->sel_end_line,   b->sel_end_char) )
    return NULL;

  return rlc_read_from_window(b,
			      b->sel_start_line, b->sel_start_char,
			      b->sel_end_line,   b->sel_end_char);
} 


static void
rlc_copy(RlcData b)
{ char *sel = rlc_selection(b);

  if ( sel && b->window )
  { int size = strlen(sel);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, size + 1);
    char far *data;
    int i;

    if ( !mem )
    { MessageBox(NULL, "Not enough memory to paste", "Error", MB_OK);
      return;
    }
    data = GlobalLock(mem);

    for(i=0; i<size; i++)
      *data++ = sel[i];
    *data = '\0';

    GlobalUnlock(mem);
    OpenClipboard(b->window);
    EmptyClipboard();
    SetClipboardData(CF_TEXT, mem);
    CloseClipboard();

    rlc_free(sel);
  }
}



		 /*******************************
		 *           REPAINT		*
		 *******************************/

static void
rlc_place_caret(RlcData b)
{ if ( b->has_focus && b->window )
  { int line = rlc_count_lines(b, b->window_start, b->caret_y);

    if ( line < b->window_size )
    { if ( b->fixedfont )
      { SetCaretPos((b->caret_x + 1) * b->cw, line * b->ch);
      } else
      { HDC hdc = GetDC(b->window);
	SIZE tsize;
	TextLine tl = &b->lines[b->caret_y];
	HFONT old;

	old = SelectObject(hdc, b->hfont);
	GetTextExtentPoint32(hdc, tl->text, b->caret_x, &tsize);
	SelectObject(hdc, old);
	ReleaseDC(b->window, hdc);

	SetCaretPos(b->cw + tsize.cx, line * b->ch);
      }
      if ( !b->caret_is_shown )
      { ShowCaret(b->window);
	b->caret_is_shown = TRUE;

	return;
      }
    } else
    { if ( b->caret_is_shown == TRUE )
      { HideCaret(b->window);
	b->caret_is_shown = FALSE;
      }
    }
  }

  b->caret_is_shown = FALSE;
}


static void
rlc_update_scrollbar(RlcData b)
{ if ( b->window )
  { int nsb_lines = rlc_count_lines(b, b->first, b->last);
    int nsb_start = rlc_count_lines(b, b->first, b->window_start);
  
    if ( nsb_lines != b->sb_lines ||
	 nsb_start != b->sb_start )
    { SetScrollRange(b->window, SB_VERT, 0, nsb_lines, FALSE);
      SetScrollPos(  b->window, SB_VERT, nsb_start, TRUE);
  
      b->sb_lines = nsb_lines;
      b->sb_start = nsb_start;
    }
  }
}


static void
rlc_redraw(RlcData b)
{ PAINTSTRUCT ps;
  HDC hdc = BeginPaint(b->window, &ps);
  int sl = max(0, ps.rcPaint.top/b->ch);
  int el = min(b->window_size, ps.rcPaint.bottom/b->ch);
  int l = rlc_add_lines(b, b->window_start, sl);
  int pl = sl;				/* physical line */
  RECT rect;
  HBRUSH bg;
  int stockbg;
  int insel = FALSE;			/* selected lines? */

  SelectObject(hdc, b->hfont);
  SetTextColor(hdc, b->foreground);
  SetBkColor(hdc, b->background);
  
  if ( b->background == RGB(255, 255, 255) )
  { bg = GetStockObject(WHITE_BRUSH);
    stockbg = TRUE;
  } else
  { bg = CreateSolidBrush(b->background);
    stockbg = FALSE;
  }

  if ( b->has_focus && b->caret_is_shown )
  { HideCaret(b->window);
    b->caret_is_shown = FALSE;
  }

  if ( rlc_count_lines(b, b->first, b->sel_start_line) <
       rlc_count_lines(b, b->first, l) &&
       rlc_count_lines(b, b->first, b->sel_end_line) >=
       rlc_count_lines(b, b->first, l) )
    insel = TRUE;

  if ( insel )
  { SetBkColor(hdc, b->sel_background);
    SetTextColor(hdc, b->sel_foreground);
  }

  for(; pl <= el; l = NextLine(b, l), pl++)
  { TextLine tl = &b->lines[l];
    char text[MAXLINE];
    int ty = b->ch * pl;
    int cx = b->cw;

    if ( !tl->text )
    { tl->size = 0;
      memset(text, ' ', b->width);
    } else
    { memcpy(text, tl->text, tl->size);
      if ( b->width > tl->size )
	memset(&text[tl->size], ' ', b->width - tl->size);
    }

    rect.top    = ty;
    rect.bottom = rect.top + b->ch;

					/* compute selection */
    if ( l == b->sel_start_line )
    { int cf = b->sel_start_char;
      int ce = (b->sel_end_line != b->sel_start_line ? b->width
						     : b->sel_end_char);
      if ( cf > 0 )
      {	TextOut(hdc, cx, ty, text, cf);
	cx += text_width(b, hdc, text, cf);
      }
      SetBkColor(hdc, b->sel_background);
      SetTextColor(hdc, b->sel_foreground);
      TextOut(hdc, cx, ty, &text[cf], ce-cf);
      cx += text_width(b, hdc, &text[cf], ce-cf);
      if ( l == b->sel_end_line )
      { SetBkColor(hdc, b->background);
	SetTextColor(hdc, b->foreground);
	TextOut(hdc, cx, ty, &text[ce], b->width - ce);
	cx += text_width(b, hdc, &text[ce], b->width - ce);
      } else
	insel = TRUE;
    } else if ( l == b->sel_end_line )	/* end of selection */
    { int ce = b->sel_end_char;

      insel = FALSE;
      TextOut(hdc, cx, ty, text, ce);
      cx += text_width(b, hdc, text, ce);
      SetBkColor(hdc, b->background);
      SetTextColor(hdc, b->foreground);
      TextOut(hdc, cx, ty, &text[ce], b->width - ce);
      cx += text_width(b, hdc, &text[ce], b->width - ce);
    } else				/* entire line in/out selection */
    { TextOut(hdc, cx, ty, text, b->width);
      cx += text_width(b, hdc, text, b->width);
    }

					/* clear remainder of line */
    if ( cx < b->width * (b->cw+1) )
    { rect.left   = cx;
      rect.right  = b->width * (b->cw+1);
      rect.top    = b->ch * pl;
      rect.bottom = rect.top + b->ch;
      FillRect(hdc, &rect, bg);
    }

    tl->changed = CHG_RESET;

    if ( l == b->last )			/* clear to end of window */
    { rect.left   = b->cw;
      rect.right  = b->width * (b->cw+1);
      rect.top    = b->ch * (pl+1);
      rect.bottom = b->ch * (el+1);
      FillRect(hdc, &rect, bg);

      break;
    }
  }
  rlc_place_caret(b);

  b->changed = CHG_RESET;
  if ( !stockbg )
    DeleteObject(bg);

  EndPaint(b->window, &ps);

  rlc_update_scrollbar(b);
}


static void
rlc_request_redraw(RlcData b)
{ if ( b->changed & CHG_CHANGED )
  { if ( b->window )
      InvalidateRect(b->window, NULL, FALSE);
  } else
  { int i = b->window_start;
    int y = 0;
    RECT rect;
    int first = TRUE;
    int clear = FALSE;

    rect.left = b->cw;
    rect.right = (b->width+1) * b->cw;
    
    for(; y < b->window_size; y++, i = NextLine(b, i))
    { TextLine l = &b->lines[i];
      
      if ( l->changed & CHG_CHANGED )
      { if ( first )
	{ rect.top = y * b->ch;
	  rect.bottom = rect.top + b->ch;
	  first = FALSE;
	} else
	  rect.bottom = (y+1) * b->ch;

	if ( l->changed & CHG_CLEAR )
	  clear = TRUE;
      }
      if ( i == b->last )
	break;
    }

    if ( !first && b->window )
      InvalidateRect(b->window, &rect, FALSE); /*clear);*/
    else if ( b->changed & CHG_CARET )
      rlc_place_caret(b);
  }
}


static void
rlc_normalise(RlcData b)
{ if ( rlc_count_lines(b, b->window_start, b->caret_y) >= b->window_size )
  { b->window_start = rlc_add_lines(b, b->caret_y, -(b->window_size-1));
    b->changed |= CHG_CARET|CHG_CLEAR|CHG_CHANGED;
    rlc_request_redraw(b);
  }
}


static void
rlc_resize_pixel_units(RlcData b, int w, int h)
{ int nw = max(20, w/b->cw)-2;		/* 1 character space for margins */
  int nh = max(1, h/b->ch);
  
  DEBUG(Dprintf("rlc_resize_pixel_units(%p, %d, %d) (%dx%d)\n",
		b, w, h, nw, nh));

  if ( b->width == nw && b->window_size == nh )
    return;				/* no real change */

  rlc_resize(b, nw, nh);
  
  if ( _rlc_resize_hook )
    (*_rlc_resize_hook)(b->width, b->window_size);
  else
  {
#ifdef SIGWINCH
    raise(SIGWINCH);
#endif
  }

  rlc_request_redraw(b);
}

		 /*******************************
		 *	       FONT		*
		 *******************************/

static void
rlc_init_text_dimensions(RlcData b, HFONT font)
{ HDC hdc;
  TEXTMETRIC tm;

  if ( font )
  { b->hfont = font;
  } else if ( b->create_attributes )
  { rlc_console_attr *a = b->create_attributes;
    if ( !a->face_name[0] )
      b->hfont = GetStockObject(ANSI_FIXED_FONT);
    else
    { LOGFONT lfont;
  
      memset(&lfont, 0, sizeof(lfont));
  
      lfont.lfHeight          = a->font_size;
      lfont.lfWeight          = a->font_weight;
      lfont.lfPitchAndFamily  = a->font_family;
      lfont.lfCharSet	      = a->font_char_set;
      strncpy(lfont.lfFaceName, a->face_name, 31);
    
      if ( !(b->hfont = CreateFontIndirect(&lfont)) )
	b->hfont = GetStockObject(ANSI_FIXED_FONT);
    }
  } else
    b->hfont = GetStockObject(ANSI_FIXED_FONT);

					/* test for fixed?*/
  hdc = GetDC(NULL);
  SelectObject(hdc, b->hfont);
  GetTextMetrics(hdc, &tm);
  b->cw = tm.tmAveCharWidth;
  b->cb = tm.tmHeight;
  b->ch = tm.tmHeight + tm.tmExternalLeading;
  b->fixedfont = (tm.tmPitchAndFamily & TMPF_FIXED_PITCH ? FALSE : TRUE);
  ReleaseDC(NULL, hdc);

  if ( b->window )
  { RECT rect;

    if ( b->has_focus == TRUE )
    { CreateCaret(b->window, NULL, b->fixedfont ? b->cw : 3, b->ch-1);
      rlc_place_caret(b);
    }

    GetClientRect(b->window, &rect);
    rlc_resize_pixel_units(b, rect.right - rect.left, rect.bottom - rect.top);
  }
}


static int
text_width(RlcData b, HDC hdc, const char *text, int len)
{ if ( b->fixedfont )
  { return len * b->cw;
  } else
  { SIZE size;

    GetTextExtentPoint32(hdc, text, len, &size);
    return size.cx;
  }
}


static void
rlc_save_font_options(HFONT font, rlc_console_attr *attr)
{ if ( font == GetStockObject(ANSI_FIXED_FONT) )
  { attr->face_name[0] = '\0';
  } else
  { LOGFONT lf;

    if ( GetObject(font, sizeof(lf), &lf) )
    { strncpy(attr->face_name, lf.lfFaceName, sizeof(attr->face_name)-1);

      attr->font_family   = lf.lfPitchAndFamily;
      attr->font_size     = lf.lfHeight;
      attr->font_weight   = lf.lfWeight;
      attr->font_char_set = lf.lfCharSet;
    }
  }
}


		 /*******************************
		 *	   FONT SELECTION	*
		 *******************************/

static void
rlc_queryfont(RlcData b)
{ CHOOSEFONT cf;
  LOGFONT lf;

  memset(&cf, 0, sizeof(cf));
  memset(&lf, 0, sizeof(lf));

  lf.lfHeight          = 16;
  lf.lfWeight          = FW_NORMAL;
  lf.lfPitchAndFamily  = FIXED_PITCH|FF_MODERN;

  cf.lStructSize = sizeof(cf);
  cf.hwndOwner   = b->window;
  cf.lpLogFont   = &lf;
  cf.Flags       = CF_SCREENFONTS|
    		   CF_NOVERTFONTS|
		   CF_NOSIMULATIONS|
		   CF_FORCEFONTEXIST|
		   CF_INITTOLOGFONTSTRUCT;
  cf.nFontType   = SCREEN_FONTTYPE;

  if ( ChooseFont(&cf) )
  { HFONT f;
    if ( (f = CreateFontIndirect(&lf)) )
    { rlc_init_text_dimensions(b, f);

      InvalidateRect(b->window, NULL, TRUE);
    }
  }
}



		 /*******************************
		 *     BUFFER INITIALISATION	*
		 *******************************/

static RlcData
rlc_make_buffer(int w, int h)
{ RlcData b = rlc_malloc(sizeof(rlc_data));
  int i;

  memset(b, 0, sizeof(*b));
  b->magic = RLC_MAGIC;

  b->height         = h;
  b->width          = w;
  b->window_size    = 25;
  b->lines          = rlc_malloc(sizeof(text_line) * h);
  b->cmdstat	    = CMD_INITIAL;
  b->changed	    = CHG_CARET|CHG_CHANGED|CHG_CLEAR;
  b->imode	    = IMODE_COOKED;	/* switch on first rlc_read() call */
  b->imodeswitch    = FALSE;
  b->lhead 	    = NULL;
  b->ltail 	    = NULL;
    
  memset(b->lines, 0, sizeof(text_line) * h);
  for(i=0; i<h; i++)
    b->lines[i].adjusted = TRUE;

  rlc_init_word_chars();

  return b;
}


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Copy all lines one `back' (i.e.  towards   older  lines).  If the oldest
(first) line is adjacent to the last, throw it away.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static void
rlc_shift_lines_down(RlcData b, int line)
{ int i = b->first;
  int p = PrevLine(b, i);

  if ( p != b->last )			/* move first (oldest line) */
  { b->first = p;
    b->lines[p] = b->lines[i];
  } else				/* delete first (oldest) line */
    rlc_free_line(b, b->first);
					/* copy the lines */
  for(p=i, i = NextLine(b, i); p != line; p=i, i = NextLine(b, i))
    b->lines[p] = b->lines[i];

  b->lines[line].text       = NULL;	/* make this one `free' */
  b->lines[line].size       = 0;
  b->lines[line].adjusted   = TRUE;
  b->lines[line].softreturn = FALSE;
}


static void
rlc_shift_lines_up(RlcData b, int line)
{ int prev = PrevLine(b, line);

  while(line != b->first)
  { b->lines[line] = b->lines[prev];
    line = prev;
    prev = PrevLine(b, prev);
  }

  rlc_reinit_line(b, b->first);
  b->first = NextLine(b, b->first);
}



static void
rlc_resize(RlcData b, int w, int h)
{ int i;

  if ( b->width == w && b->window_size == h )
    return;				/* no real change */

  DEBUG(Dprintf("Resizing %dx%d --> %dx%d\n", b->width, b->window_size, w, h));

  b->window_size = h;
  b->width = w;
  
  for(i = b->first; /*i != b->last*/; i = NextLine(b, i))
  { TextLine tl = &b->lines[i];

    if ( tl->text && tl->adjusted == FALSE )
      rlc_adjust_line(b, i);

    if ( tl->size > w )
    { if ( !tl->softreturn )		/* hard --> soft */
      { TextLine pl;

	rlc_shift_lines_down(b, i);
	DEBUG(Dprint_lines(b, b->first, b->first));
	DEBUG(Dprintf("b->first = %d, b->last = %d\n", b->first, b->last));
	pl = &b->lines[PrevLine(b, i)];	/* this is the moved line */
	tl->text = rlc_malloc(pl->size - w);
	memmove(tl->text, &pl->text[w], pl->size - w);
	DEBUG(Dprintf("Copied %d chars from line %d to %d\n",
		      pl->size - w, pl - b->lines, i));
	tl->size = pl->size - w;
	tl->adjusted = TRUE;
	tl->softreturn = FALSE;
	pl->softreturn = TRUE;
	pl->text = rlc_realloc(pl->text, w);
	pl->size = w;
	pl->adjusted = TRUE;
	i = pl - b->lines;
	DEBUG(Dprint_lines(b, b->first, b->last));
      } else				/* put in next line */
      { TextLine nl;
	int move = tl->size - w;

	if ( i == b->last )
	  rlc_add_line(b);
	nl = &b->lines[NextLine(b, i)];
	nl->text = rlc_realloc(nl->text, nl->size + move);
	memmove(&nl->text[move], nl->text, nl->size);
	memmove(nl->text, &tl->text[w], move);
	nl->size += move;
	tl->size = w;
      }	
    } else if ( tl->text && tl->softreturn && tl->size < w )
    { TextLine nl;

      if ( i == b->last )
	rlc_add_line(b);
      nl = &b->lines[NextLine(b, i)];

      nl->text = rlc_realloc(nl->text, nl->size + tl->size);
      memmove(&nl->text[tl->size], nl->text, nl->size);
      memmove(nl->text, tl->text, tl->size);
      nl->size += tl->size;
      nl->adjusted = TRUE;
      rlc_shift_lines_up(b, i);
    }

    if ( i == b->last )
      break;
  }

  for(i = NextLine(b, i); i != b->first; i = NextLine(b, i))
    rlc_free_line(b, i);

  if ( rlc_count_lines(b, b->first, b->last) < h )
    b->window_start = b->first;
  else
    b->window_start = rlc_add_lines(b, b->last, -(h-1));

  b->caret_y = b->last;
  b->caret_x = b->lines[b->last].size;

  b->changed |= CHG_CARET|CHG_CHANGED|CHG_CLEAR;

  rlc_check_assertions(b);
}


static void
rlc_reinit_line(RlcData b, int line)
{ TextLine tl = &b->lines[line];

  tl->text	 = NULL;
  tl->adjusted   = FALSE;
  tl->size       = 0;
  tl->softreturn = FALSE;
}


static void
rlc_free_line(RlcData b, int line)
{ TextLine tl = &b->lines[line];
  if ( tl->text )
  { rlc_free(tl->text);
    rlc_reinit_line(b, line);
  }
}


static void
rlc_adjust_line(RlcData b, int line)
{ TextLine tl = &b->lines[line];

  if ( tl->text && !tl->adjusted )
  { tl->text = rlc_realloc(tl->text, tl->size == 0 ? 4 : tl->size);
    tl->adjusted = TRUE;
  }
}


static void
rlc_unadjust_line(RlcData b, int line)
{ TextLine tl = &b->lines[line];

  if ( tl->text )
  { if ( tl->adjusted )
    { tl->text = rlc_realloc(tl->text, b->width + 1);
      tl->adjusted = FALSE;
    }
  } else
  { tl->text = rlc_malloc(b->width + 1);
    tl->adjusted = FALSE;
    tl->size = 0;
  }
}


static void
rlc_open_line(RlcData b)
{ int i = b->last;

  if ( i == b->sel_start_line )
    rlc_set_selection(b, 0, 0, 0, 0);	/* clear the selection */
  if ( i == b->first )
  { rlc_free_line(b, b->first);
    b->first = NextLine(b, b->first);
  }

  b->lines[i].text       = rlc_malloc(b->width + 1);
  b->lines[i].adjusted   = FALSE;
  b->lines[i].size       = 0;
  b->lines[i].softreturn = FALSE;
}


static void
rlc_add_line(RlcData b)
{ b->last = NextLine(b, b->last);
  rlc_open_line(b);
}

		 /*******************************
		 *	   CALCULATIONS		*
		 *******************************/

static int
rlc_count_lines(RlcData b, int from, int to)
{ if ( to >= from )
    return to-from;

  return to + b->height - from;
}


static int
rlc_add_lines(RlcData b, int here, int add)
{ here += add;
  while ( here < 0 )
    here += b->height;
  while ( here >= b->height )
    here -= b->height;

  return here;
}


		 /*******************************
		 *    ANSI SEQUENCE HANDLING	*
		 *******************************/

static void
rlc_need_arg(RlcData b, int arg, int def)
{ if ( b->argc < arg )
  { b->argv[arg-1] = def;
    b->argc = arg;
  }
}


static void
rlc_caret_up(RlcData b, int arg)
{ while(arg-- > 0 && b->caret_y != b->first)
    b->caret_y = PrevLine(b, b->caret_y);

  b->changed |= CHG_CARET;
}


static void
rlc_caret_down(RlcData b, int arg)
{ while ( arg-- > 0 )
  { if ( b->caret_y == b->last )
      rlc_add_line(b);
    b->caret_y = NextLine(b, b->caret_y);
    b->lines[b->caret_y].softreturn = FALSE; /* ? why not only on open? */
  }
  b->changed |= CHG_CARET;
					/* scroll? */
  if ( rlc_count_lines(b, b->window_start, b->caret_y) >= b->window_size )
  { b->window_start = rlc_add_lines(b, b->caret_y, -(b->window_size-1));
    b->changed |= CHG_CHANGED|CHG_CLEAR;
  }

  rlc_check_assertions(b);
}


static void
rlc_caret_forward(RlcData b, int arg)
{ while(arg-- > 0)
  { if ( ++b->caret_x >= b->width )
    { b->lines[b->caret_y].softreturn = TRUE;
      b->caret_x = 0;
      rlc_caret_down(b, 1);
    }
  }

  b->changed |= CHG_CARET;
}


static void
rlc_caret_backward(RlcData b, int arg)
{ while(arg-- > 0)
  { if ( b->caret_x-- == 0 )
    { rlc_caret_up(b, 1);
      b->caret_x = b->width-1;
    }
  }

  b->changed |= CHG_CARET;
}


static void
rlc_cariage_return(RlcData b)
{ b->caret_x = 0;

  b->changed |= CHG_CARET;
} 


static void
rlc_tab(RlcData b)
{ TextLine tl = &b->lines[b->caret_y];

  do
  { rlc_caret_forward(b, 1);
  } while( (b->caret_x % 8) != 0 );

  if ( tl->size < b->caret_x )
  { rlc_unadjust_line(b, b->caret_y);

    while ( tl->size < b->caret_x )
      tl->text[tl->size++] = ' ';
  }

  b->changed |= CHG_CARET;
}


static void
rlc_set_caret(RlcData b, int x, int y)
{ int cy = rlc_count_lines(b, b->window_start, b->caret_y);

  y = Bounds(y, 0, b->window_size);

  if ( y < cy )
    b->caret_y = rlc_add_lines(b, b->window_start, y);
  else
    rlc_caret_down(b, y-cy);

  b->caret_x = Bounds(x, 0, b->width-1);

  b->changed |= CHG_CARET;
}


static void
rlc_save_caret_position(RlcData b)
{ b->scaret_y = rlc_count_lines(b, b->window_start, b->caret_y);
  b->scaret_x = b->caret_x;
}


static void
rlc_restore_caret_position(RlcData b)
{ rlc_set_caret(b, b->scaret_x, b->scaret_y);
}


static void
rlc_erase_display(RlcData b)
{ int i = b->window_start;
  int last = rlc_add_lines(b, b->window_start, b->window_size);

  do
  { b->lines[i].size = 0;
    i = NextLine(b, i);
  } while ( i != last );

  b->changed |= CHG_CHANGED|CHG_CLEAR|CHG_CARET;

  rlc_set_caret(b, 0, 0);
}


static void
rlc_erase_line(RlcData b)
{ TextLine tl = &b->lines[b->caret_y];

  tl->size = b->caret_x;
  tl->changed |= CHG_CHANGED|CHG_CLEAR;
}


static void
rlc_put(RlcData b, int chr)
{ TextLine tl = &b->lines[b->caret_y];

  rlc_unadjust_line(b, b->caret_y);
  while( tl->size < b->caret_x )
    tl->text[tl->size++] = ' ';
  tl->text[b->caret_x] = chr;
  if ( tl->size <= b->caret_x )
    tl->size = b->caret_x + 1;
  tl->changed |= CHG_CHANGED;

  rlc_caret_forward(b, 1);
}

#ifdef _DEBUG
#define CMD(c) {cmd = #c; c;}
#else
#define CMD(c) {c;}
#endif

static void
rlc_putansi(RlcData b, int chr)
{
#ifdef _DEBUG
  char *cmd;
#endif

  switch(b->cmdstat)
  { case CMD_INITIAL:
      switch(chr)
      { case '\b':
	  CMD(rlc_caret_backward(b, 1));
	  break;
        case Control('G'):
	  MessageBeep(MB_ICONEXCLAMATION);
	  break;
	case '\r':
	  CMD(rlc_cariage_return(b));
	  break;
	case '\n':
	  CMD(rlc_caret_down(b, 1));
	  break;
	case '\t':
	  CMD(rlc_tab(b));
	  break;
	case 27:			/* ESC */
	  b->cmdstat = CMD_ESC;
	  break;
	default:
	  CMD(rlc_put(b, chr));
	  break;
      }
      break;
    case CMD_ESC:
      switch(chr)
      { case '[':
	  b->cmdstat = CMD_ANSI;
	  b->argc    = 0;
	  b->argstat = 0;		/* no arg */
	  break;
	default:
	  b->cmdstat = CMD_INITIAL;
	  break;
      }
      break;
    case CMD_ANSI:			/* ESC [ */
      if ( isdigit(chr) )
      { if ( !b->argstat )
	{ b->argv[b->argc] = (chr - '0');
	  b->argstat = 1;		/* positive */
	} else
	{ b->argv[b->argc] = b->argv[b->argc] * 10 + (chr - '0');
	}

	break;
      } 
      if ( !b->argstat && chr == '-' )
      { b->argstat = -1;		/* negative */
	break;
      }
      if ( b->argstat )
      { b->argv[b->argc] *= b->argstat;
	if ( b->argc < (ANSI_MAX_ARGC-1) )
	  b->argc++;			/* silently discard more of them */
	b->argstat = 0;
      }
      switch(chr)
      { case ';':
	  break;			/* wait for more args */
	case 'H':
	case 'f':
	  rlc_need_arg(b, 1, 0);
	  rlc_need_arg(b, 2, 0);
	  CMD(rlc_set_caret(b, b->argv[0], b->argv[1]));
	  break;
	case 'A':
	  rlc_need_arg(b, 1, 1);
	  CMD(rlc_caret_up(b, b->argv[0]));
	  break;
	case 'B':
	  rlc_need_arg(b, 1, 1);
	  CMD(rlc_caret_down(b, b->argv[0]));
	  break;
	case 'C':
	  rlc_need_arg(b, 1, 1);
	  CMD(rlc_caret_forward(b, b->argv[0]));
	  break;
	case 'D':
	  rlc_need_arg(b, 1, 1);
	  CMD(rlc_caret_backward(b, b->argv[0]));
	  break;
	case 's':
	  CMD(rlc_save_caret_position(b));
	  break;
	case 'u':
	  CMD(rlc_restore_caret_position(b));
	  break;
	case 'J':
	  if ( b->argv[0] == 2 )
	    CMD(rlc_erase_display(b));
	  break;
	case 'K':
	  CMD(rlc_erase_line(b));
	  break;
      }
      b->cmdstat = CMD_INITIAL;
  }

  rlc_check_assertions(b);
}


		 /*******************************
		 *	      CUT/PASTE		*
		 *******************************/

static void
rlc_paste(RlcData b)
{ HGLOBAL mem;

  if ( b->window )
  { OpenClipboard(b->window);
    if ( (mem = GetClipboardData(CF_TEXT)) )
    { char far *data = GlobalLock(mem);
      int i;
      RlcQueue q = b->queue;
  
      if ( q )
      { for(i=0; data[i]; i++)
	{ rlc_add_queue(b, q, data[i]);
	  if ( data[i] == '\r' && data[i+1] == '\n' )
	    i++;
	}
      }
  
      GlobalUnlock(mem);
    }
    CloseClipboard();
  }
}

		 /*******************************
		 *	LINE-READ SUPPORT	*
		 *******************************/

void
rlc_get_mark(rlc_console c, RlcMark m)
{ RlcData b = rlc_get_data(c);

  m->mark_x = b->caret_x;
  m->mark_y = b->caret_y;
}


void
rlc_goto_mark(rlc_console c, RlcMark m, const char *data, int offset)
{ RlcData b = rlc_get_data(c);
  
  b->caret_x = m->mark_x;
  b->caret_y = m->mark_y;

  for( ; offset-- > 0; data++ )
  { switch(*data)
    { case '\t':
	rlc_tab(b);
	break;
      case '\n':
	b->caret_x = 0;
        rlc_caret_down(b, 1);
	break;
      default:
	rlc_caret_forward(b, 1);
    }
  }
}


void
rlc_erase_from_caret(rlc_console c)
{ RlcData b = rlc_get_data(c);
  int i = b->caret_y;
  int x = b->caret_x;
  int last = rlc_add_lines(b, b->window_start, b->window_size);

  do
  { TextLine tl = &b->lines[i];

    if ( tl->size != x )
    { tl->size = x;
      tl->changed |= CHG_CHANGED|CHG_CLEAR;
    }

    i = NextLine(b, i);
    x = 0;
  } while ( i != last );
}


void
rlc_putchar(rlc_console c, int chr)
{ RlcData b = rlc_get_data(c);

  rlc_putansi(b, chr);
}


char *
rlc_read_screen(rlc_console c, RlcMark f, RlcMark t)
{ RlcData b = rlc_get_data(c);
  char *buf;

  buf = rlc_read_from_window(b, f->mark_y, f->mark_x, t->mark_y, t->mark_x);

  return buf;
}


void
rlc_update(rlc_console c)
{ RlcData b = rlc_get_data(c);

  if ( b->window )
  { rlc_normalise(b);
    rlc_request_redraw(b);
    UpdateWindow(b->window);
  }
}

		 /*******************************
		 *	  UPDATE THREAD		*
		 *******************************/

DWORD WINAPI
window_loop(LPVOID arg)
{ RlcData b = (RlcData) arg;

  rlc_create_window(b);
					/* if we do not do this, all windows */
					/* created by Prolog (XPCE) will be */
					/* in the background and inactive! */
  if ( !AttachThreadInput(b->application_thread_id,
			  b->console_thread_id, TRUE) )
    rlc_putansi(b, '!');

  PostThreadMessage(b->application_thread_id, WM_RLC_READY, 0, 0);

  while(!b->closing)
  { switch( b->imode )
    { case IMODE_COOKED:
      { char *line = read_line(b);

	if ( line != RL_CANCELED_CHARP )
	{ LQueued lq = rlc_malloc(sizeof(lqueued));
    
	  lq->next = NULL;
	  lq->line = line;
    
	  if ( b->ltail )
	  { b->ltail->next = lq;
	    b->ltail = lq;
	  } else
	  { b->lhead = b->ltail = lq;
					      /* awake main thread */
	    PostThreadMessage(b->application_thread_id, WM_RLC_INPUT, 0, 0);
	  }
	}

	break;
      }
      case IMODE_RAW:
      { MSG msg;
      
	if ( rlc_get_message(&msg, NULL, 0, 0) )
	{ TranslateMessage(&msg);
	  DispatchMessage(&msg);
	  rlc_flush_output(b);
	} else
	  goto out;

	if ( b->imodeswitch )
	{ b->imodeswitch = FALSE;
	}
      }
    }
  }

  if ( b->closing <= 2 )
  { MSG msg;
    char *waiting = "\r\nWaiting for Prolog. "
      		    "Close again to force termination ..";
      
    rlc_write(b, waiting, strlen(waiting));

    while ( b->closing <= 2 && rlc_get_message(&msg, NULL, 0, 0) )
    { TranslateMessage(&msg);
      DispatchMessage(&msg);
      rlc_flush_output(b);
    }
  }

out:
{ DWORD appthread = b->application_thread_id;
  rlc_destroy(b);

  PostThreadMessage(appthread, WM_RLC_READY, 0, 0);
}
  return 0;
}


		 /*******************************
		 *	  WATCOM/DOS I/O	*
		 *******************************/

int
getch(rlc_console c)
{ RlcData b = rlc_get_data(c);
  RlcQueue q = b->queue;
  int fromcon = (GetCurrentThreadId() == b->console_thread_id);

  while( rlc_is_empty_queue(q) )
  { if ( q->flags & RLC_EOF )
      return EOF;

    if ( !fromcon )
    { MSG msg;

      if ( rlc_get_message(&msg, NULL, 0, 0) )
      { TranslateMessage(&msg);
	DispatchMessage(&msg);
      } else
	return EOF;
    } else
    { rlc_dispatch(b);
      if ( b->imodeswitch )
      { b->imodeswitch = FALSE;
	return IMODE_SWITCH_CHAR;
      }
    }
  }

  return rlc_from_queue(q);
}


int
getche(rlc_console c)
{ RlcData b = rlc_get_data(c);
  int chr = getch(b);

  rlc_putansi(b, chr);
  return chr;
}


		 /*******************************
		 *        GO32 FUNCTIONS	*
		 *******************************/

int
getkey(rlc_console con)
{ int c;
  RlcData b = rlc_get_data(con);
  int fromcon = (GetCurrentThreadId() == b->console_thread_id);

  if ( !fromcon && b->imode != IMODE_RAW )
  { int old = b->imode;

    b->imode = IMODE_RAW;
    b->imodeswitch = TRUE;
    c = getch(b);
    b->imode = old;
    b->imodeswitch = TRUE;
  } else
    c = getch(b);

  return c;
}


int
kbhit(rlc_console c)
{ RlcData b = rlc_get_data(c);

  return !rlc_is_empty_queue(b->queue);
}


void
ScreenGetCursor(rlc_console c, int *row, int *col)
{ RlcData b = rlc_get_data(c);

  *row = rlc_count_lines(b, b->window_start, b->caret_y) + 1;
  *col = b->caret_x + 1;
}


void
ScreenSetCursor(rlc_console c, int row, int col)
{ RlcData b = rlc_get_data(c);

  rlc_set_caret(b, col-1, row-1);
}


int
ScreenCols(rlc_console c)
{ RlcData b = rlc_get_data(c);

  return b->width;
}


int
ScreenRows(rlc_console c)
{ RlcData b = rlc_get_data(c);

  return b->window_size;
}

		 /*******************************
		 *	      QUEUE		*
		 *******************************/

#define QN(q, i) ((i)+1 >= (q)->size ? 0 : (i)+1)


RlcQueue
rlc_make_queue(int size)
{ RlcQueue q;

  if ( (q = rlc_malloc(sizeof(rlc_queue))) )
  { q->first = q->last = 0;
    q->size = size;
    q->flags = 0;

    if ( (q->buffer = rlc_malloc(sizeof(short) * size)) )
      return q;
  }

  return NULL;				/* not enough memory */
}


void
rlc_free_queue(RlcQueue q)
{ if ( q )
  { if ( q->buffer )
      rlc_free(q->buffer);
    rlc_free(q);
  }
} 


static int
rlc_add_queue(RlcData b, RlcQueue q, int chr)
{ int empty = (q->first == q->last);

  if ( QN(q, q->last) != q->first )
  { q->buffer[q->last] = chr;
    q->last = QN(q, q->last);

    if ( empty )
      PostThreadMessage(b->application_thread_id, WM_RLC_INPUT, 0, 0);

    return TRUE;
  }

  return FALSE;
}


int
rlc_is_empty_queue(RlcQueue q)
{ if ( q->first == q->last )
    return TRUE;

  return FALSE;
}


void
rlc_empty_queue(RlcQueue q)
{ q->first = q->last = 0;
}


static int
rlc_from_queue(RlcQueue q)
{ if ( q->first != q->last )
  { int chr = q->buffer[q->first];

    q->first = QN(q, q->first);

    return chr;
  }

  return -1;
}


		 /*******************************
		 *	   BUFFERED I/O		*
		 *******************************/

int
rlc_read(rlc_console c, char *buf, unsigned int count)
{ RlcData d = rlc_get_data(c);
  int give;
  MSG msg;

  if ( d->closing )
    return 0;				/* signal EOF when closing */

  PostThreadMessage(d->console_thread_id,
		    WM_RLC_FLUSH,
		    0, 0);
  if ( _rlc_update_hook )
    (*_rlc_update_hook)();

  d->promptbuf[d->promptlen] = EOS;
  strcpy(d->prompt, d->promptbuf);

  if ( d->read_buffer.given >= d->read_buffer.length )
  { if ( d->read_buffer.line )
    { rlc_free(d->read_buffer.line);
      d->read_buffer.line = NULL;
    }

    if ( d->imode != IMODE_COOKED )
    { d->imode = IMODE_COOKED;
      d->imodeswitch = TRUE;
    }

    while(!d->lhead)
    { if ( rlc_get_message(&msg, NULL, 0, 0) )
      { TranslateMessage(&msg);
	DispatchMessage(&msg);
      } else
	return -1;
    }

    { LQueued lq = d->lhead;
      d->read_buffer.line = lq->line;
      if ( lq->next )
	d->lhead = lq->next;
      else
	d->lhead = d->ltail = NULL;

      rlc_free(lq);
    }

    d->read_buffer.length = strlen(d->read_buffer.line);
    d->read_buffer.given = 0;
  }

  if ( d->read_buffer.length - d->read_buffer.given > count )
    give = count;
  else
    give = d->read_buffer.length - d->read_buffer.given;

  memcpy(buf, d->read_buffer.line+d->read_buffer.given, give);
  d->read_buffer.given += give;

  return give;
}


static void
rlc_do_write(RlcData b, char *buf, int count)
{ if ( count > 0 )
  { int n = 0;
    char *s = buf;

    while(n++ < count)
    { int chr = *s++;

      if ( chr == '\n' )
	rlc_putansi(b, '\r');
      rlc_putansi(b, chr);
    }

    rlc_normalise(b);
    if ( b->window )
    { rlc_request_redraw(b);
      UpdateWindow(b->window);
    }
  }
}


int
rlc_flush_output(rlc_console c)
{ RlcData b = rlc_get_data(c);

  if ( !b )
    return -1;

  if ( b->output_queued )
  { rlc_do_write(b, b->output_queue, b->output_queued);

    b->output_queued = 0;
  }

  return 0;
}


int
rlc_write(rlc_console c, char *buf, unsigned int count)
{ DWORD result;
  char *e, *s;
  RlcData b = rlc_get_data(c);

  if ( !b )
    return -1;

  for(s=buf, e=&buf[count]; s<e; s++)
  { if ( *s == '\n' )
      b->promptlen = 0;
    else if ( b->promptlen < MAXPROMPT-1 )
      b->promptbuf[b->promptlen++] = *s;
  }

  if ( b->window )
  { if ( SendMessageTimeout(b->window,
			    WM_RLC_WRITE,
			    (WPARAM)count, 
			    (LPARAM)buf,
			    SMTO_NORMAL,
			    10000,
			    &result) )
    { PostMessage(b->window,
		  WM_RLC_FLUSH,
		  0, 0);
      return count;
    }
  }

  return -1;				/* I/O error */
}


static void
free_rlc_data(RlcData b)
{ b->magic = 42;			/* so next gets errors */

  if ( b->lines )
  { int i;

    for(i=0; i<b->height; i++)
    { if ( b->lines[i].text )
	free(b->lines[i].text);
    }

    free(b->lines);
  }
  if ( b->read_buffer.line )
    free(b->read_buffer.line);

  free(b);
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
rlc_close() tries to gracefully get rid of   the console thread. It does
so by posting WM_RLC_CLOSEWIN and then waiting for a WM_RLC_READY reply.
It waits for a maximum of  1.5  second,   which  should  be  fine as the
console thread should not have long-lasting activities.

If the timeout expires it hopes for the best. This was the old situation
and proved to be sound on Windows-NT, but not on 95 and '98.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

int
rlc_close(rlc_console c)
{ RlcData b = (RlcData)c;
  MSG msg;
  int i;

  if ( b->magic != RLC_MAGIC )
    return -1;

  rlc_save_options(b);
  b->closing = 3;
  PostMessage(b->window, WM_RLC_CLOSEWIN, 0, 0);

					/* wait for termination */
  for(i=0; i<30; i++)
  { if ( PeekMessage(&msg, NULL, WM_RLC_READY, WM_RLC_READY, PM_REMOVE) )
      break;
    Sleep(50);
  }

  b->magic = 0;
  free_user_data(c);
  free_rlc_data(b);

  return 0;
}


const char *
rlc_prompt(rlc_console c, const char *new)
{ RlcData b = rlc_get_data(c);

  if ( b )
  { if ( new )
    { strncpy(b->prompt, new, MAXPROMPT);
      b->prompt[MAXPROMPT-1] = EOS;
    }

    return b->prompt;
  }
  
  return "";
}


void
rlc_clearprompt(rlc_console c)
{ RlcData b = rlc_get_data(c);

  if ( b )
  { b->promptlen = 0;
    b->prompt[0] = EOS;
  }
}


		 /*******************************
		 *	    MISC STUFF		*
		 *******************************/

static char current_title[RLC_TITLE_MAX];

void
rlc_title(rlc_console c, char *title, char *old, int size)
{ RlcData b = rlc_get_data(c);

  if ( old )
    memmove(old, b->current_title, size);

  if ( title )
  { if ( b->window )
      SetWindowText(b->window, title);

    memmove(b->current_title, title, RLC_TITLE_MAX);
  }
}


void
rlc_icon(rlc_console c, HICON icon)
{ SetClassLong(rlc_hwnd(c), GCL_HICON, (LONG) icon);
}


int
rlc_window_pos(rlc_console c,
	       HWND hWndInsertAfter,
	       int x, int y, int w, int h,
	       UINT flags)
{ RlcData b = rlc_get_data(c);

  if ( b )
  { w *= b->cw;
    h *= b->ch;

    SetWindowPos(b->window, hWndInsertAfter,
		 x, y, w, h,
		 flags);

    return TRUE;
  }

  return FALSE;
}


HANDLE
rlc_hinstance()
{ return _rlc_hinstance;
}


HWND
rlc_hwnd(rlc_console c)
{ RlcData b = rlc_get_data(c);

  return b ? b->window : (HWND)NULL;
}

		 /*******************************
		 *	 SETTING OPTIONS	*
		 *******************************/

int
rlc_copy_output_to_debug_output(int new)
{ int old = _rlc_copy_output_to_debug_output;

  _rlc_copy_output_to_debug_output = new;

  return old;
}

RlcUpdateHook
rlc_update_hook(RlcUpdateHook new)
{ RlcUpdateHook old = _rlc_update_hook;

  _rlc_update_hook = new;
  return old;
}

RlcTimerHook
rlc_timer_hook(RlcTimerHook new)
{ RlcTimerHook old = _rlc_timer_hook;

  _rlc_timer_hook = new;
  return old;
}

RlcRenderHook
rlc_render_hook(RlcRenderHook new)
{ RlcRenderHook old = _rlc_render_hook;

  _rlc_render_hook = new;
  return old;
}

RlcRenderAllHook
rlc_render_all_hook(RlcRenderAllHook new)
{ RlcRenderAllHook old = _rlc_render_all_hook;

  _rlc_render_all_hook = new;
  return old;
}

RlcInterruptHook
rlc_interrupt_hook(RlcInterruptHook new)
{ RlcInterruptHook old = _rlc_interrupt_hook;

  _rlc_interrupt_hook = new;
  return old;
}

RlcResizeHook
rlc_resize_hook(RlcResizeHook new)
{ RlcResizeHook old = _rlc_resize_hook;

  _rlc_resize_hook = new;
  return old;
}

RlcMenuHook
rlc_menu_hook(RlcMenuHook new)
{ RlcMenuHook old = _rlc_menu_hook;

  _rlc_menu_hook = new;
  return old;
}


RlcMessageHook
rlc_message_hook(RlcMessageHook new)
{ RlcMessageHook old = _rlc_message_hook;

  _rlc_message_hook = new;
  return old;
}


int
rlc_set(rlc_console c, int what, unsigned long data, RlcFreeDataHook hook)
{ RlcData b = rlc_get_data(c);

  switch(what)
  { default:
      if ( what >= RLC_VALUE(0) &&
	   what <= RLC_VALUE(MAX_USER_VALUES) )
      { b->values[what-RLC_VALUE(0)].data = data;
	b->values[what-RLC_VALUE(0)].hook = hook;
        return TRUE;
      }
      return FALSE;
  }
}


int
rlc_get(rlc_console c, int what, unsigned long *data)
{ RlcData b = (RlcData)c;

  if ( !b )
    return FALSE;

  switch(what)
  { case RLC_APPLICATION_THREAD:
      *data = (unsigned long)b->application_thread;
      return TRUE;
    case RLC_APPLICATION_THREAD_ID:
      *data = (unsigned long)b->application_thread_id;
      return TRUE;
    default:
      if ( what >= RLC_VALUE(0) &&
	   what <= RLC_VALUE(MAX_USER_VALUES) )
      { *data = b->values[what-RLC_VALUE(0)].data;
        return TRUE;
      }
      return FALSE;
  }
}


static void
free_user_data(RlcData b)
{ user_data *d = b->values;
  int i;

  for(i=0; i<MAX_USER_VALUES; i++, d++)
  { RlcFreeDataHook hook;

    if ( (hook=d->hook) )
    { unsigned long data = d->data;
      d->hook = NULL;
      d->data = 0L;
      (*hook)(data);
    }
  }
}

		 /*******************************
		 *	       UTIL		*
		 *******************************/

static void
noMemory()
{ MessageBox(NULL, "Not enough memory", "Console", MB_OK|MB_TASKMODAL);

  ExitProcess(1);
}


void *
rlc_malloc(int size)
{ void *ptr = malloc(size);

  if ( !ptr && size > 0 )
    noMemory();

#ifdef _DEBUG
  memset(ptr, 0xbf, size);
#endif
  return ptr;
}


void *
rlc_realloc(void *ptr, int size)
{ void *ptr2 = realloc(ptr, size);

  if ( !ptr2 && size > 0 )
    noMemory();

  return ptr2;
}


void
rlc_free(void *ptr)
{ free(ptr);
}

#ifdef O_DEBUG

		 /*******************************
		 *	       DEBUG		*
		 *******************************/

static void
initHeapDebug(void)
{ int tmpFlag = _CrtSetDbgFlag( _CRTDBG_REPORT_FLAG );

  if ( !(tmpFlag & _CRTDBG_CHECK_ALWAYS_DF) )
  { /*MessageBox(NULL,
	       "setting malloc() debugging",
	       "SWI-Prolog console",
	       MB_OK|MB_TASKMODAL);*/
    tmpFlag |= _CRTDBG_CHECK_ALWAYS_DF;
    _CrtSetDbgFlag(tmpFlag);
  } else
  {
    /*MessageBox(NULL,
	       "malloc() debugging lready set",
	       "SWI-Prolog console",
	       MB_OK|MB_TASKMODAL);*/
  }
}


static void
Dprintf(const char *fmt, ...)
{ char buf[1024];
  va_list args;

  va_start(args, fmt);
  vsprintf(buf, fmt, args);
  va_end(args);

  OutputDebugString(buf);
}

static void
Dprint_lines(RlcData b, int from, int to)
{ char buf[1024];

  for( ; ; from = NextLine(b, from))
  { TextLine tl = &b->lines[from];

    memcpy(buf, tl->text, tl->size);
    buf[tl->size] = EOS;
    Dprintf("%03d: (0x%08x) \"%s\"\n", from, tl->text, buf);

    if ( from == to )
      break;
  }
}

#endif /*O_DEBUG*/
