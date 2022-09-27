/**
 * @file restconf_server.c
 *
 * @brief modified from netopeer2-server main.c,
 *  add FastCGI to be a RESTCONF server
 *
 * @author Radek Krejci <rkrejci@cesnet.cz>
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @author Hongcheng Zhong <spartazhc@gmail.com>
 * @author Alexandru Ponoviciu <alexandru.panoviciu@civica.co.uk>
 *
 * @copyright
 * Copyright (c) 2022 Civica NI Ltd
 * Copyright (c) 2019 Intel and/or its affiliates.
 * Copyright (c) 2016 - 2017 CESNET, z.s.p.o.
 *
 * This source code is licensed under BSD 3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/licenses/BSD-3-Clause
 *
 * Portions licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.  You may
 * obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <fcgiapp.h>

#include "log.h"
#include "common.h"
#include "restconf_server.h"
#include "restconf_lib.h"
#include "restconf_methods.h"
#include "restconf_path.h"

static int sock;
static volatile int stopping = 0;

/*!
 * See RFC 8040 3.3.3
 */
static int
api_yang_library_version(struct ly_ctx *ly_ctx,
                         sr_session_ctx_t *srs,
                         FCGX_Request *r)
{
    char  *media_accept;
    int    use_xml = 0; /* By default use JSON */
    const struct lys_module *yanglib = NULL;

    (void) srs;

    DBG("%s", __FUNCTION__);
    
    media_accept = FCGX_GetParam("HTTP_ACCEPT", r->envp);
    if (media_accept && strcmp(media_accept, "application/yang-data+xml")==0)
        use_xml++;
    
    yanglib = ly_ctx_get_module_latest(ly_ctx, "ietf-yang-library");
    if (!yanglib) {
        ERR("Session : missing \"ietf-yang-library\" schema in the context.");
        return NC_MSG_ERROR;
    }
    
    FCGX_SetExitStatus(200, r->out); /* OK */
    FCGX_FPrintF(r->out, "Content-Type: application/yang-data+%s\r\n", use_xml?"xml":"json");
    FCGX_FPrintF(r->out, "\r\n");
    if (use_xml){
        FCGX_FPrintF(r->out,
                     "<yang-library-version xmlns=\"urn:ietf:params:xml:ns:yang:ietf-restconf\">\n  %s\n</yang-library-version>\n",
                     yanglib->revision);
    } else {
        FCGX_FPrintF(r->out,
                     "{\"yang-library-version\": \"%s\"}",
                     yanglib->revision);
    }
    FCGX_FPrintF(r->out, "\n\n");
    
    return 0;
}

/*! Generic REST method, GET, PUT, DELETE, etc
 * @param[in]  r      Fastcgi request handle
 * @param[in]  api_path According to restconf (Sec 3.5.1.1 in [draft])
 * @param[in]  pcvec  Vector of path ie DOCUMENT_URI element
 * @param[in]  pi     Offset, where to start pcvec
 * @param[in]  qvec   Vector of query string (QUERY_STRING)
 * @param[in]  dvec   Stream input data
 * @param[in]  use_xml Set to 0 for JSON and 1 for XML
 * @param[in]  parse_xml Set to 0 for JSON and 1 for XML for input data
 */
static int
api_data(struct ly_ctx *ly_ctx,
         sr_session_ctx_t *srs,
         FCGX_Request *r,
         char         *api_path,
         rc_vec       *pcvec,
         int           pi,
         rc_vec       *qvec,
         char         *data,
         int           use_xml)
{
    int     retval = -1;
    char   *request_method;

    DBG("%s", __FUNCTION__);
    request_method = FCGX_GetParam("REQUEST_METHOD", r->envp);
    DBG("%s method:%s", __FUNCTION__, request_method);
    if (strcmp(request_method, "OPTIONS")==0)
        retval = api_data_options(r);
    else if (strcmp(request_method, "HEAD")==0)
        retval = api_data_head(ly_ctx, srs,r, pcvec, pi, qvec, use_xml);
    else if (strcmp(request_method, "GET")==0)
        retval = api_data_get(ly_ctx, srs,r, pcvec, pi, qvec, use_xml);
    else if (strcmp(request_method, "POST")==0)
        retval = api_data_post(ly_ctx, srs, r, pcvec, api_path,  pi, qvec, data, use_xml);
    else if (strcmp(request_method, "PUT")==0)
        retval = api_data_put(ly_ctx, srs, r, pcvec, api_path,  pi, qvec, data, use_xml);
    else if (strcmp(request_method, "PATCH")==0)
        retval = api_data_patch(ly_ctx, srs, r, pcvec, api_path,  pi, qvec, data, use_xml);
    else if (strcmp(request_method, "DELETE")==0)
	retval = api_data_delete(ly_ctx, srs,r, pcvec, pi, qvec, use_xml);
    else
        retval = restconf_notfound(r);
    DBG("%s retval:%d", __FUNCTION__, retval);
    return retval;
}

