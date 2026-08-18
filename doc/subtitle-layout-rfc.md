# RFC: Extensible render results and same-render subtitle layout for libass

Status: draft for libass maintainer discussion

Related discussion:

- [Text to shape / text metrics API, issue #825](https://github.com/libass/libass/issues/825)
- [Metrics and shape data proof of concept, pull request #856](https://github.com/libass/libass/pull/856)
- [RGBA output API, issue #73](https://github.com/libass/libass/issues/73)
- [Implementation draft](https://github.com/serjflint/libass/pull/1)

## Decision requested

Should libass add one extensible render entry point whose first optional output is parsed subtitle text
and layout geometry from the same invocation that produces `ASS_Image` output?

The proposal has two separable parts:

1. `ass_render_frame2`, with caller-owned expandable request and result records; and
2. an optional, bounded, renderer-owned `ASS_Layout` result.

The foundation avoids a new render symbol for each future output family. The layout extension solves a
specific existing gap without defining words, application IDs, cursor behavior, or final per-character
pixel ownership.

## Use case in simple terms

Suppose a video player pauses on a subtitle and the user clicks a word to look it up. The player knows
the subtitle text and libass knows where it drew the shaped text, but neither public API connects a
range of that text to the corresponding laid-out region. Re-rendering in another text engine is not
reliable because fonts, ASS parsing, shaping, wrapping, collision placement, and transforms may differ.

The proposed call returns the ordinary subtitle images and, when requested, the parsed text ranges and
semantic geometry that produced them. The application can tokenize the returned text itself and combine
the geometry of every layout unit intersecting a token. The same data also supports selection,
accessibility, authoring tools, and renderer diagnostics.

## Components and terminology

The relevant render stages are:

1. **Parsing.** ASS override syntax, escapes, and drawings are interpreted into logical content.
2. **Shaping.** Unicode text is mapped to positioned glyphs. Several input characters can form one
   indivisible positioning unit, and one input range can produce several glyphs.
3. **Layout.** Lines, positions, wrapping, and collision displacement are chosen.
4. **Rasterization and composition.** Transforms, clipping, borders, shadows, blur, karaoke masks, and
   alpha images are produced.

This document uses:

- **parsed logical text**: post-ASS-parse, pre-bidi UTF-8, without Unicode normalization;
- **layout unit**: a contiguous logical-text range that libass positioned as one shaping unit;
- **logical bounds**: the unit's pre-transform typographic advance/line box after wrapping and collision
  placement;
- **fill outline**: transformed glyph-fill vector geometry after placement but before clipping and
  raster effects;
- **bitmap bounds**: the union of final clipped image rectangles emitted for an event, including any
  transparent padding within those rectangles.

When the complex shaper is active, layout-unit boundaries come from its shaping clusters. A shaping
cluster is simply the renderer's mapping between an input-text range and the glyphs positioned for that
range; it is not necessarily one Unicode character, user-perceived character, or word. With the simple
shaper, one unit corresponds to one input Unicode scalar value.

All values are renderer results. They are not promised to remain numerically identical across libass
versions, fonts, platform font providers, or renderer settings.

## Why one render result

Issue #825 proposes exposing metrics and shapes from the normal layout pipeline. PR #856 demonstrated
that the data survives far enough through that pipeline to collect it. Its review also identified the
important coupling: metrics and images share rendering work, caches, change detection, collision state,
and output lifetime.

A separate metrics render can make a different decision from the render whose images are displayed. A
feature-specific `ass_render_frame_with_layout` also repeats the entry-point problem when another caller
later needs packed RGBA, dirty regions, diagnostics, or a different optional output. Issue #73 already
used the working name `ass_render_frame2` for a richer render result.

The proposed foundation adds one symbol now. A library predating that symbol does not support the API;
normal compile/link probing detects that. Expandable request/result records let later optional outputs be
added without another render symbol.

## Proposed render foundation

Names and exact field ordering remain open to maintainer preference. The ownership and compatibility
rules are the proposed contract.

```c
typedef enum ass_render_status {
    ASS_RENDER_OK = 0,
    ASS_RENDER_INVALID_REQUEST,
    ASS_RENDER_NOT_READY,
} ASS_RenderStatus;

typedef enum ass_layout_status {
    ASS_LAYOUT_NOT_REQUESTED = 0,
    ASS_LAYOUT_OK,
    ASS_LAYOUT_EMPTY,
    ASS_LAYOUT_LIMIT_EXCEEDED,
    ASS_LAYOUT_ALLOCATION_FAILED,
    ASS_LAYOUT_FAILED,
    ASS_LAYOUT_INVALID_REQUEST,
} ASS_LayoutStatus;

typedef struct ass_layout_request ASS_LayoutRequest;
typedef struct ass_layout ASS_Layout;

enum {
    ASS_RENDER_DETECT_CHANGE = 1u << 0,
};

typedef struct ass_render_request {
    size_t struct_size;
    ASS_Track *track;
    long long now_ms;
    unsigned flags;
    const ASS_LayoutRequest *layout;
} ASS_RenderRequest;

typedef struct ass_render_result {
    size_t struct_size;
    ASS_RenderStatus status;
    ASS_Image *images;
    int change;
    const ASS_Layout *layout;
    ASS_LayoutStatus layout_status;
} ASS_RenderResult;

int ass_render_frame2(ASS_Renderer *renderer,
                      const ASS_RenderRequest *request,
                      ASS_RenderResult *result);
```

`ass_render_frame2` returns zero when it could read the request/result envelopes and populate a result.
It returns a negative value when the envelopes themselves are null or shorter than their documented
minimum prefixes. Render outcome is in `result.status`; optional layout outcome is independent in
`result.layout_status`.

`ASS_RENDER_DETECT_CHANGE` requests the same comparison currently selected by passing a non-null
`detect_change` pointer. Without the flag, no comparison is performed and `change` is `-1`. Unknown flag
bits are rejected rather than silently pretending to provide behavior an older runtime does not know.

`ass_render_frame()` remains exported and delegates to the same internal render implementation. It
preserves existing image order and bytes, change classification, cache effects, event pruning, and
lifetime. A call with `detect_change == NULL` does not acquire extra comparison work.

### Request compatibility

The caller zero-initializes `ASS_RenderRequest`, sets `struct_size` to the size it compiled, and fills
the fields it needs.

- The library requires only a documented minimum prefix.
- It never reads beyond the supplied size.
- Missing tail fields have zero/default behavior.
- A larger request is accepted; unknown tail bytes are ignored.
- An explicitly set unknown flag is an invalid request.

The renderer stays a separate argument because it selects the state being operated on. Track and
timestamp live in the request because they describe this render operation and keep the function
signature stable as optional inputs grow.

### Result compatibility

The caller zero-initializes `ASS_RenderResult` and supplies writable capacity in `struct_size`. The
library first saves that capacity, clears every known result byte that fits, and then returns the size of
the result record understood by the runtime in `result.struct_size`.

The library never writes another field beyond the caller's original capacity. A caller reads a field
only when both its original capacity and the returned runtime size cover the complete field. This makes
later append-only growth safe in both directions:

- an old caller gives a new library a short buffer, so the library writes only that prefix;
- a new caller gives an old library a larger zeroed buffer, and the old library reports its shorter
  known size while leaving the unknown tail at zero/default.

The result record is caller-owned. `images` and `layout` point to renderer-owned payloads.

## Optional layout request

```c
enum {
    ASS_LAYOUT_INCLUDE_OUTLINES = 1u << 0,
    ASS_LAYOUT_UNBOUNDED = 1u << 1,
};

typedef struct ass_layout_request {
    size_t struct_size;
    unsigned flags;
    size_t max_events;
    size_t max_text_bytes;
    size_t max_units;
    size_t max_outlines;
    size_t max_outline_points;
    size_t max_bitmap_pixels;
} ASS_LayoutRequest;
```

A null `request.layout` disables collection and leaves `layout_status` as
`ASS_LAYOUT_NOT_REQUESTED`. This path performs no layout-record allocation or copying.

A non-null request is size-bounded like the render request. Bounded mode requires nonzero aggregate
limits for every collected resource. `ASS_LAYOUT_UNBOUNDED` is an explicit trusted-input mode; zero
initialization never selects it. `ASS_LAYOUT_INCLUDE_OUTLINES` lets callers omit the largest optional
geometry family when ranges and logical bounds are enough. Unknown flags are rejected.

Only records in the completed returned tree count against the named aggregate limits. Checked arithmetic
is required for byte and point totals. Once a limit or layout allocation fails, collection stops and the
partial tree is discarded while ordinary rendering continues.

## Layout result

The prototype uses renderer-owned linked records, matching `ASS_Image` and avoiding array-stride ABI
problems. The measured maximum-payload cost is reported below and is accepted for this opt-in prototype.
If maintainer constraints require a denser representation, the compatible alternative is counted views
with an explicit runtime element stride. A public contiguous array without runtime stride is not
proposed.

```c
typedef struct ass_layout_rect {
    double x, y, w, h;
} ASS_LayoutRect;

typedef struct ass_layout_outline {
    size_t struct_size;
    size_t point_count;
    size_t segment_count;
    ASS_DVector *points;
    char *segments;
    struct ass_layout_outline *next;
} ASS_LayoutOutline;

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
    char *text;
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

typedef struct ass_layout {
    size_t struct_size;
    ASS_LayoutUnitMode unit_mode;
    ASS_LayoutEvent *events;
} ASS_Layout;
```

The nested pointers follow the existing mutable-pointer style of `ASS_Image`, but all borrowed result
payload is read-only by contract. Each library-allocated record reports the size known by that runtime. Callers must check that size before
reading an appended field. `next` belongs to the stable minimum prefix so an older caller can traverse a
newer list. Incompatible representation changes require a new ABI contract rather than reinterpretation.

The prior metrics PoC also exposed run records. They are omitted here because the motivating consumers
can express their needs with event text, units, outlines, and event bitmap bounds. A demonstrated
authoring need could add a run output later through the same render result.

## Text and unit contract

Every event owns parsed logical UTF-8 and its byte length. `text_start` and `text_end` are half-open byte
offsets into that text.

Units are in logical-text order, not visual bidi order. They cover parsed text contiguously, including
line-break controls, whitespace trimmed from visible line ends, and drawing placeholders. A unit remains
present when it has no fill outline or final bitmap pixels. `line` is the zero-based line chosen by the
renderer, not a source-file line or cue number.

In `ASS_LAYOUT_UNIT_SHAPING_CLUSTER` mode, boundaries come from distinct shaping-cluster roots in logical
order. One unit may cover several Unicode scalar values and several outlines. In
`ASS_LAYOUT_UNIT_SIMPLE_SCALAR` mode, each unit covers one input scalar. The request does not change the
configured shaper; the result records which mode produced it.

## Geometry and ordering contract

All geometry uses the configured libass frame-pixel coordinate space.

- `logical_bounds` is pre-transform typographic geometry after wrapping and collision placement.
- `fill` contains transformed fill geometry, relative to the unit's `pos`, before clipping and raster
  effects.
- Event `bitmap_bounds` is the union of final emitted image rectangles for that event.

Events follow libass layer/read-order sorting. Units follow logical text order. Outlines preserve their
collector order. The API does not invent a separate per-unit paint-order index. A consumer handling
overlapping units within one event must choose a deterministic policy or use the available outline
geometry.

Clipping and compositing happen after fill outlines lose unit provenance, so the API does not attribute
final alpha-visible pixels to individual units. A consumer may intersect a semantic box with its
viewport; it must not describe that intersection as exact post-clip visibility.

## Lifetime and invalidation

`ASS_RenderResult` itself is caller storage. The image list and layout tree are immutable borrowed
payloads owned by `ASS_Renderer`. Both remain valid until the next call to either rendering entry point
on that renderer or until `ass_renderer_done()`.

Requesting layout therefore invalidates outputs from an earlier ordinary render, and an ordinary render
invalidates an earlier layout. Callers needing a longer lifetime must copy before the next render call.
The API does not add cross-thread or reentrant access to one renderer.

Event timing is copied into the layout. The result does not expose `ASS_Event *`: event pruning and
array movement can invalidate or retarget that pointer before a caller safely consumes it.

## Status and failure behavior

`ASS_RenderStatus` describes the ordinary render independently from layout:

- `ASS_RENDER_OK`: the render invocation ran; `images` may still be null when no image is active;
- `ASS_RENDER_INVALID_REQUEST`: the renderer/track relationship or requested behavior is invalid;
- `ASS_RENDER_NOT_READY`: required renderer state such as frame size or font selection is unavailable.

The implementation must not label every existing `ass_start_frame()` failure as allocation failure.
Envelope errors are additionally reported by the function's negative return because the result may be
too short to carry `status`.

Layout collection is atomic:

| Layout status | Layout pointer | Ordinary images |
| --- | --- | --- |
| `ASS_LAYOUT_NOT_REQUESTED` | null | normal result |
| `ASS_LAYOUT_OK` | complete tree | normal result |
| `ASS_LAYOUT_EMPTY` | null | normal result |
| `ASS_LAYOUT_LIMIT_EXCEEDED` | null | preserved |
| `ASS_LAYOUT_ALLOCATION_FAILED` | null | preserved |
| `ASS_LAYOUT_FAILED` | null | preserved |
| `ASS_LAYOUT_INVALID_REQUEST` | null | normal result when the render request itself is valid |

The library clears all writable result fields on every path before populating them. Reusing a result
record after an invalid, empty, or failed call cannot expose stale image or layout pointers.

The functions use libass's existing message callback and do not write diagnostics directly to stdout or
stderr.

## Implementation approach

The collector copies identity while the render pipeline still has it:

- parsed symbols provide the event text domain;
- shaping-cluster roots provide complex-mode text ranges;
- layout provides line assignment, logical geometry, and collision displacement;
- transformed glyph-fill outlines are copied before later raster stages discard unit provenance;
- final emitted image rectangles provide event bitmap bounds.

Tail cursors and monotonic line cursors keep work linear in emitted records. Layout-disabled rendering
does not execute collection work. Layout limit/allocation failure frees the complete partial tree and
does not alter ordinary image output.

## Compatibility and evolution

The public header must compile as C and C++, and the new symbol must be exported by every supported build
path. Compatible growth appends fields to size-bounded request/result or linked records. Exact
`struct_size == sizeof(...)` checks are forbidden.

The API does not claim that a new binary can call `ass_render_frame2` in a library predating the symbol.
Consumers detect that capability at build/link time. After the symbol exists, the request/result in/out
size rules support old-header/new-runtime and new-header/old-runtime combinations without out-of-bounds
access or false feature claims.

An incompatible change to an existing field's meaning, record traversal, or nested data representation
requires an explicit new ABI contract. A generic extension chain is not reserved speculatively; it can be
added later if independently developed extensions demonstrate a registry need.

## Evidence required from the implementation

The implementation is acceptable only if it proves:

- byte-for-byte and ordering equivalence between `ass_render_frame` and `ass_render_frame2` images;
- equivalent change detection, including no comparison when it is not requested;
- identical cache, pruning, and alternating old/new-call behavior;
- minimum, shorter, larger, guarded-tail, old-reader/new-writer, and new-reader/old-writer record cases;
- stale-result clearing on invalid, empty, failed, and successful calls;
- C and C++ header consumers;
- layout-disabled cost within a declared measurement noise threshold;
- Japanese, Arabic many-to-one shaping, combining marks, bidi, wrapping, escapes, drawings, collision,
  karaoke, transforms, simple shaping, clipping semantics, and simultaneous events;
- atomic bound/allocation failure with ordinary images preserved;
- maximum bounded payload with linear work and exact cleanup;
- a 10,000-render replacement/teardown stress sequence distinct from a single large frame;
- execution of the tests in both preferred Autotools and Meson paths.

The current prototype's maximum accepted mpv request contains 4,096 units, 4,096 fill-outline records,
and 45,056 outline points. On the Linux test host, a cold render took 9.066 ms and 100 warm renders
averaged 4.523 ms with layout versus 2.654 ms without it, a 1.869 ms delta. The process peak RSS for the
standalone run was 13,300 KiB. These figures are feasibility evidence for retaining linked records, not
a portable performance guarantee or an ABI limit.

The existing mpv prototype is a consumer adequacy check: it must still obtain all information needed for
ASS and libass-converted SubRip without reserved colors, color-coded IDs, a second render, or private
libass pointers.

## Alternatives considered

### Feature-specific render entry points

Rejected. They multiply with output families and make composition of optional outputs awkward.

### A separate metrics render

Rejected. It duplicates work and can produce different wrapping, cache state, collision placement, or
animation state from the images a caller displays.

### Library-allocated expandable `ASS_RenderResult`

Rejected for this proposal. A newer caller can address an appended field beyond an older runtime's
shorter allocation unless every access is guarded by runtime size. Caller-owned bounded storage makes
the write boundary explicit and still leaves nested image/layout payloads renderer-owned.

### Generic typed attachments or `pNext`

Deferred. A `{type, void *}` attachment is less statically typed than named fields and permanently
freezes tag, stride, duplicate, and direction rules. Vulkan-style chains add validation and registry
machinery without a demonstrated independent-extension ecosystem.

### Opaque refcounted result plus accessors

Viable if queued retention becomes a primary requirement. It adds allocation/lifecycle and getter
surface not needed by the current synchronous consumers. A future retain/clone API remains additive.

### Callback streaming

Rejected for the first contract. It complicates atomic whole-layout failure and consumers that need the
completed event/unit hierarchy.

### One record per glyph or Unicode scalar

Rejected. Shaping can map several scalar values to one positioned unit and one input range to several
glyph outlines.

### Add identity to `ASS_Image`

Rejected. Final image fragments no longer retain a complete event-text and shaping relationship, and a
single event or unit can contribute several images.

### Post-clip per-unit pixels

Out of scope. Exact unit-attributed visibility would require preserving identity through clipping,
blur, border, shadow, karaoke masks, and composition. Semantic geometry already supports the motivating
selection and inspection uses.

## Out of scope

- tokenization into words or application concepts;
- stable event identity across render calls;
- exact per-unit final alpha coverage;
- input-source byte offsets before ASS parsing;
- predictions stable across renderer versions or configuration;
- packed RGBA output from issue #73;
- renderer result retention or queued-frame ownership;
- any dependency on mpv or a particular client transport.

## Questions for maintainers

1. Does caller-owned request/result storage fit the intended meaning of the suggested centralized entry
   point, or is a library-owned opaque result preferable?
2. Are `ass_render_frame2`, `ASS_RenderRequest`, and `ASS_RenderResult` acceptable working names?
3. Do the measured bounded linked records fit libass conventions, or is a runtime-stride view preferable
   despite the added traversal contract?
4. Are logical bounds plus optional pre-clip fill outlines and event bitmap bounds the right geometry
   boundary?
5. Does explicit bounded versus trusted-unbounded collection fit libass's expected callers?

## Cross-project adoption

The libass capability is independently useful. mpv is one proposed consumer, but the data model and
lifetime do not depend on mpv. The projects can review their boundaries separately: libass owns render
semantics and borrowed output; mpv owns retained snapshots, coordinate provenance, invalidation, and
client transport.
