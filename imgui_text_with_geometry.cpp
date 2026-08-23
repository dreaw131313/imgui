#include "imgui_text_with_geometry.h"

#include <string>
#include "imgui_internal.h"

namespace ImStb
{
#include "imstb_textedit.h"
}


static ImVec2 InputTextCalcTextSize(ImGuiContext* ctx, const char* text_begin, const char* text_end_display, const char* text_end, const char** out_remaining, ImVec2* out_offset, ImDrawTextFlags flags)
{
    ImGuiContext& g = *ctx;
    ImGuiInputTextState* obj = &g.InputTextState;
    IM_ASSERT(text_end_display >= text_begin && text_end_display <= text_end);
    return ImFontCalcTextSizeEx(g.Font, g.FontSize, FLT_MAX, obj->WrapWidth, text_begin, text_end_display, text_end, out_remaining, out_offset, flags);
}

// Return false to discard a character.
static bool InputTextFilterCharacter(ImGuiContext* ctx, ImGuiInputTextState* state, unsigned int* p_char, ImGuiInputTextCallback callback, void* user_data, bool input_source_is_clipboard = false)
{
    IM_ASSERT(state != NULL);
    unsigned int c = *p_char;
    ImGuiInputTextFlags flags = state->Flags;

    // Filter non-printable (NB: isprint is unreliable! see #2467)
    bool apply_named_filters = true;
    if (c < 0x20)
    {
        bool pass = false;
        pass |= (c == '\n') && (flags & ImGuiInputTextFlags_Multiline) != 0;    // Note that an Enter KEY will emit \r and be ignored (we poll for KEY in InputText() code)
        if (c == '\n' && input_source_is_clipboard && (flags & ImGuiInputTextFlags_Multiline) == 0) // In single line mode, replace \n with a space
        {
            c = *p_char = ' ';
            pass = true;
        }
        pass |= (c == '\n') && (flags & ImGuiInputTextFlags_Multiline) != 0;
        pass |= (c == '\t') && (flags & ImGuiInputTextFlags_AllowTabInput) != 0;
        if (!pass)
            return false;
        apply_named_filters = false; // Override named filters below so newline and tabs can still be inserted.
    }

    if (input_source_is_clipboard == false)
    {
        // We ignore Ascii representation of delete (emitted from Backspace on OSX, see #2578, #2817)
        if (c == 127)
            return false;

        // Filter private Unicode range. GLFW on OSX seems to send private characters for special keys like arrow keys (FIXME)
        if (c >= 0xE000 && c <= 0xF8FF)
            return false;
    }

    // Filter Unicode ranges we are not handling in this build
    if (c > IM_UNICODE_CODEPOINT_MAX)
        return false;

    // Generic named filters
    if (apply_named_filters && (flags & (ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank | ImGuiInputTextFlags_CharsScientific | (ImGuiInputTextFlags)ImGuiInputTextFlags_LocalizeDecimalPoint)))
    {
        // The libc allows overriding locale, with e.g. 'setlocale(LC_NUMERIC, "de_DE.UTF-8");' which affect the output/input of printf/scanf to use e.g. ',' instead of '.'.
        // The standard mandate that programs starts in the "C" locale where the decimal point is '.'.
        // We don't really intend to provide widespread support for it, but out of empathy for people stuck with using odd API, we support the bare minimum aka overriding the decimal point.
        // Change the default decimal_point with:
        //   ImGui::GetPlatformIO()->Platform_LocaleDecimalPoint = *localeconv()->decimal_point;
        // Users of non-default decimal point (in particular ',') may be affected by word-selection logic (is_word_boundary_from_right/is_word_boundary_from_left) functions.
        ImGuiContext& g = *ctx;
        const unsigned c_decimal_point = (unsigned int)g.PlatformIO.Platform_LocaleDecimalPoint;
        if (flags & (ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_CharsScientific | (ImGuiInputTextFlags)ImGuiInputTextFlags_LocalizeDecimalPoint))
            if (c == '.' || c == ',')
                c = c_decimal_point;

        // Full-width -> half-width conversion for numeric fields: https://en.wikipedia.org/wiki/Halfwidth_and_Fullwidth_Forms_(Unicode_block)
        // While this is mostly convenient, this has the side-effect for uninformed users accidentally inputting full-width characters that they may
        // scratch their head as to why it works in numerical fields vs in generic text fields it would require support in the font.
        if (flags & (ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_CharsScientific | ImGuiInputTextFlags_CharsHexadecimal))
            if (c >= 0xFF01 && c <= 0xFF5E)
                c = c - 0xFF01 + 0x21;

        // Allow 0-9 . - + * /
        if (flags & ImGuiInputTextFlags_CharsDecimal)
            if (!(c >= '0' && c <= '9') && (c != c_decimal_point) && (c != '-') && (c != '+') && (c != '*') && (c != '/'))
                return false;

        // Allow 0-9 . - + * / e E
        if (flags & ImGuiInputTextFlags_CharsScientific)
            if (!(c >= '0' && c <= '9') && (c != c_decimal_point) && (c != '-') && (c != '+') && (c != '*') && (c != '/') && (c != 'e') && (c != 'E'))
                return false;

        // Allow 0-9 a-F A-F
        if (flags & ImGuiInputTextFlags_CharsHexadecimal)
            if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f') && !(c >= 'A' && c <= 'F'))
                return false;

        // Turn a-z into A-Z
        if (flags & ImGuiInputTextFlags_CharsUppercase)
            if (c >= 'a' && c <= 'z')
                c += (unsigned int)('A' - 'a');

        if (flags & ImGuiInputTextFlags_CharsNoBlank)
            if (ImCharIsBlankW(c))
                return false;

        *p_char = c;
    }

    // Custom callback filter
    if (flags & ImGuiInputTextFlags_CallbackCharFilter)
    {
        ImGuiContext& g = *GImGui;
        ImGuiInputTextCallbackData callback_data;
        callback_data.Ctx = &g;
        callback_data.ID = state->ID;
        callback_data.Flags = flags;
        callback_data.EventFlag = ImGuiInputTextFlags_CallbackCharFilter;
        callback_data.EventChar = (ImWchar)c;
        callback_data.EventActivated = (g.ActiveId == state->ID && g.ActiveIdIsJustActivated);
        callback_data.CursorPos = state->Stb->cursor;
        callback_data.SelectionStart = state->Stb->select_start;
        callback_data.SelectionEnd = state->Stb->select_end;
        callback_data.UserData = user_data;
        if (callback(&callback_data) != 0)
            return false;
        *p_char = callback_data.EventChar;
        if (!callback_data.EventChar)
            return false;
    }

    return true;
}

// FIXME-WORDWRAP: Bundle some of this into ImGuiTextIndex and/or extract as a different tool?
// 'max_output_buffer_size' happens to be a meaningful optimization to avoid writing the full line_index when not necessarily needed (e.g. very large buffer, scrolled up, inactive)
static int InputTextLineIndexBuild(ImGuiInputTextFlags flags, ImGuiTextIndex* line_index, const char* buf, const char* buf_end, float wrap_width, int max_output_buffer_size, const char** out_buf_end)
{
    ImGuiContext& g = *GImGui;
    int size = 0;
    const char* s;
    bool trailing_line_already_counted = false;
    if (flags & ImGuiInputTextFlags_WordWrap)
    {
        for (s = buf; s < buf_end; s = (*s == '\n') ? s + 1 : s)
        {
            if (size++ <= max_output_buffer_size)
                line_index->Offsets.push_back((int)(s - buf));
            s = ImFontCalcWordWrapPositionEx(g.Font, g.FontSize, s, buf_end, wrap_width, ImDrawTextFlags_WrapKeepBlanks);
        }
    }
    else if (buf_end != NULL)
    {
        for (s = buf; s < buf_end; s = s ? s + 1 : buf_end)
        {
            if (size++ <= max_output_buffer_size)
                line_index->Offsets.push_back((int)(s - buf));
            s = (const char*)ImMemchr(s, '\n', buf_end - s);
        }
    }
    else
    {
        // Inactive path: we don't know buf_end ahead of time.
        const char* s_eol;
        for (s = buf; ; s = s_eol + 1)
        {
            if (size++ <= max_output_buffer_size)
                line_index->Offsets.push_back((int)(s - buf));
            if ((s_eol = strchr(s, '\n')) != NULL)
                continue;
            s += strlen(s);
            trailing_line_already_counted = true;
            break;
        }
    }
    if (out_buf_end != NULL)
        *out_buf_end = buf_end = s;
    if (size == 0)
    {
        line_index->Offsets.push_back(0);
        size++;
    }
    if (buf_end > buf && buf_end[-1] == '\n' && !trailing_line_already_counted && size++ <= max_output_buffer_size)
        line_index->Offsets.push_back((int)(buf_end - buf));
    return size;
}

static int* ImLowerBound(int* in_begin, int* in_end, int v)
{
    int* in_p = in_begin;
    for (size_t count = (size_t)(in_end - in_p); count > 0; )
    {
        size_t count2 = count >> 1;
        int* mid = in_p + count2;
        if (*mid < v)
        {
            in_p = ++mid;
            count -= count2 + 1;
        }
        else
        {
            count = count2;
        }
    }
    return in_p;
}

static ImVec2 InputTextLineIndexGetPosOffset(ImGuiContext& g, ImGuiInputTextState* state, ImGuiTextIndex* line_index, const char* buf, const char* buf_end, int cursor_n)
{
    const char* cursor_ptr = buf + cursor_n;
    int* it_begin = line_index->Offsets.begin();
    int* it_end = line_index->Offsets.end();
    const int* it = ImLowerBound(it_begin, it_end, cursor_n);
    if (it > it_begin)
        if (it == it_end || *it != cursor_n || (state != NULL && state->WrapWidth > 0.0f && state->LastMoveDirectionLR == ImGuiDir_Right && cursor_ptr[-1] != '\n' && cursor_ptr[-1] != 0))
            it--;

    const int line_no = (it == it_begin) ? 0 : line_index->Offsets.index_from_ptr(it);
    const char* line_start = line_index->get_line_begin(buf, line_no);
    ImVec2 offset;
    offset.x = InputTextCalcTextSize(&g, line_start, cursor_ptr, buf_end, NULL, NULL, ImDrawTextFlags_WrapKeepBlanks).x;
    offset.y = (line_no + 1) * g.FontSize;
    return offset;
}


// Wrapper for stb_textedit.h to edit text (our wrapper is for: statically sized buffer, single-line, wchar characters. InputText converts between UTF-8 and wchar)
// With our UTF-8 use of stb_textedit:
// - STB_TEXTEDIT_GETCHAR is nothing more than a a "GETBYTE". It's only used to compare to ascii or to copy blocks of text so we are fine.
// - One exception is the STB_TEXTEDIT_IS_SPACE feature which would expect a full char in order to handle full-width space such as 0x3000 (see ImCharIsBlankW).
// - ...but we don't use that feature.
namespace ImStb
{
    static int     STB_TEXTEDIT_STRINGLEN(const ImGuiInputTextState* obj) { return obj->TextLen; }
    static char    STB_TEXTEDIT_GETCHAR(const ImGuiInputTextState* obj, int idx) { IM_ASSERT(idx >= 0 && idx <= obj->TextLen); return obj->TextSrc[idx]; }
    static float   STB_TEXTEDIT_GETWIDTH(ImGuiInputTextState* obj, int line_start_idx, int char_idx) { unsigned int c; ImTextCharFromUtf8(&c, obj->TextSrc + line_start_idx + char_idx, obj->TextSrc + obj->TextLen); if ((ImWchar)c == '\n') return IMSTB_TEXTEDIT_GETWIDTH_NEWLINE; ImGuiContext& g = *obj->Ctx; return g.FontBaked->GetCharAdvance((ImWchar)c) * g.FontBakedScale; }
    static char    STB_TEXTEDIT_NEWLINE = '\n';
    static void    STB_TEXTEDIT_LAYOUTROW(StbTexteditRow* r, ImGuiInputTextState* obj, int line_start_idx)
    {
        const char* text = obj->TextSrc;
        const char* text_remaining = NULL;
        const ImVec2 size = InputTextCalcTextSize(obj->Ctx, text + line_start_idx, text + obj->TextLen, text + obj->TextLen, &text_remaining, NULL, ImDrawTextFlags_StopOnNewLine | ImDrawTextFlags_WrapKeepBlanks);
        r->x0 = 0.0f;
        r->x1 = size.x;
        r->baseline_y_delta = size.y;
        r->ymin = 0.0f;
        r->ymax = size.y;
        r->num_chars = (int)(text_remaining - (text + line_start_idx));
    }

#define IMSTB_TEXTEDIT_GETNEXTCHARINDEX  IMSTB_TEXTEDIT_GETNEXTCHARINDEX_IMPL
#define IMSTB_TEXTEDIT_GETPREVCHARINDEX  IMSTB_TEXTEDIT_GETPREVCHARINDEX_IMPL

