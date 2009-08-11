# ***** BEGIN LICENSE BLOCK *****
# Version: MPL 1.1/GPL 2.0/LGPL 2.1
#
# The contents of this file are subject to the Mozilla Public License Version
# 1.1 (the "License"); you may not use this file except in compliance with
# the License. You may obtain a copy of the License at
# http://www.mozilla.org/MPL/
#
# Software distributed under the License is distributed on an "AS IS" basis,
# WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
# for the specific language governing rights and limitations under the
# License.
#
# The Initial Developer of the Original Code is
# Annodex Association
# Portions created by the Initial Developer are Copyright (C) 2007
# the Initial Developer. All Rights Reserved.
#
# Contributor(s): Jan Gerber
#                 Silvia Pfeiffer
#
# Alternatively, the contents of this file may be used under the terms of
# either the GNU General Public License Version 2 or later (the "GPL"), or
# the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
# in which case the provisions of the GPL or the LGPL are applicable instead
# of those above. If you wish to allow use of your version of this file only
# under the terms of either the GPL or the LGPL, and not to allow others to
# use your version of this file under the terms of the MPL, indicate your
# decision by deleting the provisions above and replace them with the notice
# and other provisions required by the GPL or the LGPL. If you do not delete
# the provisions above, a recipient may use your version of this file under
# the terms of any one of the MPL, the GPL or the LGPL.
#
# ***** END LICENSE BLOCK ***** *

# Process this file with autoconf to produce a configure script.
AC_INIT(libsydneyaudio, 0.0.1)
AC_CONFIG_SRCDIR([src/sydney_audio_alsa.c])

AC_PREREQ(2.53)

AC_CANONICAL_TARGET

AM_INIT_AUTOMAKE
AM_CONFIG_HEADER(config.h)

SHARED_VERSION_INFO="1:0:0"
SHLIB_VERSION_ARG=""

# Checks for programs
AC_PROG_CC
AC_PROG_CXX
AC_PROG_LIBTOOL
AC_PROG_INSTALL
AC_PROG_MAKE_SET

AC_C_CONST
AC_C_BIGENDIAN

dnl Add parameters for aclocal
AC_SUBST(ACLOCAL_AMFLAGS, "-I m4")

dnl Overall configuration success flag
libsydneyaudio_config_ok=yes

dnl
dnl Configure for target platform
dnl

HAVE_APPLE=no
case "$target_os" in
  linux* | solaris*)
    SHLIB_VERSION_ARG="-Wl,--version-script=Version_script"
    AC_SUBST(PLATFORM,"linux")
    ;;
  darwin* | rhapsody*)
	APPLE_LIBS="-framework CoreAudio -framework AudioUnit -framework AudioToolbox"
	AC_SUBST(APPLE_LIBS)
	AUDIO_BACKEND=apple
	;;
  *)
    ;;
esac

dnl
dnl  Detect alsa
dnl
PKG_CHECK_MODULES(ALSA, alsa, HAVE_ALSA=yes, HAVE_ALSA=no)
AC_SUBST(ALSA_CFLAGS)
AC_SUBST(ALSA_LIBS)


dnl
dnl  pulseaudio backend configure option and detect
dnl
AC_ARG_WITH(pulse,
[  --with-pulse            Use Pulseaudio backend],
[ case "$withval" in
  no)
    HAVE_PULSE=no
    ;;
  *)
    PKG_CHECK_MODULES(PULSE, libpulse, HAVE_PULSE=yes, HAVE_PULSE=no)
    ;;
  esac]
)
AC_SUBST(PULSE_CFLAGS)
AC_SUBST(PULSE_LIBS)

dnl
dnl detect sun audio
dnl
AC_CHECK_HEADER(sys/audioio.h, HAVE_SUN=yes, HAVE_SUN=no)

dnl
dnl detect apple coreaudio
dnl
AC_CHECK_HEADER(sys/audioio.h, HAVE_SUN=yes, HAVE_SUN=no)

dnl
dnl  alsa backend configure option
dnl
AC_ARG_WITH(alsa,
[  --with-alsa             Use ALSA audio backend],
[ case "$withval" in
  no)
    ;;
  *)
    HAVE_ALSA=yes
    HAVE_PULSE=no
    ;;
  esac]
)

