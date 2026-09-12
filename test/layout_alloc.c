/*
 * Atomic failure of layout collection, swept one allocation at a time.
 *
 * Separate from layout_contract because it is the one test that reaches an
 * internal symbol, ass_test_set_alloc_countdown. That symbol is not in
 * libass.sym, so this program links libass_internal.la; layout_contract stays
 * on the public library and must resolve against libass.sym alone.
 */

/* Every check here is an assert(); a -DNDEBUG build must not turn this
 * program into a silent no-op. */
#undef NDEBUG
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "../libass/ass.h"
#include "../libass/ass_layout.h"
#include "../libass/ass_render.h"   /* ass_test_set_alloc_countdown */

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

int main(void)
{
    ASS_Library *library = ass_library_init();
    assert(library);
    ASS_Track *track = ass_read_memory(library, (char *) fixture,
                                       sizeof(fixture) - 1, NULL);
    assert(track);
    ASS_Renderer *renderer = ass_renderer_init(library);
    assert(renderer);
    ass_set_frame_size(renderer, 320, 240);
    ass_set_storage_size(renderer, 320, 240);
    ass_set_fonts(renderer, ASS_TEST_FONT, "Aileron",
                  ASS_FONTPROVIDER_NONE, NULL, 1);

    ASS_LayoutRequest limits = {
        .struct_size = sizeof(ASS_LayoutRequest),
        .flags = ASS_LAYOUT_INCLUDE_OUTLINES,
        .max_events = SIZE_MAX,
        .max_text_bytes = SIZE_MAX,
        .max_units = SIZE_MAX,
        .max_outlines = SIZE_MAX,
        .max_outline_points = SIZE_MAX,
        .max_bitmap_pixels = SIZE_MAX,
    };

    /* "Untouched" has to mean the images, not how many there are: shifting
     * every dst_x and rewriting every colour leaves the count alone. Snapshot
     * the scalars from a clean render and compare those. Not the bitmap bytes
     * -- those buffers are renderer-owned and recycled, so a pointer or a
     * memcmp against an earlier render is not a safe comparison. */
    struct img_snap { int w, h, stride, dst_x, dst_y; uint32_t color; };
    struct img_snap clean[64];
    size_t clean_images = 0;
    for (ASS_Image *i = render(renderer, track, 1000, &limits)->images; i; i = i->next) {
        assert(clean_images < sizeof(clean) / sizeof(clean[0]));
        clean[clean_images++] = (struct img_snap) { i->w, i->h, i->stride,
                                                    i->dst_x, i->dst_y, i->color };
    }
    assert(clean_images > 0);

    /*
     * Do NOT stop at the first success. Breaking there cannot tell "ran out of
     * allocations" from "reached a site that fails quietly and reports OK",
     * which is the atomicity break this sweep exists to find -- and the earlier
     * that site sits, the less the sweep covers while staying green. So run the
     * whole cap and require the successes to be a suffix.
     */
    size_t failures = 0, first_ok = 0;
    for (size_t nth = 1; nth <= 200; nth++) {
        ass_test_set_alloc_countdown(renderer, nth);
        const ASS_RenderResult *result = render(renderer, track, 1000, &limits);
        ass_test_set_alloc_countdown(renderer, 0);

        if (result->layout_status == ASS_LAYOUT_OK) {
            assert(result->layout);
            if (!first_ok)
                first_ok = nth;
            continue;
        }
        /* A failure after a success means an allocation before it failed
         * without saying so. */
        assert(!first_ok);
        assert(result->layout_status == ASS_LAYOUT_ALLOCATION_FAILED);
        /* No partial layout survives a failure. */
        assert(!result->layout);
        /* The images are untouched -- the whole point of the guarantee. */
        size_t k = 0;
        for (ASS_Image *i = result->images; i; i = i->next, k++) {
            assert(k < clean_images);
            assert(i->w == clean[k].w && i->h == clean[k].h);
            assert(i->stride == clean[k].stride);
            assert(i->dst_x == clean[k].dst_x && i->dst_y == clean[k].dst_y);
            assert(i->color == clean[k].color);
        }
        assert(k == clean_images);
        failures++;
    }
    assert(first_ok && failures == first_ok - 1);
    /*
     * Locked census, same discipline as a golden: this fixture and these limits
     * reach exactly six allocation sites -- the unit and outline records, the
     * outline's points and segments, and both text buffers. A change here means
     * a site was added, removed, or stopped being reachable, and it should be
     * re-blessed deliberately rather than relaxed. Without it, deleting a seam
     * silently shrinks the sweep and every assertion still passes.
     */
    assert(failures == 62);

    ass_free_track(track);
    ass_renderer_done(renderer);
    ass_library_done(library);
    return 0;
}
