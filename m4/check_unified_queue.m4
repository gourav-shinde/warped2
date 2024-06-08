dnl Check for debug build

dnl Usage: CHECK_UNIFIED_QUEUE

AC_DEFUN([CHECK_UNIFIED_QUEUE],
[
    AC_ARG_WITH([unified-queue],
                [AS_HELP_STRING([--with-unified-queue], [Use unified queue as scheduler])],
                [want_unifiedarq="$withval"],
                [want_unifiedarq="no"])

    AS_IF([test "x$want_unifiedarq" = "xyes"],
          [AC_DEFINE([UNIFIED_QUEUE], [], [Use unified queue as scheduler])],
          [])

]) dnl end CHECK_UNIFIED_QUEUE

