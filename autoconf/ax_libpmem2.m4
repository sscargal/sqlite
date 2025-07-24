#serial 1

AC_DEFUN([AX_LIBPMEM2], [
  AC_ARG_WITH([libpmem2],
    [AS_HELP_STRING([--with-libpmem2], [use libpmem2 for persistent memory support])],
    [],
    [with_libpmem2=auto])

  if test "x$with_libpmem2" != "xno"; then
    AC_CHECK_HEADER([libpmem2.h], [have_libpmem2_h=yes], [have_libpmem2_h=no])
    AC_CHECK_LIB([pmem2], [pmem2_source_from_fd], [have_libpmem2_lib=yes], [have_libpmem2_lib=no])
    if test "$have_libpmem2_h" = yes && test "$have_libpmem2_lib" = yes; then
      AC_DEFINE([SQLITE_HAVE_LIBPMEM2], 1, [Define if libpmem2 is available])
      LIBS="$LIBS -lpmem2"
      AC_SUBST([HAVE_LIBPMEM2], [1])
    else
      AC_SUBST([HAVE_LIBPMEM2], [0])
    fi
  else
    AC_SUBST([HAVE_LIBPMEM2], [0])
  fi
])
