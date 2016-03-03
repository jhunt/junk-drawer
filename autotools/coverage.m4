AC_DEFUN_ONCE([AX_TEST_COVERAGE], [
  AC_ARG_ENABLE(coverage,
    AS_HELP_STRING([--enable-coverage],
    [enable code coverage reporting via gcov/lcov]),
    [test_coverage=yes], [test_coverage=no])

  AM_CONDITIONAL(WANT_COVERAGE, test "x$test_coverage" == "xyes")

  if test "x$GCC" != "xyes"; then
    AC_MSG_ERROR([GCC is required for --enable-coverage])
  fi

  AC_CHECK_PROG(LCOV, lcov, lcov)
  if test -z "$LCOV"; then
    AC_MSG_ERROR([Could not find lcov])
  fi

  AC_CHECK_PROG(GENHTML, genhtml, genhtml)
  if test -z "$GENHTML"; then
    AC_MSG_ERROR([Could not find genhtml from the lcov package])
  fi

  if test "x$WANT_COVERAGE" == "xyes"; then
    AC_MSG_NOTICE([coverage enabled, adding required options to CFLAGS/LDFLAGS])

    dnl Add the special gcc flags
    AS_COMPILER_FLAGS(COVERAGE_CFLAGS, "--coverage -DDEBUG")
    COVERAGE_LDFLAGS="--coverage -lgcov"
    AC_SUBST(COVERAGE_CFLAGS)
    AC_SUBST(COVERAGE_LDFLAGS)

    AC_DEFINE_UNQUOTED(GCOV_ENABLED, 1,
      [Defined if gcov is enabled to force a rebuild due to config.h changing])

     dnl Force the user to turn off optimization
     AC_MSG_NOTICE([coverage enabled, adding "-g -O0" to CFLAGS])
       AS_COMPILER_FLAGS(CFLAGS, "-g -O0")
  fi
]) dnl AX_TEST_COVERAGE
