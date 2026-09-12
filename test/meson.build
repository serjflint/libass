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

layout_contract_test = executable(
    'layout_contract_test',
    'layout_contract.c',
    install: false,
    include_directories: incs,
    dependencies: deps,
    link_with: libass_for_tools,
    c_args: ['-DASS_TEST_FONT="' + join_paths(meson.project_source_root(),
                                              'compare/test/font1.ttf') + '"'],
)

test('layout contract', layout_contract_test)

# Reaches ass_test_set_alloc_countdown, which is internal and deliberately
# absent from libass.sym. Autotools links it against libass_internal.la; meson
# has no such convenience library, so it takes libass_for_tools and relies on
# the shared build exporting internals -- which is true everywhere except
# Windows, where the generated .def is honored. Skip it there rather than
# exporting a test-only symbol to make one build system happy.
if default_library != 'shared' or host_system != 'windows'
    layout_alloc_test = executable(
        'layout_alloc_test',
        'layout_alloc.c',
        install: false,
        include_directories: incs,
        dependencies: deps,
        link_with: libass_for_tools,
        c_args: ['-DASS_TEST_FONT="' + join_paths(meson.project_source_root(),
                                                  'compare/test/font1.ttf') + '"'],
    )

    test('layout alloc', layout_alloc_test)
endif

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
