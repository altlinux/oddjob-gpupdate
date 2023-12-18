/*
   Copyright 2005,2006,2007,2010,2012,2014,2015 Red Hat, Inc.
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

#include "../config.h"
#include <sys/types.h>
#include <assert.h>
#include <fnmatch.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dbus/dbus.h>
#include "buffer.h"
#include "common.h"
#include "mainloop.h"
#include "oddjob_dbus.h"
#include "util.h"
#include <selinux/selinux.h>

struct oddjob_dbus_context {
	DBusBusType bustype;
	int reconnect_timeout;
	dbus_bool_t selinux_enabled;
	struct oddjob_dbus_service {
		struct oddjob_dbus_context *ctx;
		DBusConnection *conn;
		char *name;
		struct oddjob_dbus_object {
			char *path;
			struct oddjob_dbus_interface {
				char *interface;
				struct oddjob_dbus_method {
					char *method;
					int n_arguments;
					oddjob_dbus_handler *handler;
					void *data;
				} *methods;
				int n_methods;
			} *interfaces;
			int n_interfaces;
		} *objects;
		int n_objects;
	} *services;
	int n_services;
};

struct oddjob_dbus_message {
	DBusConnection *conn;
	DBusMessage *msg;
	int32_t result;
	int n_args;
	char **args;
	char *selinux_context;
};

static DBusHandlerResult
oddjob_dbus_filter(DBusConnection *conn, DBusMessage *message, void *user_data);

/* Acquire a well-known service name. */
static dbus_bool_t
oddjob_dbus_bind(DBusConnection *conn, const char *service_name)
{
#if DBUS_CHECK_VERSION(0,60,0)
	return dbus_bus_request_name(conn, service_name,
				     DBUS_NAME_FLAG_DO_NOT_QUEUE, NULL) ==
	       DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER;
#elif DBUS_CHECK_VERSION(0,30,0)
	return dbus_bus_request_name(conn, service_name,
				     DBUS_NAME_FLAG_PROHIBIT_REPLACEMENT |
				     DBUS_NAME_FLAG_DO_NOT_QUEUE, NULL) ==
	       DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER;
#elif DBUS_CHECK_VERSION(0,20,0)
	return dbus_bus_acquire_service(conn, service_name,
					DBUS_SERVICE_FLAG_PROHIBIT_REPLACEMENT,
					NULL) ==
	       DBUS_SERVICE_REPLY_PRIMARY_OWNER;
#else
#error "Don't know how to set service names for your version of D-Bus!"
	return FALSE;
#endif
}

/* Release a well-known service name. */
static int
oddjob_dbus_unbind(DBusConnection *conn, const char *service_name)
{
#if DBUS_CHECK_VERSION(0,60,0)
	return dbus_bus_release_name(conn, service_name, NULL);
#else
#warning "Can't unregister services with this version of D-Bus."
	return FALSE;
#endif
}

/* Completely dispose of a connection to the bus. */
void
oddjob_dbus_connection_close(DBusConnection *conn)
{
#if DBUS_CHECK_VERSION(0,34,0)
	dbus_connection_close(conn);
	dbus_connection_unref(conn);
#elif DBUS_CHECK_VERSION(0,20,0)
	dbus_connection_disconnect(conn);
#else
#error "Don't know how to disconnect from your version of D-Bus!"
#endif
}

/* Set our reconnect timeout, and make sure any open connections have their
 * exit-on-disconnect policy updated to match. */
void
oddjob_dbus_listeners_set_reconnect_timeout(struct oddjob_dbus_context *ctx,
					    int timeout)
{
	struct oddjob_dbus_service *srv;
	int i;

	ctx->reconnect_timeout = timeout;
	for (i = 0; i < ctx->n_services; i++) {
		srv = &ctx->services[i];
		/* Bring open connections in line with our disconnect policy. */
		dbus_connection_set_exit_on_disconnect(srv->conn,
						       ctx->reconnect_timeout <= 0);
	}
}

/* Create a new master state structure. */
struct oddjob_dbus_context *
oddjob_dbus_listeners_new(DBusBusType bustype, dbus_bool_t selinux_enabled)
{
	struct oddjob_dbus_context *ctx;

	ctx = oddjob_malloc0(sizeof(struct oddjob_dbus_context));
	if (ctx == NULL) {
		return NULL;
	}

	ctx->bustype = bustype;
	ctx->reconnect_timeout = 0;
	ctx->n_services = 0;
	ctx->services = NULL;
	ctx->selinux_enabled = selinux_enabled;

	return ctx;
}

/* Open a new connection for a given service name. */
static dbus_bool_t
service_connect(struct oddjob_dbus_service *srv)
{
	DBusError err;
	DBusConnection *conn;
	int attempt = 0;

	do {
		dbus_error_init(&err);
		conn = dbus_bus_get_private(srv->ctx->bustype, &err);
		if (dbus_error_is_set(&err)) {
			dbus_error_free(&err);
			fprintf(stderr, "Error connecting to bus for "
				"\"%s\" (attempt %d)!\n", srv->name,
				attempt + 1);
		}
		if ((conn == NULL) ||
		    (!dbus_connection_get_is_connected(conn))) {
			/* No joy.  Discard this attempt. */
			if (conn != NULL) {
				oddjob_dbus_connection_close(conn);
				conn = NULL;
			}
			/* Wait before trying again. In case it was just a
			 * restart, try to connect N times with our "fast"
			 * timeout, then fall back to the configured timeout. */
			if ((attempt < DEFAULT_FAST_RECONNECT_ATTEMPTS) &&
			    (srv->ctx->reconnect_timeout > DEFAULT_FAST_RECONNECT_TIMEOUT)) {
				sleep(DEFAULT_FAST_RECONNECT_TIMEOUT);
			} else {
				sleep(srv->ctx->reconnect_timeout);
			}
			attempt++;
		}
	} while (conn == NULL);

	/* Set our disconnect policy. */
	dbus_connection_set_exit_on_disconnect(conn,
					       srv->ctx->reconnect_timeout <= 0);

	/* Register our filter, which does the dispatching. */
	if (!dbus_connection_add_filter(conn, oddjob_dbus_filter,
					srv->ctx, NULL)) {
		oddjob_dbus_connection_close(conn);
		return FALSE;
	}

	/* Attempt to bind/rebind to the desired service name. */
	if (!oddjob_dbus_bind(conn, srv->name)) {
		fprintf(stderr, "Error acquiring well-known service name "
			"\"%s\"!\n", srv->name);
		oddjob_dbus_connection_close(conn);
		return FALSE;
	}

	/* Attach the connection's descriptors to the main loop. */
	mainloop_connect(conn);

	/* Done. */
	srv->conn = conn;
	return TRUE;
}

