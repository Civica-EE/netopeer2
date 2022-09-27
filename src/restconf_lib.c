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

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/types.h>



#include <nc_server.h>
#include <libyang/libyang.h>
#include <sysrepo.h>

#include "common.h"
#include "log.h"

#include "restconf_lib.h"

/*
 * Constants
 */
#define CBUFLEN_DEFAULT 1024   /* Start alloc mem length 1K */


/* See 7231 Section 6.1
 */
static const map_str2int http_reason_phrase_map[] = {
    {"Continue",                      100},
    {"Switching Protocols",           101},
    {"OK",                            200},
    {"Created",                       201},
    {"Accepted",                      202},
    {"Non-Authoritative Information", 203},
    {"No Content",                    204},
    {"Reset Content",                 205},
    {"Partial Content",               206},
    {"Multiple Choices",              300},
    {"Moved Permanently",             301},
    {"Found",                         302},
    {"See Other",                     303},
    {"Not Modified",                  304},
    {"Use Proxy",                     305},
    {"Temporary Redirect",            307},
    {"Bad Request",                   400},
    {"Unauthorized",                  401},
    {"Payment Required",              402},
    {"Forbidden",                     403},
    {"Not Found",                     404},
    {"Method Not Allowed",            405},
    {"Not Acceptable",                406},
    {"Proxy Authentication Required", 407},
    {"Request Timeout",               408},
    {"Conflict",                      409},
    {"Gone",                          410},
    {"Length Required",               411},
    {"Precondition Failed",           412},
    {"Payload Too Large",             413},
    {"URI Too Long",                  414},
    {"Unsupported Media Type",        415},
    {"Range Not Satisfiable",         416},
    {"Expectation Failed",            417},
    {"Upgrade Required",              426},
    {"Internal Server Error",         500},
    {"Not Implemented",               501},
    {"Bad Gateway",                   502},
    {"Service Unavailable",           503},
    {"Gateway Timeout",               504},
    {"HTTP Version Not Supported",    505},
    {NULL,                            -1}
};

/* See RFC 8040 Section 7:  Mapping from NETCONF<error-tag> to Status Code
 * and RFC 6241 Appendix A. NETCONF Error list
 */
int restconf_err2code(int err) {
    int code;
    switch (err)
    {
    case NC_ERR_IN_USE:
        code = 409; break;
    case NC_ERR_INVALID_VALUE: /* 400 or 404 or 406 */
        code = 400; break;
    case NC_ERR_TOO_BIG: /* 413 request ; 400 response */
        code = 413; break;
    case NC_ERR_MISSING_ATTR:
        code = 400; break;
    case NC_ERR_BAD_ATTR:
        code = 400; break;
    case NC_ERR_UNKNOWN_ATTR:
        code = 400; break;
    case NC_ERR_MISSING_ELEM:
        code = 400; break;
    case NC_ERR_BAD_ELEM:
        code = 400; break;
    case NC_ERR_UNKNOWN_ELEM:
        code = 400; break;
    case NC_ERR_UNKNOWN_NS:
        code = 400; break;
    case NC_ERR_ACCESS_DENIED: /* 401 or 403 */
        code = 401; break;
    case NC_ERR_LOCK_DENIED:
        code = 409; break;
    case NC_ERR_RES_DENIED:
        code = 409; break;
    case NC_ERR_ROLLBACK_FAILED:
        code = 500; break;
    case NC_ERR_DATA_EXISTS:
        code = 409; break;
    case NC_ERR_DATA_MISSING:
        code = 409; break;
    case NC_ERR_OP_NOT_SUPPORTED: /* 405 or 501 */
        code = 405; break;
    case NC_ERR_OP_FAILED: /* 412 or 500 */
        code = 412; break;
    case NC_ERR_MALFORMED_MSG:
        code = 400; break;
    default:
        code = 400; break;
    }
    return code;
}
/*! Map from int to string using str2int map
 * @param[in] ms   String, integer map
 * @param[in] i    Input integer
 * @retval    str  String value
 * @retval    NULL Error, not found
 * @note linear search
 */
