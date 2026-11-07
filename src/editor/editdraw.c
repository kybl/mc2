/*
   Editor text drawing.

   Copyright (C) 1996-2026
   Free Software Foundation, Inc.

   Written by:
   Paul Sheer, 1996, 1997
   Andrew Borodin <aborodin@vmail.ru> 2012-2022
   Slava Zanko <slavazanko@gmail.com>, 2013

   This file is part of the Midnight Commander.

   The Midnight Commander is free software: you can redistribute it
   and/or modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation, either version 3 of the License,
   or (at your option) any later version.

   The Midnight Commander is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/** \file
 *  \brief Source: editor text drawing
 *  \author Paul Sheer
 *  \date 1996, 1997
 */

#include <config.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

#include "lib/global.h"
#include "lib/tty/tty.h"  // tty_printf()
#include "lib/tty/key.h"  // is_idle()
#include "lib/skin.h"
#include "lib/strutil.h"  // utf string functions
#include "lib/util.h"     // is_printable()
#include "lib/widget.h"
#include "lib/charsets.h"

#include "edit-impl.h"
#include "editwidget.h"
#include "editwrap.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#define MAX_LINE_LEN 1024

/* Text styles */
#define MOD_ABNORMAL                  (1 << 8)
#define MOD_BOLD                      (1 << 9)
#define MOD_MARKED                    (1 << 10)
#define MOD_CURSOR                    (1 << 11)
#define MOD_WHITESPACE                (1 << 12)

#define edit_move(x, y)               widget_gotoyx (edit, y, x);

#define key_pending(x)                (!is_idle ())

#define EDITOR_MINIMUM_TERMINAL_WIDTH 30

/*** file scope type declarations ****************************************************************/

typedef struct
{
    unsigned int ch;
    unsigned int style;
} line_s;

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/*** file scope functions ************************************************************************/

static inline void
printwstr (const char *s, int len)
{
    if (len > 0)
        tty_printf ("%-*.*s", len, len, s);
}

/* --------------------------------------------------------------------------------------------- */

#define CHAR_CODE_BUF_SIZE BUF_TINY

static char *
format_character_code (WEdit *edit)
{
    char *buf;

    buf = g_malloc (CHAR_CODE_BUF_SIZE);

    if (edit->buffer.curs1 >= edit->buffer.size)
        strncpy (buf, "<EOF>   ", CHAR_CODE_BUF_SIZE);
    else if (edit->utf8)
    {
        unsigned int cur_utf;
        int char_length = 0;

        cur_utf = edit_buffer_get_utf (&edit->buffer, edit->buffer.curs1, &char_length);
        if (char_length > 0)
            if (cur_utf < 128)
                g_snprintf (buf, CHAR_CODE_BUF_SIZE, "%3u 0x%02X", cur_utf, cur_utf);
            else
            {
                char uplus[CHAR_CODE_BUF_SIZE];

                g_snprintf (uplus, CHAR_CODE_BUF_SIZE, "U+%04X", cur_utf);
                g_snprintf (buf, CHAR_CODE_BUF_SIZE, "%8s", uplus);
            }
        else
        {
            // cursor is over some invalid UTF-8 fragment
            cur_utf = edit_buffer_get_current_byte (&edit->buffer);
            g_snprintf (buf, CHAR_CODE_BUF_SIZE, "%3u 0x%02X", cur_utf, cur_utf);
        }
    }
    else
    {
        unsigned int cur_byte;

        cur_byte = edit_buffer_get_current_byte (&edit->buffer);
        g_snprintf (buf, CHAR_CODE_BUF_SIZE, "%3u 0x%02X", cur_byte, cur_byte);
    }

    return buf;
}

#undef CHAR_CODE_BUF_SIZE

/* --------------------------------------------------------------------------------------------- */

