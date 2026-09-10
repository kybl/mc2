/*
   Editor soft (display-only) line wrapping.

   Copyright (C) 2026
   Free Software Foundation, Inc.

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

/** \file editwrap.c
 *  \brief Source: editor soft (display-only) line wrapping
 *
 *  In soft wrap mode a logical line of the file is displayed on one or more screen rows
 *  (segments).  The file content is not changed: wrapping only affects how the text is drawn
 *  and how the cursor moves vertically.
 *
 *  A line is cut into segments at screen column boundaries.  The rules mirror the way
 *  editdraw.c draws a line so that the drawing code and the cursor positioning code agree:
 *
 *    - a TAB character is expanded according to its logical (unwrapped) column; if it does
 *      not fit to the end of the row it is truncated and the next character starts a new row;
 *    - a wide (CJK) character or a control character shown in caret notation (2 columns)
 *      that does not fit to the end of the row is moved to the next row;
 *    - the end of the line ('\n' or the end of file) occupies one screen column, so that the
 *      cursor placed after the last character of the line is always visible; a line whose
 *      content fills the row exactly therefore has one more (empty) row.
 */

#include <config.h>

#include "lib/global.h"
#include "lib/charsets.h"  // convert_*_c()
#include "lib/widget.h"    // WIDGET()

#include "edit-impl.h"
#include "editwidget.h"
#include "editwrap.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */
/**
 * Get the character at the given offset and the number of screen columns it occupies.
 *
 * The width calculation must match edit_draw_this_line() in editdraw.c.
 *
 * @param edit        editor object
 * @param q           offset of the character
 * @param col         logical column of the character (used for TAB expansion)
 * @param char_length [out] length of the character in bytes (always >= 1)
 * @param cells       [out] number of screen columns occupied by the character
 *
 * @return the character ('\n' at the end of the line)
 */

