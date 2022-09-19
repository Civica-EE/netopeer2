/* -*- mode:c; indent-tabs-mode:nil; tab-width:4; c-basic-offset:4 -*- */
/*     vi: set ts=4 sw=4 expandtab:                                    */

/*@COPYRIGHT@*/

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

/*! Determine the root of the RESTCONF API
 * @param[in]  r        Fastcgi request handle
 * @note Hardcoded to "/restconf"
 * Return see RFC8040 3.1 and RFC7320
 * In line with the best practices defined by [RFC7320], RESTCONF
 * enables deployments to specify where the RESTCONF API is located.
 */
static int
api_well_known(FCGX_Request *r)
{
    VRB("%s", __FUNCTION__);
    FCGX_FPrintF(r->out, "Content-Type: application/xrd+xml\r\n");
    FCGX_FPrintF(r->out, "\r\n");
    FCGX_SetExitStatus(200, r->out); /* OK */
    FCGX_FPrintF(r->out, "<XRD xmlns='http://docs.oasis-open.org/ns/xri/xrd-1.0'>\n");
    FCGX_FPrintF(r->out, "   <Link rel='restconf' href='cgi-bin/restconf'/>\n");
    FCGX_FPrintF(r->out, "</XRD>\r\n");

    return 0;
}


/*!
 * See https://tools.ietf.org/html/rfc7895
 */
static int
api_yang_library_version(struct ly_ctx *ly_ctx,
                         sr_session_ctx_t *srs,
                         FCGX_Request *r)
{
    char  *media_accept;
    int    use_xml = 0; /* By default use JSON */
    char   buf[120];
    const struct lys_module *yanglib = NULL;

    (void) srs;

    VRB("%s", __FUNCTION__);
    media_accept = FCGX_GetParam("HTTP_ACCEPT", r->envp);
    if (strcmp(media_accept, "application/yang-data+xml")==0)
        use_xml++;
    yanglib = ly_ctx_get_module(ly_ctx, "ietf-yang-library", NULL, 1);
    if (!yanglib) {
        ERR("Session : missing \"ietf-netconf\" schema in the context.");
        return NC_MSG_ERROR;
    }
    if (use_xml){
        sprintf(buf, "<yang-library-version xmlns=\"urn:ietf:params:xml:ns:yang:ietf-restconf\">\n  %s\n</yang-library-version>\n", yanglib->rev->date);
    } else {
        sprintf(buf, "{\"yang-library-version\": \"%s\"}", yanglib->rev->date);
    }
    FCGX_SetExitStatus(200, r->out); /* OK */
    FCGX_FPrintF(r->out, "Content-Type: application/yang-data+%s\r\n", use_xml?"xml":"json");
    FCGX_FPrintF(r->out, "\r\n");
    FCGX_FPrintF(r->out, "%s", buf);
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

    VRB("%s", __FUNCTION__);
    request_method = FCGX_GetParam("REQUEST_METHOD", r->envp);
    VRB("%s method:%s", __FUNCTION__, request_method);
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
        retval = notfound(r);
    VRB("%s retval:%d", __FUNCTION__, retval);
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

    VRB("%s", __FUNCTION__);
    request_method = FCGX_GetParam("REQUEST_METHOD", r->envp);
    VRB("%s method:%s", __FUNCTION__, request_method);
    if (strcmp(request_method, "GET")==0){
        retval = api_operations_get(ly_ctx,
                                srs,r, qvec, use_xml);
    }
    else if (strcmp(request_method, "POST")==0){
        // retval = api_operations_post(ly_ctx,
        //                         srs,r, pcvec, pi, qvec, use_xml);
    }
    else
	    retval = notfound(r);
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
    char   buf[160];
    const struct lys_module *yanglib = NULL;

    (void) srs;

    VRB("%s", __FUNCTION__);
    media_accept = FCGX_GetParam("HTTP_ACCEPT", r->envp);
    if (strcmp(media_accept, "application/yang-data+xml")==0)
        use_xml++;
    yanglib = ly_ctx_get_module(ly_ctx, "ietf-yang-library", NULL, 1);
    if (!yanglib) {
        ERR("Session : missing \"ietf-netconf\" schema in the context.");
        return NC_MSG_ERROR;
    }
    if (use_xml){
        sprintf(buf, "<restconf xmlns=\"urn:ietf:params:xml:ns:yang:ietf-restconf\">\n  <data/>\n  <operations/>\n  <yang-library-version>%s</yang-library-version>\n</restconf>", yanglib->rev->date);
    } else {
        sprintf(buf, "{\"ietf-restconf:restconf\": {\"data\": {},\"operations\": {},\"yang-library-version\": \"%s\"}}", yanglib->rev->date);
    }
    FCGX_SetExitStatus(200, r->out); /* OK */
    FCGX_FPrintF(r->out, "Content-Type: application/yang-data+%s\r\n", use_xml?"xml":"json");
    FCGX_FPrintF(r->out, "\r\n");
    FCGX_FPrintF(r->out, "%s", buf);
    FCGX_FPrintF(r->out, "\n\n");
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

    VRB("%s", __FUNCTION__);
    test(r, 1);
    
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
        notfound(r);
        goto ok;
    }
    if (strlen(pvec[0]) != 0){
        retval = notfound(r);
        goto done;
    }
    if (strcmp(pvec[1], RESTCONF_API)){
        retval = notfound(r);
        goto done;
    }
    /* print pvec for debug */
    VRB("pn = %d", pn);
    int i = 0;
    while (pvec[i] != NULL) {
        VRB("pvec[%d] = %s", i, pvec[i]);
        ++i;
    }

    if (pn == 2){
        retval = api_root(ly_ctx,srs, r);
        goto done;
    }
    if ((method = pvec[2]) == NULL){
        retval = notfound(r);
        goto done;
    }
    VRB("%s: method=%s", __FUNCTION__, method);
    if (str2rc_vec(query, '&', '=', &qvec) < 0)
        goto done;
    if (str2rc_vec(path, '/', '=', &pcvec) < 0) /* rest url eg /album=ricky/foo */
        goto done;
    /* data */
    if ((cb = readdata(r)) == NULL)
        goto done;
    data = cbuf_get(cb);
    VRB("%s DATA=%s", __FUNCTION__, data);

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
        test(r, 0);
    }
    else
        notfound(r);
ok:
    retval = 0;
done:
    VRB("%s retval:%d", __FUNCTION__, retval);
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
    
    VRB("%s: Opening FCGX socket: %s", __FUNCTION__, NP2SRV_FCGI_SOCKPATH);

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

int restconf_worker_thread (int idx, ATOMIC_T loop_continue)
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

    while (ATOMIC_LOAD_RELAXED(loop_continue))
    {
        VRB("[%d] in main while loop.", idx);

        if (FCGX_Accept_r(&request) < 0) {
            ERR("[%d] FCGX_Accept_r error.", idx);
            return -1;
        }
        
        VRB("[%d] FCGX accepted, ------------", idx);
        
        if ((path = FCGX_GetParam("REQUEST_URI", request.envp)) != NULL)
        {
            VRB("[%d] path: %s", idx, path);
            
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
            else if (strncmp(path, RESTCONF_WELL_KNOWN, strlen(RESTCONF_WELL_KNOWN)) == 0)
            {
                api_well_known(&request);
            }
            else
            {
                VRB("[%d] top-level %s not found", idx, path);
                notfound(&request);
            }
        }
        else
        {
            VRB("[%d] NULL URI", idx);
            notfound(&request);
        }
                
        FCGX_Finish_r(&request);
    }
    
    return 0;
}
