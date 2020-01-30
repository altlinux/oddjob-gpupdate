/*
   Copyright 2019, BaseALT, Ltd.
   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the
      distribution.
    * Neither the name of BaseALT, Ltd., nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
   IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
   TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
   PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
   OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "../config.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <syslog.h>
#include <dbus/dbus.h>
#include "handlers.h"
#include "selinux.h"
#include "util.h"

#define _(_x) _x

static const char *exe;
static const char *gpo_exe;
static struct passwd *pwd;

#define FLAG_QUIET	(1 << 1)

/*
 * get_gpo_dir
 *
 * Returns: the location on the filesystem where the group policy update
 * utility should be found.  FIXME: consult /etc/default/useradd.
 *
 */
static const char *
get_gpo_exe(void)
{
	return gpo_exe ? gpo_exe : "/usr/sbin/gpoa";
}

static int apply_gpo(const char *user)
{
	int status;
	pid_t pid = fork();

	switch (pid) {
	case -1:
		return 1;
	case 0:
		execl(exe, exe, user, NULL);
		return 3;
	default:
		if (waitpid(pid, &status, 0) < 0)
			return 2;
	}
	return 0;
}

/* Apply group policies via GPO applier. */
static int
gpupdate(const char *user, int flags)
{
	int ret;
	struct stat st;
	const char *log_user = user;

	/* Now make sure that the user or computer
	   a) no user (computer)
	   b) user exists
	   c) has a home directory specified which is
	      1) an absolute pathname
	      2) not an empty string
	      3) not already there */
	if (user != NULL) {
		pwd = getpwnam(user);
		if (pwd == NULL) {
			syslog(LOG_ERR, "could not look up location of home directory "
			       "for %s", user);
		} else {
			if (pwd->pw_dir == NULL) {
				syslog(LOG_ERR, "user %s has NULL home directory", user);
			}
			if ((strlen(pwd->pw_dir) == 0) || (pwd->pw_dir[0] != '/')) {
				syslog(LOG_ERR, "user %s has weird home directory (%s)", user,
				       pwd->pw_dir);
			}
		}
	} else {
		log_user = "computer";
	}
	/* Figure out which executable we're using as a applier. */
	exe = get_gpo_exe();
	if (exe != NULL) {
		/* Set the text of the result message. */
		if (!(flags & FLAG_QUIET)) {
			printf(_("Apply group policies for %s."), log_user);
		}
		syslog(LOG_NOTICE, "Apply group policies for %s.", log_user);

		if (stat(exe, &st) != 0) {
			syslog(LOG_ERR, "stat of for applier (%s) is failed", exe);
			return HANDLER_INVALID_INVOCATION;
		} else {
			if (!S_ISREG(st.st_mode) && !S_ISLNK(st.st_mode)) {
				syslog(LOG_ERR, "applier %s is not a file or symlink", exe);
				return HANDLER_INVALID_INVOCATION;
			}
		}
		ret = apply_gpo(user);
		if (ret != 0) {
			syslog(LOG_ERR,
			       "error applying GPO for %s (error code %d)", log_user, ret);
			return HANDLER_FAILURE;
		}
	}
	return 0;
}

int
main(int argc, char **argv)
{
	char **oddjob_argv, *p;
	int oddjob_argc, ret, flags = 0;

	openlog(PACKAGE "-gpupdate", LOG_PID, LOG_DAEMON);
	gpo_exe = "/usr/sbin/gpoa";

	while ((ret = getopt(argc, argv, "qp:")) != -1) {
		switch (ret) {
		case 'q':
			flags |= FLAG_QUIET;
			break;
		case 'p':
			gpo_exe = optarg;
			break;
		default:
			fprintf(stderr, "Valid options:\n"
				"-q\tDo not print messages when applying "
				"a policy.\n"
				"-p PATH\tOverride the gpo applier "
				"binary (\"%s\").\n", gpo_exe);
			return 1;
		}
	}
	ret = HANDLER_INVALID_INVOCATION;
	oddjob_argv = oddjob_collect_args(stdin);
	for (oddjob_argc = 0; (oddjob_argv != NULL) && (oddjob_argv[oddjob_argc] != NULL); oddjob_argc++) {
		if (oddjob_argc > 1)
			break;
	}
	switch (oddjob_argc) {
	case 0:
		ret = gpupdate(NULL, flags);
		break;
	case 1:
		if (strlen(oddjob_argv[0]) > 0)
			ret = gpupdate(oddjob_argv[0], flags);
		break;
	default:
		syslog(LOG_ERR, "invoked with wrong arguments");
	}
	oddjob_free_args(oddjob_argv);
	closelog();
	return ret;
}