static int
edit_wrap_get_char (const WEdit *edit, off_t q, long col, int *char_length, long *cells)
{
    int c;

    *char_length = 1;

    if (q >= edit->buffer.size)
    {
        *cells = 0;
        return '\n';
    }

    c = edit_buffer_get_byte (&edit->buffer, q);
    if (edit->utf8 && c >= 0x80)
    {
        // multibyte character; ASCII bytes are handled without the UTF-8 decoding
        c = edit_buffer_get_utf (&edit->buffer, q, char_length);
        if (*char_length < 1)
            *char_length = 1;
    }

    switch (c)
    {
    case '\n':
        *cells = 0;
        break;

    case '\t':
        *cells = TAB_SIZE - (col % TAB_SIZE);
        break;

    case ' ':
        *cells = 1;
        break;

    default:
        *cells = 1;

        if (mc_global.utf8_display)
        {
            if (!edit->utf8)
                c = convert_from_8bit_to_utf_c ((unsigned char) c, edit->converter);
            else if (g_unichar_iswide (c))
            {
                *cells = 2;
                break;
            }
        }
        else if (edit->utf8)
            c = convert_from_utf_to_current_c (c, edit->converter);
        else
            c = convert_to_display_c (c);

        // caret notation for control characters
        if (c < 32 || c == 127)
            *cells = 2;
        break;
    }

    return c;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */
/**
 * Check whether soft wrap mode is enabled for the editor.
 *
 * @param edit editor object
 *
 * @return TRUE if lines are wrapped on the screen
 */

gboolean
edit_wrap_is_enabled (const WEdit *edit)
{
    (void) edit;

    return edit_options.soft_wrap;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Get the number of screen columns available for the text in one row.
 *
 * @param edit editor object
 *
 * @return width of the text area, at least 1
 */

long
edit_wrap_width (const WEdit *edit)
{
    long width;

    width = CONST_WIDGET (edit)->rect.cols - EDIT_TEXT_HORIZONTAL_OFFSET
        - edit_options.line_state_width;
    if (edit->fullscreen == 0)
        width -= 2;  // frame

    return MAX (width, 1);
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Compute the segment (screen row) starting at the given offset.
 *
 * @param edit  editor object
 * @param start offset of the first character of the segment
 * @param col   logical column of that character (0 for the beginning of the line)
 * @param width wrap width (see edit_wrap_width())
 * @param seg   [out] segment description
 */

void
edit_wrap_get_segment (const WEdit *edit, off_t start, long col, long width, edit_wrap_seg_t *seg)
{
    off_t q = start;
    long rel = 0;

    seg->start = start;
    seg->col = col;
    seg->last = FALSE;

    while (TRUE)
    {
        int c;
        int char_length;
        long cells;

        c = edit_wrap_get_char (edit, q, col, &char_length, &cells);
        if (c == '\n')
        {
            seg->last = TRUE;
            break;
        }

        if (c == '\t')
        {
            // TAB always fits (it is truncated at the end of the row)
            col += cells;
            rel += MIN (cells, width - rel);
            q += char_length;
        }
        else
        {
            // a character that does not fit into the row is moved to the next row,
            // unless the row is empty: then it is consumed to guarantee progress
            if (rel + cells > width && rel > 0)
                break;

            col += cells;
            rel += cells;
            q += char_length;
        }

        if (rel >= width)
            break;
    }

    seg->next = q;
    seg->next_col = col;
    seg->width = MIN (rel, width);
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Advance to the next segment of the same line.
 *
 * @param edit  editor object
 * @param width wrap width
 * @param seg   [in/out] segment description
 *
 * @return TRUE if the next segment exists and was computed, FALSE if @seg was the last one
 */

gboolean
edit_wrap_next_segment (const WEdit *edit, long width, edit_wrap_seg_t *seg)
{
    if (seg->last)
        return FALSE;

    edit_wrap_get_segment (edit, seg->next, seg->next_col, width, seg);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Get the number of screen rows occupied by a logical line.
 *
 * @param edit editor object
 * @param bol  offset of the beginning of the line
 *
 * @return number of rows, at least 1
 */

long
edit_wrap_line_rows (const WEdit *edit, off_t bol)
{
    const long width = edit_wrap_width (edit);
    edit_wrap_seg_t seg;
    long rows = 1;

    edit_wrap_get_segment (edit, bol, 0, width, &seg);
    while (edit_wrap_next_segment (edit, width, &seg))
        rows++;

    return rows;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Get the n-th segment of a logical line.
 *
 * @param edit editor object
 * @param bol  offset of the beginning of the line
 * @param n    segment index (0-based); if the line has fewer segments, the last one is returned
 * @param seg  [out] segment description
 *
 * @return index of the returned segment
 */

long
edit_wrap_get_segment_n (const WEdit *edit, off_t bol, long n, edit_wrap_seg_t *seg)
{
    const long width = edit_wrap_width (edit);
    long i;

    edit_wrap_get_segment (edit, bol, 0, width, seg);
    for (i = 0; i < n && edit_wrap_next_segment (edit, width, seg); i++)
        ;

    return i;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Find the segment containing the given offset and the screen column of the offset in it.
 *
 * @param edit editor object
 * @param bol  offset of the beginning of the line
 * @param pos  offset of the character; bol <= pos <= end of the line
 * @param seg  [out] segment containing @pos
 * @param cell [out] screen column of @pos in the segment (may be NULL)
 *
 * @return index of the segment (0-based)
 */

long
edit_wrap_locate (const WEdit *edit, off_t bol, off_t pos, edit_wrap_seg_t *seg, long *cell)
{
    const long width = edit_wrap_width (edit);
    long n = 0;

    edit_wrap_get_segment (edit, bol, 0, width, seg);
    while (pos >= seg->next && !seg->last && edit_wrap_next_segment (edit, width, seg))
        n++;

    if (cell != NULL)
    {
        off_t q;
        long col = seg->col;
        long rel = 0;

        for (q = seg->start; q < pos && q < seg->next;)
        {
            int char_length;
            long cells;

            edit_wrap_get_char (edit, q, col, &char_length, &cells);
            col += cells;
            rel += MIN (cells, width - rel);
            q += char_length;
        }

        *cell = MIN (rel, width);
    }

    return n;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Get the offset of the character displayed at the given screen column of a segment.
 *
 * @param edit        editor object
 * @param seg         segment description
 * @param cell        requested screen column (0-based)
 * @param actual_cell [out] screen column of the returned offset (may be NULL)
 *
 * @return offset of the character; if @cell is beyond the segment content, the offset of the
 *         line end (for the last segment) or of the last character of the segment
 */

off_t
edit_wrap_cell_to_offset (const WEdit *edit, const edit_wrap_seg_t *seg, long cell,
                          long *actual_cell)
{
    const long width = edit_wrap_width (edit);
    off_t q, last = seg->start;
    long col = seg->col;
    long rel = 0, last_rel = 0;

    for (q = seg->start; q < seg->next;)
    {
        int char_length;
        long cells;

        edit_wrap_get_char (edit, q, col, &char_length, &cells);
        if (rel + cells > cell)
            break;  // the requested column is inside this character

        last = q;
        last_rel = rel;
        col += cells;
        rel += MIN (cells, width - rel);
        q += char_length;
    }

    if (q >= seg->next && !seg->last)
    {
        // beyond the content of a non-last segment: stay on its last character
        q = last;
        rel = last_rel;
    }

    if (actual_cell != NULL)
        *actual_cell = rel;

    return q;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Advance to the first segment of the next logical line.
 *
 * @param edit editor object
 * @param bol  [in/out] offset of the beginning of the line
 * @param line [in/out] number of the line
 * @param seg  [out] first segment of the next line
 */

void
edit_wrap_next_line (const WEdit *edit, off_t *bol, long *line, edit_wrap_seg_t *seg)
{
    off_t eol;

    eol = edit_buffer_get_eol (&edit->buffer, *bol);
    if (eol < edit->buffer.size)
        *bol = eol + 1;
    else
        *bol = edit->buffer.size;
    (*line)++;

    edit_wrap_get_segment (edit, *bol, 0, edit_wrap_width (edit), seg);
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Get the screen column of the cursor in its row.
 *
 * @param edit editor object
 *
 * @return screen column (0-based, relative to the text area)
 */

long
edit_wrap_cursor_cell (const WEdit *edit)
{
    edit_wrap_seg_t seg;
    long cell;

    edit_wrap_locate (edit, edit_buffer_get_current_bol (&edit->buffer), edit->buffer.curs1, &seg,
                      &cell);

    return cell;
}

/* --------------------------------------------------------------------------------------------- */
