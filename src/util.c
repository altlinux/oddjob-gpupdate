/*
   Copyright 2005 Red Hat, Inc.
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
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Generate argv and argc from argument string (a_str)  */
char** make_argv(char* a_str, size_t* argc_out, const char a_delim)
{
	if (!argc_out)
	{
		return NULL;
	}

	char delim[2] = {a_delim, 0};
	char** result = NULL;
	char* token = strtok(a_str, delim);

	(*argc_out) = 0;

	// getopt skip first argument........
	result = reallocarray(result, ++(*argc_out), sizeof(char*));
	result[0] = NULL;

	while(token)
	{
		result = reallocarray(result, ++(*argc_out), sizeof(char*));
		result[(*argc_out) - 1] = strdup(token);
		token = strtok(NULL, delim);
	}

	return result;
}


/* Write to a file, handling transient errors. */
ssize_t
retry_write(int fd, unsigned char *buf, size_t length)
{
	size_t start;
	ssize_t i;
	start = 0;
	while (start < length) {
		i = write(fd, buf + start, length - start);
		switch (i) {
		case 0:
			return start;
			break;
		case -1:
			switch (errno) {
			case EAGAIN:
				continue;
			default:
				return -1;
			}
			break;
		default:
			start += i;
			break;
		}
	}
	return start;
}

void *
oddjob_malloc(size_t size)
{
	void *ret;
	ret = malloc(size);
	if (ret == NULL) {
		fprintf(stderr, "Out of memory!\n");
		_exit(1);
	}
	return ret;
}

void *
oddjob_malloc0(size_t size)
{
	void *ret;
	ret = oddjob_malloc(size);
	memset(ret, 0, size);
	return ret;
}

void
oddjob_free(void *p)
{
	free(p);
}

void
oddjob_freev(void **p)
{
	int i;
	for (i = 0; (p != NULL) && (p[i] != NULL); i++) {
		oddjob_free(p[i]);
	}
	oddjob_free(p);
}

char *
oddjob_strdup(const char *s)
{
	char *r;
	r = oddjob_malloc0(strlen(s) + 1);
	strcpy(r, s);
	return r;
}

char *
oddjob_strdup_printf(const char *s, ...)
{
	char *r, buf[1];
	va_list va;
	int i;
	va_start(va, s);
	i = vsnprintf(buf, 1, s, va);
	va_end(va);
	r = oddjob_malloc0(i + 1);
	va_start(va, s);
	vsnprintf(r, i + 1, s, va);
	va_end(va);
	return r;
}

char *
oddjob_strndup(const char *s, int n)
{
	char *r, *end;
	int len;
	end = memchr(s, '\0', n);
	if ((end == NULL) || (end - s > n)) {
		len = n;
	} else {
		len = end - s;
	}
	r = oddjob_malloc0(len + 1);
	memmove(r, s, len);
	return r;
}

void
oddjob_resize_array(void **array, size_t element_size,
		    size_t current_n_elements, size_t want_n_elements)
{
	void *p;
	size_t save_n_elements;
	if (want_n_elements > 0xffff) {
		fprintf(stderr, "Internal limit exceeded.\n");
		_exit(1);
	}
	if (element_size > 0xffff) {
		fprintf(stderr, "Internal limit exceeded.\n");
		_exit(1);
	}
	p = NULL;
	if ((element_size > 0) && (want_n_elements > 0)) {
		p = oddjob_malloc0(element_size * want_n_elements);
	}
	save_n_elements = (current_n_elements < want_n_elements) ?
			  current_n_elements : want_n_elements;
	if (save_n_elements > 0) {
		memmove(p, *array, element_size * save_n_elements);
		memset(*array, 0, element_size * save_n_elements);
	}
	oddjob_free(*array);
	*array = p;
}

