/*
 * Copyright (c) 2008-2013 Martin Mitas
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "misc.h"
#include "compat.h"
#include "module.h"
#include "theme.h"
#include "xcom.h"


/***************
 *** Globals ***
 ***************/

HINSTANCE mc_instance;
HINSTANCE mc_instance_kernel32;
DWORD mc_win_version;
DWORD mc_comctl32_version;

HIMAGELIST mc_bmp_glyphs;


static void (WINAPI *fn_InitCommonControlsEx)(INITCOMMONCONTROLSEX*) = NULL;


/************************
 *** String Utilities ***
 ************************/

TCHAR*
mc_str_load(UINT id)
{
#ifndef UNICODE
    #error mc_str_load() is not (yet?) implemented for ANSI build.
#endif

    const UINT lang_id[2] = { MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
                              MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT) };
    TCHAR* rsrc_id = MAKEINTRESOURCE(id/16 + 1);
    int str_num = (id & 15);
    HRSRC rsrc;
    HGLOBAL handle;
    WCHAR* str;
    UINT len;
    int i, j;

    for(i = 0; i < MC_ARRAY_SIZE(lang_id); i++) {
        rsrc = FindResourceEx(mc_instance, RT_STRING, rsrc_id, lang_id[i]);
        if(MC_ERR(rsrc == NULL))
            goto not_found;
        handle = LoadResource(mc_instance, rsrc);
        if(MC_ERR(handle == NULL))
            goto not_found;
        str = (WCHAR*) LockResource(handle);
        if(MC_ERR(str == NULL))
            goto not_found;

        for(j = 0; j < str_num; j++)
            str += 1 + (UINT) *str;

        len = (UINT) *str;
        if(MC_ERR(len == 0))
            goto not_found;
        str++;

        /* Verify string resources are '\0'-terminated. This is not default
         * behavior or RC.EXE as well as windres.exe. For windres.exe we
         * need to have resources in the form "foo bar\0". For RC.EXE, we need
         * to use option '/n' to terminate the strings as RC.EXE even strips
         * final '\0' from the string even when explicitly specified. */
        MC_ASSERT(str[len - 1] == L'\0');

        return str;

not_found:
        MC_TRACE("mc_str_load: String %u missing [language 0x%x].", id,
                 (DWORD)(lang_id[i] == MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL)
                    ? LANGIDFROMLCID(GetThreadLocale()) : lang_id[i]));
    }

    return _T("");
}

void
mc_str_inbuf_A2A(const char* restrict from_str, char* restrict to_str, int to_str_bufsize)
{
    if(to_str_bufsize <= 0)
        return;

    strncpy(to_str, from_str ? from_str : "", to_str_bufsize);
    to_str[to_str_bufsize-1] = '\0';
}

void
mc_str_inbuf_W2W(const WCHAR* restrict from_str, WCHAR* restrict to_str, int to_str_bufsize)
{
    if(to_str_bufsize <= 0)
        return;

    wcsncpy(to_str, from_str ? from_str : L"", to_str_bufsize);
    to_str[to_str_bufsize-1] = L'\0';
}

void
mc_str_inbuf_A2W(const char* restrict from_str, WCHAR* restrict to_str, int to_str_bufsize)
{
    int n;

    if(to_str_bufsize <= 0)
        return;
    if(from_str == NULL)
        from_str = "";

    n = MultiByteToWideChar(CP_ACP, 0, from_str, -1, to_str, to_str_bufsize);
    if(MC_ERR(n == 0  &&  from_str[0] != '\0'))
        MC_TRACE_ERR("mc_str_inbuf_A2W: MultiByteToWideChar() failed.");
    to_str[to_str_bufsize-1] = L'\0';
}

void
mc_str_inbuf_W2A(const WCHAR* restrict from_str, char* restrict to_str, int to_str_bufsize)
{
    int n;

    if(to_str_bufsize <= 0)
        return;
    if(from_str == NULL)
        from_str = L"";

    n = WideCharToMultiByte(CP_ACP, 0, from_str, -1, to_str, to_str_bufsize, NULL, NULL);
    if(MC_ERR(n == 0  &&  from_str[0] != '\0'))
        MC_TRACE_ERR("mc_str_inbuf_W2A: WideCharToMultiByte() failed.");
    to_str[to_str_bufsize-1] = '\0';
}

