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
#include <security/_pam_types.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <security/pam_modules.h>
#include "common.h"
#include "oddjob_dbus.h"
#include <stdio.h>

#define PAM_DEBUG_ARG       01
#define PAM_DBUS_TIMEOUT    02

/*
 * Got from internal linux-pam pam_inline.h header.
 * Returns NULL if STR does not start with PREFIX,
 * or a pointer to the first char in STR after PREFIX.
 * The length of PREFIX is specified by PREFIX_LEN.
 */
static inline const char *
_pam_str_skip_prefix_len(const char *str, const char *prefix, size_t prefix_len)
{
	return strncmp(str, prefix, prefix_len) ? NULL : str + prefix_len;
}

static void
conv_text_info(pam_handle_t *pamh, const char *info)
{
	struct pam_conv *conv;
	int i;
	conv = NULL;
	if ((pam_get_item(pamh, PAM_CONV, (const void**) &conv) == PAM_SUCCESS) &&
	    (conv != NULL)) {
		const struct pam_message message = {
			.msg_style = PAM_TEXT_INFO,
			.msg = info,
		};
		const struct pam_message *messages[] = {
			&message,
			NULL,
		};
		struct pam_response *responses;
		if (conv->conv != NULL) {
			responses = NULL;
			i = conv->conv(1, messages,
				       &responses, conv->appdata_ptr);
			if ((i == PAM_SUCCESS) && (responses != NULL)) {
				if (responses->resp != NULL) {
					free(responses->resp);
				}
				free(responses);
			}
		}
	}
}

static int
parse_args(pam_handle_t *pamh, int argc, const char **argv, int *dbus_timeout)
{
	int intarg, optmask = 0;
	char info[128];

	for (optmask = 0; argc-- > 0; ++argv) {
		const char *str;

		if (!strcmp(*argv, "debug"))
			optmask |= PAM_DEBUG_ARG;
		else if ((str = _pam_str_skip_prefix_len(*argv, "dbus_timeout=", 13)) != NULL) {
			if (sscanf(str, "%d", &intarg) != 1) {
				if (optmask & PAM_DEBUG_ARG) {
					snprintf (info, 128, "Ignore bad gpupdate dbus_timeout option value: %s", str);
					conv_text_info(pamh, info);
				}
			} else {
				optmask |= PAM_DBUS_TIMEOUT;
				*dbus_timeout = intarg * 1000;
			}
		} else if (optmask & PAM_DEBUG_ARG) {
			snprintf (info, 128, "Ignore gpupdate unknown option: %s", *argv);
			conv_text_info(pamh, info);
		}
	}

	return optmask;
}

static void
send_pam_oddjob_gpupdate_request(pam_handle_t *pamh, int argc, const char **argv)
{
	const char *user = NULL;
	char *buf, *reply = NULL;
	ssize_t reply_size = -1;
	size_t bufsize;
	struct passwd pwd, *pw;
	int ret, result;

	/* Dbus request timeout in milliseconds */
	int dbus_timeout = -1;
	int arg_flags = parse_args(pamh, argc, argv, &dbus_timeout);

	if (arg_flags & PAM_DEBUG_ARG) {
		char info[128];
		snprintf (info, 128, "D-Bus oddjob timeout is %d", dbus_timeout);
		conv_text_info(pamh, info);
	}

	if ((pam_get_user(pamh, &user, "login: ") == PAM_SUCCESS) &&
	    (user != NULL) &&
	    (strlen(user) > 0)) {
		/* Attempt to look up information about the user. */
		bufsize = 8192;
		do {
			pw = NULL;
			buf = malloc(bufsize);
			if (buf == NULL) {
				break;
			}
			ret = getpwnam_r(user, &pwd, buf, bufsize, &pw);
			if ((ret != 0) || (pw != &pwd)) {
				pw = NULL;
				free(buf);
				buf = NULL;
				if (ret == ERANGE) {
					bufsize += 4;
				} else {
					break;
				}
			}
		} while (ret == ERANGE);
		if (pw != NULL) {
			/* If we're running with the user's privileges, then ignore. */
			if ((getuid() != pw->pw_uid) ||
			    (geteuid() != pw->pw_uid)) {
				ret = oddjob_dbus_call_method(DBUS_BUS_SYSTEM,
						      ODDJOB_SERVICE_NAME "_gpupdate",
						      "/",
						      ODDJOB_INTERFACE_NAME "_gpupdate",
						      "gpupdatefor",
						      &result,
						      dbus_timeout,
						      &reply,
						      &reply_size,
						      NULL,
						      0,
						      user,
						      NULL);
			} else if (arg_flags & PAM_DEBUG_ARG) {
				char info[128];
				snprintf (info, 128, "Ignore gpupdate for user %s with uid %d", pw->pw_name, pw->pw_uid);
				conv_text_info(pamh, info);
			}
		}
		if (buf != NULL) {
			free(buf);
		}
	}

	if ((reply_size > 0) && (reply != NULL)) {
		conv_text_info(pamh, reply);
	}
	free(reply);
}

int
pam_sm_open_session(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
	send_pam_oddjob_gpupdate_request(pamh, argc, argv);
	return PAM_IGNORE;
}
int
pam_sm_close_session(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
	return PAM_IGNORE;
}

int
pam_sm_acct_mgmt(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
	send_pam_oddjob_gpupdate_request(pamh, argc, argv);
	return PAM_IGNORE;
}