/* Close a connection for a given service name. */
static void
service_disconnect(struct oddjob_dbus_service *srv)
{
	if (srv->conn != NULL) {
		/* Detach the connection's descriptor from the main loop. */
		mainloop_disconnect(srv->conn);
		/* Remove the filter. */
		dbus_connection_remove_filter(srv->conn, oddjob_dbus_filter,
					      srv->ctx);
		/* Release the well-known name. */
		oddjob_dbus_unbind(srv->conn, srv->name);
		/* Close the connection. */
		oddjob_dbus_connection_close(srv->conn);
		srv->conn = NULL;
	}
}

/* Free a master state structure. */
void
oddjob_dbus_listeners_free(struct oddjob_dbus_context *ctx)
{
	int i, j, k, l;
	if (ctx == NULL) {
		return;
	}
	/* Clean up the context. */
	for (i = 0; i < ctx->n_services; i++) {
		/* Clean up this service. */
		service_disconnect(&ctx->services[i]);
		for (j = 0; j < ctx->services[i].n_objects; j++) {
			/* Clean up this object. */
			for (k = 0; k < ctx->services[i].objects[j].n_interfaces; k++) {
				/* Clean up this interface. */
				for (l = 0;
				     l < ctx->services[i].objects[j].interfaces[k].n_methods;
				     l++) {
					/* Clean up this method. */
					oddjob_free(ctx->services[i].objects[j].interfaces[k].methods[l].method);
					ctx->services[i].objects[j].interfaces[k].methods[l].method = NULL;
					ctx->services[i].objects[j].interfaces[k].methods[l].n_arguments = 0;
					ctx->services[i].objects[j].interfaces[k].methods[l].handler = NULL;
					ctx->services[i].objects[j].interfaces[k].methods[l].data = NULL;
				}
				oddjob_free(ctx->services[i].objects[j].interfaces[k].methods);
				ctx->services[i].objects[j].interfaces[k].methods = NULL;
				ctx->services[i].objects[j].interfaces[k].n_methods = 0;
				oddjob_free(ctx->services[i].objects[j].interfaces[k].interface);
				ctx->services[i].objects[j].interfaces[k].interface = NULL;
			}
			oddjob_free(ctx->services[i].objects[j].interfaces);
			ctx->services[i].objects[j].interfaces = NULL;
			ctx->services[i].objects[j].n_interfaces = 0;
			oddjob_free(ctx->services[i].objects[j].path);
			ctx->services[i].objects[j].path = NULL;
		}
		oddjob_free(ctx->services[i].objects);
		ctx->services[i].objects = NULL;
		ctx->services[i].n_objects = 0;
		oddjob_free(ctx->services[i].name);
		ctx->services[i].name = NULL;
	}
	oddjob_free(ctx->services);
	ctx->services = NULL;
	ctx->n_services = 0;
	oddjob_free(ctx);
}

/* Fetch and cache the caller's SELinux context. */
const char *
oddjob_dbus_message_get_selinux_context(struct oddjob_dbus_message *msg)
{
	return msg->selinux_context;
}

#ifdef SELINUX_ACLS
static char *
oddjob_dbus_get_selinux_context(DBusConnection *conn,
				const char *sender_bus_name)
{
	DBusMessage *query, *reply;
	char *ret;
	int length;
	DBusMessageIter iter, array;
	DBusError err;

	if (!is_selinux_enabled())
		return NULL;

	query = dbus_message_new_method_call(DBUS_SERVICE_DBUS,
					     DBUS_PATH_DBUS,
					     DBUS_INTERFACE_DBUS,
					     "GetConnectionSELinuxSecurityContext");
#if DBUS_CHECK_VERSION(0,30,0)
	dbus_message_append_args(query,
				 DBUS_TYPE_STRING, &sender_bus_name,
				 DBUS_TYPE_INVALID);
#elif DBUS_CHECK_VERSION(0,20,0)
	dbus_message_append_args(query,
				 DBUS_TYPE_STRING, sender_bus_name,
				 DBUS_TYPE_INVALID);
#else
#error	"Don't know how to set message arguments with your version of D-Bus!"
#endif

	memset(&err, 0, sizeof(err));
	reply = dbus_connection_send_with_reply_and_block(conn, query,
							  -1, &err);
	ret = NULL;
	if (dbus_error_is_set(&err)) {
#if DBUS_CHECK_VERSION(0,30,0)
		if ((strcmp(err.name, DBUS_ERROR_NAME_HAS_NO_OWNER) != 0) &&
		    (strcmp(err.name, DBUS_ERROR_NO_REPLY) != 0)) {
			fprintf(stderr, "Error %s: %s.\n",
				err.name, err.message);
		}
#elif DBUS_CHECK_VERSION(0,20,0)
		if ((strcmp(err.name, DBUS_ERROR_SERVICE_HAS_NO_OWNER) != 0) &&
		    (strcmp(err.name, DBUS_ERROR_NO_REPLY) != 0)) {
			fprintf(stderr, "Error %s: %s.\n",
				err.name, err.message);
		}
#else
#error	"Don't know what unknown-service/name errors look like with your version of D-Bus!"
#endif
	}
	if (reply != NULL) {
		if (dbus_message_iter_init(reply, &iter)) {
			switch (dbus_message_iter_get_arg_type(&iter)) {
			case DBUS_TYPE_ARRAY:
#if DBUS_CHECK_VERSION(0,33,0)
				/* We can't sanity check the length. */
				dbus_message_iter_recurse(&iter, &array);
				dbus_message_iter_get_fixed_array(&array, &ret,
								  &length);
				if (ret != NULL) {
					ret = oddjob_strndup(ret, length);
				}
#elif DBUS_CHECK_VERSION(0,20,0)
				/* We can't sanity check the length. */
				dbus_message_iter_get_byte_array(&iter,
								 (unsigned char **) &ret,
								 &length);
				if (ret != NULL) {
					ret = oddjob_strndup(ret, length);
				}
#else
#error "Don't know how to retrieve message arguments with your version of D-Bus!"
#endif
				break;
			case DBUS_TYPE_INVALID:
				break;
			default:
				break;
			}
		}
	}
	dbus_message_unref(query);
	if (reply != NULL) {
		dbus_message_unref(reply);
	}
	return ret;
}
#else
static char *
oddjob_dbus_get_selinux_context(DBusConnection *conn, const char *sender_bus_name)
{
	return NULL;
}
#endif

