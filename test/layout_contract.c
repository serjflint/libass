/*
 * Font-independent half of the layout contract: limits, statuses, and
 * determinism.
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

/* What every published size tag must equal. Spelled out here rather than
 * shared with the library, so the test checks the rule instead of echoing
 * whatever the library happens to compute. */
#define END_OF(type, field) (offsetof(type, field) + sizeof(((type *) 0)->field))

/* C99 spelling; _Alignof is C11 and this builds as gnu99, tcc included. */
#define ALIGN_OF(type) offsetof(struct { char c_; type t_; }, t_)

/*
 * A published tag must equal the end of the named field AND leave less than one
 * alignment unit unaccounted for.
 *
 * The equality alone is close to a tautology -- test and library name the same
 * field by hand, so they drift together -- and it is outright vacuous on any
 * record with no tail padding: two of the four on LP64, and all four on i386,
 * where long long aligns to 4. The gap check needs no padding to exist. It
 * holds on every ABI by construction and fails the moment a field appended
 * past the named one grows sizeof without the tag following.
 *
 * Still invisible to both: a field appended INTO existing tail padding, which
 * moves neither sizeof nor the tag.
 */
#define ASSERT_SIZE_TAG(type, last, tag)               \
    do {                                               \
        assert((tag) == END_OF(type, last));           \
        assert(sizeof(type) - (tag) < ALIGN_OF(type)); \
    } while (0)

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

/* Two events identical in every field the payload reports, among two others
 * filtered out by time.
 *
 * The filtered events are placed to break contiguity, not just to offset it.
 * With both trailing, the twins sit at slots 0 and 1 -- their output order --
 * and a plain rendered-event counter passes. With both leading, the twins sit
 * at 1 and 2, and a counter biased by the number of leading filtered events
 * still passes; that mutant survived the first version of this fixture.
 * Interleaved, the twins are at slots 1 and 3, which no contiguous counter
 * reaches. */
static const char duplicate_fixture[] =
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
    "Dialogue: 0,0:00:10.00,0:00:14.00,Default,,0,0,0,,later\n"
    "Dialogue: 0,0:00:00.00,0:00:04.00,Default,,0,0,0,,{\\pos(20,20)}twin\n"
    "Dialogue: 0,0:00:10.00,0:00:14.00,Default,,0,0,0,,later two\n"
    "Dialogue: 0,0:00:00.00,0:00:04.00,Default,,0,0,0,,{\\pos(20,20)}twin\n";

/*
 * For the prune ordering. Two events have already ended at the render time
 * used below, so libass drops them mid-call; one future event survives and is
 * not rendered, which keeps the twins' surviving slots (1 and 2) different
 * from their output order (0 and 1).
 */
static const char prune_fixture[] =
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
    "Dialogue: 0,0:00:00.00,0:00:00.50,Default,,0,0,0,,gone one\n"
    "Dialogue: 0,0:00:20.00,0:00:24.00,Default,,0,0,0,,future\n"
    "Dialogue: 0,0:00:00.00,0:00:04.00,Default,,0,0,0,,{\\pos(20,20)}twin\n"
    "Dialogue: 0,0:00:00.00,0:00:00.50,Default,,0,0,0,,gone two\n"
    "Dialogue: 0,0:00:00.00,0:00:04.00,Default,,0,0,0,,{\\pos(20,20)}twin\n"
    "Dialogue: 0,0:00:00.00,0:00:01.50,Default,,0,0,0,,inside the delay\n";

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
        .max_events = SIZE_MAX,
        .max_text_bytes = SIZE_MAX,
        .max_units = SIZE_MAX,
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
    size_t events, text_bytes, units;
};

