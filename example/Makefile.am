bin_PROGRAMS = okvm 

nodist_okvm_SOURCES = okvm.tpp.cpp
$(okvm_SOURCES: .cpp=.o):okvm.tpp.cpp

okvm.tpp.cpp:
	/tang/bin/tpp --no-register --redis --cppout=. okvm.tpp

okvm_SOURCES = okvm.cpp

okvm_LDADD = $(top_builddir)/src/libhiredis_okvm.la
okvm_CPPFLAGS = -I$(top_builddir)/src/ $(LIBUV_CFLAGS) $(HIREDIS_CFLAGS)
okvm_LDFLAGS = $(LIBUV_LIBS) $(HIREDIS_LIBS)

#AM_CFLAGS = 
#AM_LDFLAGS = 

#EXTRA_DIST = 
clean-local:
	@-rm *.tpp.*

