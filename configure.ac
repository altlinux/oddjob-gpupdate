AC_INIT([oddjob-gpupdate],[0.2.2])
AC_PREREQ([2.69])
PACKAGE_NAME_CAPS=`echo $PACKAGE_NAME | tr '[a-z]' '[A-Z]'`
AC_DEFINE_UNQUOTED(PACKAGE_NAME_CAPS,"$PACKAGE_NAME_CAPS",[Define to the package name, in caps.])
AC_SUBST(PACKAGE_NAME_CAPS)
AM_INIT_AUTOMAKE([foreign])
AC_CONFIG_MACRO_DIR([m4])
AC_PROG_CC
AM_MAINTAINER_MODE

AC_DEFINE(_GNU_SOURCE,1,[Use GNU libc extensions.])

LT_INIT

DBUS_PACKAGE=dbus-1
PKG_CHECK_MODULES(DBUS,$DBUS_PACKAGE)
PKG_CHECK_MODULES(XML,libxml-2.0)

AC_MSG_CHECKING([D-BUS version])
DBUS_VERSION=`pkg-config --modversion $DBUS_PACKAGE`
DBUS_PREFIX=`pkg-config --modversion $DBUS_PACKAGE`
DBUS_MAJOR_VERSION=`echo $DBUS_VERSION | cut -f1 -d.`
DBUS_MINOR_VERSION=`echo $DBUS_VERSION | cut -f2 -d.`
DBUS_MICRO_VERSION=`echo $DBUS_VERSION | cut -f3 -d.`
if test x$DBUS_MAJOR_VERSION = x ; then
	DBUS_MAJOR_VERSION=0
fi
if test x$DBUS_MINOR_VERSION = x ; then
	DBUS_MINOR_VERSION=0
fi
if test x$DBUS_MICRO_VERSION = x ; then
	DBUS_MICRO_VERSION=0
fi
AC_MSG_RESULT([pkg-config reports $DBUS_VERSION, treating as ${DBUS_MAJOR_VERSION}.${DBUS_MINOR_VERSION}.${DBUS_MICRO_VERSION}])
AC_DEFINE_UNQUOTED(ODDJOB_DBUS_MAJOR_VERSION,$DBUS_MAJOR_VERSION,[Define to the major version of D-BUS with which oddjob is being compiled.])
AC_DEFINE_UNQUOTED(ODDJOB_DBUS_MINOR_VERSION,$DBUS_MINOR_VERSION,[Define to the minor version of D-BUS with which oddjob is being compiled.])
AC_DEFINE_UNQUOTED(ODDJOB_DBUS_MICRO_VERSION,$DBUS_MICRO_VERSION,[Define to the micro version of D-BUS with which oddjob is being compiled.])
AC_DEFINE_UNQUOTED(DBUS_API_SUBJECT_TO_CHANGE,"Then finish it!",[Define to allow use of DBus development versions.])

DBUS_INTERFACE_DBUS=org.freedesktop.DBus
DBUS_INTROSPECTION_INTERFACE=${DBUS_INTERFACE_DBUS}.Introspectable
DBUS_INTROSPECTION_METHOD=Introspect
AC_SUBST(DBUS_INTROSPECTION_INTERFACE)
AC_SUBST(DBUS_INTROSPECTION_METHOD)

pam=yes
AC_CHECK_HEADERS(security/pam_modules.h)
savedLDFLAGS="$LDFLAGS"
LDFLAGS=
savedLIBS="$LIBS"
LIBS=
AC_CHECK_FUNCS([pam_get_user pam_get_item],,[AC_CHECK_LIB(pam,pam_get_user,,pam=no)])
PAM_LIBS="$LIBS"
LIBS="$savedLIBS"
LDFLAGS="$savedLDFLAGS"
AC_SUBST(PAM_LIBS)
AM_CONDITIONAL(BUILD_PAM,test x$pam = xyes)

