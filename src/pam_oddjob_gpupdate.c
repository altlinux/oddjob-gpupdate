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
#include <errno.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <security/pam_modules.h>
#include "common.h"
#include "oddjob_dbus.h"
#include <stdio.h>

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

static void
send_pam_oddjob_gpupdate_request_with_args(pam_handle_t *pamh, int argc, const char **argv)
{
	const char *user;
	char *buf, *reply = NULL;
	ssize_t reply_size = -1;
	size_t bufsize;
	struct passwd pwd, *pw;
	struct stat st;
	int ret, result;

	user = NULL;

	int dbus_timeout_milliseconds = -1;
	int dbus_timeout_flag = 0;

	for (int i = 0; i < argc; i++) {
		if (strcmp(argv[i], "dbus_timeout") == 0) {
			dbus_timeout_flag = 1;
			continue;
		}
		if (dbus_timeout_flag > 0) {
			dbus_timeout_milliseconds = atoi(argv[i]) * 1000;
            if (dbus_timeout_milliseconds == 0) {
                dbus_timeout_milliseconds = -1;
            }
		}
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
						      dbus_timeout_milliseconds,
						      &reply,
						      &reply_size,
						      NULL,
						      0,
						      user,
						      NULL);
			} else {
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

static void
send_pam_oddjob_gpupdate_request(pam_handle_t *pamh)
{
	send_pam_oddjob_gpupdate_request_with_args(pamh, 0, NULL);
}

int
pam_sm_open_session(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
	send_pam_oddjob_gpupdate_request_with_args(pamh, argc, argv);
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
	send_pam_oddjob_gpupdate_request_with_args(pamh, argc, argv);
	return PAM_IGNORE;
}