dnl
dnl  oss backend configure option
dnl
AC_CHECK_HEADER(sys/soundcard.h, HAVE_OSS=yes, HAVE_OSS=no)
AC_ARG_WITH(oss,
[  --with-oss              Use OSS audio backend],
[ case "$withval" in
  no)
    ;;
  *)
    HAVE_ALSA=no
    HAVE_PULSE=no
    ;;
  esac]
)

if test "x$HAVE_OSS" = "xyes"; then
  AUDIO_BACKEND=oss
fi
if test "x$HAVE_ALSA" = "xyes"; then
  AUDIO_BACKEND=alsa
fi
if test "x$HAVE_PULSE" = "xyes"; then
  AUDIO_BACKEND=pulse
fi
if test "x$HAVE_SUN" = "xyes"; then
  AUDIO_BACKEND=sun
fi

AM_CONDITIONAL(HAVE_OSS, test "x$AUDIO_BACKEND" = "xoss")
AM_CONDITIONAL(HAVE_ALSA, test "x$AUDIO_BACKEND" = "xalsa")
AM_CONDITIONAL(HAVE_PULSE, test "x$AUDIO_BACKEND" = "xpulse")
AM_CONDITIONAL(HAVE_SUN, test "x$AUDIO_BACKEND" = "xsun")
AM_CONDITIONAL(HAVE_APPLE, test "x$AUDIO_BACKEND" = "xapple")

dnl
dnl  Detect pthreads libraries
dnl

PTHREAD_LIBS=error
AC_CHECK_LIB(pthread, pthread_attr_init, PTHREAD_LIBS="-lpthread")
if test "x$PTHREAD_LIBS" = xerror; then
        AC_CHECK_LIB(pthreads, pthread_attr_init, PTHREAD_LIBS="-lpthreads")
fi
if test "x$PTHREAD_LIBS" = xerror; then
        AC_CHECK_LIB(c_r, pthread_attr_init, PTHREAD_LIBS="-lc_r")
fi
if test "x$PTHREAD_LIBS" = xerror; then
        AC_CHECK_FUNC(pthread_attr_init, PTHREAD_LIBS="")
fi
AC_SUBST(PTHREAD_LIBS)

SEMAPHORE_LIBS="-lpthread"
AC_SUBST(SEMAPHORE_LIBS)

dnl Use -Wall if we have gcc.
dnl changequote(,)dnl
if test "x$ac_cv_prog_gcc" = xyes ; then
  CFLAGS="$CFLAGS -Wall -g"
fi
dnl changequote([,])dnl


dnl
dnl  Configuration tests complete -- provide summary of results.
dnl

if test $libsydneyaudio_config_ok = no ; then

AC_MSG_RESULT([
**************************************************************

*** $PACKAGE $VERSION: Automatic configuration FAILED.

*** The file config.log has full details.

*** The following required libraries are missing or
*** misconfigured on your system:
])


AC_MSG_RESULT(
[*** If you install the required libraries from source, you
*** need to inform the dynamic linker of their location. If
*** you install them in a system-wide directory such as
*** /usr/local (the default), you must ensure that
*** /usr/local/lib is listed in /etc/ld.so.conf, then run
*** ldconfig to update the dynamic linking system.
*** Alternatively, you can set your LD_LIBRARY_PATH environment
*** variable to include the library installation directory.
])


AC_MSG_RESULT(
[**************************************************************
])


AC_MSG_ERROR([
***
*** After fixing the above problems, you must run ./configure again.
***
])

else

AC_SUBST(SHLIB_VERSION_ARG)
AC_SUBST(SHARED_VERSION_INFO)


AS_AC_EXPAND(LIBDIR, ${libdir})
AS_AC_EXPAND(INCLUDEDIR, ${includedir})
AS_AC_EXPAND(BINDIR, ${bindir})

AC_OUTPUT([
Makefile
src/Makefile
include/Makefile
sydneyaudio.pc
sydneyaudio-uninstalled.pc
])

AC_MSG_RESULT([
------------------------------------------------------------------------
  $PACKAGE $VERSION:  Automatic configuration OK.

    install path : .................. ${LIBDIR}
    audio backend: .................. ${AUDIO_BACKEND}

  Building:

    Type 'make' to compile $PACKAGE.

    Type 'make install' to install $PACKAGE.

------------------------------------------------------------------------
])

fi