char*
mc_str_n_A2A(const char* restrict from_str, int from_len, int* restrict ptr_to_len)
{
    char* to_str = NULL;
    int to_len = 0;

    if(from_str == NULL)
        goto out;

    if(from_len < 0)
        from_len = (int)strlen((char*)from_str);
    to_len = from_len;

    to_str = (char*) malloc(to_len + 1);
    if(MC_ERR(to_str == NULL)) {
        MC_TRACE("mc_str_n_A2A: malloc() failed.");
        return NULL;
    }

    memcpy(to_str, from_str, to_len);
    to_str[to_len] = '\0';

out:
    if(ptr_to_len != NULL)
        *ptr_to_len = to_len;
    return to_str;
}

WCHAR*
mc_str_n_W2W(const WCHAR* restrict from_str, int from_len, int* restrict ptr_to_len)
{
    WCHAR* to_str = NULL;
    int to_len = 0;

    if(from_str == NULL)
        goto out;

    if(from_len < 0)
        from_len = wcslen(from_str);
    to_len = from_len;

    to_str = (WCHAR*) malloc((to_len + 1) * sizeof(WCHAR));
    if(MC_ERR(to_str == NULL)) {
        MC_TRACE("mc_str_n_W2W: malloc() failed.");
        return NULL;
    }

    memcpy(to_str, from_str, to_len * sizeof(WCHAR));
    to_str[to_len] = L'\0';

out:
    if(ptr_to_len != NULL)
        *ptr_to_len = to_len;
    return to_str;
}

WCHAR*
mc_str_n_A2W(const char* restrict from_str, int from_len, int* restrict ptr_to_len)
{
    WCHAR* to_str = NULL;
    int to_len = 0;

    if(from_str == NULL)
        goto out;

    if(from_len < 0)
        from_len = (int)strlen((char*)from_str);

    to_len = MultiByteToWideChar(CP_ACP, 0, from_str, from_len, NULL, 0);
    if(MC_ERR(to_len == 0  &&  from_len > 0)) {
        MC_TRACE_ERR("mc_str_n_A2W: MultiByteToWideChar() check length failed.");
        return NULL;
    }

    to_str = (WCHAR*) malloc((to_len + 1) * sizeof(WCHAR));
    if(MC_ERR(to_str == NULL)) {
        MC_TRACE("mc_str_n_A2W: malloc() failed.");
        return NULL;
    }

    if(MC_ERR(MultiByteToWideChar(CP_ACP, 0, from_str, from_len, to_str, to_len) != to_len)) {
        MC_TRACE_ERR("mc_str_n_A2W: MultiByteToWideChar() conversion failed.");
        free(to_str);
        return NULL;
    }
    to_str[to_len] = L'\0';

out:
    if(ptr_to_len != NULL)
        *ptr_to_len = to_len;
    return to_str;
}

char*
mc_str_n_W2A(const WCHAR* restrict from_str, int from_len, int* restrict ptr_to_len)
{
    char* to_str = NULL;
    int to_len = 0;

    if(from_str == NULL)
        goto out;

    if(from_len < 0)
        from_len = (int)(wcslen((WCHAR*)from_str));

    to_len = WideCharToMultiByte(CP_ACP, 0, from_str, from_len,
                                 NULL, 0, NULL, NULL);
    if(MC_ERR(to_len == 0  &&  from_len > 0)) {
        MC_TRACE_ERR("mc_str_n_W2A: WideCharToMultiByte() check length failed.");
        return NULL;
    }

    to_str = (char*) malloc((to_len+1) * sizeof(WCHAR));
    if(MC_ERR(to_str == NULL)) {
        MC_TRACE("mc_str_n_W2A: malloc() failed.");
        return NULL;
    }

    if(MC_ERR(WideCharToMultiByte(CP_ACP, 0, from_str, from_len,
                                  to_str, to_len, NULL, NULL) != to_len)) {
        MC_TRACE_ERR("mc_str_n_W2A: WideCharToMultiByte() conversion failed.");
        free(to_str);
        return NULL;
    }
    to_str[to_len] = '\0';

out:
    if(ptr_to_len != NULL)
        *ptr_to_len = to_len;
    return to_str;
}


/***************************
 *** Loading System DLLs ***
 ***************************/


static HMODULE (WINAPI* mc_LoadLibraryEx)(const TCHAR*, HANDLE, DWORD) = NULL;