    static int IMSTB_TEXTEDIT_GETNEXTCHARINDEX_IMPL(ImGuiInputTextState* obj, int idx)
    {
        if (idx >= obj->TextLen)
            return obj->TextLen + 1;
        unsigned int c;
        return idx + ImTextCharFromUtf8(&c, obj->TextSrc + idx, obj->TextSrc + obj->TextLen);
    }

    static int IMSTB_TEXTEDIT_GETPREVCHARINDEX_IMPL(ImGuiInputTextState* obj, int idx)
    {
        if (idx <= 0)
            return -1;
        const char* p = ImTextFindPreviousUtf8Codepoint(obj->TextSrc, obj->TextSrc + idx);
        return (int)(p - obj->TextSrc);
    }

    static bool ImCharIsSeparatorW(unsigned int c)
    {
        static const unsigned int separator_list[] =
        {
            ',', 0x3001, '.', 0x3002, ';', 0xFF1B, '(', 0xFF08, ')', 0xFF09, '{', 0xFF5B, '}', 0xFF5D,
            '[', 0x300C, ']', 0x300D, '|', 0xFF5C, '!', 0xFF01, '\\', 0xFFE5, '/', 0x30FB, 0xFF0F,
            '\n', '\r',
        };
        for (unsigned int separator : separator_list)
            if (c == separator)
                return true;
        return false;
    }

    static int is_word_boundary_from_right(ImGuiInputTextState* obj, int idx)
    {
        // When ImGuiInputTextFlags_Password is set, we don't want actions such as Ctrl+Arrow to leak the fact that underlying data are blanks or separators.
        if ((obj->Flags & ImGuiInputTextFlags_Password) || idx <= 0)
            return 0;

        const char* curr_p = obj->TextSrc + idx;
        const char* prev_p = ImTextFindPreviousUtf8Codepoint(obj->TextSrc, curr_p);
        unsigned int curr_c; ImTextCharFromUtf8(&curr_c, curr_p, obj->TextSrc + obj->TextLen);
        unsigned int prev_c; ImTextCharFromUtf8(&prev_c, prev_p, obj->TextSrc + obj->TextLen);

        bool prev_white = ImCharIsBlankW(prev_c);
        bool prev_separ = ImCharIsSeparatorW(prev_c);
        bool curr_white = ImCharIsBlankW(curr_c);
        bool curr_separ = ImCharIsSeparatorW(curr_c);
        return ((prev_white || prev_separ) && !(curr_separ || curr_white)) || (curr_separ && !prev_separ);
    }
    static int is_word_boundary_from_left(ImGuiInputTextState* obj, int idx)
    {
        if ((obj->Flags & ImGuiInputTextFlags_Password) || idx <= 0)
            return 0;

        const char* curr_p = obj->TextSrc + idx;
        const char* prev_p = ImTextFindPreviousUtf8Codepoint(obj->TextSrc, curr_p);
        unsigned int prev_c; ImTextCharFromUtf8(&prev_c, curr_p, obj->TextSrc + obj->TextLen);
        unsigned int curr_c; ImTextCharFromUtf8(&curr_c, prev_p, obj->TextSrc + obj->TextLen);

        bool prev_white = ImCharIsBlankW(prev_c);
        bool prev_separ = ImCharIsSeparatorW(prev_c);
        bool curr_white = ImCharIsBlankW(curr_c);
        bool curr_separ = ImCharIsSeparatorW(curr_c);
        return ((prev_white) && !(curr_separ || curr_white)) || (curr_separ && !prev_separ);
    }
    static int  STB_TEXTEDIT_MOVEWORDLEFT_IMPL(ImGuiInputTextState* obj, int idx)
    {
        idx = IMSTB_TEXTEDIT_GETPREVCHARINDEX(obj, idx);
        while (idx >= 0 && !is_word_boundary_from_right(obj, idx))
            idx = IMSTB_TEXTEDIT_GETPREVCHARINDEX(obj, idx);
        return idx < 0 ? 0 : idx;
    }
    static int  STB_TEXTEDIT_MOVEWORDRIGHT_MAC(ImGuiInputTextState* obj, int idx)
    {
        int len = obj->TextLen;
        idx = IMSTB_TEXTEDIT_GETNEXTCHARINDEX(obj, idx);
        while (idx < len && !is_word_boundary_from_left(obj, idx))
            idx = IMSTB_TEXTEDIT_GETNEXTCHARINDEX(obj, idx);
        return idx > len ? len : idx;
    }
    static int  STB_TEXTEDIT_MOVEWORDRIGHT_WIN(ImGuiInputTextState* obj, int idx)
    {
        idx = IMSTB_TEXTEDIT_GETNEXTCHARINDEX(obj, idx);
        int len = obj->TextLen;
        while (idx < len && !is_word_boundary_from_right(obj, idx))
            idx = IMSTB_TEXTEDIT_GETNEXTCHARINDEX(obj, idx);
        return idx > len ? len : idx;
    }
    static int  STB_TEXTEDIT_MOVEWORDRIGHT_IMPL(ImGuiInputTextState* obj, int idx) { ImGuiContext& g = *obj->Ctx; if (g.IO.ConfigMacOSXBehaviors) return STB_TEXTEDIT_MOVEWORDRIGHT_MAC(obj, idx); else return STB_TEXTEDIT_MOVEWORDRIGHT_WIN(obj, idx); }
#define STB_TEXTEDIT_MOVEWORDLEFT       STB_TEXTEDIT_MOVEWORDLEFT_IMPL  // They need to be #define for stb_textedit.h
#define STB_TEXTEDIT_MOVEWORDRIGHT      STB_TEXTEDIT_MOVEWORDRIGHT_IMPL

// Reimplementation of stb_textedit_move_line_start()/stb_textedit_move_line_end() which supports word-wrapping.
    static int STB_TEXTEDIT_MOVELINESTART_IMPL(ImGuiInputTextState* obj, ImStb::STB_TexteditState* state, int cursor)
    {
        if (state->single_line)
            return 0;

        if (obj->WrapWidth > 0.0f)
        {
            ImGuiContext& g = *obj->Ctx;
            const char* p_cursor = obj->TextSrc + cursor;
            const char* p_bol = ImStrbol(p_cursor, obj->TextSrc);
            const char* p = p_bol;
            const char* text_end = obj->TextSrc + obj->TextLen; // End of line would be enough
            while (p >= p_bol)
            {
                const char* p_eol = ImFontCalcWordWrapPositionEx(g.Font, g.FontSize, p, text_end, obj->WrapWidth, ImDrawTextFlags_WrapKeepBlanks);
                if (p == p_cursor) // If we are already on a visible beginning-of-line, return real beginning-of-line (would be same as regular handler below)
                    return (int)(p_bol - obj->TextSrc);
                if (p_eol == p_cursor && obj->TextA[cursor] != '\n' && obj->LastMoveDirectionLR == ImGuiDir_Left)
                    return (int)(p_bol - obj->TextSrc);
                if (p_eol >= p_cursor)
                    return (int)(p - obj->TextSrc);
                p = (*p_eol == '\n') ? p_eol + 1 : p_eol;
            }
        }

        // Regular handler, same as stb_textedit_move_line_start()
        while (cursor > 0)
        {
            int prev_cursor = IMSTB_TEXTEDIT_GETPREVCHARINDEX(obj, cursor);
            if (STB_TEXTEDIT_GETCHAR(obj, prev_cursor) == STB_TEXTEDIT_NEWLINE)
                break;
            cursor = prev_cursor;
        }
        return cursor;
    }

    static int STB_TEXTEDIT_MOVELINEEND_IMPL(ImGuiInputTextState* obj, ImStb::STB_TexteditState* state, int cursor)
    {
        int n = STB_TEXTEDIT_STRINGLEN(obj);
        if (state->single_line)
            return n;

        if (obj->WrapWidth > 0.0f)
        {
            ImGuiContext& g = *obj->Ctx;
            const char* p_cursor = obj->TextSrc + cursor;
            const char* p = ImStrbol(p_cursor, obj->TextSrc);
            const char* text_end = obj->TextSrc + obj->TextLen; // End of line would be enough
            while (p < text_end)
            {
                const char* p_eol = ImFontCalcWordWrapPositionEx(g.Font, g.FontSize, p, text_end, obj->WrapWidth, ImDrawTextFlags_WrapKeepBlanks);
                cursor = (int)(p_eol - obj->TextSrc);
                if (p_eol == p_cursor && obj->LastMoveDirectionLR != ImGuiDir_Left) // If we are already on a visible end-of-line, switch to regular handle
                    break;
                if (p_eol > p_cursor)
                    return cursor;
                p = (*p_eol == '\n') ? p_eol + 1 : p_eol;
            }
        }
        // Regular handler, same as stb_textedit_move_line_end()
        while (cursor < n && STB_TEXTEDIT_GETCHAR(obj, cursor) != STB_TEXTEDIT_NEWLINE)
            cursor = IMSTB_TEXTEDIT_GETNEXTCHARINDEX(obj, cursor);
        return cursor;
    }

#define STB_TEXTEDIT_MOVELINESTART      STB_TEXTEDIT_MOVELINESTART_IMPL
#define STB_TEXTEDIT_MOVELINEEND        STB_TEXTEDIT_MOVELINEEND_IMPL

    static void STB_TEXTEDIT_DELETECHARS(ImGuiInputTextState* obj, int pos, int n)
    {
        // Offset remaining text (+ copy zero terminator)
        IM_ASSERT(obj->TextSrc == obj->TextA.Data);
        char* dst = obj->TextA.Data + pos;
        char* src = obj->TextA.Data + pos + n;
        memmove(dst, src, obj->TextLen - n - pos + 1);
        obj->EditedBefore = obj->EditedThisFrame = true;
        obj->TextLen -= n;
    }

    static int STB_TEXTEDIT_INSERTCHARS(ImGuiInputTextState* obj, int pos, const char* new_text, int new_text_len)
    {
        const bool is_resizable = (obj->Flags & ImGuiInputTextFlags_CallbackResize) != 0;
        const int text_len = obj->TextLen;
        IM_ASSERT(pos <= text_len);

        // We support partial insertion (with a mod in stb_textedit.h)
        const int avail = obj->BufCapacity - 1 - obj->TextLen;
        if (!is_resizable && new_text_len > avail)
            new_text_len = (int)(ImTextFindValidUtf8CodepointEnd(new_text, new_text + new_text_len, new_text + avail) - new_text); // Truncate to closest UTF-8 codepoint. Alternative: return 0 to cancel insertion.
        if (new_text_len == 0)
            return 0;

        // Grow internal buffer if needed
        IM_ASSERT(obj->TextSrc == obj->TextA.Data);
        if (text_len + new_text_len + 1 > obj->TextA.Size && is_resizable)
        {
            obj->TextA.resize(text_len + ImClamp(new_text_len, 32, ImMax(256, new_text_len)) + 1);
            obj->TextSrc = obj->TextA.Data;
        }

        char* text = obj->TextA.Data;
        if (pos != text_len)
            memmove(text + pos + new_text_len, text + pos, (size_t)(text_len - pos));
        memcpy(text + pos, new_text, (size_t)new_text_len);

        obj->EditedBefore = obj->EditedThisFrame = true;
        obj->TextLen += new_text_len;
        obj->TextA[obj->TextLen] = '\0';

        return new_text_len;
    }