const char *
clicon_int2str(const map_str2int *mstab,
	       int                i)
{
    const struct map_str2int *ms;

    for (ms = &mstab[0]; ms->ms_str; ms++)
	if (ms->ms_int == i)
	    return ms->ms_str;
    return NULL;
}

/*! Map from string to int using str2int map
 * @param[in] ms   String, integer map
 * @param[in] str  Input string
 * @retval    int  Value
 * @retval   -1    Error, not found
 * @see clicon_str2int_search for optimized lookup, but strings must be sorted
 */
int
clicon_str2int(const map_str2int *mstab,
	       char              *str)
{
    const struct map_str2int *ms;

    for (ms = &mstab[0]; ms->ms_str; ms++)
	if (strcmp(ms->ms_str, str) == 0)
	    return ms->ms_int;
    return -1;
}

const char *
restconf_code2reason(int code)
{
    return clicon_int2str(http_reason_phrase_map, code);
}

/*! HTTP error 404
 * @param[in]  r        Fastcgi request handle
 */
int
restconf_notfound(FCGX_Request *r)
{
    char *path;

    DBG("%s", __FUNCTION__);
    path = FCGX_GetParam("DOCUMENT_URI", r->envp);
    FCGX_FPrintF(r->out, "Status: 404\r\n"); /* 404 not found */

    FCGX_FPrintF(r->out, "Content-Type: text/html\r\n\r\n");
    FCGX_FPrintF(r->out, "<h1>Not Found</h1>\n");
    FCGX_FPrintF(r->out, "Not Found\n");
    FCGX_FPrintF(r->out, "The requested URL %s was not found on this server.\n",
		 path);
    return 0;
}

/*!
 * @param[in]  r        Fastcgi request handle
 */
static int
printparam(FCGX_Request *r,
	   char         *e,
	   int           dbgp)
{
    char *p = FCGX_GetParam(e, r->envp);

    if (dbgp) {
        DBG("%s = '%s'", e, p?p:"");
    } else
        FCGX_FPrintF(r->out, "%s = '%s'\r\n", e, p?p:"");
    return 0;
}

/*!
 * @param[in]  r        Fastcgi request handle
 */
int
restconf_dump_request (FCGX_Request *r,
                       int           dbg)
{
    printparam(r, "QUERY_STRING", dbg);
    printparam(r, "REQUEST_METHOD", dbg);
    printparam(r, "CONTENT_TYPE", dbg);
    printparam(r, "CONTENT_LENGTH", dbg);
    printparam(r, "SCRIPT_FILENAME", dbg);
    printparam(r, "SCRIPT_NAME", dbg);
    printparam(r, "REQUEST_URI", dbg);
    printparam(r, "DOCUMENT_URI", dbg);
    printparam(r, "DOCUMENT_ROOT", dbg);
    printparam(r, "SERVER_PROTOCOL", dbg);
    printparam(r, "GATEWAY_INTERFACE", dbg);
    printparam(r, "SERVER_SOFTWARE", dbg);
    printparam(r, "REMOTE_ADDR", dbg);
    printparam(r, "REMOTE_PORT", dbg);
    printparam(r, "SERVER_ADDR", dbg);
    printparam(r, "SERVER_PORT", dbg);
    printparam(r, "SERVER_NAME", dbg);
    printparam(r, "HTTP_COOKIE", dbg);
    printparam(r, "HTTPS", dbg);
    printparam(r, "HTTP_ACCEPT", dbg);
    printparam(r, "HTTP_CONTENT_TYPE", dbg);
    printparam(r, "HTTP_AUTHORIZATION", dbg);
#if 0 /* For debug */
    DBG("All environment vars:");
    {
	extern char **environ;
	int i;
	for (i = 0; environ[i] != NULL; i++){
	    DBG("%s", environ[i]);
	}
    }
    DBG("End environment vars:");
#endif
    return 0;
}

