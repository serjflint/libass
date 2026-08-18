/*
 * Structural tests for the same-render subtitle layout API. Exact glyph
 * coordinates are font-dependent; the tests cover text/range coherence,
 * event separation, lifetime, and render identity.
 */

/* Every check here is an assert(); a -DNDEBUG build must not turn this
 * program into a silent no-op. */
#undef NDEBUG
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../libass/ass.h"
#include "../libass/ass_layout.h"

#ifndef __has_feature
#define __has_feature(x) 0
#endif

static const char fixture[] =
    "[Script Info]\n"
    "ScriptType: v4.00+\n"
    "PlayResX: 640\n"
    "PlayResY: 360\n"
    "WrapStyle: 2\n"
    "[V4+ Styles]\n"
    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
    "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, "
    "ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
    "MarginR, MarginV, Encoding\n"
    "Style: Default,DejaVu Sans,32,&H00FFFFFF,&H0000FFFF,&H00000000,&H00000000,"
    "0,0,0,0,100,100,0,0,1,1,1,2,10,10,10,1\n"
    "Style: Plain,DejaVu Sans,32,&H00FFFFFF,&H0000FFFF,&H00000000,&H00000000,"
    "0,0,0,0,100,100,0,0,1,0,0,2,10,10,10,1\n"
    "[Events]\n"
    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
    "Dialogue: 0,0:00:00.00,0:00:02.00,Default,normalized,0,0,0,,"
    "{\\b1}A\\hB\\nC\\N\\{D\\}\tE{\\p1}m 0 0 l 10 0 10 10 0 10{\\p0}F\n"
    "Dialogue: 0,0:00:03.00,0:00:05.00,Default,bidi,0,0,0,,abc שלום\n"
    "Dialogue: 0,0:00:06.00,0:00:08.00,Default,shape,0,0,0,,office "
    "e\xcc\x81 日本語\n"
    "Dialogue: 0,0:00:09.00,0:00:11.00,Default,collision-a,0,0,0,,same\n"
    "Dialogue: 0,0:00:09.00,0:00:11.00,Default,collision-b,0,0,0,,same\n"
    "Dialogue: 0,0:00:12.00,0:00:14.00,Default,soft-break,0,0,0,,"
    "{\\q0}A\\nB\n"
    "Dialogue: 0,0:00:15.00,0:00:17.00,Default,hard-break,0,0,0,,"
    "{\\q2}A\\nB\n"
    "Dialogue: 0,0:00:18.00,0:00:20.00,Default,whitespace,0,0,0,,\\h\n"
    "Dialogue: 0,0:00:21.00,0:00:23.00,Default,karaoke,0,0,0,,"
    "{\\k50}ka{\\k50}ra\n"
    "Dialogue: 0,0:00:24.00,0:00:26.00,Default,transform,0,0,0,,"
    "{\\t(0,2000,\\frz45)}turn\n"
    "Dialogue: 0,0:00:27.00,0:00:29.00,Default,clip-blur,0,0,0,,"
    "{\\pos(20,20)\\an7\\blur5\\clip(0,0,35,35)}clip\n"
    "Dialogue: 0,0:00:30.00,0:00:32.00,Default,transparent,0,0,0,,"
    "{\\alpha&HFF&}ghost\n"
    "Dialogue: 0,0:00:34.00,0:00:36.00,Plain,single-cluster,0,0,0,,日\n"
    "Dialogue: 0,0:00:38.00,0:00:40.00,Default,arabic,0,0,0,,مرحبا abc\n"
    "Dialogue: 0,0:00:41.00,0:00:43.00,Default,ligature,0,0,0,,لا\n";

static const char prune_fixture[] =
    "[Script Info]\n"
    "ScriptType: v4.00+\n"
    "PlayResX: 640\n"
    "PlayResY: 360\n"
    "[V4+ Styles]\n"
    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
    "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, "
    "ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, "
    "MarginR, MarginV, Encoding\n"
    "Style: Default,DejaVu Sans,32,&H00FFFFFF,&H0000FFFF,&H00000000,&H00000000,"
    "0,0,0,0,100,100,0,0,1,1,1,2,10,10,10,1\n"
    "[Events]\n"
    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
    "Dialogue: 0,0:00:00.00,0:00:01.00,Default,expired,0,0,0,,expired\n"
    "Dialogue: 0,0:00:01.50,0:00:04.50,Default,first,0,0,0,,first\n"
    "Dialogue: 0,0:00:02.00,0:00:05.00,Default,second,0,0,0,,second\n";