static void
setup_load_sys_dll(void)
{
    /* Win 2000 does not have LoadLibraryEx(); Win XP does have LoadLibraryEx()
     * but it does not support LOAD_LIBRARY_SEARCH_SYSTEM32. */
    if(mc_win_version <= MC_WIN_XP)
        return;

    /* Win Vista/7 prior the security update KB2533623 does not support
     * the flag LOAD_LIBRARY_SEARCH_SYSTEM32 either. The update KB2533623
     * also added AddDllDirectory() so we use that as the indicator whether
     * we can use the flag. */
    if(mc_win_version < MC_WIN_8) {
        if(GetProcAddress(mc_instance_kernel32, "AddDllDirectory") == NULL)
            return;
    }

    /* Both LoadLibraryEx() and the flag LOAD_LIBRARY_SEARCH_SYSTEM32
     * should be available. */
    mc_LoadLibraryEx = (HMODULE (WINAPI*)(const TCHAR*, HANDLE, DWORD))
                GetProcAddress(mc_instance_kernel32,
                               MC_STRINGIZE(MCTRL_NAME_AW(LoadLibraryEx)));
}

HMODULE
mc_load_sys_dll(const TCHAR* dll_name)
{
    if(mc_LoadLibraryEx != NULL)
        return mc_LoadLibraryEx(dll_name, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    else
        return LoadLibrary(dll_name);
}


/*****************************
 *** Mouse Wheel Utilities ***
 *****************************/

int
mc_wheel_scroll(HWND win, BOOL is_vertical, int wheel_delta, int lines_per_page)
{
    /* We accumulate the wheel_delta until there is enough to scroll for
     * at least a single line. This improves the feel for strange values
     * of SPI_GETWHEELSCROLLLINES and for some mouses. */
    static HWND last_win = NULL;
    static int wheel[2] = { 0, 0 };  /* horizontal, vertical */

    int dir = (is_vertical ? 1 : 0);
    UINT lines_per_WHEEL_DELTA;
    int lines_to_scroll;

    /* Do not carry accumulated values between windows */
    if(win != last_win) {
        last_win = win;
        wheel[0] = 0;
        wheel[1] = 0;

        if(win == NULL)  /* just reset */
            return 0;
    }

    /* On a direction change, reset the accumulated value */
    if((wheel_delta > 0 && wheel[dir] < 0) || (wheel_delta < 0 && wheel[dir] > 0))
        wheel[dir] = 0;

    if(lines_per_page > 0) {
        /* Ask the system for scroll speed */
        if(MC_ERR(!SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &lines_per_WHEEL_DELTA, 0)))
            lines_per_WHEEL_DELTA = 3;

        /* But never scroll more then complete page */
        if(lines_per_WHEEL_DELTA == WHEEL_PAGESCROLL  ||  lines_per_WHEEL_DELTA >= lines_per_page)
            lines_per_WHEEL_DELTA = MC_MAX(1, lines_per_page);
    } else {
        /* The caller does not know what page is: The control probably only has
         * few discrete values to iterate over. */
        lines_per_WHEEL_DELTA = 1;
    }

    /* Compute lines to scroll */
    wheel[dir] += wheel_delta;
    lines_to_scroll = (wheel[dir] * (int)lines_per_WHEEL_DELTA) / WHEEL_DELTA;
    wheel[dir] -= (lines_to_scroll * WHEEL_DELTA) / (int)lines_per_WHEEL_DELTA;

    return (is_vertical ? -lines_to_scroll : lines_to_scroll);
}


/**************************
 *** Assorted Utilities ***
 **************************/

void
mc_init_common_controls(DWORD icc)
{
    if(fn_InitCommonControlsEx != NULL) {
        INITCOMMONCONTROLSEX icce = { 0 };
        icce.dwSize = sizeof(INITCOMMONCONTROLSEX);
        icce.dwICC = icc;
        fn_InitCommonControlsEx(&icce);
    } else {
        InitCommonControls();
    }
}

void
mc_icon_size(HICON icon, SIZE* size)
{
    ICONINFO ii;
    BITMAP bmp;

    if(icon == NULL) {
        size->cx = 0;
        size->cy = 0;
        return;
    }

    GetIconInfo(icon, &ii);
    GetObject(ii.hbmMask, sizeof(BITMAP), &bmp);

    size->cx = bmp.bmWidth;
    size->cy = bmp.bmHeight;

    /* In cases the HICON is monochromatic both the icon and its mask
     * are stored in the hbmMask member (upper half is the icon, the
     * lower half is the mask). */
    if(ii.hbmColor == NULL)
        size->cy /= 2;
    else
        DeleteObject(ii.hbmColor);

    DeleteObject(ii.hbmMask);
}