    // We don't use an enum so we can build even with conflicting symbols (if another user of stb_textedit.h leak their STB_TEXTEDIT_K_* symbols)
#define STB_TEXTEDIT_K_LEFT         0x200000 // keyboard input to move cursor left
#define STB_TEXTEDIT_K_RIGHT        0x200001 // keyboard input to move cursor right
#define STB_TEXTEDIT_K_UP           0x200002 // keyboard input to move cursor up
#define STB_TEXTEDIT_K_DOWN         0x200003 // keyboard input to move cursor down
#define STB_TEXTEDIT_K_LINESTART    0x200004 // keyboard input to move cursor to start of line
#define STB_TEXTEDIT_K_LINEEND      0x200005 // keyboard input to move cursor to end of line
#define STB_TEXTEDIT_K_TEXTSTART    0x200006 // keyboard input to move cursor to start of text
#define STB_TEXTEDIT_K_TEXTEND      0x200007 // keyboard input to move cursor to end of text
#define STB_TEXTEDIT_K_DELETE       0x200008 // keyboard input to delete selection or character under cursor
#define STB_TEXTEDIT_K_BACKSPACE    0x200009 // keyboard input to delete selection or character left of cursor
#define STB_TEXTEDIT_K_UNDO         0x20000A // keyboard input to perform undo
#define STB_TEXTEDIT_K_REDO         0x20000B // keyboard input to perform redo
#define STB_TEXTEDIT_K_WORDLEFT     0x20000C // keyboard input to move cursor left one word
#define STB_TEXTEDIT_K_WORDRIGHT    0x20000D // keyboard input to move cursor right one word
#define STB_TEXTEDIT_K_PGUP         0x20000E // keyboard input to move cursor up a page
#define STB_TEXTEDIT_K_PGDOWN       0x20000F // keyboard input to move cursor down a page
#define STB_TEXTEDIT_K_SHIFT        0x400000

#define IMSTB_TEXTEDIT_IMPLEMENTATION
#define IMSTB_TEXTEDIT_memmove memmove
#include "imstb_textedit.h"

// stb_textedit internally allows for a single undo record to do addition and deletion, but somehow, calling
// the stb_textedit_paste() function creates two separate records, so we perform it manually. (FIXME: Report to nothings/stb?)
    static void stb_textedit_replace(ImGuiInputTextState* str, STB_TexteditState* state, const IMSTB_TEXTEDIT_CHARTYPE* text, int text_len)
    {
        stb_text_makeundo_replace(str, state, 0, str->TextLen, text_len);
        ImStb::STB_TEXTEDIT_DELETECHARS(str, 0, str->TextLen);
        state->cursor = state->select_start = state->select_end = 0;
        if (text_len <= 0)
            return;
        int text_len_inserted = ImStb::STB_TEXTEDIT_INSERTCHARS(str, 0, text, text_len);
        if (text_len_inserted > 0)
        {
            state->cursor = state->select_start = state->select_end = text_len;
            state->has_preferred_x = 0;
            return;
        }
        IM_ASSERT(0); // Failed to insert character, normally shouldn't happen because of how we currently use stb_textedit_replace()
    }

} // namespace ImStb
namespace ImGui
{
// Find the shortest single replacement we can make to get from old_buf to new_buf
// Note that this doesn't directly alter state->TextA, state->TextLen. They are expected to be made valid separately.
// FIXME: Ideally we should transition toward (1) making InsertChars()/DeleteChars() update undo-stack (2) discourage (and keep reconcile) or obsolete (and remove reconcile) accessing buffer directly.
    static void InputTextReconcileUndoState(ImGuiInputTextState* state, const char* old_buf, int old_length, const char* new_buf, int new_length)
    {
        const int shorter_length = ImMin(old_length, new_length);
        int first_diff;
        for (first_diff = 0; first_diff < shorter_length; first_diff++)
            if (old_buf[first_diff] != new_buf[first_diff])
                break;
        if (first_diff == old_length && first_diff == new_length)
            return;

        int old_last_diff = old_length - 1;
        int new_last_diff = new_length - 1;
        for (; old_last_diff >= first_diff && new_last_diff >= first_diff; old_last_diff--, new_last_diff--)
            if (old_buf[old_last_diff] != new_buf[new_last_diff])
                break;

        const int insert_len = new_last_diff - first_diff + 1;
        const int delete_len = old_last_diff - first_diff + 1;
        if (insert_len > 0 || delete_len > 0)
            if (IMSTB_TEXTEDIT_CHARTYPE* p = stb_text_createundo(&state->Stb->undostate, first_diff, delete_len, insert_len))
                for (int i = 0; i < delete_len; i++)
                    p[i] = old_buf[first_diff + i];
    }

    void RenderTextWithFont(
        ImFont* font,
        ImDrawList* draw_list,
        float size,
        const ImVec2& pos,
        ImU32 col,
        const ImVec4& clip_rect,
        const char* text_begin,
        const char* text_end,
        float wrap_width,
        ImDrawTextFlags flags,
        ImMulitlineTextGeometryData* textGeometryData
    )
    {
         // Align to be pixel perfect
    begin:
        float x = IM_TRUNC(pos.x);
        float y = IM_TRUNC(pos.y);
        if (y > clip_rect.w)
            return;

        if (!text_end)
            text_end = text_begin + ImStrlen(text_begin); // ImGui:: functions generally already provides a valid text_end, so this is merely to handle direct calls.

        const float line_height = size;
        ImFontBaked* baked = font->GetFontBaked(size);

        const float scale = size / baked->Size;
        const float origin_x = x;
        const bool word_wrap_enabled = (wrap_width > 0.0f);

        // Fast-forward to first visible line
        const char* s = text_begin;
        if (y + line_height < clip_rect.y)
            while (y + line_height < clip_rect.y && s < text_end)
            {
                const char* line_end = (const char*)ImMemchr(s, '\n', text_end - s);
                if (word_wrap_enabled)
                {
                    // FIXME-OPT: This is not optimal as do first do a search for \n before calling CalcWordWrapPosition().
                    // If the specs for CalcWordWrapPosition() were reworked to optionally return on \n we could combine both.
                    // However it is still better than nothing performing the fast-forward!
                    s = ImFontCalcWordWrapPositionEx(font, size, s, line_end ? line_end : text_end, wrap_width, flags);
                    s = ImTextCalcWordWrapNextLineStart(s, text_end, flags);
                }
                else
                {
                    s = line_end ? line_end + 1 : text_end;
                }
                y += line_height;
            }

        // For large text, scan for the last visible line in order to avoid over-reserving in the call to PrimReserve()
        // Note that very large horizontal line will still be affected by the issue (e.g. a one megabyte string buffer without a newline will likely crash atm)
        if (text_end - s > 10000 && !word_wrap_enabled)
        {
            const char* s_end = s;
            float y_end = y;
            while (y_end < clip_rect.w && s_end < text_end)
            {
                s_end = (const char*)ImMemchr(s_end, '\n', text_end - s_end);
                s_end = s_end ? s_end + 1 : text_end;
                y_end += line_height;
            }
            text_end = s_end;
        }
        if (s == text_end)
            return;


        uint32_t currentVertexIndex = draw_list->VtxBuffer.size();

        // Reserve vertices for remaining worse case (over-reserving is useful and easily amortized)
        const int vtx_count_max = (int)(text_end - s) * 4;
        const int idx_count_max = (int)(text_end - s) * 6;
        const int idx_expected_size = draw_list->IdxBuffer.Size + idx_count_max;
        draw_list->PrimReserve(idx_count_max, vtx_count_max);
        ImDrawVert* vtx_write = draw_list->_VtxWritePtr;
        ImDrawIdx* idx_write = draw_list->_IdxWritePtr;
        unsigned int vtx_index = draw_list->_VtxCurrentIdx;
        const int cmd_count = draw_list->CmdBuffer.Size;
        const bool cpu_fine_clip = (flags & ImDrawTextFlags_CpuFineClip) != 0;

        const ImU32 col_untinted = col | ~IM_COL32_A_MASK;
        const char* word_wrap_eol = NULL;


        if (textGeometryData != nullptr)
        {
            textGeometryData->m_LinesStartPos.push_back(ImVec2(x, y));
        }

        while (s < text_end)
        {
            if (word_wrap_enabled)
            {
                // Calculate how far we can render. Requires two passes on the string data but keeps the code simple and not intrusive for what's essentially an uncommon feature.
                if (!word_wrap_eol)
                    word_wrap_eol = ImFontCalcWordWrapPositionEx(font, size, s, text_end, wrap_width - (x - origin_x), flags);

                if (s >= word_wrap_eol)
                {
                    x = origin_x;
                    y += line_height;
                    if (y > clip_rect.w)
                        break; // break out of main loop
                    word_wrap_eol = NULL;
                    s = ImTextCalcWordWrapNextLineStart(s, text_end, flags); // Wrapping skips upcoming blanks

                    if (textGeometryData != nullptr)
                    {
                        if (s > text_begin)
                        {
                            const char* previousCharPtr = s - 1;
                            if (*previousCharPtr == '\n')
                            {
                                textGeometryData->m_LinesStartPos.push_back(ImVec2(x, y));
                                ImTextGlyphGeometryData geometryData{};
                                geometryData.m_Unicode = '\n';
                                geometryData.bIsNewLine = true;
                                textGeometryData->m_TextGlyphs.push_back(geometryData);
                            }
                            else
                            {
                                textGeometryData->m_TextGlyphs.push_back({});
                            }
                        }
                        else
                        {
                            textGeometryData->m_TextGlyphs.push_back({});
                        }
                    }
                    continue;
                }
            }

            // Decode and advance source
            unsigned int c = (unsigned int)*s;
            if (c < 0x80)
                s += 1;
            else
                s += ImTextCharFromUtf8(&c, s, text_end);

            if (textGeometryData != nullptr
                && c != '\r'
                && c != '\n'
                )
            {
                if (c == ' ' || c == '\t')
                {
                    textGeometryData->m_TextGlyphs.push_back({});
                }
                else
                {
                    ImTextGlyphGeometryData glyphData{};
                    glyphData.m_StartVertexIndex = currentVertexIndex;
                    glyphData.m_Unicode = c;
                    textGeometryData->m_TextGlyphs.push_back(glyphData);
                }
            }

            if (c < 32)
            {
                if (c == '\n')
                {
                    x = origin_x;
                    y += line_height;
                    if (y > clip_rect.w)
                        break; // break out of main loop

                    if (textGeometryData != nullptr)
                    {
                        ImTextGlyphGeometryData geometryData{};
                        geometryData.m_Unicode = '\n';
                        geometryData.bIsNewLine = true;
                        textGeometryData->m_TextGlyphs.push_back(geometryData);
                        textGeometryData->m_LinesStartPos.push_back(ImVec2(x, y));
                    }

                    continue;
                }
                if (c == '\r')
                    continue;
            }

            const ImFontGlyph* glyph = baked->FindGlyph((ImWchar)c);
            //if (glyph == NULL)
            //    continue;

            float char_width = glyph->AdvanceX * scale;
            if (glyph->Visible)
            {
                // We don't do a second finer clipping test on the Y axis as we've already skipped anything before clip_rect.y and exit once we pass clip_rect.w
                float x1 = x + glyph->X0 * scale;
                float x2 = x + glyph->X1 * scale;
                float y1 = y + glyph->Y0 * scale;
                float y2 = y + glyph->Y1 * scale;
                if (x1 <= clip_rect.z && x2 >= clip_rect.x)
                {
                    // Render a character
                    float u1 = glyph->U0;
                    float v1 = glyph->V0;
                    float u2 = glyph->U1;
                    float v2 = glyph->V1;

                    // CPU side clipping used to fit text in their frame when the frame is too small. Only does clipping for axis aligned quads.
                    if (cpu_fine_clip)
                    {
                        if (x1 < clip_rect.x)
                        {
                            u1 = u1 + (1.0f - (x2 - clip_rect.x) / (x2 - x1)) * (u2 - u1);
                            x1 = clip_rect.x;
                        }
                        if (y1 < clip_rect.y)
                        {
                            v1 = v1 + (1.0f - (y2 - clip_rect.y) / (y2 - y1)) * (v2 - v1);
                            y1 = clip_rect.y;
                        }
                        if (x2 > clip_rect.z)
                        {
                            u2 = u1 + ((clip_rect.z - x1) / (x2 - x1)) * (u2 - u1);
                            x2 = clip_rect.z;
                        }
                        if (y2 > clip_rect.w)
                        {
                            v2 = v1 + ((clip_rect.w - y1) / (y2 - y1)) * (v2 - v1);
                            y2 = clip_rect.w;
                        }
                        if (y1 >= y2)
                        {
                            x += char_width;
                            continue;
                        }
                    }

                    // Support for untinted glyphs
                    ImU32 glyph_col = glyph->Colored ? col_untinted : col;

                    // We are NOT calling PrimRectUV() here because non-inlined causes too much overhead in a debug builds. Inlined here:
                    {
                        vtx_write[0].pos.x = x1; vtx_write[0].pos.y = y1; vtx_write[0].col = glyph_col; vtx_write[0].uv.x = u1; vtx_write[0].uv.y = v1;
                        vtx_write[1].pos.x = x2; vtx_write[1].pos.y = y1; vtx_write[1].col = glyph_col; vtx_write[1].uv.x = u2; vtx_write[1].uv.y = v1;
                        vtx_write[2].pos.x = x2; vtx_write[2].pos.y = y2; vtx_write[2].col = glyph_col; vtx_write[2].uv.x = u2; vtx_write[2].uv.y = v2;
                        vtx_write[3].pos.x = x1; vtx_write[3].pos.y = y2; vtx_write[3].col = glyph_col; vtx_write[3].uv.x = u1; vtx_write[3].uv.y = v2;
                        idx_write[0] = (ImDrawIdx)(vtx_index); idx_write[1] = (ImDrawIdx)(vtx_index + 1); idx_write[2] = (ImDrawIdx)(vtx_index + 2);
                        idx_write[3] = (ImDrawIdx)(vtx_index); idx_write[4] = (ImDrawIdx)(vtx_index + 2); idx_write[5] = (ImDrawIdx)(vtx_index + 3);
                        vtx_write += 4;
                        vtx_index += 4;
                        idx_write += 6;

                        currentVertexIndex += 4;
                    }
                }
            }
            x += char_width;
        }

        // Edge case: calling RenderText() with unloaded glyphs triggering texture change. It doesn't happen via ImGui:: calls because CalcTextSize() is always used.
        if (cmd_count != draw_list->CmdBuffer.Size) //-V547
        {
            IM_ASSERT(draw_list->CmdBuffer[draw_list->CmdBuffer.Size - 1].ElemCount == 0);
            draw_list->CmdBuffer.pop_back();
            draw_list->PrimUnreserve(idx_count_max, vtx_count_max);
            draw_list->AddDrawCmd();
            //IMGUI_DEBUG_LOG("RenderText: cancel and retry to missing glyphs.\n"); // [DEBUG]
            //draw_list->AddRectFilled(pos, pos + ImVec2(10, 10), IM_COL32(255, 0, 0, 255)); // [DEBUG]
            goto begin;
            //RenderText(draw_list, size, pos, col, clip_rect, text_begin, text_end, wrap_width, cpu_fine_clip); // FIXME-OPT: Would a 'goto begin' be better for code-gen?
            //return;
        }

        // Give back unused vertices (clipped ones, blanks) ~ this is essentially a PrimUnreserve() action.
        draw_list->VtxBuffer.Size = (int)(vtx_write - draw_list->VtxBuffer.Data); // Same as calling shrink()
        draw_list->IdxBuffer.Size = (int)(idx_write - draw_list->IdxBuffer.Data);
        draw_list->CmdBuffer[draw_list->CmdBuffer.Size - 1].ElemCount -= (idx_expected_size - draw_list->IdxBuffer.Size);
        draw_list->_VtxWritePtr = vtx_write;
        draw_list->_IdxWritePtr = idx_write;
        draw_list->_VtxCurrentIdx = vtx_index;
    }


