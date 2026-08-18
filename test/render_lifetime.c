/* Repeated render replacement and teardown against the renderer-owned result.
 * Pass an iteration count to run the long stress form. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef ASS_TEST_FONT
#error ASS_TEST_FONT must name the bundled test font
#endif

#include "ass.h"
#include "ass_layout.h"

static const char fixture[] =
    "[Script Info]\nScriptType: v4.00+\nPlayResX: 640\nPlayResY: 480\n\n"
    "[V4+ Styles]\n"
    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
    "Style: Default,Aileron,40,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,0,2,10,10,10,1\n\n"
    "[Events]\n"
    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
    "Dialogue: 0,0:00:00.00,9:59:59.99,Default,,0,0,0,,Sample subtitle line\n";

int main(int argc, char **argv)
{
    long iterations = argc > 1 ? strtol(argv[1], NULL, 10) : 2000;

    ASS_Library *lib = ass_library_init();
    assert(lib);
    ASS_Renderer *ren = ass_renderer_init(lib);
    assert(ren);
    ass_set_frame_size(ren, 640, 480);
    ass_set_storage_size(ren, 640, 480);
    ass_set_fonts(ren, ASS_TEST_FONT, "Aileron", ASS_FONTPROVIDER_NONE, NULL, 1);

    ASS_Track *track = ass_read_memory(lib, (char *) fixture,
                                       sizeof(fixture) - 1, NULL);
    assert(track);

    ASS_LayoutRequest layout = {
        .struct_size = sizeof(layout),
        .flags = ASS_LAYOUT_INCLUDE_OUTLINES,
        .max_events = 64,
        .max_text_bytes = 1 << 16,
        .max_units = 4096,
        .max_outlines = 4096,
        .max_outline_points = 1 << 17,
        .max_bitmap_pixels = 1 << 22,
    };
    ASS_RenderRequest req = {
        .struct_size = sizeof(req),
        .track = track,
        .flags = ASS_RENDER_DETECT_CHANGE,
        .layout = &layout,
    };

    const ASS_RenderResult *stable = NULL;
    long with_layout = 0;
    for (long i = 0; i < iterations; i++) {
        /* Move the timestamp so images and layout are genuinely replaced. */
        req.now_ms = (i % 4096) * 7;
        /* Alternate layout on/off to exercise the not-requested reset path. */
        req.layout = (i & 1) ? &layout : NULL;

        const ASS_RenderResult *res = ass_render_frame2(ren, &req);
        assert(res);
        assert(res->struct_size == sizeof(ASS_RenderResult));
        assert(res->status == ASS_RENDER_OK);
        if (!stable)
            stable = res;
        /* The renderer must reuse one record, not reallocate per frame. */
        assert(res == stable);

        if (req.layout) {
            assert(res->layout_status == ASS_LAYOUT_OK ||
                   res->layout_status == ASS_LAYOUT_EMPTY);
            if (res->layout) {
                with_layout++;
                /* Touch the tree so a stale/freed node would trap. */
                for (const ASS_LayoutEvent *e = res->layout->events; e; e = e->next) {
                    volatile size_t len = e->text_length;
                    (void) len;
                    for (const ASS_LayoutUnit *u = e->units; u; u = u->next) {
                        volatile int line = u->line;
                        (void) line;
                    }
                }
            }
        } else {
            /* A layout-free call must clear the previous frame's layout. */
            assert(!res->layout);
            assert(res->layout_status == ASS_LAYOUT_NOT_REQUESTED);
        }

        /* Interleave the legacy entry point; it must not disturb the record. */
        if ((i % 512) == 0)
            ass_render_frame(ren, track, req.now_ms, NULL);
    }

    printf("iterations=%ld layouts=%ld\n", iterations, with_layout);
    ass_free_track(track);
    ass_renderer_done(ren);
    ass_library_done(lib);
    return 0;
}
