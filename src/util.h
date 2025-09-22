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

#ifndef oddjob_util_h
#define oddjob_util_h

char** make_argv(char* a_str, size_t* argc_out, const char a_delim);
ssize_t retry_write(int fd, unsigned char *buf, size_t length);
void *oddjob_malloc(size_t size);
void *oddjob_malloc0(size_t size);
void oddjob_free(void *p);
void oddjob_freev(void *p);
char *oddjob_strdup(const char *s);
char *oddjob_strdup_printf(const char *s, ...);
char *oddjob_strndup(const char *s, int n);
void oddjob_resize_array(void **array, size_t element_size,
			 size_t current_n_elements, size_t want_n_elements);

char **oddjob_collect_args(FILE *fp);
char **oddjob_parse_args(const char *cmdline, const char **error);
void oddjob_free_args(char **args);

#endif