void
mc_font_size(HFONT font, SIZE* size, BOOL include_external_leading)
{
    /* See http://support.microsoft.com/kb/125681 */
    static const TCHAR canary_str[] = _T("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
    static const int canary_len = MC_ARRAY_SIZE(canary_str) - 1;
    HDC dc;
    HFONT old_font;
    SIZE s;
    TEXTMETRIC tm;

    if(font == NULL)
        font = GetStockObject(SYSTEM_FONT);

    dc = GetDCEx(NULL, NULL, DCX_CACHE);
    old_font = SelectObject(dc, font);
    GetTextExtentPoint32(dc, canary_str, canary_len, &s);
    GetTextMetrics(dc, &tm);
    SelectObject(dc, old_font);
    ReleaseDC(NULL, dc);

    size->cx = (s.cx / (canary_len/2) + 1) / 2;
    size->cy = tm.tmHeight;

    if(include_external_leading)
        size->cy += tm.tmExternalLeading;
}

void
mc_string_size(const TCHAR* str, HFONT font, SIZE* size)
{
    HDC dc;
    HFONT old_font;

    if(font == NULL)
        font = GetStockObject(SYSTEM_FONT);

    dc = GetDCEx(NULL, NULL, DCX_CACHE);
    old_font = SelectObject(dc, font);
    GetTextExtentPoint32(dc, str, _tcslen(str), size);
    SelectObject(dc, old_font);
    ReleaseDC(NULL, dc);
}

int
mc_pixels_from_dlus(HFONT font, int dlus, BOOL vert)
{
    SIZE font_size;
    mc_font_size(font, &font_size, FALSE);
    if(vert)
        return (dlus * font_size.cy + 2) / 8;
    else
        return (dlus * font_size.cx + 2) / 4;
}

int
mc_dlus_from_pixels(HFONT font, int pixels, BOOL vert)
{
    SIZE font_size;
    mc_font_size(font, &font_size, FALSE);
    if(vert)
        return (16 * pixels + font_size.cy) / (2 * font_size.cy);
    else
        return (8 * pixels + font_size.cx) / (2 * font_size.cx);
}


/**********************
 *** Initialization ***
 **********************/

#if DEBUG >= 2

/* Unit testing forward declarations. */
void rgn16_test(void);

static void
perform_unit_tests(void)
{
    rgn16_test();
}

#endif  /* #if DEBUG >= 2 */


#ifdef DEBUG
    #define DEFINE_WIN_VERSION(id, name)    { id, name }
#else
    #define DEFINE_WIN_VERSION(id, name)    { id }
#endif

static const struct {
    DWORD version;
#ifdef DEBUG
    const char* name;
#endif
} win_versions[] = {
    DEFINE_WIN_VERSION( MC_WIN_10,        "Windows 10" ),
    DEFINE_WIN_VERSION( MC_WIN_8_1,       "Windows 8.1" ),
    DEFINE_WIN_VERSION( MC_WIN_8,         "Windows 8" ),
    DEFINE_WIN_VERSION( MC_WIN_S2008R2,   "Windows Server 2008 R2" ),
    DEFINE_WIN_VERSION( MC_WIN_7_SP1,     "Windows 7 (SP1)" ),
    DEFINE_WIN_VERSION( MC_WIN_7,         "Windows 7" ),
    DEFINE_WIN_VERSION( MC_WIN_S2008,     "Windows Server 2008" ),
    DEFINE_WIN_VERSION( MC_WIN_VISTA_SP2, "Windows Vista (SP2)" ),
    DEFINE_WIN_VERSION( MC_WIN_VISTA_SP1, "Windows Vista (SP1)" ),
    DEFINE_WIN_VERSION( MC_WIN_VISTA,     "Windows Vista" ),
    DEFINE_WIN_VERSION( MC_WIN_S2003,     "Windows Server 2003" ),
    DEFINE_WIN_VERSION( MC_WIN_XP_SP3,    "Windows XP (SP2)" ),
    DEFINE_WIN_VERSION( MC_WIN_XP_SP2,    "Windows XP (SP2)" ),
    DEFINE_WIN_VERSION( MC_WIN_XP_SP1,    "Windows XP (SP1)" ),
    DEFINE_WIN_VERSION( MC_WIN_XP,        "Windows XP" ),
    DEFINE_WIN_VERSION( MC_WIN_2000,      "Windows 2000" ),
    DEFINE_WIN_VERSION( MC_WIN_NT4,       "Windows NT4" )
};

static void
setup_win_version(void)
{
    OSVERSIONINFOEX ver;
    DWORDLONG cond_mask;
    int i;

    cond_mask = 0;
    cond_mask = VerSetConditionMask(cond_mask, VER_MAJORVERSION, VER_GREATER_EQUAL);
    cond_mask = VerSetConditionMask(cond_mask, VER_MINORVERSION, VER_GREATER_EQUAL);
    cond_mask = VerSetConditionMask(cond_mask, VER_SERVICEPACKMAJOR, VER_GREATER_EQUAL);

    ver.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);

    for(i = 0; i < MC_ARRAY_SIZE(win_versions); i++) {
        ver.dwMajorVersion = (win_versions[i].version & 0x00ff0000) >> 16;
        ver.dwMinorVersion = (win_versions[i].version & 0x0000ff00) >> 8;
        ver.wServicePackMajor = (win_versions[i].version & 0x000000ff) >> 0;
        if(VerifyVersionInfo(&ver, VER_MAJORVERSION | VER_MINORVERSION |
                             VER_SERVICEPACKMAJOR, cond_mask)) {
            MC_TRACE("setup_win_version: Detected %hs.", win_versions[i].name);
            mc_win_version = win_versions[i].version;
            return;
        }
    }

    MC_TRACE("setup_win_version: Failed to detect Windows version.");
    mc_win_version = 0;
}