/*!
 * @param[in]  r        Fastcgi request handle
 */
cbuf *
readdata(FCGX_Request *r)
{
    int   c;
    cbuf *cb;

    if ((cb = cbuf_new()) == NULL)
	return NULL;
    while ((c = FCGX_GetChar(r->in)) != -1)
	cprintf(cb, "%c", c);
    return cb;
}


/***************************/

/*
 * Variables
 */
/* This is how large a cbuf is after calling cbuf_new. Note that the cbuf
 * may grow after calls to cprintf or cbuf_alloc */
static uint32_t cbuflen_alloc = CBUFLEN_DEFAULT;

/*! Get cbuf initial memory allocation size
 * This is how large a cbuf is after calling cbuf_new. Note that the cbuf
 * may grow after calls to cprintf or cbuf_alloc
 */
uint32_t
cbuf_alloc_get(void)
{
    return cbuflen_alloc;
}

/*! Set cbuf initial memory allocation size
 * This is how large a cbuf is after calling cbuf_new. Note that the cbuf
 * may grow after calls to cprintf or cbuf_alloc
 */
int
cbuf_alloc_set(uint32_t alloc)
{
    cbuflen_alloc = alloc;
    return 0;
}

/*! Allocate cligen buffer. The handle returned can be used in  successive sprintf calls
 * which dynamically print a string.
 * The handle should be freed by cbuf_free()
 * @retval cb   The allocated objecyt handle on success.
 * @retval NULL Error.
 */
cbuf *
cbuf_new(void)
{
    cbuf *cb;

    if ((cb = (cbuf*)malloc(sizeof(*cb))) == NULL)
	return NULL;
    memset(cb, 0, sizeof(*cb));
    if ((cb->cb_buffer = malloc(cbuflen_alloc)) == NULL)
	return NULL;
    cb->cb_buflen = cbuflen_alloc;
    memset(cb->cb_buffer, 0, cb->cb_buflen);
    cb->cb_strlen = 0;
    return cb;
}

/*! Free cligen buffer previously allocated with cbuf_new
 * @param[in]   cb  Cligen buffer
 */
void
cbuf_free(cbuf *cb)
{
    if (cb) {
	if (cb->cb_buffer)
	    free(cb->cb_buffer);
	free(cb);
    }
}

/*! Return actual byte buffer of cligen buffer
 * @param[in]   cb  Cligen buffer
 */
char*
cbuf_get(cbuf *cb)
{
    return cb->cb_buffer;
}

/*! Return length of string in cligen buffer (not buffer length itself)
 * @param[in]   cb  Cligen buffer
 * @see cbuf_buflen
 */
int
cbuf_len(cbuf *cb)
{
    return cb->cb_strlen;
}

/*! Return length of buffer itself, ie allocated bytes
 * @param[in]   cb  Cligen buffer
 * @see cbuf_len
 */
int
cbuf_buflen(cbuf *cb)
{
    return cb->cb_buflen;
}

/*! Reset a cligen buffer. That is, restart it from scratch.
 * @param[in]   cb  Cligen buffer
 */
void
cbuf_reset(cbuf *cb)
{
    cb->cb_strlen    = 0;
    cb->cb_buffer[0] = '\0';
}

/*! Append a cligen buf by printf like semantics
 *
 * @param [in]  cb      cligen buffer allocated by cbuf_new(), may be reallocated.
 * @param [in]  format  arguments uses printf syntax.
 * @retval      See printf
 */