static void
oddjob_dbus_message_set_selinux_context(struct oddjob_dbus_message *msg,
					char *context_str)
{
	if (msg->selinux_context != NULL) {
		oddjob_free(msg->selinux_context);
		msg->selinux_context = NULL;
	}
	if (context_str != NULL) {
		msg->selinux_context = oddjob_strdup(context_str);
	}
}

/* Parse options and potentially an SELinux context from a D-Bus request. */
static struct oddjob_dbus_message *
oddjob_dbus_message_from_message(DBusConnection *conn,
				 DBusMessage *message,
				 dbus_bool_t expect_an_int,
				 dbus_bool_t get_selinux_context)
{
	struct oddjob_dbus_message *msg;
	char *p, *context_str;
	const char *sender_bus_name;
	dbus_bool_t more;
	DBusMessageIter iter;
	int32_t i;

	msg = oddjob_malloc0(sizeof(struct oddjob_dbus_message));
	msg->conn = conn;
	dbus_connection_ref(msg->conn);
	msg->msg = message;
	if (msg->msg != NULL) {
		dbus_message_ref(msg->msg);
		if (dbus_message_iter_init(message, &iter)) {
			if (expect_an_int) {
				if (dbus_message_iter_get_arg_type(&iter) ==
				    DBUS_TYPE_INT32) {
#if DBUS_CHECK_VERSION(0,30,0)
					dbus_message_iter_get_basic(&iter, &i);
					msg->result = i;
#elif DBUS_CHECK_VERSION(0,20,0)
					i = dbus_message_iter_get_int32(&iter);
					msg->result = i;
#else
#error "Don't know how to retrieve message arguments with your version of D-Bus!"
#endif
				} else {
					msg->result = -1;
				}
			}
			more = TRUE;
			while (more) {
				switch (dbus_message_iter_get_arg_type(&iter)) {
				case DBUS_TYPE_STRING:
					oddjob_resize_array((void**) &msg->args,
							    sizeof(char*),
							    msg->n_args,
							    msg->n_args + 1);
#if DBUS_CHECK_VERSION(0,30,0)
					dbus_message_iter_get_basic(&iter, &p);
					msg->args[msg->n_args] = oddjob_strdup(p);
#elif DBUS_CHECK_VERSION(0,20,0)
					p = dbus_message_iter_get_string(&iter);
					msg->args[msg->n_args] = oddjob_strdup(p);
					dbus_free(p);
#else
#error "Don't know how to retrieve message arguments with your version of D-Bus!"
#endif
					msg->n_args++;
					break;
				case DBUS_TYPE_INVALID:
					more = FALSE;
					break;
				default:
					break;
				}
				if (!dbus_message_iter_has_next(&iter) ||
				    !dbus_message_iter_next(&iter)) {
					more = FALSE;
				}
			}
		}
		sender_bus_name = dbus_message_get_sender(msg->msg);
		if (sender_bus_name != NULL) {
			if (get_selinux_context) {
				context_str = oddjob_dbus_get_selinux_context(msg->conn,
									      sender_bus_name);
			} else {
				context_str = NULL;
			}
			oddjob_dbus_message_set_selinux_context(msg, context_str);
			if (context_str != NULL) {
				oddjob_free(context_str);
			}
		}
	}

	return msg;
}

struct oddjob_dbus_message *
oddjob_dbus_message_dup(struct oddjob_dbus_message *input)
{
	struct oddjob_dbus_message *msg;
	int i;

	msg = oddjob_malloc0(sizeof(struct oddjob_dbus_message));
	msg->conn = input->conn;
	dbus_connection_ref(msg->conn);
	msg->msg = input->msg;
	if (msg->msg != NULL) {
		dbus_message_ref(msg->msg);
	}
	msg->result = input->result;
	msg->n_args = input->n_args;
	msg->args = NULL;
	oddjob_resize_array((void **) &msg->args, sizeof(char *),
			    0, msg->n_args);
	for (i = 0; i < msg->n_args; i++) {
		msg->args[i] = oddjob_strdup(input->args[i]);
	}
	if (input->selinux_context != NULL) {
		oddjob_dbus_message_set_selinux_context(msg,
							input->selinux_context);
	}
	return msg;
}

void
oddjob_dbus_message_free(struct oddjob_dbus_message *msg)
{
	int i;
	if (msg != NULL) {
		oddjob_dbus_message_set_selinux_context(msg, NULL);
		if (msg->args != NULL) {
			for (i = 0; i < msg->n_args; i++) {
				oddjob_free(msg->args[i]);
			}
			oddjob_free(msg->args);
		}
		msg->args = NULL;
		msg->n_args = 0;
		msg->result = -1;
		if (msg->msg != NULL) {
			dbus_message_unref(msg->msg);
			msg->msg = NULL;
		}
		if (msg->conn != NULL) {
			dbus_connection_unref(msg->conn);
			msg->conn = NULL;
		}
		oddjob_free(msg);
	}
}

/* Reconnect any listeners which are no longer connected to the bus. */
void
oddjob_dbus_listeners_reconnect_if_needed(struct oddjob_dbus_context *ctx)
{
	struct oddjob_dbus_service *srv;
	int i;

	for (i = 0; i < ctx->n_services; i++) {
		srv = &ctx->services[i];
		/* If we're still connected, we don't need to do anything. */
		if ((srv->conn != NULL) &&
		    dbus_connection_get_is_connected(srv->conn)) {
			continue;
		}

		/* Disconnect. */
		service_disconnect(srv);

		/* Reconnect. */
		service_connect(srv);
	}
}

