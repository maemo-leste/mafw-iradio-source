#
# Makefile.am for MAFW internet radio source library.
#
# Author: Visa Smolander <visa.smolander@nokia.com>
#
# Copyright (C) 2007, 2008, 2009 Nokia. All rights reserved.

mafwext_LTLIBRARIES		= mafw-iradio-source.la

mafw_iradio_source_la_LIBADD	= $(GOBJECT_LIBS) \
				  $(MAFW_LIBS)
mafw_iradio_source_la_CPPFLAGS	= $(GOBJECT_CFLAGS) \
				  $(MAFW_CFLAGS) \
				  $(_CFLAGS)
mafw_iradio_source_la_LDFLAGS	= -module -avoid-version $(_LDFLAGS)
noinst_HEADERS			= mafw-iradio-source.h
mafw_iradio_source_la_SOURCES	= mafw-iradio-source.c \
				  mafw-iradio-source.h \
				  mafw-iradio-source-plugin.c \
				  mafw-iradio-vendor-setup.c \
				  mafw-iradio-vendor-setup.h

mafwextdir			= $(plugindir)

CLEANFILES			= *.gcno *.gcda
MAINTAINERCLEANFILES		= Makefile.in
