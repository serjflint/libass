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
    ASS_LAYOUT_UNBOUNDED = 1u << 0,
};

/*
 * Aggregate limits across all active events in one returned layout.
 *
 * Unlike ASS_RenderRequest, a zeroed request is NOT a usable default: bounded
 * collection requires every limit below to be non-zero, and a zero yields
 * ASS_LAYOUT_INVALID_REQUEST. Unbounded collection must be asked for with
 * ASS_LAYOUT_UNBOUNDED, so it can never be selected by accident; it is meant
 * for trusted authoring input, not untrusted media.
 */
struct ass_layout_request {
    size_t struct_size;
    unsigned flags;
    size_t max_events;
    size_t max_text_bytes;
    size_t max_units;
    size_t max_bitmap_pixels;
};

typedef struct ass_layout_rect {
    double x;
    double y;
    double w;
    double h;
} ASS_LayoutRect;

/*
 * A renderer positioning unit in the event's parsed logical text domain.
 * text_start/text_end are half-open UTF-8 byte offsets. logical_bounds is the
 * pre-transform typographic box after wrapping and collision placement.
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
    /*
     * This event's index in track->events, valid for the track as it stands
     * when the call returns. It is the only way to tell apart two events
     * carrying identical text and timing, which layered signs and karaoke
     * routinely produce.
     *
     * It identifies a slot, not a subtitle line, and its lifetime is the
     * result's: read it before the next rendering call on this renderer, and
     * before anything mutates the event array. ass_flush_events(),
     * ass_process_chunk() and any compaction the caller performs itself all
     * invalidate it. Automatic pruning (ass_configure_prune) does too, but
     * never for the result in hand: a render prunes before it lays out, so the
     * index it publishes already describes the pruned array.
     */
    int event_index;
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
 * Every record above publishes struct_size as the END OFFSET of the last field
 * the runtime knows, never sizeof -- see the ASS_RenderResult documentation in
 * ass.h for why, and for the gate a caller uses. The two differ whenever a
 * record carries tail padding, so `if (rec->struct_size >= sizeof(*rec))` is
 * the wrong test and will not pass on a runtime that matches your header.
 */

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
