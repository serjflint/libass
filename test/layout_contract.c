/*
 * Font-independent half of the layout contract: limits, statuses, outline
 * encoding, and determinism.
 *
 * These need no system font, so unlike the shaping suite they run everywhere,
 * including images that ship fontconfig without any fonts.
 */

/* Every check here is an assert(); a -DNDEBUG build must not turn this
 * program into a silent no-op. */
#undef NDEBUG
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../libass/ass.h"
#include "../libass/ass_layout.h"

#ifndef ASS_TEST_FONT
#error ASS_TEST_FONT must name the bundled test font
#endif

static const char fixture[] =
    "[Script Info]\n"
    "ScriptType: v4.00+\n"
    "PlayResX: 320\n"
    "PlayResY: 240\n"
    "[V4+ Styles]\n"
    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
    "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, "
    "ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
    "MarginR, MarginV, Encoding\n"
    "Style: Default,Aileron,28,&H00FFFFFF,&H0000FFFF,&H00000000,&H00000000,"
    "0,0,0,0,100,100,0,0,1,1,1,2,10,10,10,1\n"
    "[Events]\n"
    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
    "Dialogue: 0,0:00:00.00,0:00:04.00,Default,,0,0,0,,layout ab\n"
    "Dialogue: 0,0:00:00.00,0:00:04.00,Default,,0,0,0,,{\\pos(20,20)}second\n";

static ASS_Renderer *new_renderer(ASS_Library *library)
{
    ASS_Renderer *renderer = ass_renderer_init(library);
    assert(renderer);
    ass_set_frame_size(renderer, 320, 240);
    ass_set_storage_size(renderer, 320, 240);
    ass_set_fonts(renderer, ASS_TEST_FONT, "Aileron",
                  ASS_FONTPROVIDER_NONE, NULL, 1);
    return renderer;
}

static ASS_LayoutRequest unbounded_limits(void)
{
    return (ASS_LayoutRequest) {
        .struct_size = sizeof(ASS_LayoutRequest),
        .flags = ASS_LAYOUT_INCLUDE_OUTLINES,
        .max_events = SIZE_MAX,
        .max_text_bytes = SIZE_MAX,
        .max_units = SIZE_MAX,
        .max_outlines = SIZE_MAX,
        .max_outline_points = SIZE_MAX,
        .max_bitmap_pixels = SIZE_MAX,
    };
}

static const ASS_RenderResult *render(ASS_Renderer *renderer, ASS_Track *track,
                                      long long now,
                                      const ASS_LayoutRequest *limits)
{
    ASS_RenderRequest request = {
        .struct_size = sizeof(request),
        .track = track,
        .now_ms = now,
        .layout = limits,
    };
    const ASS_RenderResult *result = ass_render_frame2(renderer, &request);
    assert(result);
    assert(result->status == ASS_RENDER_OK);
    return result;
}

/* Aggregate what an unbounded run actually produced, so the limit tests can
 * ask for exactly that much rather than guessing a number that drifts with
 * the font. */
struct usage {
    size_t events, text_bytes, units, outlines, outline_points;
};

/*
 * Walk the documented segment encoding: a segment has order 1, 2, or 3, and
 * consumes that many points; the next segment reuses the following point as
 * its start. Nothing else in the suite reads `segments`, so a consumer
 * following the header could run off the end and every test would stay green.
 */
static void walk_outline(const ASS_LayoutOutline *outline)
{
    /* A unit that draws nothing, such as a space, still gets a record. */
    if (outline->point_count == 0) {
        assert(outline->segment_count == 0);
        return;
    }
    assert(outline->segment_count > 0);
    assert(outline->points);
    assert(outline->segments);

    size_t consumed = 0;
    int contours = 0;
    for (size_t i = 0; i < outline->segment_count; i++) {
        int order = outline->segments[i] & ASS_LAYOUT_OUTLINE_COUNT_MASK;
        assert(order == ASS_LAYOUT_OUTLINE_LINE_SEGMENT ||
               order == ASS_LAYOUT_OUTLINE_QUADRATIC_SPLINE ||
               order == ASS_LAYOUT_OUTLINE_CUBIC_SPLINE);
        consumed += (size_t) order;
        assert(consumed <= outline->point_count);
        if (outline->segments[i] & ASS_LAYOUT_OUTLINE_CONTOUR_END)
            contours++;
    }
    /* Every point belongs to exactly one segment, and no contour is left
     * unterminated -- both are what a caller relies on to close the path. */
    assert(consumed == outline->point_count);
    assert(contours > 0);
}

static struct usage measure(const ASS_Layout *layout)
{
    struct usage u = {0};
    for (const ASS_LayoutEvent *event = layout->events; event;
         event = event->next) {
        u.events++;
        u.text_bytes += event->text_length;
        for (const ASS_LayoutUnit *unit = event->units; unit;
             unit = unit->next) {
            u.units++;
            for (const ASS_LayoutOutline *o = unit->fill; o; o = o->next) {
                u.outlines++;
                u.outline_points += o->point_count;
                walk_outline(o);
            }
        }
    }
    return u;
}

