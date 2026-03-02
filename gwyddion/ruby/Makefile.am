# $Id: Makefile.am 20680 2017-12-18 18:29:14Z yeti-dn $

Gwyddion_RUBY = \
	gwyddion/dump.rb

EXTRA_DIST = $(Gwyddion_RUBY)

rubylibdir = $(pkglibdir)/ruby/gwyddion

if HAVE_RUBY
rubylib_DATA = $(Gwyddion_RUBY)
endif

clean-local:
	rm -f *~ *.bak

