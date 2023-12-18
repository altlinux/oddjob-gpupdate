/*
   Copyright 2005,2012,2015 Red Hat, Inc.
   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the
      distribution.
    * Neither the name of Red Hat, Inc., nor the names of its
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

#ifndef oddjob_dbus_h
#define oddjob_dbus_h

#include "../config.h"
#include <dbus/dbus.h>
#include "buffer.h"

struct oddjob_dbus_context;
struct oddjob_dbus_message;

/* Server */
struct oddjob_dbus_context *oddjob_dbus_listeners_new(DBusBusType bus, dbus_bool_t selinux_enabled);
void oddjob_dbus_listeners_reconnect_if_needed(struct oddjob_dbus_context *ctx);
void oddjob_dbus_listeners_set_reconnect_timeout(struct oddjob_dbus_context *ctx,
						 int timeout);
void oddjob_dbus_listeners_free(struct oddjob_dbus_context *ctx);
typedef void (oddjob_dbus_handler)(struct oddjob_dbus_context *ctx,
				   struct oddjob_dbus_message *msg,
				   const char *service_name,
				   const char *object_path,
				   const char *interface_name,
				   const char *method_name,
				   const char *user,
				   unsigned long uid,
				   void *data);
dbus_bool_t oddjob_dbus_listeners_add_method(struct oddjob_dbus_context *ctx,
					     const char *service_name,
					     const char *object_path,
					     const char *interface,
					     const char *method,
					     int n_arguments,
					     oddjob_dbus_handler *handler,
					     void *data);
dbus_bool_t oddjob_dbus_listeners_remove_method(struct oddjob_dbus_context *ctx,
						const char *service_name,
						const char *object_path,
						const char *interface,
						const char *method);
 
int oddjob_dbus_message_get_n_args(struct oddjob_dbus_message *msg);
const char *oddjob_dbus_message_get_arg(struct oddjob_dbus_message *msg, int n);
const char *oddjob_dbus_message_get_selinux_context(struct oddjob_dbus_message *msg);
struct oddjob_dbus_message *oddjob_dbus_message_dup(struct oddjob_dbus_message *message);
void oddjob_dbus_message_free(struct oddjob_dbus_message *msg);
void oddjob_dbus_connection_close(DBusConnection *conn);
void oddjob_dbus_send_introspection_text(struct oddjob_dbus_message *msg,
					 const char *text);
void oddjob_dbus_send_message_response_text(struct oddjob_dbus_message *msg,
					    int result_code,
					    const char *text,
					    dbus_bool_t also_signal);
void oddjob_dbus_send_message_response_success(struct oddjob_dbus_message *msg,
					       int result_code,
					       struct oddjob_buffer *outc,
					       struct oddjob_buffer *errc,
					       dbus_bool_t also_signal);
void oddjob_dbus_send_message_response_error(struct oddjob_dbus_message *msg,
					     const char *error,
					     const char *text);
int oddjob_dbus_main_iterate(struct oddjob_dbus_context *ctx);

/* Clients */
#include "oddjob.h"

#endif