int
cprintf(cbuf       *cb,
	const char *format, ...)
{
    va_list ap;
    int diff;
    int retval;

    va_start(ap, format);
  again:
    if (cb == NULL)
	return 0;
    retval = vsnprintf(cb->cb_buffer+cb->cb_strlen,
		       cb->cb_buflen-cb->cb_strlen,
		       format, ap);
    if (retval < 0)
	return -1;
    diff = cb->cb_buflen - (cb->cb_strlen + retval + 1);
    if (diff <= 0){
	cb->cb_buflen *= 2; /* Double the space - alt linear growth */
	if ((cb->cb_buffer = realloc(cb->cb_buffer, cb->cb_buflen)) == NULL)
	    return -1;
	va_end(ap);
	va_start(ap, format);
	goto again;
    }
    cb->cb_strlen += retval;

    va_end(ap);
    return retval;
}

/*! Append a character to a cbuf
  *
  * @param [in]  cb      cligen buffer allocated by cbuf_new(), may be reallocated.
  * @param [in]  c       character to append
  */
int
cbuf_append(cbuf       *cb,
            int        c)
{
    int diff;

    /* make sure we have enough space */
    diff = cb->cb_buflen - (cb->cb_strlen + 1);
    if (diff <= 0) {
	cb->cb_buflen *= 2;
	if ((cb->cb_buffer = realloc(cb->cb_buffer, cb->cb_buflen)) == NULL) {
	    return -1;
	}
    }

    cb->cb_buffer[cb->cb_strlen++] = c;
    cb->cb_buffer[cb->cb_strlen] = 0;

    return 0;
}


/*! Create and initialize a new vector (rc_vec)
 *
 * Each individual cv initialized with no value.
 * Returned rc_vec needs to be freed with rc_vec_free().
 *
 * @param[in] len    Number of cv elements. Can be zero and elements added incrementally.
 * @retval    NULL   errno set
 * @retval    cvv    allocated var vector
 * @see rc_vec_init
 */
rc_vec *
rc_vec_new(int len)
{
    rc_vec *cvv;

    if ((cvv = malloc(sizeof(*cvv))) == NULL)
	return NULL;
    memset(cvv, 0, sizeof(*cvv));
    if (rc_vec_init(cvv, len) < 0){
	free(cvv);
	return NULL;
    }
    return cvv;
}

/*! Create a new vector, initialize the first element to the contents of 'var'
 *
 * @param[in] var        rc_var to clone and add to vector
 * @retval    rc_vec     allocated rc_vec
 */
rc_vec *
rc_vec_from_var(rc_var *cv)
{
    rc_vec   *newvec = NULL;
    rc_var *tail = NULL;

    if (cv && (newvec = rc_vec_new(0))) {
        if ((tail = rc_vec_append_var(newvec, cv)) == NULL) {
            rc_vec_free(newvec);
            newvec = NULL;
        }
    }
    return newvec;
}


/*! Free a vector (rc_vec)
 *
 * Reset and free a vector as previously created by rc_vec_new(). this includes
 * freeing all cv that the rc_vec consists of.
 * @param[in]  cvv  vector
 */
int
rc_vec_free(rc_vec *cvv)
{
    if (cvv) {
	rc_vec_reset(cvv);
	free(cvv);
    }
    return 0;
}

/*! Initialize a vector (rc_vec) with 'len' numbers of variables.
 *
 * Each individual cv initialized with no value.
 *
 * @param[in] cvv  vector
 * @param[in] len  Number of cv elements. Can be zero and elements added incrementally.
 * @see rc_vec_new
 */
int
rc_vec_init(rc_vec *cvv,
	  int   len)
{
    cvv->rc_len = len;
    if (len && (cvv->rc_vec = calloc(cvv->rc_len, sizeof(rc_var))) == NULL)
	return -1;
    return 0;
}

/*! Reset vector resetting it to an initial state as returned by rc_vec_new
 *
 * @param[in]  cvv   vector
 * @see also rc_vec_free. But this function does not actually free the rc_vec.
 */