/*! Operations REST method, POST
 * @param[in]  r      Fastcgi request handle
 * @param[in]  path   According to restconf (Sec 3.5.1.1 in [draft])
 * @param[in]  pcvec  Vector of path ie DOCUMENT_URI element
 * @param[in]  pi     Offset, where to start pcvec
 * @param[in]  qvec   Vector of query string (QUERY_STRING)
 * @param[in]  data   Stream input data
 * @param[in]  parse_xml Set to 0 for JSON and 1 for XML for input data
 */
static int
api_operations(struct ly_ctx *ly_ctx,
               sr_session_ctx_t *srs,
               FCGX_Request *r,
               char         *path,
               rc_vec       *pcvec,
               int           pi,
               rc_vec       *qvec,
               char         *data,
               int           use_xml,
               int           parse_xml)
{
    int     retval = -1;
    char   *request_method;

    (void) path;
    (void) pcvec;
    (void) pi;
    (void) data;
    (void) parse_xml;

    DBG("%s", __FUNCTION__);
    request_method = FCGX_GetParam("REQUEST_METHOD", r->envp);
    DBG("%s method:%s", __FUNCTION__, request_method);
    if (strcmp(request_method, "GET")==0){
        retval = api_operations_get(ly_ctx,
                                srs,r, qvec, use_xml);
    }
    else if (strcmp(request_method, "POST")==0){
        // retval = api_operations_post(ly_ctx,
        //                         srs,r, pcvec, pi, qvec, use_xml);
    }
    else
	    retval = restconf_notfound(r);
    return retval;
}


/*! Retrieve the Top-Level API Resource
 * @param[in]  h        Clicon handle
 * @param[in]  r        Fastcgi request handle
 * @note Only returns null for operations and data,...
 * See RFC8040 3.3
 */
static int
api_root(struct ly_ctx *ly_ctx,
         sr_session_ctx_t *srs,
         FCGX_Request *r)
{
    char  *media_accept;
    int   use_xml = 0; /* By default use JSON */
    const struct lys_module *yanglib = NULL;

    (void) srs;

    DBG("ly_ctx %p srs %p", ly_ctx, srs);
    
    media_accept = FCGX_GetParam("HTTP_ACCEPT", r->envp);
    if (media_accept && strcmp(media_accept, "application/yang-data+xml")==0)
        use_xml++;
    
    yanglib = ly_ctx_get_module_latest(ly_ctx, "ietf-yang-library");
    if (!yanglib) {
        ERR("Session : missing \"ietf-yang-library\" schema in the context.");
        return NC_MSG_ERROR;
    }


    FCGX_SetExitStatus(200, r->out); /* OK */
    FCGX_FPrintF(r->out, "Content-Type: application/yang-data+%s\r\n", use_xml?"xml":"json");
    FCGX_FPrintF(r->out, "\r\n");
    
    if (use_xml){
        FCGX_FPrintF(r->out,
                     "<restconf xmlns=\"urn:ietf:params:xml:ns:yang:ietf-restconf\">\n  <data/>\n  <operations/>\n  <yang-library-version>%s</yang-library-version>\n</restconf>",
                     yanglib->revision);
    } else {
        FCGX_FPrintF(r->out,
                     "{\"ietf-restconf:restconf\": {\"data\": {},\"operations\": {},\"yang-library-version\": \"%s\"}}",
                     yanglib->revision);
    }
    
    FCGX_FPrintF(r->out, "\r\n\r\n");

    return 0;
}


/*! Process a FastCGI request
 * @param[in]  r        Fastcgi request handle
 */
