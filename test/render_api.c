/* Structural and compatibility tests for ass_render_frame2. */

/* Every check here is an assert(); a -DNDEBUG build must not turn this
 * program into a silent no-op. */
#undef NDEBUG
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../libass/ass.h"

#ifndef ASS_TEST_FONT
#error ASS_TEST_FONT must name the bundled test font
#endif

#define FIELD_END(type, field) \
    (offsetof(type, field) + sizeof(((type *) 0)->field))

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
    "Style: Default,Aileron,42,&H00FFFFFF,&H0000FFFF,&H00000000,&H00000000,"
    "0,0,0,0,100,100,0,0,1,1,1,2,10,10,10,1\n"
    "[Events]\n"
    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
    "Dialogue: 0,0:00:00.00,0:00:04.00,Default,,0,0,0,,render api\n";

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t size)
{
    const unsigned char *bytes = data;
    for (size_t i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t hash_images(ASS_Image *image)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    for (; image; image = image->next) {
        int fields[] = {
            image->w, image->h, image->stride, image->dst_x, image->dst_y,
            (int) image->color, image->type,
        };
        hash = hash_bytes(hash, fields, sizeof(fields));
        for (int y = 0; y < image->h; y++)
            hash = hash_bytes(hash, image->bitmap + y * image->stride,
                              image->w);
    }
    return hash;
}

static ASS_Renderer *new_renderer(ASS_Library *library, int configure)
{
    ASS_Renderer *renderer = ass_renderer_init(library);
    assert(renderer);
    if (configure) {
        ass_set_frame_size(renderer, 320, 240);
        ass_set_storage_size(renderer, 320, 240);
        ass_set_fonts(renderer, ASS_TEST_FONT, "Aileron",
                      ASS_FONTPROVIDER_NONE, NULL, 1);
    }
    return renderer;
}

static ASS_RenderRequest request_for(ASS_Track *track, unsigned flags)
{
    ASS_RenderRequest request = {0};
    request.struct_size = sizeof(request);
    request.track = track;
    request.now_ms = 1000;
    request.flags = flags;
    return request;
}

/* A caller may read a field only if its own sizeof and the runtime-reported
 * struct_size both cover it. */
static int field_available(const ASS_RenderResult *result, size_t field_end)
{
    return result->struct_size >= field_end;
}

int main(void)
{
    ASS_Library *library = ass_library_init();
    assert(library);
    ASS_Track *track = ass_read_memory(library, (char *) fixture,
                                       sizeof(fixture) - 1, NULL);
    assert(track);

    ASS_Renderer *legacy_renderer = new_renderer(library, 1);
    ASS_Renderer *new_api_renderer = new_renderer(library, 1);

    int legacy_change = -1;
    ASS_Image *legacy_images = ass_render_frame(legacy_renderer, track, 1000,
                                                &legacy_change);
    assert(legacy_images);
    uint64_t legacy_hash = hash_images(legacy_images);

    ASS_RenderRequest request = request_for(track, ASS_RENDER_DETECT_CHANGE);
    const ASS_RenderResult *result =
        ass_render_frame2(new_api_renderer, &request);
    assert(result);
/* The reported size is the end of the last known field, not sizeof: a field
     * appended into the tail padding would not move sizeof, and a newer caller
     * would then read bytes this runtime never wrote. */
    assert(result->struct_size ==
           FIELD_END(ASS_RenderResult, layout_status));
    assert(result->struct_size <= sizeof(ASS_RenderResult));
    assert(result->status == ASS_RENDER_OK);
    assert(result->images);
    assert(result->change == legacy_change);
    assert(hash_images(result->images) == legacy_hash);

    /* The renderer owns one record and reuses it; the caller never frees it. */
    const ASS_RenderResult *first_result = result;

    request.flags = 0;
    result = ass_render_frame2(new_api_renderer, &request);
    assert(result == first_result);
    assert(result->status == ASS_RENDER_OK);
    assert(result->change == -1);
    assert(hash_images(result->images) == legacy_hash);

    /* A request ending at now_ms defaults the absent flags field to zero. */
    request = request_for(track, UINT32_MAX);
    request.struct_size = FIELD_END(ASS_RenderRequest, now_ms);
    result = ass_render_frame2(new_api_renderer, &request);
    assert(result);
    assert(result->status == ASS_RENDER_OK);
    assert(result->change == -1);

    struct extended_request {
        ASS_RenderRequest base;
        unsigned char unknown[32];
    } extended = {0};
    extended.base = request_for(track, 0);
    extended.base.struct_size = sizeof(extended);
    memset(extended.unknown, 0xa5, sizeof(extended.unknown));
    result = ass_render_frame2(new_api_renderer, &extended.base);
    assert(result);
    assert(result->status == ASS_RENDER_OK);

    /* A newer caller against a shorter runtime record must treat the tail as
     * absent rather than as a zero value. */
    ASS_RenderResult older_runtime = {0};
    older_runtime.struct_size = FIELD_END(ASS_RenderResult, images);
    older_runtime.status = ASS_RENDER_OK;
    older_runtime.images = legacy_images;
    assert(field_available(&older_runtime, FIELD_END(ASS_RenderResult, images)));
    assert(!field_available(&older_runtime, FIELD_END(ASS_RenderResult, change)));
    assert(field_available(result, FIELD_END(ASS_RenderResult, layout_status)));

    /* A retained pointer never shows the previous frame after a failed call. */
    request = request_for(track, 0);
    result = ass_render_frame2(new_api_renderer, &request);
    assert(result && result->images);
    request.flags = UINT32_MAX;
    result = ass_render_frame2(new_api_renderer, &request);
    assert(result == first_result);
    assert(result->status == ASS_RENDER_INVALID_REQUEST);
    assert(!result->images);
    assert(result->change == -1);
    assert(!result->layout);
    assert(result->layout_status == ASS_LAYOUT_NOT_REQUESTED);

    /* No record can exist when the request is unusable. */
    request = request_for(track, 0);
    request.struct_size = FIELD_END(ASS_RenderRequest, track);
    assert(ass_render_frame2(new_api_renderer, &request) == NULL);
    request = request_for(track, 0);
    assert(ass_render_frame2(NULL, &request) == NULL);
    assert(ass_render_frame2(new_api_renderer, NULL) == NULL);

    /* A NULL track is reported in the record, not by returning NULL. */
    request = request_for(NULL, 0);
    result = ass_render_frame2(new_api_renderer, &request);
    assert(result);
    assert(result->status == ASS_RENDER_INVALID_REQUEST);
    assert(!result->images);

    ASS_Renderer *unconfigured = new_renderer(library, 0);
    request = request_for(track, ASS_RENDER_DETECT_CHANGE);
    result = ass_render_frame2(unconfigured, &request);
    assert(result);
    assert(result->status == ASS_RENDER_NOT_READY);
    assert(!result->images);
    assert(result->change == 2);
    /* Each renderer owns its own record. */
    assert(result != first_result);

    ASS_Track *empty = ass_new_track(library);
    assert(empty);
    request = request_for(empty, ASS_RENDER_DETECT_CHANGE);
    result = ass_render_frame2(new_api_renderer, &request);
    assert(result);
    assert(result->status == ASS_RENDER_OK);
    assert(!result->images);
    assert(result->change == 2);

    /* Both entry points share invalidation/cache state without divergence. */
    request = request_for(track, 0);
    result = ass_render_frame2(new_api_renderer, &request);
    assert(result);
    uint64_t alternating_hash = hash_images(result->images);
    assert(hash_images(ass_render_frame(new_api_renderer, track, 1000, NULL)) ==
           alternating_hash);
    result = ass_render_frame2(new_api_renderer, &request);
    assert(result);
    assert(hash_images(result->images) == alternating_hash);

    ass_free_track(empty);
    ass_renderer_done(unconfigured);
    ass_renderer_done(new_api_renderer);
    ass_renderer_done(legacy_renderer);
    ass_free_track(track);
    ass_library_done(library);
    return 0;
}