int
rc_vec_reset(rc_vec *cvv)
{
    rc_var *cv = NULL;

    if (!cvv) {
	    return 0;
    }

    while ((cv = rc_vec_each(cvv, cv)) != NULL)
	rc_var_reset(cv);
    if (cvv->rc_vec)
	free(cvv->rc_vec);
    if (cvv->rc_name)
	free(cvv->rc_name);
    memset(cvv, 0, sizeof(*cvv));
    return 0;
}
/*! Free pointers and resets a single variable cv
 *
 * But does not free the cgv itself!
 * the type is maintained after reset.
 */
int
rc_var_reset(rc_var *cv)
{
    if (cv->name)
	free(cv->name);
    if (cv->value)
	free(cv->value);
    memset(cv, 0, sizeof(*cv));
    return 0;
}

/*! Given a cv in a vector (rc_vec) return the next cv.
 *
 * @param[in]  cvv    The vector
 * @param[in]  cv0    Return element after this, or first element if this is NULL
 * Given an element (cv0) in a vector (rc_vec) return the next element.
 * @retval cv  Next element
 */
rc_var *
rc_vec_next(rc_vec   *cvv,
	  rc_var *cv0)
{
    rc_var *cv = NULL;
    int i;

    if (!cvv) {
	    return 0;
    }

    if (cv0 == NULL)
	cv = cvv->rc_vec;
    else {
	i = cv0 - cvv->rc_vec;
	if (i < cvv->rc_len-1)
	    cv = cv0 + 1;
    }
    return cv;
}

/*! Append a new (cv) to vector (rc_vec) and return it.
 *
 * @param[in] cvv   vector
 * @param[in] type  Append a new cv to the vector with this type
 * @retval    NULL  Error
 * @retval    cv    The new variable
 * @see also cv_new, but this is allocated contiguosly as a part of a rc_vec.
 */
rc_var *
rc_vec_add(rc_vec *cvv)
{
    int     len;
    rc_var *cv;

    if (cvv == NULL){
	return NULL;
    }
    len = cvv->rc_len + 1;

    if ((cvv->rc_vec = realloc(cvv->rc_vec, len*sizeof(rc_var))) == NULL)
	return NULL;
    cvv->rc_len = len;
    cv = rc_vec_i(cvv, len-1);
    memset(cv, 0, sizeof(*cv));
    return cv;
}

/*! Append a new var that is a clone of data in 'var' to the vector, return it
 * @param[in] cvv  vector
 * @param[in] cv   Append this to vector. Note that it is copied.
 * @retval    NULL Error
 * @retval    tail Return the new last tail variable (copy of cv)
 */
rc_var *
rc_vec_append_var(rc_vec   *cvv,
		rc_var *cv)
{
    rc_var *tail = NULL;

    if (cvv && cv && (tail = rc_vec_add(cvv))) {
        if (rc_var_cp(tail, cv) < 0) {
            rc_vec_del(cvv, tail);
            tail = NULL;
        }
    }
    return tail;
}

/*! Copy from one cv to a new cv.
 *
 * The new cv should have been be initialized, such as after rc_var_new() or
 * after rc_var_reset().
 * The new cv may involve duplicating strings, etc.

 * @retval 0   0n success,
 * @retval -1  On error with errno set (strdup errors)
 */
int
rc_var_cp(rc_var *new,
      rc_var *old)
{
    int retval = -1;

    memcpy(new, old, sizeof(*old));
    if (old->name)
	if ((new->name = strdup(old->name)) == NULL)
	    goto done;
    if (old->value)
	if ((new->value = strdup(old->value)) == NULL)
	    goto done;
    retval = 0;
  done:
    return retval;
}

/*! Delete a cv variable from a rc_vec. Note: cv is not reset & cv may be stale!
 *
 * @param[in]  cvv   vector
 * @param[in]  del   variable to delete
 *
 * @note This is a dangerous command since the cv it deletes (such as created by
 * rc_vec_add) may have been modified with realloc (eg rc_vec_add/delete) and
 * therefore can not be used as a reference.  Safer methods are to use
 * rc_vec_find/rc_vec_i to find a cv and then to immediately remove it.
 */
