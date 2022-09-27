/**
 * @file restconf_lib.c
 * @brief CGI utilities originating from CLIGen and CLIXON projects.
 * @author Hongcheng Zhong <spartazhc@gmail.com>
 * @author Alexandru Ponoviciu <alexandru.panoviciu@civica.co.uk>
 
 * @brief restconf library, some function are used from clixon
 *
 * @copyright
 * Copyright (c) 2022 Civica NI Ltd
 * Copyright (c) 2019 Intel and/or its affiliates.
 * Copyright (C) 2009-2019 Olof Hagsand and Benny Holmgren
 * Copyright (C) 2001-2019 Olof Hagsand
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _RESTCONF_LIB_H_
#define _RESTCONF_LIB_H_

#include <stdarg.h>
#include <stdint.h>
#include <fcgiapp.h>

/*
 * Types
 */


/*! Internal CLIgen buffer.
 */
struct cbuf {
    char  *cb_buffer;
    size_t cb_buflen;
    size_t cb_strlen;
};

/*
 * Types
 */
typedef struct cbuf cbuf; /* cligen buffer type is fully defined in c-file */


struct map_str2int{
    char         *ms_str;
    int           ms_int;
};
typedef struct map_str2int map_str2int;

/*
 * Constants
 */
#define RESTCONF_API       "restconf"

/*
 * Prototypes (also in clixon_restconf.h)
 */
int restconf_err2code(int err);
const char *restconf_code2reason(int code);

int restconf_notfound(FCGX_Request *r);

int restconf_dump_request(FCGX_Request *r, int dbg);
cbuf *readdata(FCGX_Request *r);

/*
 *
 *
 * CLIgen dynamic buffers
 * @code
 *   cbuf *cb;
 *   if ((cb = cbuf_new()) == NULL)
 *      err();
 *   cprintf(cb, "%d %s", 43, "go");
 *   if (write(f, cbuf_get(cb), cbuf_len(cb)) < 0)
 *      err();
 *   cbuf_free(cb);
 * @endcode
 */

/*
 * Prototypes
 */
uint32_t cbuf_alloc_get(void);
int cbuf_alloc_set(uint32_t alloc);
cbuf *cbuf_new(void);
void  cbuf_free(cbuf *cb);
char *cbuf_get(cbuf *cb);
int   cbuf_len(cbuf *cb);
int   cbuf_buflen(cbuf *cb);
#if defined(__GNUC__) && __GNUC__ >= 3
int   cprintf(cbuf *cb, const char *format, ...) __attribute__ ((format (printf, 2, 3)));
#else
int   cprintf(cbuf *cb, const char *format, ...);
#endif
void  cbuf_reset(cbuf *cb);
int   cbuf_append(cbuf *cb, int c);

struct rc_var {
    char *name;
    char *value;
};
typedef struct rc_var rc_var;
struct rc_vec {
    rc_var   *rc_vec;
    int     rc_len;
    char    *rc_name;
};
typedef struct rc_vec rc_vec;

rc_vec   *rc_vec_new(int len);
rc_vec   *rc_vec_from_var(rc_var *cv);
int     rc_vec_free(rc_vec *vr);
int     rc_vec_init(rc_vec *vr, int len);
int     rc_vec_reset(rc_vec *vr);

int     rc_vec_len(rc_vec *vr);
rc_var *rc_vec_i(rc_vec *vr, int i);
rc_var *rc_vec_next(rc_vec *vr, rc_var *cv0);
rc_var *rc_vec_add(rc_vec *vr);
rc_var *rc_vec_append_var(rc_vec *cvv, rc_var *var);
int     rc_vec_del(rc_vec *vr, rc_var *del);
rc_var *rc_vec_each(rc_vec *vr, rc_var *prev);

char*   rc_var_name_set(rc_var *cv, char *s0);
char*   rc_var_value_set(rc_var *cv, char *s0);
int     rc_var_reset(rc_var *cv);
int     rc_var_cp(rc_var *new, rc_var *old);

#endif /* _RESTCONF_LIB_H_ */
