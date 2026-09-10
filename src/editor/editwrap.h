/** \file editwrap.h
 *  \brief Header: editor soft (display-only) line wrapping
 */

#ifndef MC__EDIT_WRAP_H
#define MC__EDIT_WRAP_H

#include "lib/global.h"

#include "edit.h"

/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

/**
 * Description of one screen row (segment) of a logical line in soft wrap mode.
 */
typedef struct
{
    off_t start;    // offset of the first char of the segment
    off_t next;     // offset of the first char of the next segment or of the line end
    long col;       // logical (unwrapped) column of the first char of the segment
    long next_col;  // logical column of the first char of the next segment
    long width;     // number of screen columns occupied by the segment content
    gboolean last;  // TRUE if the segment is the last one of the line (ends with '\n' or EOF)
} edit_wrap_seg_t;

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

gboolean edit_wrap_is_enabled (const WEdit *edit);
long edit_wrap_width (const WEdit *edit);

void edit_wrap_get_segment (const WEdit *edit, off_t start, long col, long width,
                            edit_wrap_seg_t *seg);
gboolean edit_wrap_next_segment (const WEdit *edit, long width, edit_wrap_seg_t *seg);
long edit_wrap_line_rows (const WEdit *edit, off_t bol);
long edit_wrap_get_segment_n (const WEdit *edit, off_t bol, long n, edit_wrap_seg_t *seg);
long edit_wrap_locate (const WEdit *edit, off_t bol, off_t pos, edit_wrap_seg_t *seg, long *cell);
off_t edit_wrap_cell_to_offset (const WEdit *edit, const edit_wrap_seg_t *seg, long cell,
                                long *actual_cell);
void edit_wrap_next_line (const WEdit *edit, off_t *bol, long *line, edit_wrap_seg_t *seg);
long edit_wrap_cursor_cell (const WEdit *edit);

/*** inline functions ****************************************************************************/

#endif