char **
oddjob_collect_args(FILE *fp)
{
	char **ret, *thisline, *p;
	char buf[BUFSIZ];
	size_t l, m, arraysize;

	ret = NULL;
	thisline = NULL;
	arraysize = 0;
	while (fgets(buf, sizeof(buf), fp) != NULL) {
		char *tmp;
		l = thisline ? strlen(thisline) : 0;
		m = strlen(buf);
		tmp = oddjob_malloc0(l + m + 1);
		if (l > 0) {
			strcpy(tmp, thisline);
		}
		memmove(tmp + l, buf, m);
		oddjob_free(thisline);
		thisline = tmp;

		p = strpbrk(thisline, "\r\n");
		if (p != NULL) {
			*p = '\0';
			oddjob_resize_array((void**) &ret, sizeof(char*),
					    arraysize,
					    arraysize + 2);
			ret[arraysize] = thisline;
			arraysize++;
			thisline = NULL;
		}
	}
	if (thisline != NULL) {
		oddjob_resize_array((void**) &ret, sizeof(char*),
				    arraysize,
				    arraysize ?
				    arraysize + 1 : arraysize + 2);
		ret[arraysize] = thisline;
		arraysize = arraysize ? arraysize + 1 : arraysize + 2;
		thisline = NULL;
	}
	return ret;
}

void
oddjob_free_args(char **args)
{
	int i;
	if (args != NULL) {
		for (i = 0; args[i] != NULL; i++) {
			oddjob_free(args[i]);
			args[i] = NULL;
		}
		oddjob_free(args);
	}

}

char **
oddjob_parse_args(const char *cmdline, const char **error)
{
	const char *p;
	char *q, *bigbuf;
	char **argv;
	int sqlevel, dqlevel, escape;
	size_t buffersize, words;

	buffersize = strlen(cmdline) * 3;
	bigbuf = oddjob_malloc0(buffersize);

	sqlevel = dqlevel = escape = 0;
	p = cmdline;
	q = bigbuf;
	while (*p != '\0') {
		switch (*p) {
		case '\\':
			if ((dqlevel != 0) || (sqlevel != 0) || escape) {
				*q++ = *p++;
				escape = 0;
			} else {
				escape = 1;
				p++;
			}
			break;
		case '\'':
			switch (sqlevel) {
			case 0:
				if (escape || (dqlevel > 0)) {
					*q++ = *p++;
					escape = 0;
				} else {
					sqlevel = 1;
					p++;
				}
				break;
			case 1:
				sqlevel = 0;
				p++;
				break;
			default:
				break;
			}
			break;
		case '"':
			switch (dqlevel) {
			case 0:
				if (escape || (sqlevel > 0)) {
					*q++ = *p++;
					escape = 0;
				} else {
					dqlevel = 1;
					p++;
				}
				break;
			case 1:
				dqlevel = 0;
				p++;
				break;
			default:
				break;
			}
			break;
		case '\r':
		case '\n':
		case '\t':
		case ' ':
			if (escape || (dqlevel > 0) || (sqlevel > 0)) {
				*q++ = *p;
			} else {
				*q++ = '\0';
			}
			p++;
			break;
		default:
			*q++ = *p++;
			break;
		}
	}
	if (error) {
		*error = NULL;
	}
	if (dqlevel > 0) {
		if (error) {
			*error = "Unmatched \"";
		}
		oddjob_free(bigbuf);
		return NULL;
	}
	if (sqlevel > 0) {
		if (error) {
			*error = "Unmatched '";
		}
		oddjob_free(bigbuf);
		return NULL;
	}
	if (escape) {
		if (error) {
			*error = "Attempt to escape end-of-command";
		}
		oddjob_free(bigbuf);
		return NULL;
	}
	p = NULL;
	words = 0;
	for (q = bigbuf; q < bigbuf + buffersize; q++) {
		if (*q != '\0') {
			if (p == NULL) {
				p = q;
			}
		} else {
			if (p != NULL) {
				words++;
				p = NULL;
			}
		}
	}
	argv = oddjob_malloc0(sizeof(char*) * (words + 1));
	p = NULL;
	words = 0;
	for (q = bigbuf; q < bigbuf + buffersize; q++) {
		if (*q != '\0') {
			if (p == NULL) {
				p = q;
			}
		} else {
			if (p != NULL) {
				argv[words++] = oddjob_strdup(p);
				p = NULL;
			}
		}
	}
	oddjob_free(bigbuf);
	return argv;
}
