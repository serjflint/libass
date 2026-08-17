/*
 * Copyright (C) 2024 libass contributors
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef LIBASS_LAYOUT_H
#define LIBASS_LAYOUT_H

#include "ass.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ASS_LAYOUT_INCLUDE_OUTLINES = 1u << 0,
    ASS_LAYOUT_UNBOUNDED        = 1u << 1,
};

/* Aggregate limits across all active events in one returned layout. */
struct ass_layout_request {
    size_t struct_size;
    unsigned flags;
    size_t max_events;
    size_t max_text_bytes;
    size_t max_units;
    size_t max_outlines;
    size_t max_outline_points;
    size_t max_bitmap_pixels;
};

typedef struct ass_layout_rect {
    double x;
    double y;
    double w;
    double h;
} ASS_LayoutRect;

enum {
    ASS_LAYOUT_OUTLINE_LINE_SEGMENT     = 1,
    ASS_LAYOUT_OUTLINE_QUADRATIC_SPLINE = 2,
    ASS_LAYOUT_OUTLINE_CUBIC_SPLINE     = 3,
    ASS_LAYOUT_OUTLINE_COUNT_MASK       = 3,
    ASS_LAYOUT_OUTLINE_CONTOUR_END      = 4,
};

/*
 * Segments have order 1 (line), 2 (quadratic), or 3 (cubic). Each segment
 * consumes its order in points and uses the first point of the next segment
 * as its endpoint. A contour-ending segment closes onto its contour's first
 * point.
 */
typedef struct ass_layout_outline {
    size_t struct_size;
    size_t point_count;
    size_t segment_count;
    ASS_DVector *points;
    char *segments;
    struct ass_layout_outline *next;
} ASS_LayoutOutline;

/*
 * A renderer positioning unit in the event's parsed logical text domain.
 * text_start/text_end are half-open UTF-8 byte offsets. logical_bounds is the
 * pre-transform typographic box after wrapping and collision placement. fill
 * contains transformed glyph-fill outlines before clipping, blur, border,
 * shadow, karaoke masks, and alpha composition.
 */
typedef struct ass_layout_unit {
    size_t struct_size;
    size_t text_start;
    size_t text_end;
    int line;
    ASS_DVector pos;
    ASS_DVector advance;
    double asc;
    double desc;
    ASS_LayoutRect logical_bounds;
    ASS_LayoutOutline *fill;
    struct ass_layout_unit *next;
} ASS_LayoutUnit;

typedef struct ass_layout_event {
    size_t struct_size;
    long long start_ms;
    long long duration_ms;
    int has_duration;
    char *text;  // parsed post-ASS-parse, pre-bidi UTF-8
    size_t text_length;
    ASS_LayoutUnit *units;
    ASS_LayoutRect bitmap_bounds;
    int has_bitmap_bounds;
    struct ass_layout_event *next;
} ASS_LayoutEvent;

typedef enum ass_layout_unit_mode {
    ASS_LAYOUT_UNIT_SIMPLE_SCALAR = 0,
    ASS_LAYOUT_UNIT_SHAPING_CLUSTER,
} ASS_LayoutUnitMode;

struct ass_layout {
    size_t struct_size;
    ASS_LayoutUnitMode unit_mode;
    ASS_LayoutEvent *events;
};

/*
 * Layout is semantic geometry from the same invocation as result.images, not
 * a stable prediction across libass versions, fonts, or configuration. The
 * layout and images are owned by ASS_Renderer and remain valid until the next
 * rendering call on that renderer or ass_renderer_done().
 */

#ifdef __cplusplus
}
#endif

#endif /* LIBASS_LAYOUT_H */