int
rc_vec_del(rc_vec   *cvv,
	 rc_var *del)
{
    int i;
    rc_var *cv;

    if (rc_vec_len(cvv) == 0)
	return 0;

    i = 0;
    cv = NULL;
    while ((cv = rc_vec_each(cvv, cv)) != NULL) {
	if (cv == del)
	    break;
	i++;
    }
    if (i >= rc_vec_len(cvv)) /* Not found !?! */
	return rc_vec_len(cvv);

    if (i != rc_vec_len(cvv)-1) /* If not last entry, move the remaining cv's */
	memmove(&cvv->rc_vec[i], &cvv->rc_vec[i+1],
        (cvv->rc_len - i-1) * sizeof(cvv->rc_vec[0]));

    cvv->rc_len--;
    cvv->rc_vec = realloc(cvv->rc_vec, cvv->rc_len*sizeof(cvv->rc_vec[0])); /* Shrink should not fail? */

    return rc_vec_len(cvv);
}

/*! Return allocated length of a rc_vec.
 * @param[in]  cvv  vector
 */
int
rc_vec_len(rc_vec *cvv)
{
	if (!cvv) {
		return 0;
	}
    return cvv->rc_len;
}

/*! Return i:th element of vector rc_vec.
 * @param[in]  cvv   vector
 * @param[in]  i     Order of element to get
 */
rc_var *
rc_vec_i(rc_vec *cvv,
       int   i)
{
	if (!cvv) {
		return NULL;
	}
    if (i < cvv->rc_len)
	return &cvv->rc_vec[i];
    return NULL;
}

/*! Iterate through all variables in a rc_vec list
 *
 * @param[in] cvv       vector
 * @param[in] prev	    Last cgv (or NULL)
 * @retval cv           Next variable structure.
 * @retval NULL         When end of list reached.
 * @code
 *    rc_var *cv = NULL;
 *    while ((cv = rc_vec_each(cvv, cv)) != NULL)
 *	     ...
 * @endcode
 */
rc_var *
rc_vec_each(rc_vec   *cvv,
	  rc_var *prev)
{
	if (!cvv) {
		return 0;
	}

  if (prev == NULL){   /* Initialization */
      if (cvv->rc_len > 0)
	  return &cvv->rc_vec[0];
      else
	  return NULL;
  }
  return rc_vec_next(cvv, prev);
}

/*! Set new varable name.
 * Free previous string if existing.
 * @param[in] cv     variable
 * @param[in] s0     New name
 * @retval    s0     Return the new name
 * @retval    NULL   Error
 */
char *
rc_var_name_set(rc_var *cv,
	    char   *s0)
{
    char *s1 = NULL;

    if (cv == NULL)
	return 0;

    /* Duplicate s0. Must be done before a free, in case s0 is part of the original */
    if (s0){
	if ((s1 = strdup(s0)) == NULL)
	    return NULL; /* error in errno */
    }
    if (cv->name != NULL)
	free(cv->name);
    cv->name = s1;
    return s1;
}
/*! Set new value.
 * Free previous string if existing.
 * @param[in] cv     variable
 * @param[in] s0     New name
 * @retval    s0     Return the new name
 * @retval    NULL   Error
 */
char *
rc_var_value_set(rc_var *cv,
	    char   *s0)
{
    char *s1 = NULL;

    if (cv == NULL)
	return 0;

    /* Duplicate s0. Must be done before a free, in case s0 is part of the original */
    if (s0){
	if ((s1 = strdup(s0)) == NULL)
	    return NULL; /* error in errno */
    }
    if (cv->value != NULL)
	free(cv->value);
    cv->value = s1;
    return s1;
}