static void
setup_comctl32_version(void)
{
    HMODULE dll;
    DLLGETVERSIONPROC fn_DllGetVersion;
    DLLVERSIONINFO vi;

    /* GetModuleHandle() is safe here instead of LoadLibrary() because
     * MCTRL.DLL is linked with COMCTL32.DLL. Hence it already has been
     * mapped in memory of the process when we come here.
     */
    dll = GetModuleHandle(_T("COMCTL32.DLL"));
    MC_ASSERT(dll != NULL);

    /* Remember InitCommonControlsEx address for mc_init_common_controls(). */
    fn_InitCommonControlsEx = (void (WINAPI *)(INITCOMMONCONTROLSEX*))
                GetProcAddress(dll, "InitCommonControlsEx");

    /* Detect COMCTL32.DLL version */
    fn_DllGetVersion = (DLLGETVERSIONPROC) GetProcAddress(dll, "DllGetVersion");
    if(MC_ERR(fn_DllGetVersion == NULL)) {
        MC_TRACE("setup_comctl32_version: DllGetVersion not found. "
                 "Assuming COMCTL32.DLL 4.0");
        mc_comctl32_version = MC_DLL_VER(4, 0);
        return;
    }

    vi.cbSize = sizeof(DLLVERSIONINFO);
    fn_DllGetVersion(&vi);
    MC_TRACE("setup_comctl32_version: Detected COMCTL32.DLL %u.%u (build %u)",
             (unsigned)vi.dwMajorVersion, (unsigned)vi.dwMinorVersion,
             (unsigned)vi.dwBuildNumber);

    mc_comctl32_version = MC_DLL_VER(vi.dwMajorVersion, vi.dwMinorVersion);
}

int
mc_init_module(void)
{
    /* Load set of helper symbols used for helper buttons of more complex
     * controls */
    mc_bmp_glyphs = ImageList_LoadBitmap(mc_instance, MAKEINTRESOURCE(
                           IDR_GLYPHS), MC_BMP_GLYPH_W, 1, RGB(255,0,255));
    if(MC_ERR(mc_bmp_glyphs == NULL)) {
        MC_TRACE_ERR("mc_init_module: ImageList_LoadBitmap() failed");
        return -1;
    }

    /* Retrieve version of Windows and COMCTL32.DLL */
    setup_win_version();
    setup_load_sys_dll();
    setup_comctl32_version();

#if DEBUG >= 2
    /* In debug builds, we may want to run few basic unit tests. */
    perform_unit_tests();
#endif

    /* Success */
    return 0;
}

void
mc_fini_module(void)
{
    ImageList_Destroy(mc_bmp_glyphs);
}

/****************
 *** Tooltips ***
 ****************/

