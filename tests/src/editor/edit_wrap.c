/*
   src/editor - tests for soft (display-only) line wrapping

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

#define TEST_SUITE_NAME "/src/editor"

#include "tests/mctest.h"

#include "lib/charsets.h"
#include "lib/keybind.h"  // CK_*
#include "src/selcodepage.h"

#include "src/editor/editwidget.h"
#include "src/editor/editwrap.h"

static WGroup owner;
static WEdit *test_edit;

#define WRAP_WIDTH 10

/* --------------------------------------------------------------------------------------------- */

/* @Before */
static void
setup (void)
{
    WRect r;

    str_init_strings (NULL);

    mc_global.sysconfig_dir = (char *) TEST_SHARE_DIR;
    load_codepages_list ();

    edit_options.filesize_threshold = (char *) "64M";
    edit_options.soft_wrap = TRUE;
    edit_options.line_state = FALSE;
    edit_options.line_state_width = 0;
    edit_options.cursor_beyond_eol = FALSE;
    edit_options.fake_half_tabs = FALSE;
    option_tab_spacing = 8;

    // 8 rows of WRAP_WIDTH columns, fullscreen (no frame)
    rect_init (&r, 0, 0, 8, WRAP_WIDTH);
    test_edit = edit_init (NULL, &r, NULL);
    memset (&owner, 0, sizeof (owner));
    group_add_widget (&owner, WIDGET (test_edit));

    mc_global.source_codepage = 0;
    mc_global.display_codepage = 0;
    mc_global.utf8_display = FALSE;
    cp_source = "ASCII";
    cp_display = "ASCII";

    do_set_codepage (0);
    edit_set_codeset (test_edit);
}

/* --------------------------------------------------------------------------------------------- */

/* @After */
static void
teardown (void)
{
    edit_clean (test_edit);
    group_remove_widget (test_edit);
    g_free (test_edit);

    free_codepages_list ();
    str_uninit_strings ();
}

/* --------------------------------------------------------------------------------------------- */

static void
test_load_text (const char *text)
{
    // edit_insert() keeps the line counters up to date
    for (; *text != '\0'; text++)
        edit_insert (test_edit, *text);

    // move cursor to the beginning of the text
    edit_cursor_move (test_edit, -test_edit->buffer.curs1);
    test_edit->start_display = 0;
    test_edit->start_line = 0;
    test_edit->start_wrap_row = 0;
}

/* --------------------------------------------------------------------------------------------- */

