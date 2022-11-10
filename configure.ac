m4_define([ctntad_major_version], [0])
m4_define([ctntad_minor_version], [2])
m4_define([ctntad_micro_version], [1])
m4_define([ctntad_version],
          [ctntad_major_version.ctntad_minor_version.ctntad_micro_version])

AC_PREREQ([2.63])
AC_INIT([ctntad], [ctntad_version], [linux@cetoncorp.com])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])

AM_INIT_AUTOMAKE()
m4-ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_CONFIG_FILES(Makefile
                 src/Makefile)


LT_PREREQ([2.2.6])
LT_INIT()


# Checks for programs.
AC_PROG_CC

# Checks for libraries.
PKG_CHECK_MODULES(GUPNP, gupnp-1.2)
PKG_CHECK_MODULES(GSSDP, gssdp-1.2)
PKG_CHECK_MODULES(GIO, gio-2.0)
PKG_CHECK_MODULES(GTHREAD, gthread-2.0)
PKG_CHECK_MODULES(GUSB, gusb >= 0.1.4 )

# Checks for header files.
AC_CHECK_HEADERS(stdlib.h)

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.

AC_OUTPUT