HWND
mc_tooltip_create(HWND parent, HWND notify, BOOL track)
{
    HWND ret;
    TTTOOLINFO info = { 0 };
    NMTOOLTIPSCREATED ttc = { { 0 }, 0 };

    ret = CreateWindow(TOOLTIPS_CLASS, NULL, WS_POPUP,
                       0, 0, 0, 0, parent, 0, 0, 0);
    if(MC_ERR(ret == NULL)) {
        MC_TRACE_ERR("tooltip_create: CreateWindow() failed");
        return NULL;
    }

    info.cbSize = sizeof(TTTOOLINFO);
    info.uFlags = TTF_IDISHWND | TTF_ABSOLUTE;
    if(track)
        info.uFlags |= TTF_TRACK;

    info.hwnd = parent;
    MC_SEND(ret, TTM_ADDTOOL, 0, &info);

    if(notify != NULL) {
        ttc.hdr.hwndFrom = parent;
        ttc.hdr.idFrom = GetWindowLong(parent, GWL_ID);
        ttc.hdr.code = NM_TOOLTIPSCREATED;
        ttc.hwndToolTips = ret;
        MC_SEND(notify, WM_NOTIFY, ttc.hdr.idFrom, &ttc);
    }

    return ret;
}

void
mc_tooltip_track_activate(HWND parent, HWND tooltip, BOOL show)
{
    TTTOOLINFO info = { 0 };

    info.cbSize = sizeof(TTTOOLINFO);
    info.hwnd = parent;
    MC_SEND(tooltip, TTM_TRACKACTIVATE, show, &info);
}

void
mc_tooltip_set_track_pos(HWND parent, HWND tooltip, int x, int y)
{
    TTTOOLINFO info = { 0 };
    DWORD size;
    POINT pt;

    info.cbSize = sizeof(TTTOOLINFO);
    info.hwnd = parent;

    size = MC_SEND(tooltip, TTM_GETBUBBLESIZE, 0, &info);
    pt.x = x - LOWORD(size) / 2;
    pt.y = y - HIWORD(size) - 5;
    ClientToScreen(parent, &pt);
    MC_SEND(tooltip, TTM_TRACKPOSITION, 0, MAKELPARAM(pt.x, pt.y));
}

void
mc_tooltip_set_text(HWND parent, HWND tooltip, const TCHAR* str)
{
    TTTOOLINFO info = { 0 };

    info.cbSize = sizeof(TTTOOLINFO);
    info.hwnd = parent;
    info.lpszText = (TCHAR*) str;
    MC_SEND(tooltip, TTM_UPDATETIPTEXT, 0, &info);
}

/*****************
 *** DllMain() ***
 *****************/

BOOL WINAPI
DllMain(HINSTANCE instance, DWORD reason, VOID* ignored)
{
    switch(reason) {
        case DLL_PROCESS_ATTACH:
            MC_TRACE("***************************************"
                     "***************************************");
            MC_TRACE("DllMain(DLL_PROCESS_ATTACH): MCTRL.DLL version %hs (%d bit)",
                     MC_VERSION_STR, 8 * sizeof(void*));

            mc_instance = instance;

            /* This is somewhat scary to do within DllMain() context because of
             * all the limitations DllMain() imposes. But I believe it's valid
             * because system DLL loader lock is reentrant and "KERNEL32.DLL"
             * must already be mapped into the process memory and initialized
             * (Otherwise InitializeCriticalSection() would not be allowed
             * to work).
             *
             * Unfortunately we need it this early for compat_init() below.
             */
            mc_instance_kernel32 = GetModuleHandle(_T("KERNEL32.DLL"));
            if(MC_ERR(mc_instance_kernel32 == NULL)) {
                MC_TRACE_ERR("DllMain: GetModuleHandle(KERNEL32.DLL) failed.");
                return FALSE;
            }

            DisableThreadLibraryCalls(instance);

            compat_init();
            debug_init();
            module_init();
            xcom_init();
            break;

        case DLL_PROCESS_DETACH:
        {
            xcom_fini();
            module_fini();
            debug_fini();
            compat_fini();
            MC_TRACE("DllMain(DLL_PROCESS_DETACH)");
            break;
        }
    }

    return TRUE;
}


/* Include the main public header file as we actually never do this elsewhere.
 * This at least verifies there is no compilation problem with it,
 * e.g. some idendifier clash between some of the public headers etc. */
#include "mctrl.h"