static void message_callback(int level, const char *format, va_list arguments,
                             void *data)
{
    (void) data;
    if (level <= 7) {
        vfprintf(stderr, format, arguments);
        fputc('\n', stderr);
    }
}

static uint64_t hash_bytes(uint64_t hash, const unsigned char *data, size_t size)
{
    for (size_t i = 0; i < size; i++) {
        hash ^= data[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t hash_images(ASS_Image *image)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    for (; image; image = image->next) {
        int values[] = {
            image->w, image->h, image->stride, image->dst_x, image->dst_y,
            (int) image->color, image->type,
        };
        hash = hash_bytes(hash, (const unsigned char *) values, sizeof(values));
        for (int y = 0; y < image->h; y++)
            hash = hash_bytes(hash, image->bitmap + y * image->stride, image->w);
    }
    return hash;
}

static ASS_LayoutRect emitted_image_bounds(ASS_Image *image)
{
    int left = INT_MAX, top = INT_MAX, right = INT_MIN, bottom = INT_MIN;
    for (; image; image = image->next) {
        if (image->w <= 0 || image->h <= 0 ||
            (image->color & 0xff) == 0xff)
            continue;
        left = left < image->dst_x ? left : image->dst_x;
        top = top < image->dst_y ? top : image->dst_y;
        right = right > image->dst_x + image->w ?
            right : image->dst_x + image->w;
        bottom = bottom > image->dst_y + image->h ?
            bottom : image->dst_y + image->h;
    }
    if (left == INT_MAX)
        return (ASS_LayoutRect) {0};
    return (ASS_LayoutRect) {left, top, right - left, bottom - top};
}

static int utf8_boundary(const char *text, size_t length, size_t offset)
{
    return offset <= length &&
           (offset == 0 || offset == length ||
            (((unsigned char) text[offset] & 0xc0) != 0x80));
}

static double rightmost_cluster_logical(const ASS_LayoutEvent *metrics)
{
    double right = -HUGE_VAL;
    for (ASS_LayoutUnit *cluster = metrics->units;
         cluster; cluster = cluster->next) {
        right = fmax(right, cluster->logical_bounds.x +
                            cluster->logical_bounds.w);
    }
    return right;
}

static ASS_LayoutUnit *cluster_covering(ASS_LayoutEvent *metrics,
                                            size_t start, size_t end)
{
    for (ASS_LayoutUnit *cluster = metrics->units;
         cluster; cluster = cluster->next) {
        if (cluster->text_start <= start && end <= cluster->text_end)
            return cluster;
    }
    return NULL;
}

static ASS_LayoutUnit *cluster_at(ASS_LayoutEvent *metrics, size_t offset)
{
    for (ASS_LayoutUnit *cluster = metrics->units;
         cluster; cluster = cluster->next) {
        if (cluster->text_start <= offset && offset < cluster->text_end)
            return cluster;
    }
    return NULL;
}

static size_t validate_metrics(ASS_LayoutEvent *metrics)
{
    size_t events = 0;
    for (; metrics; metrics = metrics->next) {
        assert(metrics->struct_size == sizeof(*metrics));
        assert(metrics->has_duration);
        assert(metrics->text);
        assert(strlen(metrics->text) == metrics->text_length);
        size_t previous = 0;
        for (ASS_LayoutUnit *cluster = metrics->units;
             cluster; cluster = cluster->next) {
            assert(cluster->struct_size == sizeof(*cluster));
            assert(cluster->text_start < cluster->text_end);
            assert(cluster->text_start == previous);
            assert(cluster->text_end <= metrics->text_length);
            assert(utf8_boundary(metrics->text, metrics->text_length,
                                 cluster->text_start));
            assert(utf8_boundary(metrics->text, metrics->text_length,
                                 cluster->text_end));
            assert(cluster->line >= 0);
            assert(cluster->logical_bounds.w >= 0);
            assert(cluster->logical_bounds.h >= 0);
            previous = cluster->text_end;
        }
        assert(previous == metrics->text_length);
        events++;
    }
    return events;
}

static void assert_ranges(ASS_LayoutEvent *metrics, const size_t (*ranges)[2],
                          size_t count)
{
    ASS_LayoutUnit *cluster = metrics->units;
    for (size_t i = 0; i < count; i++) {
        assert(cluster);
        assert(cluster->text_start == ranges[i][0]);
        assert(cluster->text_end == ranges[i][1]);
        cluster = cluster->next;
    }
    assert(!cluster);
}

static ASS_LayoutRequest valid_limits(void)
{
    return (ASS_LayoutRequest) {
        .struct_size = sizeof(ASS_LayoutRequest),
        .max_events = SIZE_MAX,
        .max_text_bytes = SIZE_MAX,
        .max_units = SIZE_MAX,
        .max_bitmap_pixels = SIZE_MAX,
    };
}

static ASS_Renderer *new_renderer(ASS_Library *library)
{
    ASS_Renderer *renderer = ass_renderer_init(library);
    assert(renderer);
    ass_set_frame_size(renderer, 640, 360);
    ass_set_storage_size(renderer, 640, 360);
    ass_set_fonts(renderer, NULL, "DejaVu Sans", ASS_FONTPROVIDER_FONTCONFIG,
                  NULL, 1);
    return renderer;
}

static ASS_Image *render_bounded(ASS_Renderer *renderer, ASS_Track *track,
                                 long long now, ASS_LayoutRequest *limits,
                                 ASS_LayoutEvent **metrics,
                                 ASS_LayoutStatus *status)
{
    ASS_RenderRequest request = {
        .struct_size = sizeof(request),
        .track = track,
        .now_ms = now,
        .layout = limits,
    };
    const ASS_RenderResult *result = ass_render_frame2(renderer, &request);
    assert(result);
    const ASS_Layout *layout = result->layout;
    ASS_Image *images = result->images;
    *status = result->layout_status;
    if (layout) {
        assert(layout->struct_size == sizeof(*layout));
        assert(layout->unit_mode == ASS_LAYOUT_UNIT_SIMPLE_SCALAR ||
               layout->unit_mode == ASS_LAYOUT_UNIT_SHAPING_CLUSTER);
    }
    *metrics = layout ? layout->events : NULL;
    return images;
}

static ASS_LayoutUnitMode last_unit_mode;

static ASS_Image *render_unbounded(ASS_Renderer *renderer, ASS_Track *track,
                                   long long now, int *detect_change,
                                   ASS_LayoutEvent **metrics)
{
    ASS_LayoutRequest layout_request = {
        .struct_size = sizeof(layout_request),
        .flags = ASS_LAYOUT_UNBOUNDED,
    };
    ASS_RenderRequest request = {
        .struct_size = sizeof(request),
        .track = track,
        .now_ms = now,
        .flags = detect_change ? ASS_RENDER_DETECT_CHANGE : 0,
        .layout = &layout_request,
    };
    const ASS_RenderResult *result = ass_render_frame2(renderer, &request);
    assert(result);
    if (detect_change)
        *detect_change = result->change;
    const ASS_Layout *layout = result->layout;
    ASS_LayoutStatus status = result->layout_status;
    ASS_Image *images = result->images;
    if (layout) {
        assert(status == ASS_LAYOUT_OK);
        assert(layout->struct_size == sizeof(*layout));
        assert(layout->unit_mode == ASS_LAYOUT_UNIT_SIMPLE_SCALAR ||
               layout->unit_mode == ASS_LAYOUT_UNIT_SHAPING_CLUSTER);
    }
    *metrics = layout ? layout->events : NULL;
    if (layout)
        last_unit_mode = layout->unit_mode;
    return images;
}

int main(void)
{
    ASS_Library *library = ass_library_init();
    assert(library);
    ass_set_message_cb(library, message_callback, NULL);
    ASS_Renderer *renderer = new_renderer(library);
    ASS_Track *track = ass_read_memory(library, (char *) fixture,
                                       sizeof(fixture) - 1, NULL);
    assert(track);
    ASS_Image *plain = ass_render_frame(renderer, track, 1000, NULL);
    assert(plain);
    uint64_t plain_hash = hash_images(plain);

    ASS_LayoutEvent *metrics = NULL;
    ASS_Image *with_metrics = render_unbounded(
        renderer, track, 1000, NULL, &metrics);
    assert(with_metrics);
    assert(hash_images(with_metrics) == plain_hash);
    assert(validate_metrics(metrics) == 1);
    assert(last_unit_mode == ASS_LAYOUT_UNIT_SHAPING_CLUSTER);
    assert(metrics->has_bitmap_bounds);
    assert(metrics->bitmap_bounds.w > 0);
    assert(metrics->bitmap_bounds.h > 0);
    ASS_LayoutRect pixels = emitted_image_bounds(with_metrics);
    assert(metrics->bitmap_bounds.x == pixels.x);
    assert(metrics->bitmap_bounds.y == pixels.y);
    assert(metrics->bitmap_bounds.w == pixels.w);
    assert(metrics->bitmap_bounds.h == pixels.h);
    const char expected[] = "A\xc2\xa0" "B\nC\n{D} E\xef\xbf\xbc" "F";
    assert(strcmp(metrics->text, expected) == 0);

    ASS_LayoutStatus status = ASS_LAYOUT_OK;
    ASS_LayoutRequest limits = valid_limits();
    limits.max_text_bytes = 1;
    with_metrics = render_bounded(renderer, track, 1000, &limits, &metrics,
                                  &status);
    assert(with_metrics);
    assert(hash_images(with_metrics) == plain_hash);
    assert(!metrics);
    assert(status == ASS_LAYOUT_LIMIT_EXCEEDED);

    limits = valid_limits();
    limits.max_units = 1;
    with_metrics = render_bounded(renderer, track, 1000, &limits, &metrics,
                                  &status);
    assert(with_metrics);
    assert(hash_images(with_metrics) == plain_hash);
    assert(!metrics);
    assert(status == ASS_LAYOUT_LIMIT_EXCEEDED);

    limits = valid_limits();
    limits.max_bitmap_pixels = 1;
    with_metrics = render_bounded(renderer, track, 1000, &limits, &metrics,
                                  &status);
    assert(with_metrics);
    assert(hash_images(with_metrics) == plain_hash);
    assert(!metrics);
    assert(status == ASS_LAYOUT_LIMIT_EXCEEDED);

    limits = valid_limits();
    limits.max_events = 1;
    ASS_Image *collision = ass_render_frame(renderer, track, 10000, NULL);
    assert(collision);
    uint64_t collision_hash = hash_images(collision);
    with_metrics = render_bounded(renderer, track, 10000, &limits, &metrics,
                                  &status);
    assert(with_metrics);
    assert(hash_images(with_metrics) == collision_hash);
    assert(!metrics);
    assert(status == ASS_LAYOUT_LIMIT_EXCEEDED);

    const size_t limit_offsets[] = {
        offsetof(ASS_LayoutRequest, max_events),
        offsetof(ASS_LayoutRequest, max_text_bytes),
        offsetof(ASS_LayoutRequest, max_units),
        offsetof(ASS_LayoutRequest, max_bitmap_pixels),
    };
    for (size_t i = 0; i < sizeof(limit_offsets) / sizeof(*limit_offsets); i++) {
        limits = valid_limits();
        *(size_t *) ((char *) &limits + limit_offsets[i]) = 0;
        with_metrics = render_bounded(renderer, track, 1000, &limits,
                                      &metrics, &status);
        assert(with_metrics);
        assert(hash_images(with_metrics) == plain_hash);
        assert(!metrics);
        assert(status == ASS_LAYOUT_INVALID_REQUEST);
    }
    limits = valid_limits();
    limits.struct_size = offsetof(ASS_LayoutRequest, max_bitmap_pixels);
    with_metrics = render_bounded(renderer, track, 1000, &limits,
                                  &metrics, &status);
    assert(with_metrics);
    assert(hash_images(with_metrics) == plain_hash);
    assert(!metrics);
    assert(status == ASS_LAYOUT_INVALID_REQUEST);

    limits = valid_limits();
    limits.struct_size = sizeof(limits) + 64;
    with_metrics = render_bounded(renderer, track, 1000, &limits,
                                  &metrics, &status);
    assert(with_metrics);
    assert(hash_images(with_metrics) == plain_hash);
    assert(metrics);
    assert(status == ASS_LAYOUT_OK);
    with_metrics = render_bounded(renderer, track, 1000, NULL, &metrics,
                                  &status);
    assert(with_metrics);
    assert(hash_images(with_metrics) == plain_hash);
    assert(!metrics);
    assert(status == ASS_LAYOUT_NOT_REQUESTED);

    limits = valid_limits();
    with_metrics = render_bounded(renderer, track, 37000, &limits, &metrics,
                                  &status);
    assert(!with_metrics);
    assert(!metrics);
    assert(status == ASS_LAYOUT_EMPTY);

    with_metrics = render_unbounded(renderer, track, 1000, NULL,
                                                  &metrics);
    assert(with_metrics);
    assert(validate_metrics(metrics) == 1);

    with_metrics = render_unbounded(renderer, track, 4000, NULL,
                                                  &metrics);
    assert(with_metrics);
    assert(validate_metrics(metrics) == 1);
    assert(strcmp(metrics->text, "abc שלום") == 0);
    ASS_LayoutUnit *first_hebrew = cluster_covering(metrics, 4, 6);
    ASS_LayoutUnit *last_hebrew = cluster_covering(metrics, 10, 12);
    assert(first_hebrew && last_hebrew);
    assert(first_hebrew->pos.x > last_hebrew->pos.x);

    with_metrics = render_unbounded(renderer, track, 7000, NULL,
                                                  &metrics);
    assert(with_metrics);
    assert(validate_metrics(metrics) == 1);
    assert(strcmp(metrics->text, "office e\xcc\x81 日本語") == 0);
    ASS_LayoutUnit *combining_base = cluster_at(metrics, 7);
    ASS_LayoutUnit *combining_mark = cluster_at(metrics, 8);
    assert(combining_base && combining_mark);
    assert(combining_base != cluster_at(metrics, 10));
    assert(combining_base->text_start == 7);
    assert(combining_mark->text_end == 10);
    assert(combining_base == combining_mark ||
           combining_base->text_end == combining_mark->text_start);

    with_metrics = render_unbounded(renderer, track, 10000, NULL,
                                                  &metrics);
    assert(with_metrics);
    assert(validate_metrics(metrics) == 2);
    ASS_LayoutEvent *first = metrics;
    ASS_LayoutEvent *second = metrics->next;
    assert(first && second && first->units && second->units);
    assert(strcmp(first->text, second->text) == 0);
    assert(first->units->pos.y != second->units->pos.y);

    with_metrics = render_unbounded(renderer, track, 13000, NULL,
                                                  &metrics);
    assert(with_metrics);
    assert(validate_metrics(metrics) == 1);
    assert(strcmp(metrics->text, "A B") == 0);
    assert(cluster_at(metrics, 0)->line == cluster_at(metrics, 2)->line);

    with_metrics = render_unbounded(renderer, track, 16000, NULL,
                                                  &metrics);
    assert(with_metrics);
    assert(validate_metrics(metrics) == 1);
    assert(strcmp(metrics->text, "A\nB") == 0);
    assert(cluster_at(metrics, 0)->line == 0);
    assert(cluster_at(metrics, 2)->line == 1);

    with_metrics = render_unbounded(renderer, track, 19000, NULL,
                                                  &metrics);
    assert(!with_metrics);
    assert(validate_metrics(metrics) == 1);
    assert(strcmp(metrics->text, "\xc2\xa0") == 0);
    assert(!metrics->has_bitmap_bounds);

    with_metrics = render_unbounded(renderer, track, 21250, NULL,
                                                  &metrics);
    assert(with_metrics);
    assert(validate_metrics(metrics) == 1);
    uint64_t early_karaoke_hash = hash_images(with_metrics);
    with_metrics = render_unbounded(renderer, track, 21750, NULL,
                                                  &metrics);
    assert(with_metrics);
    assert(validate_metrics(metrics) == 1);
    assert(hash_images(with_metrics) != early_karaoke_hash);

    with_metrics = render_unbounded(renderer, track, 24250, NULL,
                                                  &metrics);
    assert(with_metrics);
    uint64_t early_transform_hash = hash_images(with_metrics);
    double early_logical_right = rightmost_cluster_logical(metrics);
    with_metrics = render_unbounded(renderer, track, 25750, NULL,
                                                  &metrics);
    assert(with_metrics);
    assert(hash_images(with_metrics) != early_transform_hash);
    assert(rightmost_cluster_logical(metrics) == early_logical_right);

    with_metrics = render_unbounded(renderer, track, 28000, NULL,
                                                  &metrics);
    assert(with_metrics);
    assert(validate_metrics(metrics) == 1);
    assert(metrics->has_bitmap_bounds);
    assert(metrics->bitmap_bounds.x + metrics->bitmap_bounds.w <= 35);

    with_metrics = render_unbounded(renderer, track, 31000, NULL,
                                                  &metrics);
    assert(!with_metrics);
    assert(validate_metrics(metrics) == 1);
    assert(strcmp(metrics->text, "ghost") == 0);
    assert(!metrics->has_bitmap_bounds);

    int stress_iterations = __has_feature(address_sanitizer) ? 50 : 1000;
    const char *stress_env = getenv("ASS_LAYOUT_STRESS_ITERATIONS");
    if (stress_env) {
        char *end = NULL;
        long requested = strtol(stress_env, &end, 10);
        assert(end && !*end && requested > 0 && requested <= INT_MAX);
        stress_iterations = requested;
    }
    for (int i = 0; i < stress_iterations; i++) {
        long long timestamp = (i & 1) ? 4000 : 7000;
        with_metrics = render_unbounded(renderer, track, timestamp,
                                                      NULL, &metrics);
        assert(with_metrics);
        assert(validate_metrics(metrics) == 1);
    }

    if (getenv("ASS_LAYOUT_BENCHMARK")) {
        int iterations = 5000;
        const char *benchmark_env = getenv("ASS_LAYOUT_BENCHMARK_ITERATIONS");
        if (benchmark_env) {
            char *end = NULL;
            long requested = strtol(benchmark_env, &end, 10);
            assert(end && !*end && requested > 0 && requested <= INT_MAX);
            iterations = requested;
        }
        volatile ASS_Image *sink = NULL;
        clock_t started = clock();
        for (int i = 0; i < iterations; i++)
            sink = ass_render_frame(renderer, track, 4000, NULL);
        double plain_ms = 1000.0 * (clock() - started) /
                          CLOCKS_PER_SEC / iterations;
        started = clock();
        for (int i = 0; i < iterations; i++)
            sink = render_unbounded(renderer, track, 4000, NULL,
                                                  &metrics);
        double metrics_ms = 1000.0 * (clock() - started) /
                            CLOCKS_PER_SEC / iterations;
        assert(sink);
        fprintf(stderr,
                "benchmark: plain=%.4f ms metrics=%.4f ms delta=%.4f ms "
                "ratio=%.2fx\n",
                plain_ms, metrics_ms, metrics_ms - plain_ms,
                metrics_ms / plain_ms);
    }

    with_metrics = render_unbounded(renderer, track, 35000, NULL,
                                                  &metrics);
    assert(with_metrics);
    assert(validate_metrics(metrics) == 1);
    assert(metrics->units && !metrics->units->next);

    with_metrics = render_unbounded(renderer, track, 39000, NULL,
                                                  &metrics);
    assert(with_metrics);
    assert(validate_metrics(metrics) == 1);
    assert(strcmp(metrics->text, "مرحبا abc") == 0);
    assert(metrics->units && metrics->units->next);
    ASS_LayoutUnit *first_arabic = cluster_at(metrics, 0);
    ASS_LayoutUnit *last_arabic = cluster_at(metrics, 8);
    assert(first_arabic && last_arabic);
    assert(first_arabic->pos.x > last_arabic->pos.x);

    with_metrics = render_unbounded(renderer, track, 42000, NULL,
                                                  &metrics);
    assert(with_metrics);
    assert(validate_metrics(metrics) == 1);
    assert(strcmp(metrics->text, "لا") == 0);
    ASS_LayoutUnit *lam = cluster_at(metrics, 0);
    ASS_LayoutUnit *alef = cluster_at(metrics, 2);
    assert(lam && lam == alef);
    assert(lam->text_start == 0);
    assert(lam->text_end == strlen(metrics->text));

    const long long equivalence_times[] = {
        1000, 4000, 7000, 10000, 13000, 16000, 19000, 21250,
        21750, 24250, 25750, 28000, 31000, 35000, 39000, 42000,
    };
    for (size_t i = 0; i < sizeof(equivalence_times) / sizeof(*equivalence_times); i++) {
        with_metrics = render_unbounded(
            renderer, track, equivalence_times[i], NULL, &metrics);
        uint64_t metrics_hash = hash_images(with_metrics);
        plain = ass_render_frame(renderer, track, equivalence_times[i], NULL);
        assert(hash_images(plain) == metrics_hash);
    }

    with_metrics = render_unbounded(renderer, track, 37000, NULL,
                                                  &metrics);
    assert(!with_metrics);
    assert(!metrics);

    int first_style = track->events[0].Style;
    track->events[0].Style = INT_MAX;
    with_metrics = render_unbounded(renderer, track, 1000, NULL,
                                                  &metrics);
    assert(!with_metrics);
    assert(!metrics);
    track->events[0].Style = first_style;

    with_metrics = render_unbounded(renderer, track, 1000, NULL,
                                                  &metrics);
    assert(with_metrics);
    assert(validate_metrics(metrics) == 1);

    ASS_Renderer *simple_renderer = new_renderer(library);
    ass_set_shaper(simple_renderer, ASS_SHAPING_SIMPLE);
    with_metrics = render_unbounded(simple_renderer, track, 39000,
                                                  NULL, &metrics);
    assert(with_metrics);
    assert(validate_metrics(metrics) == 1);
    assert(strcmp(metrics->text, "مرحبا abc") == 0);
    assert(last_unit_mode == ASS_LAYOUT_UNIT_SIMPLE_SCALAR);
    const size_t simple_ranges[][2] = {
        {0, 2}, {2, 4}, {4, 6}, {6, 8}, {8, 10},
        {10, 11}, {11, 12}, {12, 13}, {13, 14},
    };
    assert_ranges(metrics, simple_ranges,
                  sizeof(simple_ranges) / sizeof(*simple_ranges));
    ass_renderer_done(simple_renderer);

    ASS_Track *empty_track = ass_new_track(library);
    assert(empty_track);
    limits = valid_limits();
    with_metrics = render_bounded(renderer, empty_track, 1000, &limits, &metrics,
                                  &status);
    assert(!with_metrics);
    assert(!metrics);
    assert(status == ASS_LAYOUT_EMPTY);

    limits = valid_limits();
    limits.struct_size = 0;
    with_metrics = render_bounded(renderer, empty_track, 1000, &limits,
                                  &metrics, &status);
    assert(!with_metrics);
    assert(!metrics);
    assert(status == ASS_LAYOUT_INVALID_REQUEST);

    with_metrics = render_unbounded(renderer, empty_track, 1000, NULL,
                                    &metrics);
    assert(!with_metrics);
    assert(!metrics);

    ASS_Renderer *unconfigured_renderer = ass_renderer_init(library);
    assert(unconfigured_renderer);
    limits = valid_limits();
    with_metrics = render_bounded(unconfigured_renderer, track, 1000, &limits,
                                  &metrics, &status);
    assert(!with_metrics);
    assert(!metrics);
    assert(status == ASS_LAYOUT_INVALID_REQUEST);

    with_metrics = render_unbounded(unconfigured_renderer, empty_track, 1000,
                                    NULL, &metrics);
    assert(!with_metrics);
    assert(!metrics);
    ass_renderer_done(unconfigured_renderer);
    ass_free_track(empty_track);

    ASS_Track *prune_track = ass_read_memory(library, (char *) prune_fixture,
                                             sizeof(prune_fixture) - 1, NULL);
    assert(prune_track);
    ass_configure_prune(prune_track, 0);
    with_metrics = render_unbounded(renderer, prune_track, 3000,
                                                  NULL, &metrics);
    assert(with_metrics);
    assert(validate_metrics(metrics) == 2);
    assert(prune_track->n_events == 2);
    assert(metrics->start_ms == 1500);
    assert(metrics->duration_ms == 3000);
    assert(strcmp(metrics->text, "first") == 0);
    assert(metrics->next->start_ms == 2000);
    assert(metrics->next->duration_ms == 3000);
    assert(strcmp(metrics->next->text, "second") == 0);
    ass_free_track(prune_track);

    ass_flush_events(track);
    with_metrics = render_unbounded(renderer, track, 1000, NULL,
                                                  &metrics);
    assert(!with_metrics);
    assert(!metrics);

    ass_free_track(track);
    ass_renderer_done(renderer);
    ass_library_done(library);
    return 0;
}