static inline void
status_string (WEdit *edit, char *s, int w)
{
    char *character_code;

    character_code = format_character_code (edit);

    // The field lengths just prevent the status line from shortening too much
    if (edit_options.simple_statusbar)
        g_snprintf (s, w, "%c%c%c%c %3ld %5ld/%ld %6ld/%ld [%s] %s",
                    edit->mark1 != edit->mark2 ? (edit->column_highlight ? 'C' : 'B') : '-',  //
                    edit->modified != 0 ? 'M' : '-',                                          //
                    macro_index < 0 ? '-' : 'R',                                              //
                    edit->overwrite == 0 ? '-' : 'O',                                         //
                    edit->curs_col + edit->over_col,                                          //
                    edit->buffer.curs_line + 1,                                               //
                    edit->buffer.lines + 1,                                                   //
                    (long) edit->buffer.curs1,                                                //
                    (long) edit->buffer.size,                                                 //
                    character_code,
                    mc_global.source_codepage >= 0 ? get_codepage_id (mc_global.source_codepage)
                                                   : "");
    else
        g_snprintf (s, w, "[%c%c%c%c] %2ld L:[%3ld+%2ld %3ld/%3ld] *(%-4ld/%4ldb) [%s]  %s",
                    edit->mark1 != edit->mark2 ? (edit->column_highlight ? 'C' : 'B') : '-',  //
                    edit->modified != 0 ? 'M' : '-',                                          //
                    macro_index < 0 ? '-' : 'R',                                              //
                    edit->overwrite == 0 ? '-' : 'O',                                         //
                    edit->curs_col + edit->over_col,                                          //
                    edit->start_line + 1,                                                     //
                    edit->curs_row,                                                           //
                    edit->buffer.curs_line + 1,                                               //
                    edit->buffer.lines + 1,                                                   //
                    (long) edit->buffer.curs1,                                                //
                    (long) edit->buffer.size,                                                 //
                    character_code,
                    mc_global.source_codepage >= 0 ? get_codepage_id (mc_global.source_codepage)
                                                   : "");

    g_free (character_code);
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Draw the status line at the top of the screen for fullscreen editor window.
 *
 * @param edit  editor object
 * @param color color pair
 */

static inline void
edit_status_fullscreen (WEdit *edit, int color)
{
    Widget *h = WIDGET (WIDGET (edit)->owner);
    const int w = h->rect.cols;
    const int gap = 3;        // between the filename and the status
    const int right_gap = 5;  // at the right end of the screen
    const int preferred_fname_len = 16;
    char *status;
    size_t status_size;
    int status_len;
    const char *fname = "";
    int fname_len;

    status_size = w + 1;
    status = g_malloc (status_size);
    status_string (edit, status, status_size);
    status_len = (int) str_term_width1 (status);

    if (edit->filename_vpath != NULL)
    {
        fname = vfs_path_get_last_path_str (edit->filename_vpath);

        if (!edit_options.state_full_filename)
            fname = x_basename (fname);
    }

    fname_len = str_term_width1 (fname);
    if (fname_len < preferred_fname_len)
        fname_len = preferred_fname_len;

    if (fname_len + gap + status_len + right_gap >= w)
    {
        if (preferred_fname_len + gap + status_len + right_gap >= w)
            fname_len = preferred_fname_len;
        else
            fname_len = w - (gap + status_len + right_gap);
        fname = str_trunc (fname, fname_len);
    }

    widget_gotoyx (h, 0, 0);
    tty_setcolor (color);
    printwstr (fname, fname_len + gap);
    printwstr (status, w - (fname_len + gap));

    if (edit_options.simple_statusbar && w > EDITOR_MINIMUM_TERMINAL_WIDTH)
    {
        int percent;

        percent = edit_buffer_calc_percent (&edit->buffer, edit->buffer.curs1);
        widget_gotoyx (h, 0, w - 6 - 6);
        tty_printf (" %3d%%", percent);
    }

    g_free (status);
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Draw status line for editor window if window is not in fullscreen mode.
 *
 * @param edit editor object
 */

static inline void
edit_status_window (WEdit *edit)
{
    Widget *w = WIDGET (edit);
    int y, x;
    int cols = w->rect.cols;

    tty_setcolor (STATUSBAR_COLOR);

    if (cols > 5)
    {
        const char *fname;

        if (edit->filename_vpath != NULL)
        {
            fname = vfs_path_get_last_path_str (edit->filename_vpath);

            if (!edit_options.state_full_filename)
                fname = x_basename (fname);
        }
        else
            fname = _ ("NoName");

        edit_move (2, 0);
        tty_printf ("[%s]", str_term_trim (fname, w->rect.cols - 8 - 6));
    }

    tty_getyx (&y, &x);
    x -= w->rect.x;
    x += 4;
    if (x + 6 <= cols - 2 - 6)
    {
        edit_move (x, 0);
        tty_printf ("[%c%c%c%c]",
                    edit->mark1 != edit->mark2 ? (edit->column_highlight ? 'C' : 'B') : '-',
                    edit->modified != 0 ? 'M' : '-', macro_index < 0 ? '-' : 'R',
                    edit->overwrite == 0 ? '-' : 'O');
    }

    if (cols > 30)
    {
        edit_move (2, w->rect.lines - 1);
        tty_printf ("%3ld %5ld/%ld %6ld/%ld", edit->curs_col + edit->over_col,
                    edit->buffer.curs_line + 1, edit->buffer.lines + 1, (long) edit->buffer.curs1,
                    (long) edit->buffer.size);
    }

    /*
     * If we are at the end of file, print <EOF>,
     * otherwise print the current character as is (if printable),
     * as decimal and as hex.
     */
    if (cols > 42)
    {
        char *character_code;

        edit_move (32, w->rect.lines - 1);
        character_code = format_character_code (edit);
        tty_printf ("[%s]", character_code);
        g_free (character_code);
    }
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Draw a frame around edit area.
 *
 * @param edit   editor object
 * @param color  color pair
 * @param active TRUE if editor object is focused
 */

static inline void
edit_draw_frame (const WEdit *edit, int color, gboolean active)
{
    const Widget *w = CONST_WIDGET (edit);

    // draw a frame around edit area
    tty_setcolor (color);
    // draw double frame for active window if skin supports that
    tty_draw_box (w->rect.y, w->rect.x, w->rect.lines, w->rect.cols, !active);
    // draw a drag marker
    if (edit->drag_state == MCEDIT_DRAG_NONE)
    {
        tty_setcolor (EDITOR_FRAME_DRAG_COLOR);
        widget_gotoyx (w, w->rect.lines - 1, w->rect.cols - 1);
        tty_print_char (mc_tty_frm[MC_TTY_FRM_RIGHTBOTTOM]);
    }
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Draw a window control buttons.
 *
 * @param edit  editor object
 * @param color color pair
 */

static inline void
edit_draw_window_icons (const WEdit *edit, int color)
{
    const Widget *w = CONST_WIDGET (edit);
    char tmp[17];

    tty_setcolor (color);
    if (edit->fullscreen != 0)
        widget_gotoyx (w->owner, 0, WIDGET (w->owner)->rect.cols - 6);
    else
        widget_gotoyx (w, 0, w->rect.cols - 8);
    g_snprintf (tmp, sizeof (tmp), "[%s][%s]", edit_window_state_char, edit_window_close_char);
    tty_print_string (tmp);
}

/* --------------------------------------------------------------------------------------------- */

static inline void
print_to_widget (WEdit *edit, long row, int start_col, int start_col_real, long end_col, long scol,
                 line_s line[], char *status, int bookmarked)
{
    Widget *w = WIDGET (edit);
    line_s *p;
    int x1, y, cols_to_skip;
    int i;
    int wrap_start;
    int len;

    x1 = start_col + EDIT_TEXT_HORIZONTAL_OFFSET + edit_options.line_state_width;
    y = row + EDIT_TEXT_VERTICAL_OFFSET;
    cols_to_skip = abs (start_col_real);

    if (edit->fullscreen == 0)
    {
        x1++;
        y++;
    }

    tty_setcolor (EDITOR_NORMAL_COLOR);
    if (bookmarked != 0)
        tty_setcolor (bookmarked);

    len = end_col + 1 - start_col;
    wrap_start = edit_options.word_wrap_line_length + scol;

    if (len > 0 && w->rect.y + y >= 0)
    {
        if (!edit_options.show_right_margin || wrap_start > end_col)
            tty_draw_hline (w->rect.y + y, w->rect.x + x1, ' ', len);
        else if (wrap_start < 0)
        {
            tty_setcolor (EDITOR_RIGHT_MARGIN_COLOR);
            tty_draw_hline (w->rect.y + y, w->rect.x + x1, ' ', len);
        }
        else
        {
            if (wrap_start > 0)
                tty_draw_hline (w->rect.y + y, w->rect.x + x1, ' ', wrap_start);

            len -= wrap_start;
            if (len > 0)
            {
                tty_setcolor (EDITOR_RIGHT_MARGIN_COLOR);
                tty_draw_hline (w->rect.y + y, w->rect.x + x1 + wrap_start, ' ', len);
            }
        }
    }

    if (edit_options.line_state)
    {
        tty_setcolor (EDITOR_LINE_STATE_COLOR);

        for (i = 0; i < LINE_STATE_WIDTH; i++)
        {
            edit_move (x1 + i - edit_options.line_state_width, y);
            if (status[i] == '\0')
                status[i] = ' ';
            tty_print_char (status[i]);
        }
    }

    edit_move (x1, y);

    i = 1;
    for (p = line; p->ch != 0; p++)
    {
        int style;
        unsigned int textchar;

        if (cols_to_skip != 0)
        {
            cols_to_skip--;
            continue;
        }

        style = p->style & 0xFF00;
        textchar = p->ch;

        if ((style & MOD_WHITESPACE) != 0)
        {
            if ((style & MOD_MARKED) == 0)
                tty_setcolor (EDITOR_WHITESPACE_COLOR);
            else
            {
                textchar = ' ';
                tty_setcolor (EDITOR_MARKED_COLOR);
            }
        }
        else if ((style & MOD_BOLD) != 0)
            tty_setcolor (EDITOR_BOLD_COLOR);
        else if ((style & MOD_MARKED) != 0)
            tty_setcolor (EDITOR_MARKED_COLOR);
        else if ((style & MOD_ABNORMAL) != 0)
            tty_setcolor (EDITOR_NONPRINTABLE_COLOR);
        else
            tty_setcolor (p->style >> 16);

        if (edit_options.show_right_margin)
        {
            if (i > edit_options.word_wrap_line_length + scol)
                tty_setcolor (EDITOR_RIGHT_MARGIN_COLOR);
            i++;
        }

        tty_print_anychar (textchar);
    }
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Draw one screen row of the text.
 *
 * @param edit      editor object
 * @param b         offset of the beginning of the logical line
 * @param line      number of the logical line
 * @param row       screen row
 * @param start_col first screen column to draw
 * @param end_col   last screen column to draw
 * @param seg       segment of the line to draw (soft wrap mode) or NULL to draw the line
 *                  according to the horizontal scroll position
 */

static void
edit_draw_this_line (WEdit *edit, off_t b, long line, long row, long start_col, long end_col,
                     const edit_wrap_seg_t *seg)
{
    Widget *w = WIDGET (edit);
    line_s line_buf[MAX_LINE_LEN];
    line_s *p = line_buf;
    off_t q;
    int col, start_col_real;
    int abn_style;
    int book_mark = 0;
    char line_stat[LINE_STATE_WIDTH + 1] = "\0";
    // horizontal offset of the drawn text: scroll position or the wrapped segment start
    long scol;

    if (row > w->rect.lines - 1 - EDIT_TEXT_VERTICAL_OFFSET - 2 * (edit->fullscreen != 0 ? 0 : 1))
        return;

    if (book_mark_query_color (edit, line, EDITOR_BOOKMARK_COLOR))
        book_mark = EDITOR_BOOKMARK_COLOR;
    else if (book_mark_query_color (edit, line, EDITOR_BOOKMARK_FOUND_COLOR))
        book_mark = EDITOR_BOOKMARK_FOUND_COLOR;

    if (book_mark != 0)
        abn_style = book_mark << 16;
    else
        abn_style = MOD_ABNORMAL;

    end_col -= EDIT_TEXT_HORIZONTAL_OFFSET + edit_options.line_state_width;
    if (edit->fullscreen == 0)
    {
        end_col--;
        if (w->rect.x + w->rect.cols <= WIDGET (w->owner)->rect.cols)
            end_col--;
    }

    if (seg == NULL)
    {
        scol = edit->start_col;
        q = edit_move_forward3 (edit, b, start_col - scol, 0);
        col = (int) edit_move_forward3 (edit, b, 0, q);
    }
    else
    {
        scol = -seg->col;
        q = seg->start;
        col = (int) seg->col;
        // never draw more than the segment width: the rest belongs to the next row
        end_col = MIN (end_col, edit_wrap_width (edit) - 1);
    }
    start_col_real = col + scol;

    if (edit_options.line_state)
    {
        if (line <= edit->buffer.lines && (seg == NULL || seg->start == b))
            g_snprintf (line_stat, sizeof (line_stat), "%7ld ", line + 1);
        else
        {
            memset (line_stat, ' ', LINE_STATE_WIDTH);
            line_stat[LINE_STATE_WIDTH] = '\0';
        }

        if ((seg == NULL || seg->start == b)
            && book_mark_query_color (edit, line, EDITOR_BOOKMARK_COLOR))
            g_snprintf (line_stat, 2, "*");
    }

    if (col <= -(scol + 16))
        start_col_real = start_col = 0;
    else
    {
        off_t m1 = 0, m2 = 0;

        eval_marks (edit, &m1, &m2);

        if (line <= edit->buffer.lines)
        {
            off_t tws = 0;

            if (edit_options.visible_tws && tty_use_colors ())
                for (tws = edit_buffer_get_eol (&edit->buffer, b); tws > b; tws--)
                {
                    unsigned int c;

                    c = edit_buffer_get_byte (&edit->buffer, tws - 1);
                    if (!whitespace (c))
                        break;
                }

            while (col <= end_col - scol)
            {
                int char_length = 1;
                unsigned int c;
                gboolean wide_width_char = FALSE;
                gboolean control_char = FALSE;
                gboolean printable;

                p->ch = 0;
                p->style = q == edit->buffer.curs1 ? MOD_CURSOR : 0;

                if (q >= m1 && q < m2)
                {
                    if (!edit->column_highlight)
                        p->style |= MOD_MARKED;
                    else
                    {
                        long x, cl;

                        x = (long) edit_move_forward3 (edit, b, 0, q);
                        cl = MIN (edit->column1, edit->column2);
                        if (x >= cl)
                        {
                            cl = MAX (edit->column1, edit->column2);
                            if (x < cl)
                                p->style |= MOD_MARKED;
                        }
                    }
                }

                if (q == edit->bracket)
                    p->style |= MOD_BOLD;
                if (q >= edit->found_start && q < (off_t) (edit->found_start + edit->found_len))
                    p->style |= MOD_BOLD;

                if (edit->utf8)
                    c = edit_buffer_get_utf (&edit->buffer, q, &char_length);
                else
                    c = edit_buffer_get_byte (&edit->buffer, q);

                // we don't use bg for mc - fg contains both
                if (book_mark != 0)
                    p->style |= book_mark << 16;
                else
                {
                    int color;

                    color = edit_get_syntax_color (edit, q);
                    p->style |= color << 16;
                }

                switch (c)
                {
                case '\n':
                    col = end_col - scol + 1;  // quit
                    break;

                case '\t':
                {
                    int tab_over;
                    int i;

                    i = TAB_SIZE - ((int) col % TAB_SIZE);
                    tab_over = (end_col - scol) - (col + i - 1);
                    if (tab_over < 0)
                        i += tab_over;
                    col += i;
                    if ((edit_options.visible_tabs || (edit_options.visible_tws && q >= tws))
                        && enable_show_tabs_tws && tty_use_colors ())
                    {
                        int style;

                        if ((p->style & MOD_MARKED) != 0)
                            style = p->style;
                        else if (book_mark != 0)
                            style = c | (book_mark << 16);
                        else
                            style = p->style | MOD_WHITESPACE;

                        if (i > 1)
                        {
                            p->ch = '<';
                            p->style = style;
                            p++;

                            while (--i > 1)
                            {
                                p->ch = '-';
                                p->style = style;
                                p++;
                            }
                        }

                        p->ch = '>';
                        p->style = style;
                        p++;
                    }
                    else
                    {
                        if (edit_options.visible_tws && q >= tws && enable_show_tabs_tws
                            && tty_use_colors ())
                        {
                            p->ch = '.';
                            p->style |= MOD_WHITESPACE;
                        }
                        else
                            p->ch |= ' ';

                        const int style = p->style & ~MOD_CURSOR;

                        p++;
                        while (--i != 0)
                        {
                            p->ch = ' ';
                            p->style = style;
                            p++;
                        }
                    }
                }
                break;

                case ' ':
                    if (edit_options.visible_tws && q >= tws && enable_show_tabs_tws
                        && tty_use_colors ())
                    {
                        p->ch = '.';
                        p->style |= MOD_WHITESPACE;
                        p++;
                        col++;
                        break;
                    }
                    MC_FALLTHROUGH;

                default:
                    if (mc_global.utf8_display)
                    {
                        if (!edit->utf8)
                            c = convert_from_8bit_to_utf_c ((unsigned char) c, edit->converter);
                        else if (g_unichar_iswide (c))
                        {
                            wide_width_char = TRUE;
                            col++;
                        }
                    }
                    else if (edit->utf8)
                        c = convert_from_utf_to_current_c (c, edit->converter);
                    else
                        c = convert_to_display_c (c);

                    // Caret notation for control characters
                    if (c < 32)
                    {
                        p->ch = '^';
                        p->style = abn_style;
                        p++;
                        p->ch = c + 0x40;
                        p->style = abn_style;
                        p++;
                        col += 2;
                        control_char = TRUE;
                        break;
                    }
                    if (c == 127)
                    {
                        p->ch = '^';
                        p->style = abn_style;
                        p++;
                        p->ch = '?';
                        p->style = abn_style;
                        p++;
                        col += 2;
                        control_char = TRUE;
                        break;
                    }

                    if (edit->utf8)
                    {
                        if (mc_global.utf8_display)
                            // c is gunichar
                            printable = g_unichar_isprint (c);
                        else
                            // c was gunichar; now c is 8-bit char converted from gunichar
                            printable = is_printable (c);
                    }
                    else
                        // c is 8-bit char
                        printable = is_printable (c);

                    if (printable)
                        p->ch = c;
                    else
                    {
                        p->ch = '.';
                        p->style = abn_style;
                    }
                    p++;
                    col++;
                    break;
                }  // case

                q++;
                if (char_length > 1)
                    q += char_length - 1;

                if (col > (end_col - scol + 1))
                {
                    if (wide_width_char)
                    {
                        p--;
                        break;
                    }
                    if (control_char)
                    {
                        p -= 2;
                        break;
                    }
                }
            }
        }
    }

    p->ch = 0;

    print_to_widget (edit, row, start_col, start_col_real, end_col, scol, line_buf, line_stat,
                     book_mark);
}

/* --------------------------------------------------------------------------------------------- */

static inline void
edit_draw_this_char (WEdit *edit, off_t curs, long row, long start_column, long end_column)
{
    off_t b;
    long line;

    b = edit_buffer_get_bol (&edit->buffer, curs);

    if (!edit_wrap_is_enabled (edit))
        edit_draw_this_line (edit, b, edit->start_line + row, row, start_column, end_column, NULL);
    else
    {
        edit_wrap_seg_t seg;

        // the row does not give the line number in soft wrap mode
        if (curs <= edit->buffer.curs1)
            line = edit->buffer.curs_line
                - edit_buffer_count_lines (&edit->buffer, curs, edit->buffer.curs1);
        else
            line = edit->buffer.curs_line
                + edit_buffer_count_lines (&edit->buffer, edit->buffer.curs1, curs);

        edit_wrap_locate (edit, b, curs, &seg, NULL);
        edit_draw_this_line (edit, b, line, row, start_column, end_column, &seg);
    }
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Draw screen rows in soft wrap mode.
 *
 * @param edit         editor object
 * @param start_row    first row to draw
 * @param start_column first column to draw
 * @param end_row      last row to draw
 * @param end_column   last column to draw
 *
 * @return FALSE if drawing was interrupted by a pending key, TRUE otherwise
 */

static gboolean
edit_draw_wrapped_rows (WEdit *edit, long start_row, long start_column, long end_row,
                        long end_column)
{
    edit_wrap_seg_t seg;
    off_t b = edit->start_display;
    long line = edit->start_line;
    long row;

    edit_wrap_clamp_start (edit);
    edit_wrap_get_segment (edit, b, 0, edit_wrap_width (edit), &seg);

    // skip hidden rows of the top line and rows above start_row
    for (row = 0; row < edit->start_wrap_row + start_row; row++)
        if (!edit_wrap_next_segment (edit, edit_wrap_width (edit), &seg))
            edit_wrap_next_line (edit, &b, &line, &seg);

    for (row = start_row; row <= end_row; row++)
    {
        if (key_pending (edit))
            return FALSE;

        edit_draw_this_line (edit, b, line, row, start_column, end_column, &seg);

        if (!edit_wrap_next_segment (edit, edit_wrap_width (edit), &seg))
            edit_wrap_next_line (edit, &b, &line, &seg);
    }

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */
/** cursor must be in screen for other than REDRAW_PAGE passed in force */

static inline void
render_edit_text (WEdit *edit, long start_row, long start_column, long end_row, long end_column)
{
    static long prev_curs_row = 0;
    static off_t prev_curs = 0;

    Widget *we = WIDGET (edit);
    Widget *wh = WIDGET (we->owner);
    WRect *w = &we->rect;

    int force = edit->force;
    int y1, x1, y2, x2;
    int last_line, last_column;

    // draw only visible region

    last_line = wh->rect.y + wh->rect.lines - 1;

    y1 = w->y;
    if (y1 > last_line - 1)  // buttonbar
        return;

    last_column = wh->rect.x + wh->rect.cols - 1;

    x1 = w->x;
    if (x1 > last_column)
        return;

    y2 = w->y + w->lines - 1;
    if (y2 < wh->rect.y + 1)  // menubar
        return;

    x2 = w->x + w->cols - 1;
    if (x2 < wh->rect.x)
        return;

    if ((force & REDRAW_IN_BOUNDS) == 0)
    {
        // !REDRAW_IN_BOUNDS means to ignore bounds and redraw whole rows
        // draw only visible region

        if (y2 <= last_line - 1)  // buttonbar
            end_row = w->lines - 1;
        else if (y1 >= wh->rect.y + 1)  // menubar
            end_row = wh->rect.lines - 1 - y1 - 1;
        else
            end_row = start_row + wh->rect.lines - 1 - 1;

        if (x2 <= last_column)
            end_column = w->cols - 1;
        else if (x1 >= wh->rect.x)
            end_column = wh->rect.cols - 1 - x1;
        else
            end_column = start_column + wh->rect.cols - 1;
    }

    /*
     * If the position of the page has not moved then we can draw the cursor
     * character only.  This will prevent line flicker when using arrow keys.
     */
    if ((force & REDRAW_CHAR_ONLY) == 0 || (force & REDRAW_PAGE) != 0)
    {
        long row = 0;
        long b;

        if (edit_wrap_is_enabled (edit))
        {
            /* In soft wrap mode any change of a line may change the number of its rows
             * and shift all the rows below, so redraw the whole page. */
            if (!edit_draw_wrapped_rows (edit, start_row, start_column, end_row, end_column))
                return;
        }
        else if ((force & REDRAW_PAGE) != 0)
        {
            b = edit_buffer_get_forward_offset (&edit->buffer, edit->start_display, start_row, 0);
            for (row = start_row; row <= end_row; row++)
            {
                if (key_pending (edit))
                    return;
                edit_draw_this_line (edit, b, edit->start_line + row, row, start_column, end_column,
                                     NULL);
                b = edit_buffer_get_forward_offset (&edit->buffer, b, 1, 0);
            }
        }
        else
        {
            long curs_row = edit->curs_row;

            if ((force & REDRAW_BEFORE_CURSOR) != 0 && start_row < curs_row)
            {
                long upto;

                b = edit->start_display;
                upto = MIN (curs_row - 1, end_row);
                for (row = start_row; row <= upto; row++)
                {
                    if (key_pending (edit))
                        return;
                    edit_draw_this_line (edit, b, edit->start_line + row, row, start_column,
                                         end_column, NULL);
                    b = edit_buffer_get_forward_offset (&edit->buffer, b, 1, 0);
                }
            }

            //          if (force & REDRAW_LINE)          ---> default
            b = edit_buffer_get_current_bol (&edit->buffer);
            if (curs_row >= start_row && curs_row <= end_row)
            {
                if (key_pending (edit))
                    return;
                edit_draw_this_line (edit, b, edit->start_line + curs_row, curs_row, start_column,
                                     end_column, NULL);
            }

            if ((force & REDRAW_AFTER_CURSOR) != 0 && end_row > curs_row)
            {
                b = edit_buffer_get_forward_offset (&edit->buffer, b, 1, 0);
                for (row = MAX (curs_row + 1, start_row); row <= end_row; row++)
                {
                    if (key_pending (edit))
                        return;
                    edit_draw_this_line (edit, b, edit->start_line + row, row, start_column,
                                         end_column, NULL);
                    b = edit_buffer_get_forward_offset (&edit->buffer, b, 1, 0);
                }
            }

            if ((force & REDRAW_LINE_ABOVE) != 0 && curs_row >= 1)
            {
                row = curs_row - 1;
                b = edit_buffer_get_current_bol (&edit->buffer);
                b = edit_buffer_get_backward_offset (&edit->buffer, b, 1);
                if (row >= start_row && row <= end_row)
                {
                    if (key_pending (edit))
                        return;
                    edit_draw_this_line (edit, b, edit->start_line + row, row, start_column,
                                         end_column, NULL);
                }
            }

            if ((force & REDRAW_LINE_BELOW) != 0 && row < w->lines - 1)
            {
                row = curs_row + 1;
                b = edit_buffer_get_current_bol (&edit->buffer);
                b = edit_buffer_get_forward_offset (&edit->buffer, b, 1, 0);
                if (row >= start_row && row <= end_row)
                {
                    if (key_pending (edit))
                        return;
                    edit_draw_this_line (edit, b, edit->start_line + row, row, start_column,
                                         end_column, NULL);
                }
            }
        }
    }
    else if (prev_curs_row < edit->curs_row)
    {
        // with the new text highlighting, we must draw from the top down
        edit_draw_this_char (edit, prev_curs, prev_curs_row, start_column, end_column);
        edit_draw_this_char (edit, edit->buffer.curs1, edit->curs_row, start_column, end_column);
    }
    else
    {
        edit_draw_this_char (edit, edit->buffer.curs1, edit->curs_row, start_column, end_column);
        edit_draw_this_char (edit, prev_curs, prev_curs_row, start_column, end_column);
    }

    edit->force = 0;

    prev_curs_row = edit->curs_row;
    prev_curs = edit->buffer.curs1;
}

/* --------------------------------------------------------------------------------------------- */

static inline void
edit_render (WEdit *edit, int page, int row_start, int col_start, int row_end, int col_end)
{
    if (page != 0)  // if it was an expose event, 'page' would be set
        edit->force |= REDRAW_PAGE | REDRAW_IN_BOUNDS;

    render_edit_text (edit, row_start, col_start, row_end, col_end);

    /*
     * edit->force != 0 means a key was pending and the redraw
     * was halted, so next time we must redraw everything in case stuff
     * was left undrawn from a previous key press.
     */
    if (edit->force != 0)
        edit->force |= REDRAW_PAGE;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Scroll the text so that the cursor is on the screen (soft wrap mode).
 *
 * @param edit editor object
 */

static void
edit_wrap_scroll_screen_over_cursor (WEdit *edit)
{
    const long lines = edit_wrap_text_lines (edit);
    long b_extreme = EDIT_BOTTOM_EXTREME;
    long t_extreme = EDIT_TOP_EXTREME;
    long outby;

    // no horizontal scrolling in soft wrap mode
    edit->start_col = 0;

    if (edit->found_len != 0)
    {
        b_extreme = MAX (lines / 4, b_extreme);
        t_extreme = MAX (lines / 4, t_extreme);
    }
    if (b_extreme + t_extreme + 1 > lines)
    {
        long n;

        n = b_extreme + t_extreme;
        if (n == 0)
            n = 1;
        b_extreme = (b_extreme * (lines - 1)) / n;
        t_extreme = (t_extreme * (lines - 1)) / n;
    }

    if (edit->buffer.curs_line < edit->start_line
        || edit->buffer.curs_line > edit->start_line + lines)
    {
        /* The cursor is far away from the page: instead of counting the rows in between,
         * start the page from the cursor row and scroll it back to the wanted position. */
        edit_wrap_seg_t seg;
        const gboolean above = edit->buffer.curs_line < edit->start_line;

        edit->start_line = edit->buffer.curs_line;
        edit->start_display = edit_buffer_get_current_bol (&edit->buffer);
        edit->start_wrap_row =
            edit_wrap_locate (edit, edit->start_display, edit->buffer.curs1, &seg, NULL);
        edit->force |= REDRAW_PAGE;
        edit->force &= (0xfff - REDRAW_CHAR_ONLY);

        edit_scroll_upward (edit, MAX (above ? t_extreme : lines - 1 - b_extreme, 0));
    }

    edit_update_curs_row (edit);
    outby = edit->curs_row - lines + 1 + b_extreme;
    if (outby > 0)
        edit_scroll_downward (edit, outby);
    outby = t_extreme - edit->curs_row;
    if (outby > 0)
        edit_scroll_upward (edit, outby);
    edit_update_curs_row (edit);
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

void
edit_status (WEdit *edit, gboolean active)
{
    int color;

    if (edit->fullscreen != 0)
    {
        color = STATUSBAR_COLOR;
        edit_status_fullscreen (edit, color);
    }
    else
    {
        color = edit->drag_state != MCEDIT_DRAG_NONE ? EDITOR_FRAME_DRAG_COLOR
            : active                                 ? EDITOR_FRAME_ACTIVE_COLOR
                                                     : EDITOR_FRAME_COLOR;
        edit_draw_frame (edit, color, active);
        edit_status_window (edit);
    }

    edit_draw_window_icons (edit, color);
}

/* --------------------------------------------------------------------------------------------- */

/** this scrolls the text so that cursor is on the screen */
void
edit_scroll_screen_over_cursor (WEdit *edit)
{
    WRect *w = &WIDGET (edit)->rect;

    long p;
    long outby;
    int b_extreme, t_extreme, l_extreme, r_extreme;

    if (w->lines <= 0 || w->cols <= 0)
        return;

    /* The code below temporarily shrinks the widget to the text area; the soft wrap code
     * computes the text area from the widget itself, so it must run on the unchanged widget. */
    if (edit_wrap_is_enabled (edit))
    {
        edit_wrap_scroll_screen_over_cursor (edit);
        return;
    }

    rect_resize (w, -EDIT_TEXT_VERTICAL_OFFSET,
                 -(EDIT_TEXT_HORIZONTAL_OFFSET + edit_options.line_state_width));

    if (edit->fullscreen == 0)
        rect_grow (w, -1, -1);

    r_extreme = EDIT_RIGHT_EXTREME;
    l_extreme = EDIT_LEFT_EXTREME;
    b_extreme = EDIT_BOTTOM_EXTREME;
    t_extreme = EDIT_TOP_EXTREME;
    if (edit->found_len != 0)
    {
        b_extreme = MAX (w->lines / 4, b_extreme);
        t_extreme = MAX (w->lines / 4, t_extreme);
    }
    if (b_extreme + t_extreme + 1 > w->lines)
    {
        int n;

        n = b_extreme + t_extreme;
        if (n == 0)
            n = 1;
        b_extreme = (b_extreme * (w->lines - 1)) / n;
        t_extreme = (t_extreme * (w->lines - 1)) / n;
    }
    if (l_extreme + r_extreme + 1 > w->cols)
    {
        int n;

        n = l_extreme + r_extreme;
        if (n == 0)
            n = 1;
        l_extreme = (l_extreme * (w->cols - 1)) / n;
        r_extreme = (r_extreme * (w->cols - 1)) / n;
    }
    p = edit_get_col (edit) + edit->over_col;
    edit_update_curs_row (edit);
    outby = p + edit->start_col - w->cols + 1 + (r_extreme + edit->found_len);
    if (outby > 0)
        edit_scroll_right (edit, outby);
    outby = l_extreme - p - edit->start_col;
    if (outby > 0)
        edit_scroll_left (edit, outby);
    p = edit->curs_row;
    outby = p - w->lines + 1 + b_extreme;
    if (outby > 0)
        edit_scroll_downward (edit, outby);
    outby = t_extreme - p;
    if (outby > 0)
        edit_scroll_upward (edit, outby);
    edit_update_curs_row (edit);

    rect_resize (w, EDIT_TEXT_VERTICAL_OFFSET,
                 EDIT_TEXT_HORIZONTAL_OFFSET + edit_options.line_state_width);
    if (edit->fullscreen == 0)
        rect_grow (w, 1, 1);
}

/* --------------------------------------------------------------------------------------------- */

void
edit_render_keypress (WEdit *edit)
{
    edit_render (edit, 0, 0, 0, 0, 0);
}

/* --------------------------------------------------------------------------------------------- */