dnl Oh, for the clear management of OID arcs....
NAMESPACE=ru.basealt
AC_SUBST(NAMESPACE)
NAMESPACEPATH=`echo "$NAMESPACE" | sed -e 's,^,/,' -e 's,\.,/,g'`
AC_SUBST(NAMESPACEPATH)
AC_DEFINE_UNQUOTED(ODDJOB_NAMESPACE,"$NAMESPACE",[Define to the namespace in which the oddjob service resides.])
AC_DEFINE_UNQUOTED(ODDJOB_NAMESPACE_PATH,"$NAMESPACEPATH",[Define to the prefix of the object paths used by oddjob.])

DEFAULT_FAST_RECONNECT_ATTEMPTS=10
AC_DEFINE_UNQUOTED(DEFAULT_FAST_RECONNECT_ATTEMPTS,$DEFAULT_FAST_RECONNECT_ATTEMPTS,[Define to the number of times oddjobd will attempt a "fast reconnect", before switching to waiting for the full reconnect timeout between attempts.])
AC_SUBST(DEFAULT_FAST_RECONNECT_ATTEMPTS)
DEFAULT_FAST_RECONNECT_TIMEOUT=5
AC_DEFINE_UNQUOTED(DEFAULT_FAST_RECONNECT_TIMEOUT,$DEFAULT_FAST_RECONNECT_TIMEOUT,[Define to the default amount of time which oddjobd will pause between attempts to reconnect to the system message bus after being disconnected for any reason.])
AC_SUBST(DEFAULT_FAST_RECONNECT_TIMEOUT)
DEFAULT_RECONNECT_TIMEOUT=30
AC_DEFINE_UNQUOTED(DEFAULT_RECONNECT_TIMEOUT,$DEFAULT_RECONNECT_TIMEOUT,[Define to the default amount of time which oddjobd will pause between attempts to reconnect to the system message bus after being disconnected for any reason.])
AC_SUBST(DEFAULT_RECONNECT_TIMEOUT)

AC_ARG_ENABLE(systemd,
AS_HELP_STRING([--enable-systemd],[install unit file for systemd]),
SYSTEMD=$enableval,
SYSTEMD=no)

AC_SUBST(SYSTEMD)
AM_CONDITIONAL(SYSTEMD,test x$SYSTEMD != xno)
if test x$SYSTEMD = xyes ; then
	SYSTEMDSYSTEMUNITDIR=`pkg-config --variable=systemdsystemunitdir systemd 2> /dev/null`
	AC_MSG_RESULT(will install systemd unit file)
else
	AC_MSG_RESULT(will install sysv init script)
fi
AC_SUBST(SYSTEMDSYSTEMUNITDIR)

selinux_labels=default
AC_ARG_WITH(selinux-labels,
AS_HELP_STRING(--with-selinux-labels,[Apply SELinux labels to files created by helpers.]),
selinux_labels=$withval,
selinux_labels=default)

selinux_acls=default
AC_ARG_WITH(selinux-acls,
AS_HELP_STRING(--with-selinux-acls,[Recognize SELinux process attributes in oddjobd ACLs.]),
selinux_acls=$withval,
selinux_acls=default)

