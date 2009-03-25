#
# Makefile.am for MAFW internet radio source library.
#
# Author: Visa Smolander <visa.smolander@nokia.com>
#
# Copyright (C) 2007, 2008, 2009 Nokia. All rights reserved.

TESTS				= test-iradio-source

check_PROGRAMS			= $(TESTS)
noinst_PROGRAMS			= $(TESTS)

AM_CFLAGS			= $(_CFLAGS)
AM_LDFLAGS			= $(_LDFLAGS)

test_iradio_source_SOURCES	= test-iradio-source.c

AM_CPPFLAGS			= $(CHECKMORE_CFLAGS) \
				  $(GOBJECT_CFLAGS) \
				  $(MAFW_CFLAGS) \
				  -I$(top_srcdir) -g

LDADD				= $(CHECKMORE_LIBS) \
				  $(GOBJECT_LIBS) \
				  $(MAFW_LIBS) \
				  $(top_builddir)/iradio-source/mafw-iradio-source.la

EXTRA_DIST			= test-bookmarks.xml

CLEANFILES			= $(BUILT_SOURCES) $(TESTS) \
				  test-iradiosource.db *.gcno *.gcda
DISTCLEANFILES			= $(BUILT_SOURCES) $(TESTS)
MAINTAINERCLEANFILES		= Makefile.in $(BUILT_SOURCES) $(TESTS)

# Runs valgrind on tests.
vg: $(TESTS)
	for test in $^; do \
		G_DEBUG="always-malloc" \
		libtool --mode=execute valgrind --leak-check=full --suppressions=test.suppressions\
			$$test 2> vglog.$$test; \
	done
	-rm -f vgcore.*
