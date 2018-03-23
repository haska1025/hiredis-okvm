ACLOCAL_AMFLAGS =-I m4

SUBDIRS = src test
DIST_SUBDIRS= src test

include doxygen.am
MOSTLYCLEANFILES = $(DX_CLEANFILES)


EXTRA_DIST = $(DX_CONFIG) 
miscdir = $(datadir)/doc/@PACKAGE@-@VERSION@
misc_DATA = README COPYING

install-data-local: doxygen-doc
	@$(INSTALL) -d $(DESTDIR)$(miscdir)
	@cp -r -f doxygen-doc $(DESTDIR)$(miscdir)

uninstall-local:
	@rm -rf $(DESTDIR)$(miscdir)/doxygen-doc

distclean-local:
	@rm -rf *.cache *~

clean-local:
	@-rm -rf doxygen-doc

