lib_LTLIBRARIES  = libhiredis_okvm.la

libhiredis_okvm_la_SOURCES = hiredis_okvm.c \
							 hiredis_okvm_thread.c
#libhiredis_okvm_la_LDADD = 
libhiredis_okvm_la_CFLAGS = $(LIBUV_CFLAGS) $(HIREDIS_CFLAGS)
libhiredis_okvm_la_LDFLAGS = -version-info 0:0:0 $(LIBUV_LIBS) $(HIREDIS_LIBS)

libhiredis_okvmincdir = @includedir@/hiredis_okvm
libhiredis_okvminc_HEADERS = hiredis_okvm.h

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = hiredis_okvm.pc
#AM_CFLAGS = 
#AM_LDFLAGS = 

EXTRA_DIST = hiredis_okvm.pc.in