    bool InputTextEx_WithGeometry(
        const char* label,
        const char* hint,
        char* buf,
        int buf_size,
        const ImVec2& size_arg,
        ImGuiInputTextFlags flags,
        ImGuiInputTextCallback callback,
        void* callback_user_data,
        ImMulitlineTextGeometryData* textGeometryData
    )
    {
        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems)
            return false;

        IM_ASSERT(buf != NULL && buf_size >= 0);
        IM_ASSERT(!((flags & ImGuiInputTextFlags_CallbackHistory) && (flags & ImGuiInputTextFlags_Multiline)));        // Can't use both together (they both use up/down keys)
        IM_ASSERT(!((flags & ImGuiInputTextFlags_CallbackCompletion) && (flags & ImGuiInputTextFlags_AllowTabInput))); // Can't use both together (they both use tab key)
        IM_ASSERT(!((flags & ImGuiInputTextFlags_ElideLeft) && (flags & ImGuiInputTextFlags_Multiline)));              // Multiline does not not work with left-trimming
        IM_ASSERT((flags & ImGuiInputTextFlags_WordWrap) == 0 || (flags & ImGuiInputTextFlags_Password) == 0);         // WordWrap does not work with Password mode.
        IM_ASSERT((flags & ImGuiInputTextFlags_WordWrap) == 0 || (flags & ImGuiInputTextFlags_Multiline) != 0);        // WordWrap does not work in single-line mode.

        ImGuiContext& g = *GImGui;
        ImGuiIO& io = g.IO;
        const ImGuiStyle& style = g.Style;

        const bool RENDER_SELECTION_WHEN_INACTIVE = false;
        const bool is_multiline = (flags & ImGuiInputTextFlags_Multiline) != 0;

        if (is_multiline) // Open group before calling GetID() because groups tracks id created within their scope (including the scrollbar)
            BeginGroup();
        const ImGuiID id = window->GetID(label);
        const char* label_end = FindRenderedTextEnd(label);
        const ImVec2 label_size = CalcTextSize(label, label_end, false);
        const ImVec2 frame_size = CalcItemSize(size_arg, CalcItemWidth(), (is_multiline ? g.FontSize * 8.0f : label_size.y) + style.FramePadding.y * 2.0f); // Arbitrary default of 8 lines high for multi-line
        const ImVec2 total_size = ImVec2(frame_size.x + (label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f), frame_size.y);

        const ImRect frame_bb(window->DC.CursorPos, window->DC.CursorPos + frame_size);
        const ImRect total_bb(frame_bb.Min, frame_bb.Min + total_size);