/* Check if a message matches a given path. */
static dbus_bool_t
oddjob_dbus_message_has_path(DBusMessage *message, const char *path)
{
#if DBUS_CHECK_VERSION(0,34,0)
	return dbus_message_has_path(message, path);
#elif DBUS_CHECK_VERSION(0,20,0)
	const char *msgpath;
	msgpath = dbus_message_get_path(message);
	if ((msgpath == NULL) && (path == NULL)) {
		return TRUE;
	}
	if ((msgpath != NULL) && (path != NULL) &&
	    (strcmp(msgpath, path) == 0)) {
		return TRUE;
	}
	return FALSE;
#else
#error "Don't know how to check message information in your version of D-Bus!"
#endif
}

/* Find exactly one interface of this object which provides the listed method,
 * for cases where the interface name was omitted from the call message. */
static struct oddjob_dbus_interface *
guess_interface(struct oddjob_dbus_object *obj, const char *method)
{
	int i, j;
	struct oddjob_dbus_interface *iface, *match = NULL;

	/* Look for any interface containing the named method. */
	for (i = 0; i < obj->n_interfaces; i++) {
		iface = &obj->interfaces[i];
		for (j = 0; j < iface->n_methods; j++) {
			if (strcmp(iface->methods[j].method, method) == 0) {
				if (match != NULL) {
					/* No ambiguity allowed. */
					return NULL;
				}
				match = iface;
			}
		}
	}
	return match;
}