int main(void)
{
    ASS_Library *library = ass_library_init();
    assert(library);
    ASS_Track *track = ass_read_memory(library, (char *) fixture,
                                       sizeof(fixture) - 1, NULL);
    assert(track);
    ASS_Renderer *renderer = new_renderer(library);

    /* Collecting layout must not disturb the images. */
    uint64_t plain_images = 0;
    ASS_Image *plain = ass_render_frame(renderer, track, 1000, NULL);
    assert(plain);
    for (ASS_Image *i = plain; i; i = i->next)
        plain_images++;

    ASS_LayoutRequest limits = unbounded_limits();
    const ASS_RenderResult *result = render(renderer, track, 1000, &limits);
    assert(result->layout_status == ASS_LAYOUT_OK);
    assert(result->layout);
    uint64_t layout_images = 0;
    for (ASS_Image *i = result->images; i; i = i->next)
        layout_images++;
    assert(layout_images == plain_images);

    struct usage used = measure(result->layout);
    assert(used.events == 2);
    assert(used.units > 0);
    assert(used.outlines > 0);
    assert(used.outline_points > 0);

    /* A budget that fits exactly must be accepted. Only the rejecting side of
     * each limit was covered before, so an off-by-one that rejects a
     * sufficient budget -- making bounded mode unusable -- went unnoticed. */
    ASS_LayoutRequest exact = {
        .struct_size = sizeof(ASS_LayoutRequest),
        .flags = ASS_LAYOUT_INCLUDE_OUTLINES,
        .max_events = used.events,
        .max_text_bytes = used.text_bytes,
        .max_units = used.units,
        .max_outlines = used.outlines,
        .max_outline_points = used.outline_points,
        .max_bitmap_pixels = SIZE_MAX,
    };
    result = render(renderer, track, 1000, &exact);
    assert(result->layout_status == ASS_LAYOUT_OK);
    assert(result->layout);
    struct usage again = measure(result->layout);
    assert(again.events == used.events);
    assert(again.units == used.units);
    assert(again.outlines == used.outlines);
    assert(again.outline_points == used.outline_points);

    /* One below each limit must be refused, and must not damage the images. */
    const size_t *fields[] = {
        &exact.max_events, &exact.max_text_bytes, &exact.max_units,
        &exact.max_outlines, &exact.max_outline_points,
    };
    for (size_t f = 0; f < sizeof(fields) / sizeof(*fields); f++) {
        ASS_LayoutRequest tight = exact;
        size_t *field = (size_t *) ((char *) &tight +
                                    ((const char *) fields[f] -
                                     (const char *) &exact));
        if (*field == 0)
            continue;
        (*field)--;
        result = render(renderer, track, 1000, &tight);
        assert(result->layout_status == ASS_LAYOUT_LIMIT_EXCEEDED);
        assert(!result->layout);
        uint64_t still = 0;
        for (ASS_Image *i = result->images; i; i = i->next)
            still++;
        assert(still == plain_images);
    }

    /* Zero-initialised bounded limits are rejected, not treated as unlimited. */
    ASS_LayoutRequest zeroed = {.struct_size = sizeof(ASS_LayoutRequest)};
    result = render(renderer, track, 1000, &zeroed);
    assert(result->layout_status == ASS_LAYOUT_INVALID_REQUEST);
    assert(!result->layout);

    /* Unbounded must be asked for explicitly and then really is unbounded. */
    ASS_LayoutRequest trusted = {
        .struct_size = sizeof(ASS_LayoutRequest),
        .flags = ASS_LAYOUT_UNBOUNDED | ASS_LAYOUT_INCLUDE_OUTLINES,
    };
    result = render(renderer, track, 1000, &trusted);
    assert(result->layout_status == ASS_LAYOUT_OK);
    assert(result->layout);
    struct usage unbounded_use = measure(result->layout);
    assert(unbounded_use.units == used.units);

    /* Omitting outlines drops the outline data but keeps the units. */
    ASS_LayoutRequest no_outlines = exact;
    no_outlines.flags = 0;
    no_outlines.max_outlines = 0;
    no_outlines.max_outline_points = 0;
    result = render(renderer, track, 1000, &no_outlines);
    assert(result->layout_status == ASS_LAYOUT_OK);
    assert(result->layout);
    size_t bare_units = 0;
    for (const ASS_LayoutEvent *e = result->layout->events; e; e = e->next)
        for (const ASS_LayoutUnit *u = e->units; u; u = u->next) {
            assert(!u->fill);
            bare_units++;
        }
    assert(bare_units == used.units);

    /* An unknown flag is refused rather than ignored. */
    ASS_LayoutRequest bogus = exact;
    bogus.flags |= 1u << 20;
    result = render(renderer, track, 1000, &bogus);
    assert(result->layout_status == ASS_LAYOUT_INVALID_REQUEST);
    assert(!result->layout);

    /* A short request cannot reach the limit fields, so it is refused too. */
    ASS_LayoutRequest truncated = exact;
    truncated.struct_size = offsetof(ASS_LayoutRequest, max_bitmap_pixels);
    result = render(renderer, track, 1000, &truncated);
    assert(result->layout_status == ASS_LAYOUT_INVALID_REQUEST);
    assert(!result->layout);

    ass_free_track(track);
    ass_renderer_done(renderer);
    ass_library_done(library);
    return 0;
}
