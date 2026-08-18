ACLOCAL_AMFLAGS = -I m4
AM_CPPFLAGS = -I$(top_srcdir)/libass

# Using gnu99 to gain compatibility between const and
# non-const array types without needing to jump to ISO C23.
# Otherwise intended to be compatible with ISO C.
#
# Similarly we only strictly _need_ ISO C APIs, but we’ll use
# some additional APIs from e.g. GNU and POSIX when available.
# Unfortunately defining _POSIX_C_SOURCE may end up hiding additional
# non-POSIX interfaces even if their respective feature macro is set too.
# However, POSIX interfaces are usually implicitly enabled
# with other feature macros, thus omit the POSIX one.
AM_CFLAGS = -std=gnu99 -D_GNU_SOURCE -D_XPLATFORM_SOURCE

EXTRA_DIST = libass.pc.in Changelog MAINTAINERS RELEASEVERSION ltnasm.sh

# Source of pre-generated C files
EXTRA_DIST += legacy_arabic_cmap/ArabicPUASimplified.txt \
              legacy_arabic_cmap/ArabicPUATraditional.txt \
              legacy_arabic_cmap/gen_arabic_charmap.py

# Meson build setup
EXTRA_DIST += gen_defs.py meson_options.txt meson.build \
              libass/meson.build libass/ass/meson.build \
              fuzz/meson.build checkasm/meson.build \
              compare/meson.build \
              profile/meson.build test/meson.build \
              test/render_api.c test/render_lifetime.c \
              test/subtitle_layout.c \
              compare/test/font1.ttf

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libass.pc

nasm_verbose = $(nasm_verbose_$(V))
nasm_verbose_ = $(nasm_verbose_$(AM_DEFAULT_VERBOSITY))
nasm_verbose_0 = @echo "  NASM    " $@;

lib_LTLIBRARIES =
noinst_LTLIBRARIES =
noinst_PROGRAMS =
check_PROGRAMS =
TESTS =

include libass/Makefile_library.am
include Makefile_util.am

# No -static here, unlike the programs in Makefile_util.am: those link the
# always-static libass_internal.la, while these link the public libass.la and
# must therefore resolve only against libass.sym. -static also breaks them in
# a --disable-static build.
check_PROGRAMS += test/render_api
TESTS += test/render_api$(EXEEXT)
test_render_api_SOURCES = test/render_api.c
test_render_api_CPPFLAGS = $(AM_CPPFLAGS) \
    -DASS_TEST_FONT='"$(abs_top_srcdir)/compare/test/font1.ttf"'
test_render_api_LDADD = libass/libass.la
test_render_api_LDFLAGS = $(AM_LDFLAGS)

check_PROGRAMS += test/render-lifetime
TESTS += test/render-lifetime$(EXEEXT)
test_render_lifetime_SOURCES = test/render_lifetime.c
test_render_lifetime_CPPFLAGS = $(AM_CPPFLAGS) \
    -DASS_TEST_FONT='"$(abs_top_srcdir)/compare/test/font1.ttf"'
test_render_lifetime_LDADD = libass/libass.la
test_render_lifetime_LDFLAGS = $(AM_LDFLAGS)

if FONTCONFIG
check_PROGRAMS += test/subtitle-layout
TESTS += test/subtitle-layout$(EXEEXT)
test_subtitle_layout_SOURCES = test/subtitle_layout.c
test_subtitle_layout_LDADD = libass/libass.la
test_subtitle_layout_LDFLAGS = $(AM_LDFLAGS)
endif

# Tests
if ENABLE_FUZZ
check: check-art-fuzz
.PHONY: check-art-fuzz
check-art-fuzz: fuzz/fuzz
	@if [ -z '$(ART_SAMPLES)' ] ; then \
		echo "ART_SAMPLES location not set; cannot run regression tests!"; \
	else \
		cd '$(ART_SAMPLES)'/crash/ ; \
		./run-all.sh '$(abs_top_builddir)'/fuzz/fuzz ; \
	fi
endif

if ENABLE_COMPARE
check: check-art-compare
.PHONY: check-art-compare
check-art-compare: compare/compare
	@if [ -z '$(ART_SAMPLES)' ] ; then \
		echo "ART_SAMPLES location not set; cannot run regression tests!"; \
	else \
		cd '$(ART_SAMPLES)'/regression/ ; \
		./run-all.sh '$(abs_top_builddir)'/compare/compare ; \
	fi
endif
