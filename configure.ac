dnl Process this file with autoconf to produce a configure script.

define([svnversion], esyscmd([sh -c "LANG=en_US.UTF-8 LANGUAGE=en_US.UTF-8 svnversion|sed -n s/^\(.*\)[[:blank:]]\+.*/\1/p| sed s/:/./g| sed s/M//g |tr -d '\n'"])) 

dnl Initialization
AC_INIT([hiredis-okvm], [1.0.svnversion], [root@gnetis.com])
AC_CONFIG_AUX_DIR(auxdir)
AM_INIT_AUTOMAKE
AM_MAINTAINER_MODE

dnl Package information
AC_CONFIG_MACRO_DIR([m4])

dnl Checks for programs.
AC_PROG_CC
AC_PROG_CXX
AC_PROG_INSTALL
AC_PROG_LIBTOOL
AC_HEADER_STDC

dnl Checks for libraries.
dnl AC_CHECK_LIB(LIBRARY, FUNCT, [ACT-IF-FOUND], [ACT-IF-NOT])
dnl Check header
dnl AC CHECK HEADERS(HEADERS...)
dnl
dnl Checks for typedefs, structures, and compiler characteristics.
dnl
dnl Checks for library functions.

dnl Check module with PKG_CONFIG
PKG_CHECK_MODULES([LIBUV],[libuv])
PKG_CHECK_MODULES([HIREDIS],[hiredis])

AC_CHECK_HEADERS([syslog.h])
dnl Final Output
AC_OUTPUT([
Makefile src/Makefile test/Makefile
src/hiredis_okvm.pc
])

