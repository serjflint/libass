# The libpng-based comparison program stays behind the `test` option; the
# public-API tests below need neither it nor libpng, and gating them there
# meant a default meson build exercised none of ass_render_frame2.
if get_option('test').enabled()
    libass_test = executable(
        'libass_test',
        files('test.c'),
        install: false,
        include_directories: incs,
        dependencies: deps + png_deps,
        link_with: libass_for_tools,
    )
endif

render_api_test = executable(
    'render_api_test',
    'render_api.c',
    install: false,
    include_directories: incs,
    dependencies: deps,
    link_with: libass_for_tools,
    c_args: ['-DASS_TEST_FONT="' + join_paths(meson.project_source_root(),
                                              'compare/test/font1.ttf') + '"'],
)

test('render api', render_api_test)

render_lifetime_test = executable(
    'render_lifetime_test',
    'render_lifetime.c',
    install: false,
    include_directories: incs,
    dependencies: deps,
    link_with: libass_for_tools,
    c_args: ['-DASS_TEST_FONT="' + join_paths(meson.project_source_root(),
                                              'compare/test/font1.ttf') + '"'],
)

test('render lifetime', render_lifetime_test)

if fontconfig
    subtitle_layout_test = executable(
        'subtitle_layout_test',
        'subtitle_layout.c',
        install: false,
        include_directories: incs,
        dependencies: deps,
        link_with: libass_for_tools,
    )

    test('subtitle-layout', subtitle_layout_test)
endif