/* @Test */
START_TEST (test_wrap_width)
{
    // given: fullscreen window WRAP_WIDTH columns wide
    // then
    ck_assert_int_eq (edit_wrap_width (test_edit), WRAP_WIDTH);

    // when: framed window
    test_edit->fullscreen = 0;
    // then
    ck_assert_int_eq (edit_wrap_width (test_edit), WRAP_WIDTH - 2);

    // when: line numbers are shown
    test_edit->fullscreen = 1;
    edit_options.line_state_width = 3;
    // then
    ck_assert_int_eq (edit_wrap_width (test_edit), WRAP_WIDTH - 3);
    edit_options.line_state_width = 0;
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

static const struct test_line_rows_ds
{
    const char *text;
    long rows;
} test_line_rows_ds[] = {
    { "", 1 },                      // 0. empty file
    { "\n", 1 },                    // 1. empty line
    { "abc\n", 1 },                 // 2. short line
    { "123456789\n", 1 },           // 3. width - 1 chars: the line end still fits into the row
    { "1234567890\n", 2 },          // 4. exactly width chars: the line end needs one more row
    { "1234567890", 2 },            // 5. the same without newline at the end of file
    { "12345678901\n", 2 },         // 6. width + 1 chars
    { "12345678901234567890", 3 },  // 7. 2 * width chars
    { "\t\n", 1 },                  // 8. one tab (8 columns)
    { "\t\t\n", 2 },           // 9. two tabs: the second one is truncated at the end of the row
    { "12\t\t\n", 2 },         // 10. second tab is truncated, line end on the next row
    { "12345678\t\n", 2 },     // 11. tab starts at column 8 and ends at 10
    { "123456789\x01\n", 2 },  // 12. ^A does not fit: moved to the next row
    { "12345678\x01\n", 2 },   // 13. ^A fits exactly; line end on the next row
    { "1234567\x01\n", 1 },    // 14. ^A fits, line end too
};

/* @Test(dataSource = "test_line_rows_ds") */
START_PARAMETRIZED_TEST (test_line_rows, test_line_rows_ds)
{
    // given
    test_load_text (data->text);

    // then
    ck_assert_int_eq (edit_wrap_line_rows (test_edit, 0), data->rows);
}
END_PARAMETRIZED_TEST

/* --------------------------------------------------------------------------------------------- */

/* @Test */
START_TEST (test_segments)
{
    edit_wrap_seg_t seg;

    // given: tab at logical column 12 (expands to 16), 'x' at 16
    test_load_text ("123456789012\txyz\n");
    //             rows: [1234567890] [12<-->xyz]

    // when
    edit_wrap_get_segment (test_edit, 0, 0, WRAP_WIDTH, &seg);
    // then
    ck_assert_int_eq (seg.start, 0);
    ck_assert_int_eq (seg.col, 0);
    ck_assert_int_eq (seg.next, 10);
    ck_assert_int_eq (seg.next_col, 10);
    ck_assert_int_eq (seg.width, 10);
    mctest_assert_false (seg.last);

    // when
    mctest_assert_true (edit_wrap_next_segment (test_edit, WRAP_WIDTH, &seg));
    // then: "12" + tab (4 columns) + "xyz" and the line end
    ck_assert_int_eq (seg.start, 10);
    ck_assert_int_eq (seg.col, 10);
    ck_assert_int_eq (seg.next, 16);
    ck_assert_int_eq (seg.next_col, 19);
    ck_assert_int_eq (seg.width, 9);
    mctest_assert_true (seg.last);

    // when
    mctest_assert_false (edit_wrap_next_segment (test_edit, WRAP_WIDTH, &seg));

    ck_assert_int_eq (edit_wrap_line_rows (test_edit, 0), 2);
    ck_assert_int_eq (edit_wrap_get_segment_n (test_edit, 0, 1, &seg), 1);
    ck_assert_int_eq (seg.start, 10);
    // beyond the last segment: the last one is returned
    ck_assert_int_eq (edit_wrap_get_segment_n (test_edit, 0, 5, &seg), 1);
    ck_assert_int_eq (seg.start, 10);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* @Test */
START_TEST (test_tab_at_row_boundary)
{
    edit_wrap_seg_t seg;

    // given: tab at column 8 spans columns 8..15, i.e. across the row boundary
    test_load_text ("12345678\tab\n");

    // when
    edit_wrap_get_segment (test_edit, 0, 0, WRAP_WIDTH, &seg);
    // then: the truncated tab ends the first row
    ck_assert_int_eq (seg.next, 9);
    ck_assert_int_eq (seg.next_col, 16);
    ck_assert_int_eq (seg.width, 10);
    mctest_assert_false (seg.last);

    // when
    edit_wrap_next_segment (test_edit, WRAP_WIDTH, &seg);
    // then: "ab" starts the second row at logical column 16
    ck_assert_int_eq (seg.start, 9);
    ck_assert_int_eq (seg.col, 16);
    ck_assert_int_eq (seg.width, 2);
    mctest_assert_true (seg.last);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* @Test */
START_TEST (test_wide_chars)
{
    edit_wrap_seg_t seg;
    long cell;

    // given: UTF-8 text with wide (CJK) chars; width 10: "abcdefghi" + U+4E2D would need 11
    mc_global.utf8_display = TRUE;
    test_edit->utf8 = TRUE;
    test_load_text ("abcdefghi\xe4\xb8\xad\xe6\x96\x87z\n");  // "abcdefghi中文z\n"

    // when
    edit_wrap_get_segment (test_edit, 0, 0, WRAP_WIDTH, &seg);
    // then: the wide char is moved to the next row, the first row has 9 columns
    ck_assert_int_eq (seg.next, 9);
    ck_assert_int_eq (seg.width, 9);
    mctest_assert_false (seg.last);

    // when
    edit_wrap_next_segment (test_edit, WRAP_WIDTH, &seg);
    // then: 2 wide chars (4 columns) + 'z'
    ck_assert_int_eq (seg.start, 9);
    ck_assert_int_eq (seg.col, 9);
    ck_assert_int_eq (seg.width, 5);
    mctest_assert_true (seg.last);

    // when: the second wide char (bytes 12..14) is at cell 2 of the second row
    ck_assert_int_eq (edit_wrap_locate (test_edit, 0, 12, &seg, &cell), 1);
    ck_assert_int_eq (cell, 2);
    // and: cell 3 (the second half of the wide char) maps to the wide char
    ck_assert_int_eq (edit_wrap_cell_to_offset (test_edit, &seg, 3, &cell), 12);
    ck_assert_int_eq (cell, 2);
    // and: cell 4 is 'z'
    ck_assert_int_eq (edit_wrap_cell_to_offset (test_edit, &seg, 4, &cell), 15);
    ck_assert_int_eq (cell, 4);

    mc_global.utf8_display = FALSE;
    test_edit->utf8 = FALSE;
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* @Test */
START_TEST (test_locate_and_cells)
{
    edit_wrap_seg_t seg;
    long cell;

    // given
    test_load_text ("12345678901234567890\nab\n");

    // offsets 0..9 in row 0, 10..19 in row 1, line end (20) in row 2
    ck_assert_int_eq (edit_wrap_locate (test_edit, 0, 0, &seg, &cell), 0);
    ck_assert_int_eq (cell, 0);
    ck_assert_int_eq (edit_wrap_locate (test_edit, 0, 9, &seg, &cell), 0);
    ck_assert_int_eq (cell, 9);
    ck_assert_int_eq (edit_wrap_locate (test_edit, 0, 10, &seg, &cell), 1);
    ck_assert_int_eq (cell, 0);
    ck_assert_int_eq (edit_wrap_locate (test_edit, 0, 19, &seg, &cell), 1);
    ck_assert_int_eq (cell, 9);
    ck_assert_int_eq (edit_wrap_locate (test_edit, 0, 20, &seg, &cell), 2);
    ck_assert_int_eq (cell, 0);
    mctest_assert_true (seg.last);
    ck_assert_int_eq (seg.width, 0);

    // cell -> offset in the middle row
    edit_wrap_get_segment_n (test_edit, 0, 1, &seg);
    ck_assert_int_eq (edit_wrap_cell_to_offset (test_edit, &seg, 0, &cell), 10);
    ck_assert_int_eq (cell, 0);
    ck_assert_int_eq (edit_wrap_cell_to_offset (test_edit, &seg, 9, &cell), 19);
    ck_assert_int_eq (cell, 9);
    // beyond a non-last row: the last char of the row
    ck_assert_int_eq (edit_wrap_cell_to_offset (test_edit, &seg, 15, &cell), 19);
    ck_assert_int_eq (cell, 9);

    // second line: beyond the last row: the line end
    edit_wrap_get_segment_n (test_edit, 21, 0, &seg);
    ck_assert_int_eq (edit_wrap_cell_to_offset (test_edit, &seg, 7, &cell), 23);
    ck_assert_int_eq (cell, 2);
    ck_assert_int_eq (edit_wrap_cell_to_offset (test_edit, &seg, 1, &cell), 22);
    ck_assert_int_eq (cell, 1);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* @Test */
START_TEST (test_next_line)
{
    edit_wrap_seg_t seg;
    off_t bol = 0;
    long line = 0;

    // given
    test_load_text ("12345678901\nab\ncd");

    // when
    edit_wrap_next_line (test_edit, &bol, &line, &seg);
    // then
    ck_assert_int_eq (bol, 12);
    ck_assert_int_eq (line, 1);
    ck_assert_int_eq (seg.start, 12);
    ck_assert_int_eq (seg.width, 2);

    // when
    edit_wrap_next_line (test_edit, &bol, &line, &seg);
    // then: last line without newline
    ck_assert_int_eq (bol, 15);
    ck_assert_int_eq (line, 2);
    mctest_assert_true (seg.last);
    ck_assert_int_eq (seg.width, 2);

    // when: beyond the end of file
    edit_wrap_next_line (test_edit, &bol, &line, &seg);
    // then
    ck_assert_int_eq (bol, 17);
    ck_assert_int_eq (line, 3);
    mctest_assert_true (seg.last);
    ck_assert_int_eq (seg.width, 0);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* @Test */
START_TEST (test_cursor_move_rows)
{
    // given: line of 20 chars (3 rows: 10, 10, line end) followed by a short line
    test_load_text ("12345678901234567890\nabc\n");
    edit_cursor_move (test_edit, 3);
    test_edit->prev_col = 3;

    // when: down by one row
    edit_move_down (test_edit, 1, FALSE);
    // then: still on the first line, second row, same screen column
    ck_assert_int_eq (test_edit->buffer.curs1, 13);
    ck_assert_int_eq (test_edit->buffer.curs_line, 0);
    ck_assert_int_eq (edit_wrap_cursor_cell (test_edit), 3);

    // when: down to the (empty) third row
    edit_move_down (test_edit, 1, FALSE);
    // then: cursor at the line end
    ck_assert_int_eq (test_edit->buffer.curs1, 20);
    ck_assert_int_eq (edit_wrap_cursor_cell (test_edit), 0);

    // when: down to the next line
    edit_move_down (test_edit, 1, FALSE);
    // then: remembered column 3 is used again; the line is shorter
    ck_assert_int_eq (test_edit->buffer.curs_line, 1);
    ck_assert_int_eq (test_edit->buffer.curs1, 24);  // 'abc' + line end
    ck_assert_int_eq (edit_wrap_cursor_cell (test_edit), 3);

    // when: up by two rows
    edit_move_up (test_edit, 2, FALSE);
    // then
    ck_assert_int_eq (test_edit->buffer.curs_line, 0);
    ck_assert_int_eq (test_edit->buffer.curs1, 13);

    // when: up beyond the beginning of file
    edit_move_up (test_edit, 5, FALSE);
    // then: first row, same column
    ck_assert_int_eq (test_edit->buffer.curs1, 3);

    // when: down beyond the end of file
    edit_move_down (test_edit, 100, FALSE);
    // then: last row of the file (the empty line after the last newline)
    ck_assert_int_eq (test_edit->buffer.curs_line, 2);
    ck_assert_int_eq (test_edit->buffer.curs1, 25);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* @Test */
START_TEST (test_curs_row_and_scroll)
{
    // given: line 0 has 3 rows, line 1 has 1 row, line 2 has 2 rows, line 3 (empty) 1 row
    test_load_text ("12345678901234567890\nabc\n123456789012\n");

    // cursor at the beginning of line 2
    edit_cursor_move (test_edit, 25);
    ck_assert_int_eq (test_edit->buffer.curs_line, 2);

    // when
    edit_update_curs_row (test_edit);
    // then: rows of lines 0 and 1 are above
    ck_assert_int_eq (test_edit->curs_row, 4);

    // when: 7 rows in the file, 8 rows on the screen: no scrolling possible
    edit_scroll_downward (test_edit, 2);
    // then
    ck_assert_int_eq (test_edit->start_line, 0);
    ck_assert_int_eq (test_edit->start_wrap_row, 0);
    ck_assert_int_eq (test_edit->curs_row, 4);

    // given: smaller window: 4 rows
    WIDGET (test_edit)->rect.lines = 4;

    // when: scroll down by 2 rows: the top line is still line 0, but its third row is on top
    edit_scroll_downward (test_edit, 2);
    // then
    ck_assert_int_eq (test_edit->start_line, 0);
    ck_assert_int_eq (test_edit->start_wrap_row, 2);
    ck_assert_int_eq (test_edit->curs_row, 2);

    // when: scroll down by 1 more row
    edit_scroll_downward (test_edit, 1);
    // then: top of page is line 1
    ck_assert_int_eq (test_edit->start_line, 1);
    ck_assert_int_eq (test_edit->start_display, 21);
    ck_assert_int_eq (test_edit->start_wrap_row, 0);
    ck_assert_int_eq (test_edit->curs_row, 1);

    // when: scroll up by 2 rows
    edit_scroll_upward (test_edit, 2);
    // then: second row of line 0 is on top
    ck_assert_int_eq (test_edit->start_line, 0);
    ck_assert_int_eq (test_edit->start_display, 0);
    ck_assert_int_eq (test_edit->start_wrap_row, 1);
    ck_assert_int_eq (test_edit->curs_row, 3);

    // when: scroll up beyond the beginning
    edit_scroll_upward (test_edit, 10);
    // then
    ck_assert_int_eq (test_edit->start_line, 0);
    ck_assert_int_eq (test_edit->start_wrap_row, 0);
    ck_assert_int_eq (test_edit->curs_row, 4);

    // when: scroll down as far as possible
    edit_scroll_downward (test_edit, 100);
    // then: the last row of the file is at the bottom of the 4-row window: rows 3..6 are shown
    ck_assert_int_eq (test_edit->start_line, 1);
    ck_assert_int_eq (test_edit->start_wrap_row, 0);
    ck_assert_int_eq (test_edit->curs_row, 1);

    // when: cursor moves to the first line, above the page
    edit_cursor_move (test_edit, -test_edit->buffer.curs1);
    edit_update_curs_row (test_edit);
    // then: negative row
    ck_assert_int_eq (test_edit->curs_row, -3);

    // when: the page is scrolled to make the cursor visible
    edit_scroll_screen_over_cursor (test_edit);
    // then
    ck_assert_int_eq (test_edit->start_line, 0);
    ck_assert_int_eq (test_edit->start_wrap_row, 0);
    ck_assert_int_eq (test_edit->curs_row, 0);

    // when: cursor goes to the end of file (below the page)
    edit_cursor_move (test_edit, test_edit->buffer.size);
    edit_scroll_screen_over_cursor (test_edit);
    // then: the cursor is on the last row of the window
    ck_assert_int_eq (test_edit->curs_row, 3);
    ck_assert_int_eq (test_edit->start_line, 1);
    ck_assert_int_eq (test_edit->start_wrap_row, 0);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* @Test */
START_TEST (test_move_display)
{
    // given: 4 rows window; line 0 has 3 rows, line 1 has 1 row, line 2 has 2 rows, line 3 has 1
    test_load_text ("12345678901234567890\nabc\n123456789012\n");
    WIDGET (test_edit)->rect.lines = 4;

    // when
    edit_move_display (test_edit, 1);
    // then
    ck_assert_int_eq (test_edit->start_line, 1);
    ck_assert_int_eq (test_edit->start_display, 21);
    ck_assert_int_eq (test_edit->start_wrap_row, 0);

    // when: line 3 on top would leave only one row on the screen: scrolled back
    edit_move_display (test_edit, 3);
    // then
    ck_assert_int_eq (test_edit->start_line, 1);
    ck_assert_int_eq (test_edit->start_wrap_row, 0);

    // when
    edit_move_display (test_edit, 0);
    // then
    ck_assert_int_eq (test_edit->start_line, 0);
    ck_assert_int_eq (test_edit->start_display, 0);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* @Test */
START_TEST (test_mouse_cursor)
{
    // given: line 0 has 3 rows, line 1 has 1 row
    test_load_text ("12345678901234567890\nabc\n");

    // when: click on row 1, column 4
    edit_wrap_update_cursor (test_edit, 4, 1);
    // then
    ck_assert_int_eq (test_edit->buffer.curs1, 14);
    ck_assert_int_eq (test_edit->prev_col, 4);

    // when: click behind the end of the short line
    edit_wrap_update_cursor (test_edit, 7, 3);
    // then: cursor at the line end
    ck_assert_int_eq (test_edit->buffer.curs_line, 1);
    ck_assert_int_eq (test_edit->buffer.curs1, 24);

    // when: click below the last row of the file
    edit_wrap_update_cursor (test_edit, 0, 7);
    // then: the last row
    ck_assert_int_eq (test_edit->buffer.curs_line, 2);
    ck_assert_int_eq (test_edit->buffer.curs1, 25);

    // when: the top of the page is the second row of line 0; click on row 0, column 2
    test_edit->start_wrap_row = 1;
    edit_wrap_update_cursor (test_edit, 2, 0);
    // then
    ck_assert_int_eq (test_edit->buffer.curs1, 12);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

static const struct test_scroll_text_area_ds
{
    int fullscreen;
    int line_state_width;
    int cols;
    int lines;
} test_scroll_text_area_ds[] = {
    { 1, 3, WRAP_WIDTH + 3, 8 },      // 0. line numbers
    { 0, 0, WRAP_WIDTH + 2, 8 + 2 },  // 1. framed window
    { 0, 3, WRAP_WIDTH + 5, 8 + 2 },  // 2. both
};

/* @Test(dataSource = "test_scroll_text_area_ds") */
START_PARAMETRIZED_TEST (test_scroll_text_area, test_scroll_text_area_ds)
{
    // given: the text area is WRAP_WIDTH x 8 in all cases; every line fits into one row
    // (edit_scroll_screen_over_cursor() used to compute the rows with a narrower width)
    test_load_text ("12345678\n12345678\n12345678\n12345678\n12345678\n12345678\n"
                    "12345678\n12345678\n12345678\n12345678\n12345678\n12345678\n");
    test_edit->fullscreen = data->fullscreen;
    edit_options.line_state_width = data->line_state_width;
    WIDGET (test_edit)->rect.cols = data->cols;
    WIDGET (test_edit)->rect.lines = data->lines;
    ck_assert_int_eq (edit_wrap_width (test_edit), WRAP_WIDTH);
    ck_assert_int_eq (edit_wrap_text_lines (test_edit), 8);

    // when: the cursor is on line 9, below the page
    edit_cursor_move (test_edit, 9 * 9);
    edit_scroll_screen_over_cursor (test_edit);

    // then: the page is scrolled by 2 rows and the cursor is on the last row
    ck_assert_int_eq (test_edit->start_line, 2);
    ck_assert_int_eq (test_edit->start_wrap_row, 0);
    ck_assert_int_eq (test_edit->curs_row, 7);

    edit_options.line_state_width = 0;
}
END_PARAMETRIZED_TEST

/* --------------------------------------------------------------------------------------------- */

/* @Test */
START_TEST (test_start_in_middle_of_line)
{
    // given: line 0 has 2 rows: [0123456789] [abcde]
    test_load_text ("0123456789abcde\ntop\nmore\n");

    // when: the top of the page points to the second row, but not to the beginning of a line
    test_edit->start_display = 10;
    test_edit->start_line = 0;
    test_edit->start_wrap_row = 0;
    edit_cursor_move (test_edit, 12);
    edit_update_curs_row (test_edit);

    // then: it is normalized to the beginning of the line + hidden rows
    ck_assert_int_eq (test_edit->start_display, 0);
    ck_assert_int_eq (test_edit->start_wrap_row, 1);
    ck_assert_int_eq (test_edit->curs_row, 0);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* @Test */
START_TEST (test_backspace_at_top_line)
{
    // given: line 1 ("top") is the top line of the page and the cursor is at its beginning
    test_load_text ("0123456789abcde\ntop\nmore\n");
    test_edit->start_display = 16;
    test_edit->start_line = 1;
    test_edit->start_wrap_row = 0;
    edit_cursor_move (test_edit, 16);
    edit_update_curs_row (test_edit);
    ck_assert_int_eq (test_edit->curs_row, 0);

    // when: BackSpace joins it with the line above: [0123456789] [abcdetop]
    edit_backspace (test_edit, FALSE);
    edit_scroll_screen_over_cursor (test_edit);

    // then: the joined text stays on top, the page starts at the second row of line 0
    ck_assert_int_eq (test_edit->buffer.curs1, 15);
    ck_assert_int_eq (test_edit->start_line, 0);
    ck_assert_int_eq (test_edit->start_display, 0);
    ck_assert_int_eq (test_edit->start_wrap_row, 1);
    ck_assert_int_eq (test_edit->curs_row, 0);
    ck_assert_int_eq (edit_wrap_cursor_cell (test_edit), 5);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* @Test */
START_TEST (test_cursor_beyond_eol_stays_in_row)
{
    int i;

    // given
    edit_options.cursor_beyond_eol = TRUE;
    test_load_text ("abc\n");
    edit_cursor_move (test_edit, 3);

    // when: Right far beyond the end of the line
    for (i = 0; i < 20; i++)
        edit_execute_cmd (test_edit, CK_Right, -1);

    // then: the cursor stops at the last column of the row
    ck_assert_int_eq (edit_wrap_cursor_cell (test_edit) + test_edit->over_col, WRAP_WIDTH - 1);

    // when: a character is typed
    edit_execute_cmd (test_edit, -1, 'Z');

    // then: it is inserted where the cursor was shown
    ck_assert_int_eq (test_edit->buffer.size, 11);
    ck_assert_int_eq (edit_buffer_get_byte (&test_edit->buffer, WRAP_WIDTH - 1), 'Z');

    edit_options.cursor_beyond_eol = FALSE;
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* @Test */
START_TEST (test_line_moves_keep_utf8_boundary)
{
    // given: UTF-8 text, the first line starts with a wide char (cells 0-1)
    mc_global.utf8_display = TRUE;
    test_edit->utf8 = TRUE;
    test_load_text ("\xe4\xb8\xad\xe6\x96\x87x\nabc\nabc\n");  // "中文x\nabc\nabc\n"

    // when: cursor on 'b' of line 1 (screen column 1), Ctrl-Home
    edit_cursor_move (test_edit, 9);
    test_edit->prev_col = 1;
    edit_execute_cmd (test_edit, CK_Top, -1);

    // then: the cursor is on the wide char, not inside its UTF-8 sequence
    ck_assert_int_eq (test_edit->buffer.curs1, 0);

    // when: from 'b' of line 2, go to line 0 (goto line)
    edit_cursor_move (test_edit, 13 - test_edit->buffer.curs1);
    test_edit->prev_col = 1;
    edit_move_to_line (test_edit, 0);

    // then
    ck_assert_int_eq (test_edit->buffer.curs1, 0);

    mc_global.utf8_display = FALSE;
    test_edit->utf8 = FALSE;
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* @Test */
START_TEST (test_far_cursor_row)
{
    int i;

    // given: 100 short lines, the page starts at line 50
    for (i = 0; i < 100; i++)
        test_load_text ("x\n");
    edit_cursor_move (test_edit, -test_edit->buffer.curs1);
    test_edit->start_line = 50;
    test_edit->start_display = 100;
    test_edit->start_wrap_row = 0;

    // when: the cursor is far above the page
    edit_update_curs_row (test_edit);
    // then: the row is off the screen, above
    ck_assert_int_lt (test_edit->curs_row, 0);

    // when: the cursor is far below the page
    edit_cursor_move (test_edit, 198);
    edit_update_curs_row (test_edit);
    // then: the row is off the screen, below
    ck_assert_int_ge (test_edit->curs_row, 8);

    // when: the page is scrolled to the cursor
    edit_scroll_screen_over_cursor (test_edit);
    // then: the cursor is on the last row
    ck_assert_int_eq (test_edit->buffer.curs_line, 99);
    ck_assert_int_eq (test_edit->curs_row, 7);
    ck_assert_int_eq (test_edit->start_line, 99 - 7);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */
/* Randomized property test                                                                      */
/* --------------------------------------------------------------------------------------------- */

static unsigned long prop_seed;

static unsigned long
prop_rand (void)
{
    prop_seed = prop_seed * 6364136223846793005UL + 1442695040888963407UL;
    return (prop_seed >> 33);
}

/* --------------------------------------------------------------------------------------------- */

static long
prop_lines_before (off_t pos)
{
    long n = 0;
    off_t i;

    for (i = 0; i < pos; i++)
        if (edit_buffer_get_byte (&test_edit->buffer, i) == '\n')
            n++;
    return n;
}

/* --------------------------------------------------------------------------------------------- */
/** global screen row of the first row of line @line */

static long
prop_line_first_row (long line)
{
    off_t b = 0;
    long l, rows = 0;

    for (l = 0; l < line; l++)
    {
        rows += edit_wrap_line_rows (test_edit, b);
        b = edit_buffer_get_forward_offset (&test_edit->buffer, b, 1, 0);
    }
    return rows;
}

/* --------------------------------------------------------------------------------------------- */
/** global screen row of the byte offset @pos */

static long
prop_global_row (off_t pos)
{
    edit_wrap_seg_t seg;
    const off_t bol = edit_buffer_get_bol (&test_edit->buffer, pos);

    return prop_line_first_row (prop_lines_before (pos))
        + edit_wrap_locate (test_edit, bol, pos, &seg, NULL);
}

/* --------------------------------------------------------------------------------------------- */

static long
prop_total_rows (void)
{
    return prop_line_first_row (test_edit->buffer.lines)
        + edit_wrap_line_rows (test_edit,
                               edit_buffer_get_bol (&test_edit->buffer, test_edit->buffer.size));
}

/* --------------------------------------------------------------------------------------------- */

static long
prop_top_row (void)
{
    return prop_line_first_row (test_edit->start_line) + test_edit->start_wrap_row;
}

/* --------------------------------------------------------------------------------------------- */
/** byte offsets where characters start (with the UTF-8 decoding the editor uses) */

static gboolean
prop_is_char_start (off_t pos)
{
    const int c = edit_buffer_get_byte (&test_edit->buffer, pos);

    return !test_edit->utf8 || c < 0x80 || c >= 0xC0;
}

/* --------------------------------------------------------------------------------------------- */

static void
prop_set_cursor (off_t pos)
{
    edit_cursor_move (test_edit, pos - test_edit->buffer.curs1);
    test_edit->prev_col = edit_wrap_cursor_cell (test_edit);
    test_edit->prev_col_wrap_width = edit_wrap_width (test_edit);
    test_edit->over_col = 0;
}

/* --------------------------------------------------------------------------------------------- */

static void
prop_set_top_row (long row)
{
    off_t b = 0;
    long l = 0;

    while (TRUE)
    {
        const long n = edit_wrap_line_rows (test_edit, b);

        if (row < n || l >= test_edit->buffer.lines)
        {
            test_edit->start_line = l;
            test_edit->start_display = b;
            test_edit->start_wrap_row = MIN (row, n - 1);
            return;
        }
        row -= n;
        b = edit_buffer_get_forward_offset (&test_edit->buffer, b, 1, 0);
        l++;
    }
}

/* --------------------------------------------------------------------------------------------- */

/* @Test */
START_TEST (test_random_properties)
{
    static const char *tokens[] = {
        "a",
        "b",
        "c",
        " ",
        " ",
        "\t",
        "\x01",
        "\n",
        "\n",
        "xyz",
        "\xc5\xbe",          // z with caron: 2 bytes, 1 column
        "\xe4\xb8\xad",      // CJK: 3 bytes, 2 columns
        "\xf0\x9f\x98\x80",  // emoji: 4 bytes, 2 columns
    };
    int iter;

    prop_seed = 20261108;

    for (iter = 0; iter < 250; iter++)
    {
        GString *text;
        const gboolean utf8 = (iter % 3) != 0;
        long W, H, ntok, i, total;
        off_t bol, pos;

        // given: a random document, width and height
        text = g_string_new ("");
        ntok = prop_rand () % 120;
        for (i = 0; i < ntok; i++)
            g_string_append (text, tokens[prop_rand () % G_N_ELEMENTS (tokens)]);

        edit_clean (test_edit);
        {
            WRect r;

            W = 1 + prop_rand () % 24;
            H = 1 + prop_rand () % 8;
            rect_init (&r, 0, 0, H, W);
            test_edit = edit_init (test_edit, &r, NULL);
            WIDGET (test_edit)->rect = r;
            test_edit->fullscreen = 1;  // edit_clean() resets it
        }
        mc_global.utf8_display = utf8;
        test_edit->utf8 = utf8;
        test_load_text (text->str);
        g_string_free (text, TRUE);
        ck_assert_int_eq (edit_wrap_width (test_edit), W);

        // then: segments cover each line contiguously, every char start roundtrips
        bol = 0;
        while (TRUE)
        {
            edit_wrap_seg_t seg;
            const off_t eol = edit_buffer_get_eol (&test_edit->buffer, bol);
            off_t expect_start = bol;
            long nseg = 0;

            edit_wrap_get_segment (test_edit, bol, 0, W, &seg);
            while (TRUE)
            {
                long last_cell = -1;

                ck_assert_int_eq (seg.start, expect_start);
                ck_assert (seg.width >= 0 && seg.width <= W);
                ck_assert (seg.last || seg.next > seg.start);
                for (pos = seg.start; pos < seg.next || (seg.last && pos == eol); pos++)
                {
                    edit_wrap_seg_t s2;
                    long cell, cell2;

                    if (pos < eol && !prop_is_char_start (pos))
                        continue;
                    ck_assert_int_eq (edit_wrap_locate (test_edit, bol, pos, &s2, &cell), nseg);
                    ck_assert_int_eq (s2.start, seg.start);
                    ck_assert (cell > last_cell);
                    last_cell = cell;
                    ck_assert_int_eq (edit_wrap_cell_to_offset (test_edit, &seg, cell, &cell2),
                                      pos);
                    ck_assert_int_eq (cell2, cell);
                    if (pos == eol)
                        ck_assert_int_eq (cell, seg.width);
                }
                nseg++;
                expect_start = seg.next;
                if (!edit_wrap_next_segment (test_edit, W, &seg))
                    break;
            }
            ck_assert_int_eq (expect_start, eol);
            ck_assert_int_eq (edit_wrap_line_rows (test_edit, bol), nseg);
            if (eol >= test_edit->buffer.size)
                break;
            bol = eol + 1;
        }

        total = prop_total_rows ();

        // then: Down/Up move by exactly one global row, the cell does not exceed prev_col
        for (i = 0; i < 6; i++)
        {
            long g, cell, k;

            do
                pos = prop_rand () % (test_edit->buffer.size + 1);
            while (pos < test_edit->buffer.size && !prop_is_char_start (pos));
            prop_set_cursor (pos);
            test_edit->prev_col = prop_rand () % (W + 2);
            g = prop_global_row (pos);

            for (k = 0; k < 3; k++)
            {
                const gboolean down = (prop_rand () % 2) == 0;
                const long want = down ? MIN (g + 1, total - 1) : MAX (g - 1, 0);

                if (down)
                    edit_move_down (test_edit, 1, FALSE);
                else
                    edit_move_up (test_edit, 1, FALSE);
                if (want != g)
                {
                    ck_assert_int_eq (prop_global_row (test_edit->buffer.curs1), want);
                    cell = edit_wrap_cursor_cell (test_edit);
                    ck_assert (cell <= test_edit->prev_col || cell == 0);
                    ck_assert (prop_is_char_start (test_edit->buffer.curs1)
                               || test_edit->buffer.curs1 >= test_edit->buffer.size);
                }
                g = prop_global_row (test_edit->buffer.curs1);
            }
        }

        // then: scrolling moves the top of the page by the exact (clamped) number of rows
        for (i = 0; i < 4; i++)
        {
            const long max_top = MAX (total - H, 0);
            const long top0 = prop_rand () % (max_top + 1);
            const long k = prop_rand () % (H + 3);

            prop_set_top_row (top0);
            ck_assert_int_eq (prop_top_row (), top0);
            edit_scroll_downward (test_edit, k);
            ck_assert_int_eq (prop_top_row (), top0 + MAX (0, MIN (k, total - top0 - H)));
            prop_set_top_row (top0);
            edit_scroll_upward (test_edit, k);
            ck_assert_int_eq (prop_top_row (), MAX (top0 - k, 0));
        }

        // then: the cursor is on the screen after edit_scroll_screen_over_cursor(); the page
        // does not move if the cursor already was on the screen
        for (i = 0; i < 4; i++)
        {
            const long top0 = prop_rand () % total;
            long g, top;

            do
                pos = prop_rand () % (test_edit->buffer.size + 1);
            while (pos < test_edit->buffer.size && !prop_is_char_start (pos));
            prop_set_top_row (top0);
            prop_set_cursor (pos);
            g = prop_global_row (pos);
            edit_scroll_screen_over_cursor (test_edit);
            top = prop_top_row ();
            ck_assert_int_eq (test_edit->curs_row, g - top);
            ck_assert (test_edit->curs_row >= 0 && test_edit->curs_row < H);
            if (g >= top0 && g < top0 + H)
                ck_assert_int_eq (top, top0);
            ck_assert_int_eq (test_edit->start_line, prop_lines_before (test_edit->start_display));
            ck_assert_int_eq (edit_buffer_get_bol (&test_edit->buffer, test_edit->start_display),
                              test_edit->start_display);
        }

        // then: a top of the page in the middle of a line is normalized to the row containing it
        if (test_edit->buffer.size > 0)
        {
            const off_t mid = prop_rand () % test_edit->buffer.size;

            if (prop_is_char_start (mid))
            {
                test_edit->start_display = mid;
                test_edit->start_line = prop_lines_before (mid);
                test_edit->start_wrap_row = 0;
                prop_set_cursor (mid);
                edit_update_curs_row (test_edit);
                ck_assert_int_eq (prop_top_row (), prop_global_row (mid));
                ck_assert_int_eq (test_edit->curs_row, 0);
            }
        }
    }

    mc_global.utf8_display = FALSE;
    test_edit->utf8 = FALSE;
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc_core;

    tc_core = tcase_create ("Core");

    tcase_add_checked_fixture (tc_core, setup, teardown);

    // Add new tests here: ***************
    tcase_add_test (tc_core, test_wrap_width);
    mctest_add_parameterized_test (tc_core, test_line_rows, test_line_rows_ds);
    tcase_add_test (tc_core, test_segments);
    tcase_add_test (tc_core, test_tab_at_row_boundary);
    tcase_add_test (tc_core, test_wide_chars);
    tcase_add_test (tc_core, test_locate_and_cells);
    tcase_add_test (tc_core, test_next_line);
    tcase_add_test (tc_core, test_cursor_move_rows);
    tcase_add_test (tc_core, test_curs_row_and_scroll);
    tcase_add_test (tc_core, test_move_display);
    tcase_add_test (tc_core, test_mouse_cursor);
    mctest_add_parameterized_test (tc_core, test_scroll_text_area, test_scroll_text_area_ds);
    tcase_add_test (tc_core, test_start_in_middle_of_line);
    tcase_add_test (tc_core, test_backspace_at_top_line);
    tcase_add_test (tc_core, test_cursor_beyond_eol_stays_in_row);
    tcase_add_test (tc_core, test_line_moves_keep_utf8_boundary);
    tcase_add_test (tc_core, test_far_cursor_row);
    tcase_add_test (tc_core, test_random_properties);
    // ***********************************

    return mctest_run_all (tc_core);
}

/* --------------------------------------------------------------------------------------------- */