static int
api_restconf(struct ly_ctx *ly_ctx,
             sr_session_ctx_t *srs,
             FCGX_Request *r)
{
    int    retval = -1;
    char  *path;
    char  *query;
    char  *method;
    char **pvec = NULL;
    int    pn;
    rc_vec  *qvec = NULL;
    rc_vec  *pcvec = NULL; /* for rest api */
    cbuf  *cb = NULL;
    char  *data;
    // int    authenticated = 0;
    char  *media_accept;
    char  *media_content_type;
    int    parse_xml = 0; /* By default expect and parse JSON */
    int    use_xml = 0;   /* By default use JSON */

#ifndef NDBEBUG
    DBG("%s", __FUNCTION__);
    restconf_dump_request(r, 1);
#endif
    
    path = FCGX_GetParam("REQUEST_URI", r->envp);
    query = FCGX_GetParam("QUERY_STRING", r->envp);
    /* get xml/json in put and output */
    media_accept = FCGX_GetParam("HTTP_ACCEPT", r->envp);
    if (media_accept && strcmp(media_accept, "application/yang-data+xml")==0)
	    use_xml++;
    media_content_type = FCGX_GetParam("HTTP_CONTENT_TYPE", r->envp);
    if (media_content_type && strcmp(media_content_type, "application/yang-data+xml")==0)
	    parse_xml++;
    if ((pvec = clicon_strsep(path, "/", &pn)) == NULL)
	    goto done;
    
    /* Sanity check of path. Should be /restconf/ */
    if (pn < 2){
        restconf_notfound(r);
        goto ok;
    }
    if (strlen(pvec[0]) != 0){
        retval = restconf_notfound(r);
        goto done;
    }
    if (strcmp(pvec[1], RESTCONF_API)){
        retval = restconf_notfound(r);
        goto done;
    }

#ifndef NDEBUG
    /* print pvec for debug */
    DBG("pn = %d", pn);
    int i = 0;
    while (pvec[i] != NULL) {
        DBG("pvec[%d] = %s", i, pvec[i]);
        ++i;
    }
#endif
    
    if (pn == 2){
        retval = api_root(ly_ctx,srs, r);
        goto done;
    }
    if ((method = pvec[2]) == NULL){
        retval = restconf_notfound(r);
        goto done;
    }
    DBG("%s: method=%s", __FUNCTION__, method);
    
    if (str2rc_vec(query, '&', '=', &qvec) < 0)
        goto done;
    if (str2rc_vec(path, '/', '=', &pcvec) < 0) /* rest url eg /album=ricky/foo */
        goto done;
    /* data */
    if ((cb = readdata(r)) == NULL)
        goto done;
    data = cbuf_get(cb);
    DBG("%s DATA=%s", __FUNCTION__, data);

    if (strcmp(method, "yang-library-version")==0){
        if (api_yang_library_version(ly_ctx, srs, r) < 0)
	    goto done;
    }
    else if (strcmp(method, "data") == 0){ /* restconf, skip /api/data */
        if (api_data(ly_ctx, srs, r, path, pcvec, 2, qvec, data, use_xml) < 0)
	    goto done;
    }
    else if (strcmp(method, "operations") == 0){ /* rpc */
        if (api_operations(ly_ctx, srs, r, path, pcvec, 2, qvec, data, use_xml, parse_xml) < 0)
	    goto done;
    }
    else if (strcmp(method, "test") == 0)
    {
        FCGX_SetExitStatus(200, r->out); /* OK */
        FCGX_FPrintF(r->out, "Content-Type: text/html\r\n\r\n");
        restconf_dump_request(r, 0);
    }
    else
        restconf_notfound(r);
ok:
    retval = 0;
done:
    DBG("%s retval:%d", __FUNCTION__, retval);
    if (pvec)
	free(pvec);
    return retval;
}



/*****************************************************************************
 *
 * Public functions
 *
 ******************************************************************************/

int restconf_server_init (int backlog)
{
    if (FCGX_Init() != 0) {
        return -1;
    }
    
    DBG("%s: Opening FCGX socket: %s", __FUNCTION__, NP2SRV_FCGI_SOCKPATH);

    if ((sock = FCGX_OpenSocket(NP2SRV_FCGI_SOCKPATH, backlog)) < 0){
        ERR("FCGX_Init error.");
        return -1;
    }
    
    /* set mod 777 temporarily , change to 774 and use www-data later
     * umask settings may interfer: we want group to write: this is 774 */
    if (chmod(NP2SRV_FCGI_SOCKPATH, S_IRWXU|S_IRWXG|S_IRWXO) < 0){ /* S_IRWXU|S_IRWXG|S_IROTH */
        ERR("Chmod error.");
        return -1;
    }
    
    return 0;
}

int restconf_server_shutdown ()
{
    close(sock);

    return 0;
}

int restconf_server_stop ()
{
    FCGX_ShutdownPending();
    stopping = 1;
    return 0;
}


//extern ATOMIC_T loop_continue;

int restconf_worker_thread (int idx)
{
    FCGX_Request  request;
    char *path;
    struct ly_ctx *ly_ctx;
    sr_session_ctx_t *srs = np2srv.sr_sess;

    // Some const nonsense here
    ly_ctx = (struct ly_ctx*) sr_get_context(sr_session_get_connection(srs));
    
    if (FCGX_InitRequest(&request, sock, 0) != 0)
    {
        ERR("FCGX_InitRequest error.");
        return -1;
    }

    while (!stopping)
    {
        DBG("[%d] in main while loop.", idx);

        if (FCGX_Accept_r(&request) < 0) {
            if (stopping)
                break;
            ERR("[%d] FCGX_Accept_r error %s.", idx);
            return -1;
        }
        
        DBG("[%d] FCGX accepted, ------------", idx);
        
        if ((path = FCGX_GetParam("REQUEST_URI", request.envp)) != NULL)
        {
            DBG("[%d] path: '%s'", idx, path);
            
            if (strncmp(path, "/" RESTCONF_API, strlen("/" RESTCONF_API)) == 0)
            {
                api_restconf(ly_ctx, srs, &request); /* This is the function */
            }
#if 0
            else if (strncmp(path+1, stream_path, strlen(stream_path)) == 0)
            {
                api_stream(h, &request, stream_path, &finish);
            }
#endif
            else
            {
                DBG("[%d] top-level %s not found", idx, path);
                restconf_notfound(&request);
            }
        }
        else
        {
            DBG("[%d] NULL URI", idx);
            restconf_notfound(&request);
        }
                
        FCGX_Finish_r(&request);
    }

    DBG("Restconf thread %d exiting", idx);
    
    return 0;
}