static struct usage measure(const ASS_Layout *layout)
{
    struct usage u = {0};
    for (const ASS_LayoutEvent *event = layout->events; event;
         event = event->next) {
        u.events++;
        u.text_bytes += event->text_length;
        for (const ASS_LayoutUnit *unit = event->units; unit;
             unit = unit->next)
            u.units++;
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

    /* A budget that fits exactly must be accepted. Only the rejecting side of
     * each limit was covered before, so an off-by-one that rejects a
     * sufficient budget -- making bounded mode unusable -- went unnoticed. */
    ASS_LayoutRequest exact = {
        .struct_size = sizeof(ASS_LayoutRequest),
        .max_events = used.events,
        .max_text_bytes = used.text_bytes,
        .max_units = used.units,
        .max_bitmap_pixels = SIZE_MAX,
    };
    result = render(renderer, track, 1000, &exact);
    assert(result->layout_status == ASS_LAYOUT_OK);
    assert(result->layout);
    struct usage again = measure(result->layout);
    assert(again.events == used.events);
    assert(again.units == used.units);

    /* One below each limit must be refused, and must not damage the images. */
    const size_t *fields[] = {
        &exact.max_events, &exact.max_text_bytes, &exact.max_units,
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
        .flags = ASS_LAYOUT_UNBOUNDED,
    };
    result = render(renderer, track, 1000, &trusted);
    assert(result->layout_status == ASS_LAYOUT_OK);
    assert(result->layout);
    struct usage unbounded_use = measure(result->layout);
    assert(unbounded_use.units == used.units);

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

    /* Every published size tag is the end offset of the record's last known
     * field, never sizeof. See ASSERT_SIZE_TAG for what each half catches and
     * what neither does. */
    result = render(renderer, track, 1000, &limits);
    assert(result->layout_status == ASS_LAYOUT_OK);
    ASSERT_SIZE_TAG(ASS_RenderResult, layout_status, result->struct_size);
    ASSERT_SIZE_TAG(ASS_Layout, events, result->layout->struct_size);
    for (const ASS_LayoutEvent *e = result->layout->events; e; e = e->next) {
        ASSERT_SIZE_TAG(ASS_LayoutEvent, event_index, e->struct_size);
        for (const ASS_LayoutUnit *u = e->units; u; u = u->next)
            ASSERT_SIZE_TAG(ASS_LayoutUnit, next, u->struct_size);
    }

    /*
     * Two events with identical text and identical timing are one document's
     * normal output -- a layered sign, a karaoke duplicate -- and every other
     * field of the payload is the same for both. event_index is the only thing
     * that tells the caller which is which.
     */
    ASS_Track *twins = ass_read_memory(library, (char *) duplicate_fixture,
                                       sizeof(duplicate_fixture) - 1, NULL);
    assert(twins);
    assert(twins->n_events == 4);
    result = render(renderer, twins, 1000, &limits);
    assert(result->layout_status == ASS_LAYOUT_OK);

    int claimed[4] = {0, 0, 0, 0};
    size_t twin_count = 0;
    const ASS_LayoutEvent *first = NULL;
    for (const ASS_LayoutEvent *e = result->layout->events; e; e = e->next) {
        assert(e->event_index >= 0 && e->event_index < twins->n_events);
        /* Distinct: no two records may claim one slot. */
        assert(!claimed[e->event_index]++);
        /* Correct: the slot it claims has the timing it reports. An index that
         * merely counted output order would pass the distinctness check above
         * and fail here, because the third event is filtered out by time. */
        assert(twins->events[e->event_index].Start == e->start_ms);
        assert(twins->events[e->event_index].Duration == e->duration_ms);
        if (!first)
            first = e;
        twin_count++;
    }
    assert(twin_count == 2);
    /* The twins really are indistinguishable without the index. */
    assert(first && first->next);
    assert(first->text_length == first->next->text_length);
    assert(!memcmp(first->text, first->next->text, first->text_length));
    assert(first->start_ms == first->next->start_ms);
    assert(first->duration_ms == first->next->duration_ms);
    assert(first->event_index != first->next->event_index);
    /* The events that never rendered are the slots nobody claimed: the twins
     * are at 1 and 3, while their output order is 0 and 1. */
    assert(!claimed[0]);
    assert(!claimed[2]);

    ass_free_track(twins);

    /*
     * libass prunes expired events inside the render call when the caller has
     * enabled it. If that happens after the layout is collected, every
     * published index describes an array the caller no longer has: at best out
     * of range, at worst resolving to a different event that carries the same
     * timing -- which is exactly the case event_index exists to disambiguate,
     * so the timing assertion above would pass on it.
     */
    ASS_Track *pruned = ass_read_memory(library, (char *) prune_fixture,
                                        sizeof(prune_fixture) - 1, NULL);
    assert(pruned);
    assert(pruned->n_events == 6);
    /*
     * A nonzero delay, so the `- prune_delay` term is observable. At now=2000
     * the threshold is 1000: the two events ending at 1000 go, and the one
     * ending at 1500 stays. Drop the delay term and the threshold becomes 2000,
     * which takes that event too -- the n_events control below catches it.
     */
    ass_configure_prune(pruned, 1000);
    result = render(renderer, pruned, 2000, &limits);
    assert(result->layout_status == ASS_LAYOUT_OK);
    /* The prune really ran, and took exactly the two fully-expired events. */
    assert(pruned->n_events == 4);

    size_t pruned_count = 0;
    for (const ASS_LayoutEvent *e = result->layout->events; e; e = e->next) {
        assert(e->event_index >= 0 && e->event_index < pruned->n_events);
        assert(pruned->events[e->event_index].Start == e->start_ms);
        assert(pruned->events[e->event_index].Duration == e->duration_ms);
        /* Not the output ordinal: the surviving future event holds slot 0. */
        assert(e->event_index == (int) pruned_count + 1);
        pruned_count++;
    }
    assert(pruned_count == 2);

    /*
     * Moving the prune ahead of layout is only safe if it does not change what
     * gets rendered. Same document, same time, prune off: the layout must be
     * identical, which is the claim that a rendered event is never a prunable
     * one.
     */
    ASS_Track *unpruned = ass_read_memory(library, (char *) prune_fixture,
                                          sizeof(prune_fixture) - 1, NULL);
    assert(unpruned);
    const ASS_RenderResult *ref = render(renderer, unpruned, 2000, &limits);
    assert(ref->layout_status == ASS_LAYOUT_OK);
    assert(unpruned->n_events == 6);  /* untouched, so this is the control */

    /*
     * Walk both layouts in lockstep rather than comparing totals. Counting is
     * the wrong oracle here: the way an earlier prune could change output is
     * through the event array the render loop and collision resolution walk,
     * and that moves geometry, not cardinality. This fixture's two rendered
     * events are literal twins, so laying out the wrong one is invisible to
     * every counter and visible in pos.
     */
    ASS_LayoutRequest ref_limits = limits;
    const ASS_LayoutEvent *a = render(renderer, pruned, 2000, &ref_limits)->layout->events;
    /* Both results cannot be live at once -- the record is renderer-owned and
     * reused -- so the reference is re-rendered on a second renderer. */
    ASS_Renderer *other = new_renderer(library);
    const ASS_RenderResult *ref2 = render(other, unpruned, 2000, &ref_limits);
    assert(ref2->layout_status == ASS_LAYOUT_OK);
    const ASS_LayoutEvent *b = ref2->layout->events;

    for (; a && b; a = a->next, b = b->next) {
        assert(a->start_ms == b->start_ms && a->duration_ms == b->duration_ms);
        assert(a->text_length == b->text_length);
        assert(!memcmp(a->text, b->text, a->text_length));
        /* event_index is expected to differ: the pruned array is shorter. */
        const ASS_LayoutUnit *ua = a->units, *ub = b->units;
        for (; ua && ub; ua = ua->next, ub = ub->next) {
            assert(ua->pos.x == ub->pos.x && ua->pos.y == ub->pos.y);
            assert(ua->advance.x == ub->advance.x && ua->advance.y == ub->advance.y);
            assert(ua->line == ub->line);
            assert(ua->text_start == ub->text_start && ua->text_end == ub->text_end);
            assert(ua->logical_bounds.x == ub->logical_bounds.x);
            assert(ua->logical_bounds.w == ub->logical_bounds.w);
        }
        assert(!ua && !ub);
    }
    assert(!a && !b);
    ass_renderer_done(other);

    /*
     * A frame that fails to start prunes nothing. The prune sits after the
     * ass_start_frame check for that reason, and nothing tested it. A renderer
     * with a zero frame size returns NOT_READY before touching the track, so
     * the event array must come back untouched. The render() helper asserts
     * ASS_RENDER_OK, so this calls the entry point directly.
     */
    ASS_Renderer *stillborn = ass_renderer_init(library);
    assert(stillborn);
    ass_set_frame_size(stillborn, 0, 0);
    ass_set_fonts(stillborn, ASS_TEST_FONT, "Aileron", ASS_FONTPROVIDER_NONE, NULL, 1);
    ass_configure_prune(pruned, 0);
    int before = pruned->n_events;
    ASS_RenderRequest stillborn_req = {
        .struct_size = sizeof(stillborn_req),
        .track = pruned,
        .now_ms = 100000,   /* everything is expired by now */
        .layout = &limits,
    };
    const ASS_RenderResult *stillborn_res =
        ass_render_frame2(stillborn, &stillborn_req);
    assert(stillborn_res);
    assert(stillborn_res->status == ASS_RENDER_NOT_READY);
    assert(pruned->n_events == before);
    ass_renderer_done(stillborn);

    ass_free_track(unpruned);
    ass_free_track(pruned);
    ass_free_track(track);
    ass_renderer_done(renderer);
    ass_library_done(library);
    return 0;
}
