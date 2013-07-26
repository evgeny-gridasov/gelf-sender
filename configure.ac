dnl Process this file with autoconf to produce a configure script.
AC_PREREQ(2.59)
AC_INIT(gelf-sender, 1.0, evgeny.gridasov@gmail.com)


AC_CANONICAL_SYSTEM
AM_INIT_AUTOMAKE()

AC_PROG_CC


AC_CONFIG_FILES(Makefile src/Makefile)

AC_CHECK_LIB(z, deflate,,AC_MSG_ERROR([zlib is required]))

case "$target_os" in
mingw*)
  AC_CHECK_LIB(ws2_32, _head_libws2_32_a,,
      AC_CHECK_LIB(ws2_32, _head_lib32_libws2_32_a,,
          AC_MSG_ERROR([winsock2 is required])
  ))
  AC_CHECK_LIB(json-c, json_object_new_object,,AC_MSG_ERROR([json-c is required]))
  LDFLAGS="$LDFLAGS -static"
  ;;
*)
  AC_CHECK_LIB(json, json_object_new_object)
  ;;
esac;

AC_OUTPUT

