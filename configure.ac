#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.68])
AC_INIT([ctntad], [0.1], [linux@cetoncorp.com])
AM_INIT_AUTOMAKE()
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_HEADERS([config.h])

m4-ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_CONFIG_FILES(Makefile
                 src/Makefile)

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
PKG_CHECK_MODULES(GUPNP, gupnp-1.0)
PKG_CHECK_MODULES(GTHREAD, gthread-2.0)

# Checks for header files.
AC_CHECK_HEADERS(stdlib.h)

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.

AC_OUTPUT