LIBSsave="$LIBS"
LIBS="$LIBS $DBUS_LIBS"
AC_CHECK_FUNCS(dbus_bus_release_name)
if test x$ac_cv_func_dbus_bus_release_name = xno ; then
	AC_MSG_WARN([D-Bus doesn't appear to provide dbus_bus_release_name(), oddjobd will be unable to unregister unused services when reloading configuration.])
fi
LIBS="$LIBSsave"

if test x$selinux_acls != xno ; then
	# Assume that if the "don't know what the client's context is" error
	# is defined, that the server supports being asked the question.
	CFLAGsave="$CFLAGS"
	CFLAGS="$CFLAGS $DBUS_CFLAGS"
	CPPFLAGsave="$CPPFLAGS"
	CPPFLAGS="$CPPFLAGS $DBUS_CFLAGS"
	AC_CHECK_DECL(DBUS_ERROR_SELINUX_SECURITY_CONTEXT_UNKNOWN,,,[
		      #include <dbus/dbus.h>
		      ])
	CFLAGS="$CFLAGsave"
	CPPFLAGS="$CPPFLAGsave"
	if test x$ac_cv_have_decl_DBUS_ERROR_SELINUX_SECURITY_CONTEXT_UNKNOWN != xyes ; then
		if test x$selinux_acls = xdefault ; then
			AC_MSG_WARN([D-Bus doesn't appear to provide GetConnectionSELinuxSecurityContext(), disabling SELinux ACL support.])
			selinux_acls=no
		else
			AC_MSG_ERROR([D-Bus doesn't appear to provide GetConnectionSELinuxSecurityContext(), stopping.])
		fi
	fi
fi

define_selinux_labels=false
SELINUX_CFLAGS=
SELINUX_LIBS=
if test x$selinux_labels != xno ; then
	CFLAGsave="$CFLAGS"
	LIBsave="$LIBS"
	LDFLAGSsave="$LDFLAGS"
	CFLAGS=
	LIBS=
	LDFLAGS=
	if test -d $withval ; then
		CFLAGS="-I$withval/include"
		LIBS="-L$withval/`basename $libdir`"
	fi
	AC_CHECK_HEADERS(selinux/selinux.h)
	if test x$ac_cv_header_selinux_selinux_h != xyes ; then
		if test x$selinux != default ; then
			AC_MSG_ERROR([Could not locate SELinux headers.])
		else
			AC_MSG_WARN([Could not locate SELinux headers.])
		fi
	fi
	AC_CHECK_FUNC(setfscreatecon,,AC_CHECK_LIB(selinux,setfscreatecon))
	if test x$ac_cv_func_setfscreatecon != xyes ; then
		if test x$ac_cv_lib_selinux_setfscreatecon != xyes ; then
			if test x$selinux_labels != default ; then
				AC_MSG_ERROR([Could not locate SELinux library.])
			else
				AC_MSG_WARN([Could not locate SELinux library.])
			fi
		fi
	fi
	AC_CHECK_FUNCS(matchpathcon_init)
	if test x$ac_cv_header_selinux_selinux_h = xyes ; then
		if test x$ac_cv_func_setfscreatecon = xyes ; then
			define_selinux_labels=true
		fi
		if test x$ac_cv_lib_selinux_setfscreatecon = xyes ; then
			define_selinux_labels=true
		fi
	fi
	if $define_selinux_labels ; then
		AC_DEFINE(SELINUX_LABELS,1,[Define to build SELinux label-aware helpers.])
	fi
	SELINUX_CFLAGS="$CFLAGS"
	SELINUX_LIBS="$LIBS"
	CFLAGS="$CFLAGsave"
	LIBS="$LIBsave"
	LDFLAGS="$LDFLAGSsave"
fi
#
#  Even if we have libselinux, D-BUS itself might not be able to give us the
#  info we need for enforcing SELinux ACLS, so jump through a flaming hoop.
#
SELINUX_ACLS_XML_START="<!-- "
SELINUX_ACLS_XML_END=" -->"
SELINUX_ACLS_MAN_SPECIFIC=".\\ "
if $define_selinux_labels ; then
	if test x$selinux_acls != xno ; then
		AC_DEFINE(SELINUX_ACLS,1,[Define if you want SELinux ACL support in oddjobd.])
		SELINUX_ACLS_XML_START=
		SELINUX_ACLS_XML_END=
		SELINUX_ACLS_MAN_SPECIFIC=
	fi
fi
AC_SUBST(SELINUX_CFLAGS)
AC_SUBST(SELINUX_LIBS)
AC_SUBST(SELINUX_ACLS_XML_START)
AC_SUBST(SELINUX_ACLS_XML_END)
AC_SUBST(SELINUX_ACLS_MAN_SPECIFIC)

python=default
AC_ARG_WITH(python,
AS_HELP_STRING(--with-python,[Build Python bindings for liboddjob.]),
python=$withval,
python=default)
if test x$python = xyes ; then
	AM_PATH_PYTHON(,,[
		if test x$python = xyes ; then
			AC_MSG_ERROR([no suitable Python interpreter found])
		fi
	])
fi

mysysconfdir=`eval echo "$sysconfdir" | sed s,^NONE,"$prefix",`
mysysconfdir=`eval echo "$mysysconfdir" | sed s,^NONE,"$ac_default_prefix",`
mydatadir=`eval echo "$datadir" | sed s,^NONE,"$prefix",`
mydatadir=`eval echo "$mydatadir" | sed s,^NONE,"$ac_default_prefix",`
mypkglibdir=`eval echo "$libdir/oddjob" | sed s,^NONE,"$exec_prefix",`
mypkglibdir=`eval echo "$mypkglibdir" | sed s,^NONE,"$prefix",`
mypkglibdir=`eval echo "$mypkglibdir" | sed s,^NONE,"$ac_default_prefix",`
mypkglibexecdir=`eval echo "$libexecdir/oddjob" | sed s,^NONE,"$exec_prefix",`
mypkglibexecdir=`eval echo "$mypkglibexecdir" | sed s,^NONE,"$prefix",`
mypkglibexecdir=`eval echo "$mypkglibexecdir" | sed s,^NONE,"$ac_default_prefix",`
mylocalstatedir=`eval echo "$localstatedir" | sed s,^NONE,"$prefix",`
mylocalstatedir=`eval echo "$mylocalstatedir" | sed s,^NONE,"$ac_default_prefix",`
AC_DEFINE_UNQUOTED(SYSCONFDIR,"$mysysconfdir",[Define to the directory in which oddjobd can find its configuration file.])
AC_DEFINE_UNQUOTED(PKGLIBDIR,"$mypkglibexecdir",[Define to a directory in which oddjobd can find some of its helpers.])
AC_DEFINE_UNQUOTED(PKGLIBEXECDIR,"$mypkglibexecdir",[Define to a directory in which oddjobd can find some of its helpers.])
AC_DEFINE_UNQUOTED(DATADIR,"$mydatadir",[Define to the directory in which shared data is stored.])
AC_DEFINE_UNQUOTED(LOCALSTATEDIR,"$mylocalstatedir",[Define to the directory in which local state information is stored.])
AC_DEFINE_UNQUOTED(LOCKDIR,"$mylocalstatedir/lock",[Define to the directory in which local lock information is kept.])
AC_SUBST(mypkglibdir)
AC_SUBST(mypkglibexecdir)
AC_SUBST(mysysconfdir)
AC_SUBST(mydatadir)

AC_SYS_LARGEFILE
AC_CONFIG_HEADER(config.h)

AC_SUBST(DBUS_PACKAGE)
AC_DEFINE_UNQUOTED(DBUS_PACKAGE,"$DBUS_PACKAGE",[Define to the package name for D-BUS.])

AC_ARG_ENABLE(pie,
AS_HELP_STRING(--enable-pie,[Build position-independent executables.]),
pie=$enableval,
pie=no)
if test x$pie = xyes ; then
	AC_MSG_RESULT(building position-independent executables)
else
	AC_MSG_RESULT(NOT building position-independent executables)
fi
AM_CONDITIONAL(PIE,[test x$pie = xyes])

AC_ARG_ENABLE(now,
AS_HELP_STRING(--enable-now,[Mark binaries with bind-now flag.]),
now=$enableval,
now=no)
if test x$now = xyes ; then
	AC_MSG_RESULT(building bind-now executables)
else
	AC_MSG_RESULT(NOT building bind-now executables)
fi
AM_CONDITIONAL(NOW,[test x$now = xyes])

AC_CONFIG_COMMANDS(src/gpupdatefor+x,[chmod +x src/gpupdatefor])
AC_CONFIG_COMMANDS(src/gpupdateforme+x,[chmod +x src/gpupdateforme])

AC_CONFIG_FILES([
Makefile
autoversion
src/Makefile
src/oddjob-gpupdate.conf.5
src/oddjobd-gpupdate.conf.5
src/pam_oddjob_gpupdate.8
src/oddjob-gpupdate.conf
src/oddjobd-gpupdate.conf
src/gpupdatefor
])
AC_OUTPUT