/* Handle incoming messages. */
static DBusHandlerResult
oddjob_dbus_filter(DBusConnection *conn, DBusMessage *message, void *user_data)
{
	struct oddjob_dbus_context *ctx;
	struct oddjob_dbus_service *srv = NULL;
	struct oddjob_dbus_object *obj;
	struct oddjob_dbus_interface *interface;
	struct oddjob_dbus_method *method;
	struct oddjob_dbus_message *msg;
	char n_args[LINE_MAX];
	const char *sender_bus_name;
	const char *called_service, *called_path,
		   *called_interface, *called_member;
	unsigned long uid;
	struct passwd *pwd;
	int i, j;

	/* Figure out which connection received the request.  We'll use its
	 * name instead of the destination from the message, in case it was
	 * addressed to the unique name instead of the well-known name. */
	ctx = user_data;
	for (i = 0; i < ctx->n_services; i++) {
		srv = &ctx->services[i];
		if (srv->conn == conn) {
			break;
		}
	}
	if (i > ctx->n_services) {
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	/* If it's a global signal, check for disconnect. */
	if (ctx->reconnect_timeout > 0) {
		/* Disconnect from the message bus itself. */
		if (dbus_message_has_sender(message,
					    DBUS_SERVICE_DBUS) &&
		    oddjob_dbus_message_has_path(message, DBUS_PATH_DBUS) &&
		    dbus_message_is_signal(message,
					   DBUS_INTERFACE_DBUS,
					   "Disconnected")) {
			return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
		}
		/* Disconnect from the library. */
#if DBUS_CHECK_VERSION(0,30,0)
		if (oddjob_dbus_message_has_path(message, DBUS_PATH_LOCAL) &&
		    dbus_message_is_signal(message,
					   DBUS_INTERFACE_LOCAL,
					   "Disconnected")) {
			return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
		}
#elif DBUS_CHECK_VERSION(0,20,0)
		if (oddjob_dbus_message_has_path(message,
						 DBUS_PATH_ORG_FREEDESKTOP_LOCAL) &&
		    dbus_message_is_signal(message,
					   DBUS_INTERFACE_ORG_FREEDESKTOP_LOCAL,
					   "Disconnected")) {
			return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
		}
#else
#error "Don't know how to read message data for your version of D-Bus!"
#endif
	}

	/* We only care about method calls to our services, so check that it's a
	 * method call that we know how to handle. */
	called_service = dbus_message_get_destination(message);
	called_path = dbus_message_get_path(message);
	called_interface = dbus_message_get_interface(message);
	called_member = dbus_message_get_member(message);

	/* Check that the message is a method call. */
	if ((called_service == NULL) || (called_path == NULL) ||
	    (called_member == NULL)) {
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}
	if (called_interface != NULL) {
		if (!(dbus_message_is_method_call(message,
						  called_interface,
						  called_member))) {
			return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
		}
	}

	/* Build our message structure. */
	msg = oddjob_dbus_message_from_message(conn, message, FALSE, ctx->selinux_enabled);
	if (msg == NULL) {
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	/* Find the bus address of the message sender. */
	sender_bus_name = dbus_message_get_sender(message);
	if (sender_bus_name == NULL) {
		oddjob_dbus_send_message_response_error(msg,
							ODDJOB_ERROR_UNKNOWN_SENDER,
							n_args);
		oddjob_dbus_message_free(msg);
		return DBUS_HANDLER_RESULT_HANDLED;
	}

	/* Get the called object path and find the object. */
	for (i = 0; (called_path != NULL) && (i < srv->n_objects); i++) {
		if (fnmatch(srv->objects[i].path, called_path,
			    ODDJOB_OBJECT_FNMATCH_FLAGS) == 0) {
			break;
		}
	}
	if (i >= srv->n_objects) {
		oddjob_dbus_send_message_response_error(msg,
							ODDJOB_ERROR_NO_OBJECT,
							called_path ?
							called_path : "");
		oddjob_dbus_message_free(msg);
		return DBUS_HANDLER_RESULT_HANDLED;
	} else {
		obj = &srv->objects[i];
	}

	/* Get the called interface, if there is one, and find the right
	 * interface. */
	if (called_interface == NULL) {
		interface = guess_interface(obj, called_member);
	} else {
		for (i = 0; i < obj->n_interfaces; i++) {
			if (strcmp(obj->interfaces[i].interface,
				   called_interface) == 0) {
				break;
			}
		}
		if (i >= obj->n_interfaces) {
			interface = NULL;
		} else {
			interface = &obj->interfaces[i];
		}
	}
	if (interface == NULL) {
		oddjob_dbus_send_message_response_error(msg,
							ODDJOB_ERROR_NO_INTERFACE,
							called_interface ?
							called_interface : "");
		oddjob_dbus_message_free(msg);
		return DBUS_HANDLER_RESULT_HANDLED;
	}

	/* Find the method in the interface. */
	for (i = 0;
	     (called_member != NULL) && (i < interface->n_methods);
	     i++) {
		if (strcmp(interface->methods[i].method, called_member) == 0) {
			break;
		}
	}
	if (i >= interface->n_methods) {
		oddjob_dbus_send_message_response_error(msg,
							ODDJOB_ERROR_NO_METHOD,
							called_member ?
							called_member : "");
		oddjob_dbus_message_free(msg);
		return DBUS_HANDLER_RESULT_HANDLED;
	}

	/* Check that we actually have a method registered. */
	if (interface->methods[i].handler == NULL) {
		oddjob_dbus_send_message_response_error(msg,
							ODDJOB_ERROR_UNIMPLEMENTED_METHOD,
							called_member ?
							called_member : "");
		oddjob_dbus_message_free(msg);
		return DBUS_HANDLER_RESULT_HANDLED;
	} else {
		method = &interface->methods[i];
	}

	/* Get the UID of the sending user and resolve it to a name. */
	uid = dbus_bus_get_unix_user(conn, sender_bus_name, NULL);
	pwd = getpwuid(uid);
	if ((pwd == NULL) || (pwd->pw_uid != uid)) {
		snprintf(n_args, sizeof(n_args), "UID=%lu", uid);
		oddjob_dbus_send_message_response_error(msg,
							ODDJOB_ERROR_UNKNOWN_USER,
							n_args);
		oddjob_dbus_message_free(msg);
		return DBUS_HANDLER_RESULT_HANDLED;
	}

	/* Check the arguments for verboten chars. */
	for (j = 0; j < msg->n_args; j++) {
		if (strpbrk(msg->args[j], "\r\n") != NULL) {
			break;
		}
	}
	if (j < msg->n_args) {
		oddjob_dbus_send_message_response_error(msg,
							ODDJOB_ERROR_INVALID_CALL,
							"invalid invocation");
		oddjob_dbus_message_free(msg);
		return DBUS_HANDLER_RESULT_HANDLED;
	}

	/* Check the number of arguments. */
	if (msg->n_args != method->n_arguments) {
		snprintf(n_args, sizeof(n_args),
			 "wrong number of arguments: "
			 "expected %d, called with %d",
			 method->n_arguments, msg->n_args);
		oddjob_dbus_send_message_response_error(msg,
							ODDJOB_ERROR_INVALID_CALL,
							n_args);
		oddjob_dbus_message_free(msg);
		return DBUS_HANDLER_RESULT_HANDLED;
	}

	/* Actually call the handler. */
	method->handler(ctx, msg, srv->name, called_path, interface->interface,
			called_member, pwd->pw_name, uid, method->data);
	oddjob_dbus_message_free(msg);

	return DBUS_HANDLER_RESULT_HANDLED;
}

dbus_bool_t
oddjob_dbus_listeners_add_method(struct oddjob_dbus_context *ctx,
				 const char *service_name,
				 const char *object_path,
				 const char *interface,
				 const char *method,
				 int n_arguments,
				 oddjob_dbus_handler *handler,
				 void *data)
{
	struct oddjob_dbus_service *srv;
	struct oddjob_dbus_object *obj;
	struct oddjob_dbus_interface *interf;
	struct oddjob_dbus_method *meth;
	int i;

	/* find the service, creating it if it doesn't already exist. */
	for (i = 0; i < ctx->n_services; i++) {
		if ((ctx->services[i].name != NULL) &&
		    (strcmp(service_name, ctx->services[i].name) == 0)) {
			break;
		}
	}
	if (i >= ctx->n_services) {
		oddjob_resize_array((void**)&ctx->services,
				    sizeof(ctx->services[0]),
				    ctx->n_services, i + 1);
		srv = &ctx->services[i];
		memset(srv, 0, sizeof(*srv));
		srv->name = oddjob_strdup(service_name);
		srv->ctx = ctx;
		if (!service_connect(srv)) {
			return FALSE;
		}
		ctx->n_services = i + 1;
	} else {
		srv = &ctx->services[i];
	}

	/* find the object, creating it if it doesn't already exist. */
	for (i = 0; i < srv->n_objects; i++) {
		if ((srv->objects[i].path != NULL) &&
		    (strcmp(object_path, srv->objects[i].path) == 0)) {
			break;
		}
	}
	if (i >= srv->n_objects) {
		oddjob_resize_array((void**)&srv->objects,
				    sizeof(srv->objects[0]),
				    srv->n_objects, i + 1);
		srv->objects[i].path = oddjob_strdup(object_path);
		srv->objects[i].interfaces = NULL;
		srv->objects[i].n_interfaces = 0;
		srv->n_objects = i + 1;
	}
	obj = &srv->objects[i];

	/* find the interface, creating it if it doesn't already exist. */
	for (i = 0; i < obj->n_interfaces; i++) {
		if ((obj->interfaces[i].interface != NULL) &&
		    (strcmp(interface, obj->interfaces[i].interface) == 0)) {
			break;
		}
	}
	if (i >= obj->n_interfaces) {
		oddjob_resize_array((void**)&obj->interfaces,
				    sizeof(obj->interfaces[0]),
				    obj->n_interfaces, i + 1);
		obj->interfaces[i].interface = oddjob_strdup(interface);
		obj->n_interfaces = i + 1;
	}
	interf = &obj->interfaces[i];

	/* find the method, creating it if it doesn't already exist */
	for (i = 0; i < interf->n_methods; i++) {
		if ((interf->methods[i].method != NULL) &&
		    (strcmp(method, interf->methods[i].method) == 0)) {
			break;
		}
	}
	if (i >= interf->n_methods) {
		oddjob_resize_array((void**)&interf->methods,
				    sizeof(interf->methods[0]),
				    interf->n_methods, i + 1);
		interf->methods[i].method = oddjob_strdup(method);
		interf->n_methods = i + 1;
	}

	/* set the method's pointers */
	meth = &interf->methods[i];
	meth->n_arguments = n_arguments;
	meth->handler = handler;
	meth->data = data;

	return TRUE;
}

dbus_bool_t
oddjob_dbus_listeners_remove_method(struct oddjob_dbus_context *ctx,
				    const char *service_name,
				    const char *object_path,
				    const char *interface,
				    const char *method)
{
	int i;
	struct oddjob_dbus_service *srv;
	struct oddjob_dbus_object *obj;
	struct oddjob_dbus_interface *interf;
	struct oddjob_dbus_method *meth;

	/* find the service */
	srv = NULL;
	for (i = 0; i < ctx->n_services; i++) {
		if ((ctx->services[i].name != NULL) &&
		    (strcmp(service_name, ctx->services[i].name) == 0)) {
			srv = &ctx->services[i];
			break;
		}
	}
	if (srv == NULL) {
		return TRUE;
	}

	/* find the object */
	obj = NULL;
	for (i = 0; i < srv->n_objects; i++) {
		if ((srv->objects[i].path != NULL) &&
		    (strcmp(object_path, srv->objects[i].path) == 0)) {
			obj = &srv->objects[i];
			break;
		}
	}
	if (obj == NULL) {
		return TRUE;
	}

	/* find the interface */
	interf = NULL;
	for (i = 0; i < obj->n_interfaces; i++) {
		if ((obj->interfaces[i].interface != NULL) &&
		    (strcmp(interface, obj->interfaces[i].interface) == 0)) {
			interf = &obj->interfaces[i];
			break;
		}
	}
	if (interf == NULL) {
		return TRUE;
	}

	/* find the method */
	meth = NULL;
	for (i = 0; i < interf->n_methods; i++) {
		if ((interf->methods[i].method != NULL) &&
		    (strcmp(method, interf->methods[i].method) == 0)) {
			meth = &interf->methods[i];
			break;
		}
	}
	if (meth == NULL) {
		return TRUE;
	}

	/* now, if the interface has exactly one method, free it, else just
	 * remove this method from its list */
	oddjob_free(meth->method);
	meth->n_arguments = 0;
	meth->handler = NULL;
	meth->data = NULL;
	if (interf->n_methods > 1) {
		for (i = 0; i < interf->n_methods; i++) {
			if (&interf->methods[i] == meth) {
				memmove(&interf->methods[i],
					&interf->methods[i + 1],
					sizeof(interf->methods[i]) *
					       (interf->n_methods - (i + 1)));
				break;
			}
		}
		oddjob_resize_array((void**)&interf->methods,
				    sizeof(interf->methods[0]),
				    interf->n_methods, interf->n_methods - 1);
		interf->n_methods--;
	} else {
		oddjob_resize_array((void**)&interf->methods,
				    sizeof(interf->methods[0]),
				    interf->n_methods, 0);
		interf->n_methods = 0;
	}

	/* if this interface still has methods, we're done */
	if (interf->n_methods > 0) {
		return TRUE;
	}

	/* now, if the object has exactly one interface, free it, else just
	 * remove this interface from its list */
	oddjob_free(interf->interface);
	if (obj->n_interfaces > 1) {
		for (i = 0; i < obj->n_interfaces; i++) {
			if (&obj->interfaces[i] == interf) {
				memmove(&obj->interfaces[i],
					&obj->interfaces[i + 1],
					sizeof(obj->interfaces[i]) *
					       (obj->n_interfaces - (i + 1)));
				break;
			}
		}
		oddjob_resize_array((void**)&obj->interfaces,
				    sizeof(obj->interfaces[0]),
				    obj->n_interfaces, obj->n_interfaces - 1);
		obj->n_interfaces--;
	} else {
		oddjob_resize_array((void**)&obj->interfaces,
				    sizeof(obj->interfaces[0]),
				    obj->n_interfaces, 0);
		obj->n_interfaces = 0;
	}

	/* if this object still has interfaces, then we're done */
	if (obj->n_interfaces > 0) {
		return TRUE;
	}

	/* now, if the service has exactly one object, free it, else just
	 * remove this object from its list */
	oddjob_free(obj->path);
	if (srv->n_objects > 1) {
		for (i = 0; i < srv->n_objects; i++) {
			if (&srv->objects[i] == obj) {
				memmove(&srv->objects[i],
					&srv->objects[i + 1],
					sizeof(srv->objects[i]) *
					       (srv->n_objects - (i + 1)));
				break;
			}
		}
		oddjob_resize_array((void**)&srv->objects,
				    sizeof(srv->objects[0]),
				    srv->n_objects, srv->n_objects - 1);
		srv->n_objects--;
	} else {
		oddjob_resize_array((void**)&srv->objects,
				    sizeof(srv->objects[0]),
				    srv->n_objects, 0);
		srv->n_objects = 0;
	}

	/* if this service still has objects, we're done */
	if (srv->n_objects > 0) {
		return TRUE;
	}

	/* now, stop offering the service. if the listener has exactly one
	 * service, free it, else just remove this service from its list */
	service_disconnect(srv);
	oddjob_free(srv->name);

	if (ctx->n_services > 1) {
		for (i = 0; i < ctx->n_services; i++) {
			if (&ctx->services[i] == srv) {
				memmove(&ctx->services[i],
					&ctx->services[i + 1],
					sizeof(ctx->services[i]) *
					       (ctx->n_services - (i + 1)));
				break;
			}
		}
		oddjob_resize_array((void**)&ctx->services,
				    sizeof(ctx->services[0]),
				    srv->n_objects, srv->n_objects - 1);
		srv->n_objects--;
	} else {
		oddjob_resize_array((void**)&ctx->services,
				    sizeof(ctx->services[0]),
				    srv->n_objects, 0);
		ctx->n_services = 0;
	}

	return TRUE;
}

int
oddjob_dbus_message_get_n_args(struct oddjob_dbus_message *msg)
{
	return msg->n_args;
}

const char *
oddjob_dbus_message_get_arg(struct oddjob_dbus_message *msg, int n)
{
	if (n >= msg->n_args) {
		return NULL;
	}
	return msg->args[n];
}

void
oddjob_dbus_send_introspection_text(struct oddjob_dbus_message *msg,
				    const char *text)
{
	DBusMessage *message;
	const char *empty = "";

	message = dbus_message_new_method_return(msg->msg);
#if DBUS_CHECK_VERSION(0,30,0)
	dbus_message_append_args(message,
				 DBUS_TYPE_STRING, text ? &text : &empty,
				 DBUS_TYPE_INVALID);
#elif DBUS_CHECK_VERSION(0,20,0)
	dbus_message_append_args(message,
				 DBUS_TYPE_STRING, text ? text : empty,
				 DBUS_TYPE_INVALID);
#else
#error	"Don't know how to set message arguments with your version of D-Bus!"
#endif
	dbus_connection_send(msg->conn, message, NULL);
	dbus_message_unref(message);
}

void
oddjob_dbus_send_message_response_text_int(DBusMessage *message,
					   struct oddjob_dbus_message *msg,
					   int32_t result,
					   const char *text)
{
	const char *empty = "";

#if DBUS_CHECK_VERSION(0,30,0)
	dbus_message_append_args(message,
				 DBUS_TYPE_INT32, &result,
				 DBUS_TYPE_INVALID);
#else
	dbus_message_append_args(message,
				 DBUS_TYPE_INT32, result,
				 DBUS_TYPE_INVALID);
#endif
#if DBUS_CHECK_VERSION(0,30,0)
	dbus_message_append_args(message,
				 DBUS_TYPE_STRING, text ? &text : &empty,
				 DBUS_TYPE_INVALID);
#elif DBUS_CHECK_VERSION(0,20,0)
	dbus_message_append_args(message,
				 DBUS_TYPE_STRING, text ? text : empty,
				 DBUS_TYPE_INVALID);
#else
#error	"Don't know how to set message arguments with your version of D-Bus!"
#endif
#if DBUS_CHECK_VERSION(0,30,0)
	dbus_message_append_args(message,
				 DBUS_TYPE_STRING, &empty,
				 DBUS_TYPE_INVALID);
#elif DBUS_CHECK_VERSION(0,20,0)
	dbus_message_append_args(message,
				 DBUS_TYPE_STRING, empty,
				 DBUS_TYPE_INVALID);
#else
#error	"Don't know how to set message arguments with your version of D-Bus!"
#endif
	dbus_connection_send(msg->conn, message, NULL);
}

void
oddjob_dbus_send_message_response_text(struct oddjob_dbus_message *msg,
				       int result_code,
				       const char *text,
				       dbus_bool_t also_signal)
{
	DBusMessage *message;
	const char *sender;
	dbus_uint32_t serial;
	int32_t result;

	result = result_code;
	message = dbus_message_new_method_return(msg->msg);
	if (message != NULL) {
		oddjob_dbus_send_message_response_text_int(message, msg,
							   result, text);
		dbus_message_unref(message);
	}
	if (!also_signal && (dbus_message_get_interface(msg->msg) != NULL)) {
		return;
	}
	message = dbus_message_new_signal(dbus_message_get_path(msg->msg),
					  dbus_message_get_interface(msg->msg),
					  dbus_message_get_member(msg->msg));
	if (message != NULL) {
		serial = dbus_message_get_serial(msg->msg);
		dbus_message_set_reply_serial(message, serial);
		sender = dbus_message_get_sender(msg->msg);
		if ((sender != NULL) &&
		    dbus_message_set_destination(message, sender)) {
			oddjob_dbus_send_message_response_text_int(message,
								   msg,
								   result,
								   text);
		}
		dbus_message_unref(message);
	}
}

static void
oddjob_dbus_send_message_response_success_int(DBusMessage *message,
					      struct oddjob_dbus_message *msg,
					      int32_t result,
					      struct oddjob_buffer *outc,
					      struct oddjob_buffer *errc)
{
	const char *p;

#if DBUS_CHECK_VERSION(0,30,0)
	dbus_message_append_args(message,
				 DBUS_TYPE_INT32, &result,
				 DBUS_TYPE_INVALID);
#else
	dbus_message_append_args(message,
				 DBUS_TYPE_INT32, result,
				 DBUS_TYPE_INVALID);
#endif
	if ((oddjob_buffer_length(outc) > 0) &&
	    (oddjob_buffer_data(outc)[oddjob_buffer_length(outc)] != '\0')) {
		abort();
	}
#if DBUS_CHECK_VERSION(0,30,0)
	p = (const char *) oddjob_buffer_data(outc);
	dbus_message_append_args(message,
				 DBUS_TYPE_STRING, &p,
				 DBUS_TYPE_INVALID);
	p = (const char *) oddjob_buffer_data(errc);
	dbus_message_append_args(message,
				 DBUS_TYPE_STRING, &p,
				 DBUS_TYPE_INVALID);
#elif DBUS_CHECK_VERSION(0,20,0)
	p = (const char *) oddjob_buffer_data(outc);
	dbus_message_append_args(message,
				 DBUS_TYPE_STRING, p,
				 DBUS_TYPE_INVALID);
	p = (const char *) oddjob_buffer_data(errc);
	dbus_message_append_args(message,
				 DBUS_TYPE_STRING, p,
				 DBUS_TYPE_INVALID);
#else
#error	"Don't know how to set message arguments with your version of D-Bus!"
#endif
	dbus_connection_send(msg->conn, message, NULL);
}

void
oddjob_dbus_send_message_response_success(struct oddjob_dbus_message *msg,
					  int result_code,
					  struct oddjob_buffer *outc,
					  struct oddjob_buffer *errc,
					  dbus_bool_t also_signal)
{
	DBusMessage *message;
	const char *sender;
	dbus_uint32_t serial;
	int32_t result;

	result = result_code;
	message = dbus_message_new_method_return(msg->msg);
	if (message != NULL) {
		oddjob_dbus_send_message_response_success_int(message, msg,
							      result,
							      outc, errc);
		dbus_message_unref(message);
	}
	if (!also_signal && (dbus_message_get_interface(msg->msg) != NULL)) {
		return;
	}
	message = dbus_message_new_signal(dbus_message_get_path(msg->msg),
					  dbus_message_get_interface(msg->msg),
					  dbus_message_get_member(msg->msg));
	if (message != NULL) {
		serial = dbus_message_get_serial(msg->msg);
		dbus_message_set_reply_serial(message, serial);
		sender = dbus_message_get_sender(msg->msg);
		if ((sender != NULL) &&
		    dbus_message_set_destination(message, sender)) {
			oddjob_dbus_send_message_response_success_int(message,
								      msg,
								      result,
								      outc,
								      errc);
		}
		dbus_message_unref(message);
	}
}

void
oddjob_dbus_send_message_response_error(struct oddjob_dbus_message *msg,
					const char *error,
					const char *text)
{
	DBusMessage *message;
	message = dbus_message_new_error(msg->msg, error, text);
	dbus_connection_send(msg->conn, message, NULL);
	dbus_message_unref(message);
}

int
oddjob_dbus_call_bus_methodv(DBusBusType bus,
			     const char *service, const char *object_path,
			     const char *interface, const char *method,
			     int *result, int timeout_milliseconds,
			     char **output, ssize_t *output_length,
			     char **error, ssize_t *error_length,
			     char **argv)
{
	DBusConnection *conn;
	DBusMessage *message, *reply;
	DBusError err;
	struct oddjob_dbus_message *msg;
	int ret, i;
	const char *p;

	memset(&err, 0, sizeof(err));
	dbus_error_init(&err);
	conn = dbus_bus_get(bus, &err);
	if (conn == NULL) {
		if ((output != NULL) && (output_length != NULL)) {
			i = strlen(err.name) + 2 + strlen(err.message) + 1;
			*output = malloc(i);
			if (*output != NULL) {
				*output_length = sprintf(*output, "%s: %s",
							 err.name, err.message);
			}
		}
		if ((error != NULL) && (error_length != NULL)) {
			i = strlen(err.name) + 2 + strlen(err.message) + 1;
			*error = malloc(i);
			if (*error != NULL) {
				*error_length = sprintf(*error, "%s: %s",
							err.name, err.message);
			}
		}
		dbus_error_free(&err);
		return -2;
	}

	dbus_connection_ref(conn);
	message = dbus_message_new_method_call(service,
					       object_path,
					       interface,
					       method);
	for (i = 0; (argv != NULL) && (argv[i] != NULL); i++) {
		p = argv[i];
#if DBUS_CHECK_VERSION(0,30,0)
		dbus_message_append_args(message,
					 DBUS_TYPE_STRING, &p,
					 DBUS_TYPE_INVALID);
#elif DBUS_CHECK_VERSION(0,20,0)
		dbus_message_append_args(message,
					 DBUS_TYPE_STRING, p,
					 DBUS_TYPE_INVALID);
#else
#error		"Don't know how to set message arguments with your version of D-Bus!"
#endif
	}
	reply = dbus_connection_send_with_reply_and_block(conn, message,
							  timeout_milliseconds,
							  &err);
	msg = oddjob_dbus_message_from_message(conn, reply, TRUE, FALSE);
	if (result) {
		*result = msg->result;
	}
	if ((output != NULL) && (output_length != NULL) && (msg->n_args > 0)) {
		i = strlen(msg->args[0]);
		*output = malloc(i + 1);
		if (*output != NULL) {
			memcpy(*output, msg->args[0], i + 1);
			*output_length = i;
		}
	}
	if ((error != NULL) && (error_length != NULL) && (msg->n_args > 1)) {
		i = strlen(msg->args[1]);
		*error = malloc(i + 1);
		if (*error != NULL) {
			memcpy(*error, msg->args[1], i + 1);
			*error_length = i;
		}
	}
	if (dbus_error_is_set(&err)) {
		if ((output != NULL) && (output_length != NULL)) {
			i = strlen(err.name) + 2 + strlen(err.message) + 1;
			*output = malloc(i);
			if (*output != NULL) {
				*output_length = sprintf(*output, "%s: %s",
							 err.name, err.message);
			}
		}
		if ((error != NULL) && (error_length != NULL)) {
			i = strlen(err.name) + 2 + strlen(err.message) + 1;
			*error = malloc(i);
			if (*error != NULL) {
				*error_length = sprintf(*error, "%s: %s",
							err.name, err.message);
			}
		}
		dbus_error_free(&err);
		ret = -1;
	} else {
		ret = 0;
	}

	oddjob_dbus_message_free(msg);

	if (reply != NULL) {
		dbus_message_unref(reply);
	}

	dbus_message_unref(message);
	dbus_connection_unref(conn);

	return ret;
}

int
oddjob_dbus_call_method(DBusBusType bus,
			const char *service, const char *object_path,
			const char *interface, const char *method,
			int *result, int timeout_milliseconds,
			char **output, ssize_t *output_length,
			char **error, ssize_t *error_length,
			...)
{
	va_list ap;
	char **argv;
	char *p;
	int i;

	argv = NULL;
	i = 0;
	va_start(ap, error_length);
	while ((p = va_arg(ap, char*)) != NULL) {
		oddjob_resize_array((void **) &argv, sizeof(char*), i, i + 2);
		argv[i] = p;
		i++;
	}
	va_end(ap);
	i = oddjob_dbus_call_bus_methodv(bus,
					 service, object_path,
					 interface, method,
					 result, timeout_milliseconds,
					 output, output_length,
					 error, error_length,
					 argv);
	oddjob_free(argv);
	return i;
}

int
oddjob_dbus_main_iterate(struct oddjob_dbus_context *ctx)
{
	struct oddjob_dbus_service *srv;
	int ret = 0, i;

	mainloop_reset_signal_handlers();
	ret = mainloop_iterate();
	for (i = 0; i < ctx->n_services; i++) {
		srv = &ctx->services[i];
		while (dbus_connection_get_dispatch_status(srv->conn) ==
		       DBUS_DISPATCH_DATA_REMAINS) {
			dbus_connection_dispatch(srv->conn);
		}
#if DBUS_CHECK_VERSION(0,30,0)
		while (dbus_connection_has_messages_to_send(srv->conn)) {
			dbus_connection_flush(srv->conn);
		}
#elif DBUS_CHECK_VERSION(0,20,0)
		while (dbus_connection_get_outgoing_size(srv->conn) > 0) {
			dbus_connection_flush(srv->conn);
		}
#else
#error "Don't know how to check if messages need to be flushed!"
#endif
	}
	return ret;
}

const char *
oddjob_dbus_get_default_service(void)
{
	return ODDJOB_NAMESPACE "." PACKAGE_NAME;
}

const char *
oddjob_dbus_get_default_object(void)
{
	return ODDJOB_NAMESPACE_PATH "/" PACKAGE_NAME;
}

const char *
oddjob_dbus_get_default_interface(void)
{
	return ODDJOB_NAMESPACE "." PACKAGE_NAME;
}