        ImGuiWindow* draw_window = window;
        ImVec2 inner_size = frame_size;
        ImGuiLastItemData item_data_backup;
        if (is_multiline)
        {
            ImVec2 backup_pos = window->DC.CursorPos;
            ItemSize(total_bb, style.FramePadding.y);
            bool no_clip = (g.InputTextDeactivatedState.ID == id) || (g.ActiveId == id) || (id == g.NavActivateId); // Mimic some of ItemAdd() logic + add InputTextDeactivatedState.ID check.
            if (!ItemAdd(total_bb, id, &frame_bb, ImGuiItemFlags_Inputable) && !no_clip)
            {
                EndGroup();
                return false;
            }
            item_data_backup = g.LastItemData;
            window->DC.CursorPos = backup_pos;

            // Prevent NavActivation from explicit Tabbing when our widget accepts Tab inputs: this allows cycling through widgets without stopping.
            if (g.NavActivateId == id && (g.NavActivateFlags & ImGuiActivateFlags_FromTabbing) && !(g.NavActivateFlags & ImGuiActivateFlags_FromFocusApi) && (flags & ImGuiInputTextFlags_AllowTabInput))
                g.NavActivateId = 0;

            // Prevent NavActivate reactivating in BeginChild() when we are already active.
            const ImGuiID backup_activate_id = g.NavActivateId;
            if (g.ActiveId == id) // Prevent reactivation
                g.NavActivateId = 0;

            // We reproduce the contents of BeginChildFrame() in order to provide 'label' so our window internal data are easier to read/debug.
            PushStyleColor(ImGuiCol_ChildBg, style.Colors[ImGuiCol_FrameBg]);
            PushStyleVar(ImGuiStyleVar_ChildRounding, style.FrameRounding);
            PushStyleVar(ImGuiStyleVar_ChildBorderSize, style.FrameBorderSize);
            PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0)); // Ensure no clip rect so mouse hover can reach FramePadding edges
            bool child_visible = BeginChildEx(label, id, frame_bb.GetSize(), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoMove);
            g.NavActivateId = backup_activate_id;
            PopStyleVar(3);
            PopStyleColor();
            if (!child_visible && !no_clip)
            {
                EndChild();
                EndGroup();
                return false;
            }
            draw_window = g.CurrentWindow; // Child window
            draw_window->DC.NavLayersActiveMaskNext |= (1 << draw_window->DC.NavLayerCurrent); // This is to ensure that EndChild() will display a navigation highlight so we can "enter" into it.
            draw_window->DC.CursorPos += style.FramePadding;
            inner_size.x -= draw_window->ScrollbarSizes.x;

            // FIXME: Could this be a ImGuiChildFlags to affect the SetLastItemDataForWindow() call?
            g.LastItemData.ID = id;
            g.LastItemData.ItemFlags = item_data_backup.ItemFlags;
            g.LastItemData.StatusFlags = item_data_backup.StatusFlags;
        }
        else
        {
            // Support for internal ImGuiInputTextFlags_MergedItem flag, which could be redesigned as an ItemFlags if needed (with test performed in ItemAdd)
            ItemSize(total_bb, style.FramePadding.y);
            if (!(flags & ImGuiInputTextFlags_TempInput))
                if (!ItemAdd(total_bb, id, &frame_bb, ImGuiItemFlags_Inputable))
                    return false;
        }

        // Ensure mouse cursor is set even after switching to keyboard/gamepad mode. May generalize further? (#6417)
        bool hovered = ItemHoverable(frame_bb, id, g.LastItemData.ItemFlags | ImGuiItemFlags_NoNavDisableMouseHover);
        if (hovered)
            SetMouseCursor(ImGuiMouseCursor_TextInput);
        if (hovered && g.NavHighlightItemUnderNav)
            hovered = false;

        // We are only allowed to access the state if we are already the active widget.
        ImGuiInputTextState* state = GetInputTextState(id);

        if (g.LastItemData.ItemFlags & ImGuiItemFlags_ReadOnly)
            flags |= ImGuiInputTextFlags_ReadOnly;
        const bool is_readonly = (flags & ImGuiInputTextFlags_ReadOnly) != 0;
        const bool is_password = (flags & ImGuiInputTextFlags_Password) != 0;
        const bool is_undoable = (flags & ImGuiInputTextFlags_NoUndoRedo) == 0;
        const bool is_resizable = (flags & ImGuiInputTextFlags_CallbackResize) != 0;
        if (is_resizable)
            IM_ASSERT(callback != NULL); // Must provide a callback if you set the ImGuiInputTextFlags_CallbackResize flag!

        // Word-wrapping: enforcing a fixed width not altered by vertical scrollbar makes things easier, notably to track cursor reliably and avoid one-frame glitches.
        // Instead of using ImGuiWindowFlags_AlwaysVerticalScrollbar we account for that space if the scrollbar is not visible.
        const bool is_wordwrap = (flags & ImGuiInputTextFlags_WordWrap) != 0;
        float wrap_width = 0.0f;
        if (is_wordwrap)
            wrap_width = ImMax(1.0f, GetContentRegionAvail().x + (draw_window->ScrollbarY ? 0.0f : -g.Style.ScrollbarSize));

        const bool user_clicked = hovered && io.MouseClicked[0];
        const bool input_requested_by_nav = (g.ActiveId != id) && (g.NavActivateId == id);
        const bool input_requested_by_reactivate = (g.InputTextReactivateId == id); // for io.ConfigInputTextEnterKeepActive
        const bool input_requested_by_user = (user_clicked) || (g.ActiveId == 0 && (flags & ImGuiInputTextFlags_TempInput) && g.InputTextDeactivatedState.ID != id);
        const ImGuiID scrollbar_id = (is_multiline && state != NULL) ? GetWindowScrollbarID(draw_window, ImGuiAxis_Y) : 0;
        const bool user_scroll_finish = is_multiline && state != NULL && g.ActiveId == 0 && g.ActiveIdPreviousFrame == scrollbar_id;
        const bool user_scroll_active = is_multiline && state != NULL && g.ActiveId == scrollbar_id;
        bool clear_active_id = false;
        bool select_all = false;

        float scroll_y = is_multiline ? draw_window->Scroll.y : FLT_MAX;

        const bool init_reload_from_user_buf = (state != NULL && state->WantReloadUserBuf);
        const bool init_changed_specs_multiline = (state != NULL && (state->Stb->single_line != !is_multiline)); // state != NULL means its our state.
        const bool init_changed_specs_readonly = (state != NULL && ((state->Flags ^ flags) & ImGuiInputTextFlags_ReadOnly)); // state != NULL means its our state.
        const bool init_make_active = (input_requested_by_user || input_requested_by_nav || input_requested_by_reactivate || user_scroll_finish);
        if (init_reload_from_user_buf)
        {
            int new_len = (int)ImStrlen(buf);
            IM_ASSERT(new_len + 1 <= buf_size && "Is your input buffer properly zero-terminated?");
            state->WantReloadUserBuf = false;
            InputTextReconcileUndoState(state, state->TextA.Data, state->TextLen, buf, new_len);
            state->TextA.resize(buf_size + 1); // we use +1 to make sure that .Data is always pointing to at least an empty string.
            state->TextLen = new_len;
            memcpy(state->TextA.Data, buf, state->TextLen + 1);
            state->Stb->select_start = state->ReloadSelectionStart;
            state->Stb->cursor = state->Stb->select_end = state->ReloadSelectionEnd; // will be clamped to bounds below
        }
        else if ((init_make_active && g.ActiveId != id) || init_changed_specs_multiline || init_changed_specs_readonly)
        {
            // Access state even if we don't own it yet.
            state = &g.InputTextState;
            state->CursorAnimReset();

            // Backup state of deactivating item so they'll have a chance to do a write to output buffer on the same frame they report IsItemDeactivatedAfterEdit (#4714)
            if (state->ID != id && state->ID == g.ActiveId && (init_make_active && g.ActiveId != id)) //-V560
                InputTextDeactivateHook(state->ID); // <-- this is essentially an earlier call to what SetActiveID() would do below.

            // Take a copy of the initial buffer value.
            // From the moment we focused we are normally ignoring the content of 'buf' (unless we are in read-only mode)
            const int buf_len = (int)ImStrlen(buf);
            IM_ASSERT(((buf_len + 1 <= buf_size) || (buf_len == 0 && buf_size == 0)) && "Is your input buffer properly zero-terminated?");
            if (!user_scroll_finish)
            {
                state->TextToRevertTo.resize(buf_len + 1);    // UTF-8. we use +1 to make sure that .Data is always pointing to at least an empty string.
                memcpy(state->TextToRevertTo.Data, buf, buf_len + 1);
            }

            // Preserve cursor position and undo/redo stack if we come back to same widget
            // FIXME: Since we reworked this on 2022/06, may want to differentiate recycle_cursor vs recycle_undostate?
            bool recycle_state = (state->ID == id && !init_changed_specs_multiline);
            if (recycle_state && !init_changed_specs_readonly && (state->TextLen != buf_len || (state->TextA.Data == NULL || strncmp(state->TextA.Data, buf, buf_len) != 0)))
                recycle_state = false;

            // Start edition
            state->ID = id;
            state->TextLen = buf_len;
            state->EditedBefore = false;
            if (!is_readonly)
            {
                state->TextA.resize(buf_size + 1); // we use +1 to make sure that .Data is always pointing to at least an empty string.
                memcpy(state->TextA.Data, buf, state->TextLen + 1);
            }

            // Find initial scroll position for right alignment
            state->Scroll = ImVec2(0.0f, 0.0f);
            if (flags & ImGuiInputTextFlags_ElideLeft)
                state->Scroll.x += ImMax(0.0f, CalcTextSize(buf).x - frame_size.x + style.FramePadding.x * 2.0f);

            // Recycle existing cursor/selection/undo stack but clamp position
            // Note a single mouse click will override the cursor/position immediately by calling stb_textedit_click handler.
            if (!recycle_state)
                stb_textedit_initialize_state(state->Stb, !is_multiline);

            if (!is_multiline)
            {
                if (flags & ImGuiInputTextFlags_AutoSelectAll)
                    select_all = true;
                if (input_requested_by_nav && (!recycle_state || !(g.NavActivateFlags & ImGuiActivateFlags_TryToPreserveState)))
                    select_all = true;
                if (user_clicked && io.KeyCtrl)
                    select_all = true;
            }

            if (flags & ImGuiInputTextFlags_AlwaysOverwrite)
                state->Stb->insert_mode = 1; // stb field name is indeed incorrect (see #2863)
        }

        const bool is_osx = io.ConfigMacOSXBehaviors;
        if (init_make_active && g.ActiveId != id)
        {
            IM_ASSERT(state && state->ID == id);
            SetActiveID(id, window);
            SetFocusID(id, window);
            FocusWindow(window);
            if (input_requested_by_nav)
                SetNavCursorVisibleAfterMove();
        }
        if (g.ActiveId == id)
        {
            // Declare some inputs, the other are registered and polled via Shortcut() routing system.
            // FIXME: The reason we don't use Shortcut() is we would need a routing flag to specify multiple mods, or to all mods combination into individual shortcuts.
            const ImGuiKey always_owned_keys[] = { ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_Delete, ImGuiKey_Backspace, ImGuiKey_Home, ImGuiKey_End };
            for (ImGuiKey key : always_owned_keys)
                SetKeyOwner(key, id);
            if (user_clicked)
                SetKeyOwner(ImGuiKey_MouseLeft, id);
            g.ActiveIdUsingNavDirMask |= (1 << ImGuiDir_Left) | (1 << ImGuiDir_Right);
            if (is_multiline || (flags & ImGuiInputTextFlags_CallbackHistory))
            {
                g.ActiveIdUsingNavDirMask |= (1 << ImGuiDir_Up) | (1 << ImGuiDir_Down);
                SetKeyOwner(ImGuiKey_UpArrow, id);
                SetKeyOwner(ImGuiKey_DownArrow, id);
            }
            if (is_multiline)
            {
                SetKeyOwner(ImGuiKey_PageUp, id);
                SetKeyOwner(ImGuiKey_PageDown, id);
            }
            // FIXME: May be a problem to always steal Alt on OSX, would ideally still allow an uninterrupted Alt down-up to toggle menu
            if (is_osx)
                SetKeyOwner(ImGuiMod_Alt, id);

            // Expose scroll in a manner that is agnostic to us using a child window
            if (is_multiline && state != NULL)
                state->Scroll.y = draw_window->Scroll.y;

            // Read-only mode always ever read from source buffer. Refresh TextLen when active.
            if (is_readonly && state != NULL)
                state->TextLen = (int)ImStrlen(buf);
            if (state != NULL)
                state->CursorClamp();
            //if (is_readonly && state != NULL)
            //    state->TextA.clear(); // Uncomment to facilitate debugging, but we otherwise prefer to keep/amortize th allocation.
        }
        if (state != NULL)
            state->TextSrc = is_readonly ? buf : state->TextA.Data;

        // We have an edge case if ActiveId was set through another widget (e.g. widget being swapped), clear id immediately (don't wait until the end of the function)
        if (g.ActiveId == id && state == NULL)
            ClearActiveID();

        // Release focus when we click outside
        if (g.ActiveId == id && io.MouseClicked[0] && !init_make_active) //-V560
            clear_active_id = true;

        // Lock the decision of whether we are going to take the path displaying the cursor or selection
        bool render_cursor = (g.ActiveId == id) || (state && user_scroll_active);
        bool render_selection = state && (state->HasSelection() || select_all) && (RENDER_SELECTION_WHEN_INACTIVE || render_cursor);
        bool value_changed = false;
        bool validated = false;

        // Select the buffer to render.
        const bool buf_display_from_state = (render_cursor || render_selection || g.ActiveId == id) && !is_readonly && state;
        bool is_displaying_hint = (hint != NULL && (buf_display_from_state ? state->TextA.Data : buf)[0] == 0);

        // Password pushes a temporary font with only a fallback glyph
        if (is_password && !is_displaying_hint)
            PushPasswordFont();

        if (state != NULL && state->ID == id)
        {
            state->Flags = flags;
            //state->LastFrameActive = g.FrameCount;

            // Word-wrapping: attempt to keep cursor in view while resizing frame/parent (FIXME-WORDWRAP: would be better to preserve same relative offset)
            if (is_wordwrap && state->WrapWidth != wrap_width)
            {
                state->CursorCenterY = true;
                state->WrapWidth = wrap_width;
                render_cursor = true;
            }
        }

        // Process mouse inputs and character inputs
        if (g.ActiveId == id)
        {
            IM_ASSERT(state != NULL);
            state->EditedThisFrame = false;
            state->BufCapacity = buf_size;
            state->WrapWidth = wrap_width;

            // Although we are active we don't prevent mouse from hovering other elements unless we are interacting right now with the widget.
            // Down the line we should have a cleaner library-wide concept of Selected vs Active.
            g.ActiveIdAllowOverlap = !io.MouseDown[0];

            // Edit in progress
            const float mouse_x = (io.MousePos.x - frame_bb.Min.x - style.FramePadding.x) + state->Scroll.x;
            const float mouse_y = (is_multiline ? (io.MousePos.y - draw_window->DC.CursorPos.y) : (g.FontSize * 0.5f));

            if (select_all)
            {
                state->SelectAll();
                state->SelectedAllMouseLock = true;
            }
            else if (hovered && io.MouseClickedCount[0] >= 2 && !io.KeyShift)
            {
                stb_textedit_click(state, state->Stb, mouse_x, mouse_y);
                const int multiclick_count = (io.MouseClickedCount[0] - 2);
                if ((multiclick_count % 2) == 0)
                {
                    // Double-click: Select word
                    // We always use the "Mac" word advance for double-click select vs Ctrl+Right which use the platform dependent variant:
                    // FIXME: There are likely many ways to improve this behavior, but there's no "right" behavior (depends on use-case, software, OS)
                    const bool is_bol = (state->Stb->cursor == 0) || ImStb::STB_TEXTEDIT_GETCHAR(state, state->Stb->cursor - 1) == '\n';
                    if (STB_TEXT_HAS_SELECTION(state->Stb) || !is_bol)
                        state->OnKeyPressed(STB_TEXTEDIT_K_WORDLEFT);
                    //state->OnKeyPressed(STB_TEXTEDIT_K_WORDRIGHT | STB_TEXTEDIT_K_SHIFT);
                    if (!STB_TEXT_HAS_SELECTION(state->Stb))
                        ImStb::stb_textedit_prep_selection_at_cursor(state->Stb);
                    state->Stb->cursor = ImStb::STB_TEXTEDIT_MOVEWORDRIGHT_MAC(state, state->Stb->cursor);
                    state->Stb->select_end = state->Stb->cursor;
                    ImStb::stb_textedit_clamp(state, state->Stb);
                }
                else
                {
                    // Triple-click: Select line
                    const bool is_eol = ImStb::STB_TEXTEDIT_GETCHAR(state, state->Stb->cursor) == '\n';
                    state->WrapWidth = 0.0f; // Temporarily disable wrapping so we use real line start.
                    state->OnKeyPressed(STB_TEXTEDIT_K_LINESTART);
                    state->OnKeyPressed(STB_TEXTEDIT_K_LINEEND | STB_TEXTEDIT_K_SHIFT);
                    state->OnKeyPressed(STB_TEXTEDIT_K_RIGHT | STB_TEXTEDIT_K_SHIFT);
                    state->WrapWidth = wrap_width;
                    if (!is_eol && is_multiline)
                    {
                        ImSwap(state->Stb->select_start, state->Stb->select_end);
                        state->Stb->cursor = state->Stb->select_end;
                    }
                    state->CursorFollow = false;
                }
                state->CursorAnimReset();
            }
            else if (io.MouseClicked[0] && !state->SelectedAllMouseLock)
            {
                if (hovered)
                {
                    if (io.KeyShift)
                        stb_textedit_drag(state, state->Stb, mouse_x, mouse_y);
                    else
                        stb_textedit_click(state, state->Stb, mouse_x, mouse_y);
                    state->CursorAnimReset();
                }
            }
            else if (io.MouseDown[0] && !state->SelectedAllMouseLock && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f))
            {
                stb_textedit_drag(state, state->Stb, mouse_x, mouse_y);
                state->CursorAnimReset();
                state->CursorFollow = true;
            }
            if (state->SelectedAllMouseLock && !io.MouseDown[0])
                state->SelectedAllMouseLock = false;

            // We expect backends to emit a Tab key but some also emit a Tab character which we ignore (#2467, #1336)
            // (For Tab and Enter: Win32/SFML/Allegro are sending both keys and chars, GLFW and SDL are only sending keys. For Space they all send all threes)
            if ((flags & ImGuiInputTextFlags_AllowTabInput) && !is_readonly)
            {
                if (Shortcut(ImGuiKey_Tab, ImGuiInputFlags_Repeat, id))
                {
                    unsigned int c = '\t'; // Insert TAB
                    if (InputTextFilterCharacter(&g, state, &c, callback, callback_user_data))
                        state->OnCharPressed(c);
                }
                // FIXME: Implement Shift+Tab
                /*
                if (Shortcut(ImGuiKey_Tab | ImGuiMod_Shift, ImGuiInputFlags_Repeat, id))
                {
                }
                */
            }

            // Process regular text input (before we check for Return because using some IME will effectively send a Return?)
            // We ignore Ctrl inputs, but need to allow Alt+Ctrl as some keyboards (e.g. German) use AltGR (which _is_ Alt+Ctrl) to input certain characters.
            const bool ignore_char_inputs = (io.KeyCtrl && !io.KeyAlt) || (is_osx && io.KeyCtrl);
            if (io.InputQueueCharacters.Size > 0)
            {
                if (!ignore_char_inputs && !is_readonly && !input_requested_by_nav)
                    for (int n = 0; n < io.InputQueueCharacters.Size; n++)
                    {
                        // Insert character if they pass filtering
                        unsigned int c = (unsigned int)io.InputQueueCharacters[n];
                        if (c == '\t') // Skip Tab, see above.
                            continue;
                        if (InputTextFilterCharacter(&g, state, &c, callback, callback_user_data))
                            state->OnCharPressed(c);
                    }

                // Consume characters
                io.InputQueueCharacters.resize(0);
            }
        }

        // Process other shortcuts/key-presses
        bool revert_edit = false;
        if (g.ActiveId == id && !g.ActiveIdIsJustActivated && !clear_active_id)
        {
            IM_ASSERT(state != NULL);

            const int row_count_per_page = ImMax((int)((inner_size.y - style.FramePadding.y) / g.FontSize), 1);
            state->Stb->row_count_per_page = row_count_per_page;

            const int k_mask = (io.KeyShift ? STB_TEXTEDIT_K_SHIFT : 0);
            const bool is_wordmove_key_down = is_osx ? io.KeyAlt : io.KeyCtrl;                     // OS X style: Text editing cursor movement using Alt instead of Ctrl
            const bool is_startend_key_down = is_osx && io.KeyCtrl && !io.KeySuper && !io.KeyAlt;  // OS X style: Line/Text Start and End using Cmd+Arrows instead of Home/End

            // Using Shortcut() with ImGuiInputFlags_RouteFocused (default policy) to allow routing operations for other code (e.g. calling window trying to use Ctrl+A and Ctrl+B: former would be handled by InputText)
            // Otherwise we could simply assume that we own the keys as we are active.
            const ImGuiInputFlags f_repeat = ImGuiInputFlags_Repeat;
            const bool is_cut = (Shortcut(ImGuiMod_Ctrl | ImGuiKey_X, f_repeat, id) || Shortcut(ImGuiMod_Shift | ImGuiKey_Delete, f_repeat, id)) && !is_readonly && !is_password && (!is_multiline || state->HasSelection());
            const bool is_copy = (Shortcut(ImGuiMod_Ctrl | ImGuiKey_C, 0, id) || Shortcut(ImGuiMod_Ctrl | ImGuiKey_Insert, 0, id)) && !is_password && (!is_multiline || state->HasSelection());
            const bool is_paste = (Shortcut(ImGuiMod_Ctrl | ImGuiKey_V, f_repeat, id) || Shortcut(ImGuiMod_Shift | ImGuiKey_Insert, f_repeat, id)) && !is_readonly;
            const bool is_undo = (Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, f_repeat, id)) && !is_readonly && is_undoable;
            const bool is_redo = (Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, f_repeat, id) || Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, f_repeat, id)) && !is_readonly && is_undoable;
            const bool is_select_all = Shortcut(ImGuiMod_Ctrl | ImGuiKey_A, 0, id);

            // We allow validate/cancel with Nav source (gamepad) to makes it easier to undo an accidental NavInput press with no keyboard wired, but otherwise it isn't very useful.
            const bool nav_gamepad_active = (io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) != 0 && (io.BackendFlags & ImGuiBackendFlags_HasGamepad) != 0;
            const bool is_enter = Shortcut(ImGuiKey_Enter, f_repeat, id) || Shortcut(ImGuiKey_KeypadEnter, f_repeat, id);
            const bool is_ctrl_enter = Shortcut(ImGuiMod_Ctrl | ImGuiKey_Enter, f_repeat, id) || Shortcut(ImGuiMod_Ctrl | ImGuiKey_KeypadEnter, f_repeat, id);
            const bool is_shift_enter = Shortcut(ImGuiMod_Shift | ImGuiKey_Enter, f_repeat, id) || Shortcut(ImGuiMod_Shift | ImGuiKey_KeypadEnter, f_repeat, id);
            const bool is_gamepad_validate = nav_gamepad_active && IsKeyPressed(ImGuiKey_NavGamepadActivate, false);
            const bool is_cancel = Shortcut(ImGuiKey_Escape, f_repeat, id) || (nav_gamepad_active && Shortcut(ImGuiKey_NavGamepadCancel, f_repeat, id));

            // FIXME: Should use more Shortcut() and reduce IsKeyPressed()+SetKeyOwner(), but requires modifiers combination to be taken account of.
            // FIXME-OSX: Missing support for Alt(option)+Right/Left = go to end of line, or next line if already in end of line.
            if (IsKeyPressed(ImGuiKey_LeftArrow)) { state->OnKeyPressed((is_startend_key_down ? STB_TEXTEDIT_K_LINESTART : is_wordmove_key_down ? STB_TEXTEDIT_K_WORDLEFT : STB_TEXTEDIT_K_LEFT) | k_mask); }
            else if (IsKeyPressed(ImGuiKey_RightArrow)) { state->OnKeyPressed((is_startend_key_down ? STB_TEXTEDIT_K_LINEEND : is_wordmove_key_down ? STB_TEXTEDIT_K_WORDRIGHT : STB_TEXTEDIT_K_RIGHT) | k_mask); }
            else if (IsKeyPressed(ImGuiKey_UpArrow) && is_multiline) { if (io.KeyCtrl) SetScrollY(draw_window, ImMax(draw_window->Scroll.y - g.FontSize, 0.0f)); else state->OnKeyPressed((is_startend_key_down ? STB_TEXTEDIT_K_TEXTSTART : STB_TEXTEDIT_K_UP) | k_mask); }
            else if (IsKeyPressed(ImGuiKey_DownArrow) && is_multiline) { if (io.KeyCtrl) SetScrollY(draw_window, ImMin(draw_window->Scroll.y + g.FontSize, GetScrollMaxY())); else state->OnKeyPressed((is_startend_key_down ? STB_TEXTEDIT_K_TEXTEND : STB_TEXTEDIT_K_DOWN) | k_mask); }
            else if (IsKeyPressed(ImGuiKey_PageUp) && is_multiline) { state->OnKeyPressed(STB_TEXTEDIT_K_PGUP | k_mask); scroll_y -= row_count_per_page * g.FontSize; }
            else if (IsKeyPressed(ImGuiKey_PageDown) && is_multiline) { state->OnKeyPressed(STB_TEXTEDIT_K_PGDOWN | k_mask); scroll_y += row_count_per_page * g.FontSize; }
            else if (IsKeyPressed(ImGuiKey_Home)) { state->OnKeyPressed(io.KeyCtrl ? STB_TEXTEDIT_K_TEXTSTART | k_mask : STB_TEXTEDIT_K_LINESTART | k_mask); }
            else if (IsKeyPressed(ImGuiKey_End)) { state->OnKeyPressed(io.KeyCtrl ? STB_TEXTEDIT_K_TEXTEND | k_mask : STB_TEXTEDIT_K_LINEEND | k_mask); }
            else if (IsKeyPressed(ImGuiKey_Delete) && !is_readonly && !is_cut)
            {
                if (!state->HasSelection())
                {
                    // OSX doesn't seem to have Super+Delete to delete until end-of-line, so we don't emulate that (as opposed to Super+Backspace)
                    if (is_wordmove_key_down)
                        state->OnKeyPressed(STB_TEXTEDIT_K_WORDRIGHT | STB_TEXTEDIT_K_SHIFT);
                }
                state->OnKeyPressed(STB_TEXTEDIT_K_DELETE | k_mask);
            }
            else if (IsKeyPressed(ImGuiKey_Backspace) && !is_readonly)
            {
                if (!state->HasSelection())
                {
                    if (is_wordmove_key_down)
                        state->OnKeyPressed(STB_TEXTEDIT_K_WORDLEFT | STB_TEXTEDIT_K_SHIFT);
                    else if (is_osx && io.KeyCtrl && !io.KeyAlt && !io.KeySuper)
                        state->OnKeyPressed(STB_TEXTEDIT_K_LINESTART | STB_TEXTEDIT_K_SHIFT);
                }
                state->OnKeyPressed(STB_TEXTEDIT_K_BACKSPACE | k_mask);
            }
            else if (is_enter || is_ctrl_enter || is_shift_enter || is_gamepad_validate)
            {
                // Determine if we turn Enter into a \n character
                bool ctrl_enter_for_new_line = (flags & ImGuiInputTextFlags_CtrlEnterForNewLine) != 0;
                bool is_new_line = is_multiline && !is_gamepad_validate && (is_shift_enter || (is_enter && !ctrl_enter_for_new_line) || (is_ctrl_enter && ctrl_enter_for_new_line));
                if (!is_new_line)
                {
                    validated = clear_active_id = true;
                    if (io.ConfigInputTextEnterKeepActive && !is_multiline && !is_ctrl_enter && !is_shift_enter)
                    {
                        // Queue reactivation, so that e.g. IsItemDeactivatedAfterEdit() will work. (#9001)
                        state->SelectAll(); // No need to scroll
                        g.InputTextReactivateId = id; // Mark for reactivation on next frame
                    }
                }
                else if (!is_readonly)
                {
                    // Insert new line
                    unsigned int c = '\n';
                    if (InputTextFilterCharacter(&g, state, &c, callback, callback_user_data))
                        state->OnCharPressed(c);
                }
            }
            else if (is_cancel)
            {
                if (flags & ImGuiInputTextFlags_EscapeClearsAll)
                {
                    if (state->TextA.Data[0] != 0)
                    {
                        revert_edit = true;
                    }
                    else
                    {
                        render_cursor = render_selection = false;
                        clear_active_id = true;
                    }
                }
                else
                {
                    clear_active_id = revert_edit = true;
                    render_cursor = render_selection = false;
                }
            }
            else if (is_undo || is_redo)
            {
                state->OnKeyPressed(is_undo ? STB_TEXTEDIT_K_UNDO : STB_TEXTEDIT_K_REDO);
                state->ClearSelection();
            }
            else if (is_select_all)
            {
                state->SelectAll();
                state->CursorFollow = true;
            }
            else if (is_cut || is_copy)
            {
                // Cut, Copy
                if (g.PlatformIO.Platform_SetClipboardTextFn != NULL)
                {
                    // SetClipboardText() only takes null terminated strings + state->TextSrc may point to read-only user buffer, so we need to make a copy.
                    const int ib = state->HasSelection() ? ImMin(state->Stb->select_start, state->Stb->select_end) : 0;
                    const int ie = state->HasSelection() ? ImMax(state->Stb->select_start, state->Stb->select_end) : state->TextLen;
                    g.TempBuffer.reserve(ie - ib + 1);
                    memcpy(g.TempBuffer.Data, state->TextSrc + ib, ie - ib);
                    g.TempBuffer.Data[ie - ib] = 0;
                    SetClipboardText(g.TempBuffer.Data);
                }
                if (is_cut)
                {
                    if (!state->HasSelection())
                        state->SelectAll();
                    state->CursorFollow = true;
                    stb_textedit_cut(state, state->Stb);
                }
            }
            else if (is_paste)
            {
                if (const char* clipboard = GetClipboardText())
                {
                    // Filter pasted buffer
                    const int clipboard_len = (int)ImStrlen(clipboard);
                    const char* clipboard_end = clipboard + clipboard_len;
                    ImVector<char> clipboard_filtered;
                    clipboard_filtered.reserve(clipboard_len + 1);
                    for (const char* s = clipboard; *s != 0; )
                    {
                        unsigned int c;
                        int in_len = ImTextCharFromUtf8(&c, s, clipboard_end);
                        s += in_len;
                        if (!InputTextFilterCharacter(&g, state, &c, callback, callback_user_data, true))
                            continue;
                        char c_utf8[5];
                        ImTextCharToUtf8(c_utf8, c);
                        int out_len = (int)ImStrlen(c_utf8);
                        clipboard_filtered.resize(clipboard_filtered.Size + out_len);
                        memcpy(clipboard_filtered.Data + clipboard_filtered.Size - out_len, c_utf8, out_len);
                    }
                    if (clipboard_filtered.Size > 0) // If everything was filtered, ignore the pasting operation
                    {
                        clipboard_filtered.push_back(0);
                        stb_textedit_paste(state, state->Stb, clipboard_filtered.Data, clipboard_filtered.Size - 1);
                        state->CursorFollow = true;
                    }
                }
            }

            // Update render selection flag after events have been handled, so selection highlight can be displayed during the same frame.
            render_selection |= state->HasSelection() && (RENDER_SELECTION_WHEN_INACTIVE || render_cursor);
        }

        // Process revert and user callbacks
        const char* apply_new_text = NULL;
        int apply_new_text_length = 0;
        if (g.ActiveId == id)
        {
            IM_ASSERT(state != NULL);
            if (revert_edit && !is_readonly)
            {
                if (flags & ImGuiInputTextFlags_EscapeClearsAll)
                {
                    // Clear input
                    IM_ASSERT(state->TextA.Data[0] != 0);
                    apply_new_text = "";
                    apply_new_text_length = 0;
                    value_changed = true;
                    char empty_string = 0;
                    stb_textedit_replace(state, state->Stb, &empty_string, 0);
                }
                else if (strcmp(state->TextA.Data, state->TextToRevertTo.Data) != 0)
                {
                    apply_new_text = state->TextToRevertTo.Data;
                    apply_new_text_length = state->TextToRevertTo.Size - 1;

                    // Restore initial value. Only return true if restoring to the initial value changes the current buffer contents.
                    // Push records into the undo stack so we can Ctrl+Z the revert operation itself
                    value_changed = true;
                    stb_textedit_replace(state, state->Stb, state->TextToRevertTo.Data, state->TextToRevertTo.Size - 1);
                }
            }

            // User callback
            if ((flags & (ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_CallbackEdit | ImGuiInputTextFlags_CallbackAlways)) != 0)
            {
                IM_ASSERT(callback != NULL);

                // The reason we specify the usage semantic (Completion/History) is that Completion needs to disable keyboard TABBING at the moment.
                ImGuiInputTextFlags event_flag = 0;
                ImGuiKey event_key = ImGuiKey_None;
                if ((flags & ImGuiInputTextFlags_CallbackCompletion) != 0 && Shortcut(ImGuiKey_Tab, 0, id))
                {
                    event_flag = ImGuiInputTextFlags_CallbackCompletion;
                    event_key = ImGuiKey_Tab;
                }
                else if ((flags & ImGuiInputTextFlags_CallbackHistory) != 0 && IsKeyPressed(ImGuiKey_UpArrow))
                {
                    event_flag = ImGuiInputTextFlags_CallbackHistory;
                    event_key = ImGuiKey_UpArrow;
                }
                else if ((flags & ImGuiInputTextFlags_CallbackHistory) != 0 && IsKeyPressed(ImGuiKey_DownArrow))
                {
                    event_flag = ImGuiInputTextFlags_CallbackHistory;
                    event_key = ImGuiKey_DownArrow;
                }
                else if ((flags & ImGuiInputTextFlags_CallbackEdit) && state->EditedThisFrame)
                {
                    event_flag = ImGuiInputTextFlags_CallbackEdit;
                }
                else if (flags & ImGuiInputTextFlags_CallbackAlways)
                {
                    event_flag = ImGuiInputTextFlags_CallbackAlways;
                }

                if (event_flag)
                {
                    ImGuiInputTextCallbackData callback_data;
                    callback_data.Ctx = &g;
                    callback_data.ID = id;
                    callback_data.Flags = flags;
                    callback_data.EventFlag = event_flag;
                    callback_data.EventActivated = (g.ActiveId == state->ID && g.ActiveIdIsJustActivated);
                    callback_data.UserData = callback_user_data;

                    // FIXME-OPT: Undo stack reconcile needs a backup of the data until we rework API, see #7925
                    char* callback_buf = is_readonly ? buf : state->TextA.Data;
                    IM_ASSERT(callback_buf == state->TextSrc);
                    state->CallbackTextBackup.resize(state->TextLen + 1);
                    memcpy(state->CallbackTextBackup.Data, callback_buf, state->TextLen + 1);

                    callback_data.EventKey = event_key;
                    callback_data.Buf = callback_buf;
                    callback_data.BufTextLen = state->TextLen;
                    callback_data.BufSize = state->BufCapacity;
                    callback_data.BufDirty = false;
                    callback_data.CursorPos = state->Stb->cursor;
                    callback_data.SelectionStart = state->Stb->select_start;
                    callback_data.SelectionEnd = state->Stb->select_end;

                    // Call user code
                    callback(&callback_data);

                    // Read back what user may have modified
                    callback_buf = is_readonly ? buf : state->TextA.Data; // Pointer may have been invalidated by a resize callback
                    IM_ASSERT(callback_data.Buf == callback_buf);         // Invalid to modify those fields
                    IM_ASSERT(callback_data.BufSize == state->BufCapacity);
                    IM_ASSERT(callback_data.Flags == flags);
                    if (callback_data.BufDirty || callback_data.CursorPos != state->Stb->cursor)
                        state->CursorFollow = true;
                    state->Stb->cursor = ImClamp(callback_data.CursorPos, 0, callback_data.BufTextLen);
                    state->Stb->select_start = ImClamp(callback_data.SelectionStart, 0, callback_data.BufTextLen);
                    state->Stb->select_end = ImClamp(callback_data.SelectionEnd, 0, callback_data.BufTextLen);
                    if (callback_data.BufDirty)
                    {
                        // Callback may update buffer and thus set buf_dirty even in read-only mode.
                        IM_ASSERT(callback_data.BufTextLen == (int)ImStrlen(callback_data.Buf)); // You need to maintain BufTextLen if you change the text!
                        InputTextReconcileUndoState(state, state->CallbackTextBackup.Data, state->CallbackTextBackup.Size - 1, callback_data.Buf, callback_data.BufTextLen);
                        state->TextLen = callback_data.BufTextLen;  // Assume correct length and valid UTF-8 from user, saves us an extra strlen()
                        state->EditedBefore = state->EditedThisFrame = true;
                        state->CursorAnimReset();
                    }
                }
            }

            // Write back result string if modified.
            // FIXME-OPT: Could mark dirty state from the stb_textedit callbacks
            if (!is_readonly)
            {
                if (g.LastItemData.ItemFlags & ImGuiItemFlags_LiveEditOnInputText)
                {
                    // Apply when modified
                    if (strcmp(state->TextSrc, buf) != 0)
                    {
                        apply_new_text = state->TextSrc;
                        apply_new_text_length = state->TextLen;
                        value_changed = true;
                    }
                }
                else
                {
                    // Apply on validation/deactivation, otherwise cancel out previous apply attempts (e.g. revert)
                    value_changed = ((validated || clear_active_id || revert_edit) && strcmp(state->TextSrc, buf) != 0);
                    apply_new_text = value_changed ? state->TextSrc : NULL;
                    apply_new_text_length = value_changed ? state->TextLen : 0;
                }
            }
        }

        // Handle reapplying final data on deactivation (see InputTextDeactivateHook() for details)
        // This is used when e.g. losing focus or tabbing out into another InputText() which may already be using the temp buffer.
        if (g.InputTextDeactivatedState.ID == id)
        {
            // The state only exists after an Edit. IsItemDeactivatedAfterEdit() is not valid in every code path (see "widgets_inputtext_status_noliveedit" test).
            if ((g.ActiveId != id && IsItemDeactivated()) || (g.ActiveId == id && (flags & ImGuiInputTextFlags_TempInput)))
                if (!is_readonly && strcmp(g.InputTextDeactivatedState.TextA.Data, buf) != 0)
                {
                    apply_new_text = g.InputTextDeactivatedState.TextA.Data;
                    apply_new_text_length = g.InputTextDeactivatedState.TextA.Size - 1;
                    value_changed = true;
                    //IMGUI_DEBUG_LOG("InputText(): apply Deactivated data for 0x%08X: \"%.*s\".\n", id, apply_new_text_length, apply_new_text);
                }
            g.InputTextDeactivatedState.ID = 0;
        }

        // Write back result to user buffer. This can currently only happen when (g.ActiveId == id) or when just deactivated.
        // - As soon as the InputText() is active, our stored in-widget value gets priority over any underlying modification of the user buffer.
        // - Make sure we always reapply the live buffer back to the input/user buffer before clearing ActiveId, even thought strictly speaking
        //   it was not modified on this frame. This allows the user to use InputText() without maintaining any user-side storage.
        //   (PS: if you use this property together with ImGuiInputTextFlags_CallbackResize, you are at the risk of recreating a temporary
        //    allocated/string object every frame. Which in the grand scheme of scheme is nothing, but isn't dear imgui vibe).
        if (apply_new_text != NULL)
        {
            IM_ASSERT(apply_new_text_length >= 0);
            if (is_resizable)
            {
                ImGuiInputTextCallbackData callback_data;
                callback_data.Ctx = &g;
                callback_data.ID = id;
                callback_data.Flags = flags;
                callback_data.EventFlag = ImGuiInputTextFlags_CallbackResize;
                callback_data.EventActivated = (state != NULL && g.ActiveId == state->ID && g.ActiveIdIsJustActivated);
                callback_data.Buf = buf;
                callback_data.BufTextLen = apply_new_text_length;
                callback_data.BufSize = ImMax(buf_size, apply_new_text_length + 1);
                callback_data.UserData = callback_user_data;
                callback(&callback_data);
                buf = callback_data.Buf;
                buf_size = callback_data.BufSize;
                apply_new_text_length = ImMin(callback_data.BufTextLen, buf_size - 1);
                IM_ASSERT(apply_new_text_length <= buf_size);
            }
            //IMGUI_DEBUG_PRINT("InputText(\"%s\"): apply_new_text length %d\n", label, apply_new_text_length);

            // If the underlying buffer resize was denied or not carried to the next frame, apply_new_text_length+1 may be >= buf_size.
            ImStrncpy(buf, apply_new_text, ImMin(apply_new_text_length + 1, buf_size));
        }

        // Release active ID at the end of the function (so e.g. pressing Return still does a final application of the value)
        // Otherwise request text input ahead for next frame.
        if (g.ActiveId == id && clear_active_id)
        {
            state->EditedBefore = false; // Data already applied: avoid InputTextDeactivateHook() taking a record now or later if same id is activated again without editing.
            ClearActiveID();
        }

        // Render frame
        if (!is_multiline)
        {
            RenderNavCursor(frame_bb, id);
            RenderFrame(frame_bb.Min, frame_bb.Max, GetColorU32(ImGuiCol_FrameBg), true, style.FrameRounding);
        }

        ImVec2 draw_pos = is_multiline ? draw_window->DC.CursorPos : frame_bb.Min + style.FramePadding;
        ImRect clip_rect(frame_bb.Min.x, frame_bb.Min.y, frame_bb.Min.x + inner_size.x, frame_bb.Min.y + inner_size.y); // Not using frame_bb.Max because we have adjusted size
        if (is_multiline)
            clip_rect.ClipWith(draw_window->ClipRect);

        // Set upper limit of single-line InputTextEx() at 2 million characters strings. The current pathological worst case is a long line
        // without any carriage return, which would makes ImFont::RenderText() reserve too many vertices and probably crash. Avoid it altogether.
        // Note that we only use this limit on single-line InputText(), so a pathologically large line on a InputTextMultiline() would still crash.
        const int buf_display_max_length = 2 * 1024 * 1024;
        const char* buf_display = buf_display_from_state ? state->TextA.Data : buf; //-V595
        const char* buf_display_end = NULL; // We have specialized paths below for setting the length

        // Display hint when contents is empty
        // At this point we need to handle the possibility that a callback could have modified the underlying buffer (#8368)
        const bool new_is_displaying_hint = (hint != NULL && (buf_display_from_state ? state->TextA.Data : buf)[0] == 0);
        if (new_is_displaying_hint != is_displaying_hint)
        {
            if (is_password && !is_displaying_hint)
                PopPasswordFont();
            is_displaying_hint = new_is_displaying_hint;
            if (is_password && !is_displaying_hint)
                PushPasswordFont();
        }
        if (is_displaying_hint)
        {
            buf_display = hint;
            buf_display_end = hint + ImStrlen(hint);
        }
        else
        {
            if (render_cursor || render_selection || g.ActiveId == id)
                buf_display_end = buf_display + state->TextLen; //-V595
            else if (is_multiline && !is_wordwrap)
                buf_display_end = NULL; // Inactive multi-line: end of buffer will be output by InputTextLineIndexBuild() special strchr() path.
            else
                buf_display_end = buf_display + ImStrlen(buf_display);
        }

        // Calculate visibility
        int line_visible_n0 = 0, line_visible_n1 = 1;
        if (is_multiline)
            CalcClipRectVisibleItemsY(clip_rect, draw_pos, g.FontSize, &line_visible_n0, &line_visible_n1);

        // Build line index for easy data access (makes code below simpler and faster)
        ImGuiTextIndex* line_index = &g.InputTextLineIndex;
        line_index->Offsets.resize(0);
        int line_count = 1;
        if (is_multiline)
        {
            // If scrolling is expected to change build full index.
            // FIXME-OPT: Could append to index when new value of line_visible_n1 becomes bigger, see second call to CalcClipRectVisibleItemsY() below.
            bool will_scroll_y = state && ((state->CursorFollow && render_cursor) || (state->CursorCenterY && (render_cursor || render_selection)));
            line_count = InputTextLineIndexBuild(flags, line_index, buf_display, buf_display_end, wrap_width, will_scroll_y ? INT_MAX : line_visible_n1 + 1, buf_display_end ? NULL : &buf_display_end);
        }
        line_index->EndOffset = (int)(buf_display_end - buf_display);
        line_visible_n1 = ImMin(line_visible_n1, line_count);

        // Store text height (we don't need width)
        float text_size_y = line_count * g.FontSize;
        //GetForegroundDrawList()->AddRect(draw_pos + ImVec2(0, line_visible_n0 * g.FontSize), draw_pos + ImVec2(frame_size.x, line_visible_n1 * g.FontSize), IM_COL32(255, 0, 0, 255));

        // Calculate blinking cursor position
        const ImVec2 cursor_offset = render_cursor && state ? InputTextLineIndexGetPosOffset(g, state, line_index, buf_display, buf_display_end, state->Stb->cursor) : ImVec2(0.0f, 0.0f);
        ImVec2 draw_scroll;

        // Render text. We currently only render selection when the widget is active or while scrolling.
        const ImU32 text_col = GetColorU32(is_displaying_hint ? ImGuiCol_TextDisabled : ImGuiCol_Text);
        if (render_cursor || render_selection)
        {
            // Render text (with cursor and selection)
            // This is going to be messy. We need to:
            // - Display the text (this alone can be more easily clipped)
            // - Handle scrolling, highlight selection, display cursor (those all requires some form of 1d->2d cursor position calculation)
            // - Measure text height (for scrollbar)
            // We are attempting to do most of that in **one main pass** to minimize the computation cost (non-negligible for large amount of text) + 2nd pass for selection rendering (we could merge them by an extra refactoring effort)
            // FIXME: This should occur on buf_display but we'd need to maintain cursor/select_start/select_end for UTF-8.
            IM_ASSERT(state != NULL);
            state->LineCount = line_count;

            // Scroll
            float new_scroll_y = scroll_y;
            if (render_cursor && state->CursorFollow)
            {
                // Horizontal scroll in chunks of quarter width
                if (!(flags & ImGuiInputTextFlags_NoHorizontalScroll))
                {
                    const float scroll_increment_x = inner_size.x * 0.25f;
                    const float visible_width = inner_size.x - style.FramePadding.x;
                    if (cursor_offset.x < state->Scroll.x)
                        state->Scroll.x = IM_TRUNC(ImMax(0.0f, cursor_offset.x - scroll_increment_x));
                    else if (cursor_offset.x - visible_width >= state->Scroll.x)
                        state->Scroll.x = IM_TRUNC(cursor_offset.x - visible_width + scroll_increment_x);
                }
                else
                {
                    state->Scroll.x = 0.0f;
                }

                // Vertical scroll
                if (is_multiline)
                {
                    // Test if cursor is vertically visible
                    if (cursor_offset.y - g.FontSize < scroll_y)
                        new_scroll_y = ImMax(0.0f, cursor_offset.y - g.FontSize);
                    else if (cursor_offset.y - (inner_size.y - style.FramePadding.y * 2.0f) >= scroll_y)
                        new_scroll_y = cursor_offset.y - inner_size.y + style.FramePadding.y * 2.0f;
                }
                state->CursorFollow = false;
            }
            if (state->CursorCenterY)
            {
                if (is_multiline)
                    new_scroll_y = cursor_offset.y - g.FontSize - (inner_size.y * 0.5f - style.FramePadding.y);
                state->CursorCenterY = false;
                render_cursor = false;
            }
            if (new_scroll_y != scroll_y)
            {
                const float scroll_max_y = ImMax((text_size_y + style.FramePadding.y * 2.0f) - inner_size.y, 0.0f);
                scroll_y = ImClamp(new_scroll_y, 0.0f, scroll_max_y);
                draw_pos.y += (draw_window->Scroll.y - scroll_y);   // Manipulate cursor pos immediately avoid a frame of lag
                draw_window->Scroll.y = scroll_y;
                CalcClipRectVisibleItemsY(clip_rect, draw_pos, g.FontSize, &line_visible_n0, &line_visible_n1);
                line_visible_n1 = ImMin(line_visible_n1, line_count);
            }

            // Draw selection
            draw_scroll.x = state->Scroll.x;
            if (render_selection)
            {
                const ImU32 bg_color = GetColorU32(ImGuiCol_TextSelectedBg, render_cursor ? 1.0f : 0.6f); // FIXME: current code flow mandate that render_cursor is always true here, we are leaving the transparent one for tests.
                const float bg_offy_up = is_multiline ? 0.0f : -1.0f; // FIXME-DPI: those offsets should be part of the style? they don't play so well with multi-line selection.
                const float bg_offy_dn = is_multiline ? 0.0f : 2.0f;
                const float bg_eol_width = IM_TRUNC(g.FontBaked->GetCharAdvance((ImWchar)' ') * 0.50f); // So we can see selected empty lines

                const char* text_selected_begin = buf_display + ImMin(state->Stb->select_start, state->Stb->select_end);
                const char* text_selected_end = buf_display + ImMax(state->Stb->select_start, state->Stb->select_end);
                for (int line_n = line_visible_n0; line_n < line_visible_n1; line_n++)
                {
                    const char* p = line_index->get_line_begin(buf_display, line_n);
                    const char* p_eol = line_index->get_line_end(buf_display, line_n);
                    const bool p_eol_is_wrap = (p_eol < buf_display_end && *p_eol != '\n');
                    if (p_eol_is_wrap)
                        p_eol++;
                    const char* line_selected_begin = (text_selected_begin > p) ? text_selected_begin : p;
                    const char* line_selected_end = (text_selected_end < p_eol) ? text_selected_end : p_eol;

                    float rect_width = 0.0f;
                    if (line_selected_begin < line_selected_end)
                        rect_width += CalcTextSize(line_selected_begin, line_selected_end).x;
                    if (text_selected_begin <= p_eol && text_selected_end > p_eol && !p_eol_is_wrap)
                        rect_width += bg_eol_width; // So we can see selected empty lines
                    if (rect_width == 0.0f)
                        continue;

                    ImRect rect;
                    rect.Min.x = draw_pos.x - draw_scroll.x + CalcTextSize(p, line_selected_begin).x;
                    rect.Min.y = draw_pos.y - draw_scroll.y + line_n * g.FontSize;
                    rect.Max.x = rect.Min.x + rect_width;
                    rect.Max.y = rect.Min.y + bg_offy_dn + g.FontSize;
                    rect.Min.y += bg_offy_up;
                    rect.ClipWith(clip_rect);
                    draw_window->DrawList->AddRectFilled(rect.Min, rect.Max, bg_color);
                }
            }
        }

        // Find render position for right alignment (single-line only)
        if (g.ActiveId != id && (flags & ImGuiInputTextFlags_ElideLeft) && !render_cursor && !render_selection)
            draw_pos.x = ImMin(draw_pos.x, frame_bb.Max.x - CalcTextSize(buf_display, NULL).x - style.FramePadding.x);
        //draw_scroll.x = state->Scroll.x; // Preserve scroll when inactive?

        // Render text
        if ((is_multiline || (buf_display_end - buf_display) < buf_display_max_length) && (text_col & IM_COL32_A_MASK) && (line_visible_n0 < line_visible_n1))
        {
            const char* lineBegin = line_index->get_line_begin(buf_display, line_visible_n0);

            if (textGeometryData != nullptr)
            {
                textGeometryData->Clear();
                textGeometryData->m_DrawList = draw_window->DrawList;
                textGeometryData->m_FirstVertex = static_cast<uint32_t>(draw_window->DrawList->VtxBuffer.size());

                textGeometryData->m_BufferStartDisplay = lineBegin - buf_display;
            }


            RenderTextWithFont(g.Font,
                draw_window->DrawList,
                g.FontSize,
                draw_pos - draw_scroll + ImVec2(0.0f, line_visible_n0 * g.FontSize),
                text_col,
                clip_rect.AsVec4(),
                line_index->get_line_begin(buf_display, line_visible_n0),
                line_index->get_line_end(buf_display, line_visible_n1 - 1),
                wrap_width,
                ImDrawTextFlags_WrapKeepBlanks | ImDrawTextFlags_CpuFineClip,
                textGeometryData
            );

            if (textGeometryData != nullptr)
            {
                textGeometryData->m_VertexCount = draw_window->DrawList->VtxBuffer.size() - textGeometryData->m_FirstVertex;
            }
        }

        // Render blinking cursor
        if (render_cursor)
        {
            state->CursorAnim += io.DeltaTime;
            bool cursor_is_visible = (!g.IO.ConfigInputTextCursorBlink) || (state->CursorAnim <= 0.0f) || ImFmod(state->CursorAnim, 1.20f) <= 0.80f;
            ImVec2 cursor_screen_pos = ImTrunc(draw_pos + cursor_offset - draw_scroll);
            ImRect cursor_screen_rect(cursor_screen_pos.x, cursor_screen_pos.y - g.FontSize + 0.5f, cursor_screen_pos.x + 1.0f, cursor_screen_pos.y - 1.5f);
            if (cursor_is_visible && cursor_screen_rect.Overlaps(clip_rect))
                draw_window->DrawList->AddLineV(cursor_screen_rect.Min.x, cursor_screen_rect.Min.y, cursor_screen_rect.Max.y, GetColorU32(ImGuiCol_InputTextCursor), style.InputTextCursorSize);

            // Notify OS of text input position for advanced IME (-1 x offset so that Windows IME can cover our cursor. Bit of an extra nicety.)
            // This is required for some backends (SDL3) to start emitting character/text inputs.
            // As per #6341, make sure we don't set that on the deactivating frame.
            if (!is_readonly && g.ActiveId == id)
            {
                ImGuiPlatformImeData* ime_data = &g.PlatformImeData; // (this is a public struct, passed to io.Platform_SetImeDataFn() handler)
                ime_data->WantVisible = true;
                ime_data->WantTextInput = true;
                ime_data->InputPos = ImVec2(cursor_screen_pos.x - 1.0f, cursor_screen_pos.y - g.FontSize);
                ime_data->InputLineHeight = g.FontSize;
                ime_data->ViewportId = window->Viewport->ID;
            }


            if (textGeometryData != nullptr)
            {
                textGeometryData->m_TextCursorScreenPosition = cursor_screen_pos;
                textGeometryData->m_TextCursorScreenRectMin = cursor_screen_rect.Min;
                textGeometryData->m_TextCursorScreenRectMax = cursor_screen_rect.Max;

                textGeometryData->m_bHasValidCursorData = true;
            }
        }

        if (is_password && !is_displaying_hint)
            PopPasswordFont();

        if (is_multiline)
        {
            // For focus requests to work on our multiline we need to ensure our child ItemAdd() call specifies the ImGuiItemFlags_Inputable (see #4761, #7870)...
            Dummy(ImVec2(0.0f, text_size_y + style.FramePadding.y));
            g.NextItemData.ItemFlagsSet |= (ImGuiItemFlags)ImGuiItemFlags_Inputable | ImGuiItemFlags_NoTabStop;
            EndChild();
            item_data_backup.StatusFlags |= (g.LastItemData.StatusFlags & ImGuiItemStatusFlags_HoveredWindow);

            // ...and then we need to undo the group overriding last item data, which gets a bit messy as EndGroup() tries to forward scrollbar being active...
            // FIXME: This quite messy/tricky, should attempt to get rid of the child window.
            EndGroup();
            if (g.LastItemData.ID == 0 || g.LastItemData.ID != GetWindowScrollbarID(draw_window, ImGuiAxis_Y))
            {
                g.LastItemData.ID = id;
                g.LastItemData.ItemFlags = item_data_backup.ItemFlags;
                g.LastItemData.StatusFlags = item_data_backup.StatusFlags;
            }
        }
        if (state && is_readonly)
            state->TextSrc = NULL;

        // Log as text
        if (g.LogEnabled && (!is_password || is_displaying_hint))
        {
            LogSetNextTextDecoration("{", "}");
            LogRenderedText(&draw_pos, buf_display, buf_display_end);
        }

        if (label_size.x > 0)
            RenderText(ImVec2(frame_bb.Max.x + style.ItemInnerSpacing.x, frame_bb.Min.y + style.FramePadding.y), label, label_end, false);

        if (value_changed)
            MarkItemEdited(id);

        IMGUI_TEST_ENGINE_ITEM_INFO(id, label, g.LastItemData.StatusFlags | ImGuiItemStatusFlags_Inputable);
        if ((flags & ImGuiInputTextFlags_EnterReturnsTrue) != 0)
            return validated;
        else
            return value_changed;
    }


    IMGUI_API bool InputTextMultilineWithGeometry(
        const char* label,
        char* buf,
        size_t buf_size,
        const ImVec2& size,
        ImGuiInputTextFlags flags,
        ImGuiInputTextCallback callback,
        void* user_data,
        ImMulitlineTextGeometryData* textGeometryData
    )
    {
        return InputTextEx_WithGeometry(label, NULL, buf, (int)buf_size, size, flags | ImGuiInputTextFlags_Multiline, callback, user_data, textGeometryData);
    }

}
