/**
 * @file restconf_method.c
 * @author Hongcheng Zhong <spartazhc@gmail.com>
 * @brief restconf HTTP method implement:
 * use op_editconfig in netopeer2-server to be base of POST, PUT, PATCH
 * use op_get_config to be base of GET method
 *
 * Copyright (c) 2019 Intel and/or its affiliates.
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

/**
 * @file op_editconfig.c
 * @author Radek Krejci <rkrejci@cesnet.cz>
 * @brief NETCONF <edit-config> operation implementation
 *
 * Copyright (c) 2016 CESNET, z.s.p.o.
 *
 * This source code is licensed under BSD 3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/licenses/BSD-3-Clause
 */
/**
 * @file op_get_config.c
 * @author Radek Krejci <rkrejci@cesnet.cz>
 * @author Michal Vasko <mvasko@cesnet.cz>
 * @brief NETCONF <get> and <get-config> operations implementation
 *
 * Copyright (c) 2016 CESNET, z.s.p.o.
 *
 * This source code is licensed under BSD 3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/licenses/BSD-3-Clause
 */

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <syslog.h>
#include <fcntl.h>
#include <assert.h>
#include <time.h>
#include <signal.h>
#include <limits.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <fcgiapp.h>
#include <sysrepo.h>

#include <nc_server.h>

#include "log.h"
#include "common.h"
#include "restconf_lib.h"
#include "restconf_path.h"

#include "netconf.h"
#include "netconf_nmda.h"

// apanovic@asidua.com 21/11/2021 - these are no longer public and no way to
// extract any data out of them (talk about encapsulation gone mad)
// vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv

struct nc_server_reply {
    NC_RPL type;
};

struct nc_server_reply_data {
    NC_RPL type;
    struct lyd_node *data;
    char free;
    NC_WD_MODE wd;
};

struct nc_server_reply_error {
    NC_RPL type;
    struct ly_ctx *ctx;
    struct nc_server_error **err;
    uint32_t count;
};

// apanovic@asidua.com 21/11/2021 - ^^^^^^^^^^^^^^^^^


enum NP2_EDIT_DEFOP {
    NP2_EDIT_DEFOP_NONE = 0,
    NP2_EDIT_DEFOP_MERGE,
    NP2_EDIT_DEFOP_REPLACE,
};

enum NP2_EDIT_OP {
    NP2_EDIT_ERROR = -1,
    NP2_EDIT_NONE = 0,
    NP2_EDIT_MERGE,
    NP2_EDIT_CREATE,
    NP2_EDIT_REPLACE_INNER,
    NP2_EDIT_REPLACE,
    NP2_EDIT_DELETE,
    NP2_EDIT_REMOVE
};


enum NP2_EDIT_ERROPT {
    NP2_EDIT_ERROPT_STOP,
    NP2_EDIT_ERROPT_CONT,
    NP2_EDIT_ERROPT_ROLLBACK
};

enum NP2_EDIT_TESTOPT {
    NP2_EDIT_TESTOPT_TESTANDSET,
    NP2_EDIT_TESTOPT_SET,
    NP2_EDIT_TESTOPT_TEST
};

/* work for lyd_print_clb */
#define BUFFERSIZE 8192
struct buff {
    char buf[BUFFERSIZE]; /* fix length, not good */
    size_t len;
};

void
rc_write_error(FCGX_Request *r, int use_xml, struct nc_server_error *err)
{
    if (use_xml) {
        FCGX_FPrintF(r->out, "  <error>\n    <error-type>");
    } else {
        FCGX_FPrintF(r->out, "    \"error\" : [\n      {\n        \"error-type\" : \"");
    }
    switch (nc_err_get_type(err)) {
    case NC_ERR_TYPE_TRAN:
        FCGX_FPrintF(r->out, "transport");
        break;
    case NC_ERR_TYPE_RPC:
        FCGX_FPrintF(r->out, "rpc");
        break;
    case NC_ERR_TYPE_PROT:
        FCGX_FPrintF(r->out, "protocol");
        break;
    case NC_ERR_TYPE_APP:
        FCGX_FPrintF(r->out, "application");
        break;
    default:
        EINT;
        return;
    }
    if (use_xml) {
        FCGX_FPrintF(r->out, "</error-type>\n    <error-tag>");
    } else {
        FCGX_FPrintF(r->out, "\",\n        \"error-tag\" : \"");
    }
    switch (nc_err_get_tag(err)) {
    case NC_ERR_IN_USE:
        FCGX_FPrintF(r->out, "in-use");
        break;
    case NC_ERR_INVALID_VALUE:
        FCGX_FPrintF(r->out, "invalid-value");
        break;
    case NC_ERR_TOO_BIG:
        FCGX_FPrintF(r->out, "too-big");
        break;
    case NC_ERR_MISSING_ATTR:
        FCGX_FPrintF(r->out, "missing-attribute");
        break;
    case NC_ERR_BAD_ATTR:
        FCGX_FPrintF(r->out, "bad-attribute");
        break;
    case NC_ERR_UNKNOWN_ATTR:
        FCGX_FPrintF(r->out, "unknown-attribute");
        break;
    case NC_ERR_MISSING_ELEM:
        FCGX_FPrintF(r->out, "missing-element");
        break;
    case NC_ERR_BAD_ELEM:
        FCGX_FPrintF(r->out, "bad-element");
        break;
    case NC_ERR_UNKNOWN_ELEM:
        FCGX_FPrintF(r->out, "unknown-element");
        break;
    case NC_ERR_UNKNOWN_NS:
        FCGX_FPrintF(r->out, "unknown-namespace");
        break;
    case NC_ERR_ACCESS_DENIED:
        FCGX_FPrintF(r->out, "access-denied");
        break;
    case NC_ERR_LOCK_DENIED:
        FCGX_FPrintF(r->out, "lock-denied");
        break;
    case NC_ERR_RES_DENIED:
        FCGX_FPrintF(r->out, "resource-denied");
        break;
    case NC_ERR_ROLLBACK_FAILED:
        FCGX_FPrintF(r->out, "rollback-failed");
        break;
    case NC_ERR_DATA_EXISTS:
        FCGX_FPrintF(r->out, "data-exists");
        break;
    case NC_ERR_DATA_MISSING:
        FCGX_FPrintF(r->out, "data-missing");
        break;
    case NC_ERR_OP_NOT_SUPPORTED:
        FCGX_FPrintF(r->out, "operation-not-supported");
        break;
    case NC_ERR_OP_FAILED:
        FCGX_FPrintF(r->out, "operation-failed");
        break;
    case NC_ERR_MALFORMED_MSG:
        FCGX_FPrintF(r->out, "malformed-message");
        break;
    default:
        EINT;
        return;
    }
    if (use_xml) {
        FCGX_FPrintF(r->out, "</error-tag>\n");
        if (nc_err_get_app_tag(err)) {
            FCGX_FPrintF(r->out, "    <error-app-tag>%s</error-app-tag>\n", nc_err_get_app_tag(err));
        }
        if (nc_err_get_path(err)) {
            FCGX_FPrintF(r->out, "    <error-path>%s</error-path>\n", nc_err_get_path(err));
        }
        if (nc_err_get_msg(err)) {
            FCGX_FPrintF(r->out, "    <error-message>%s</error-message>\n", nc_err_get_msg(err));
        }
        FCGX_FPrintF(r->out, "  </error>\n");
    } else {
        FCGX_FPrintF(r->out, "\",\n");
        if (nc_err_get_app_tag(err)) {
            FCGX_FPrintF(r->out, "        \"error-app-tag\" : \"%s\",\n", nc_err_get_app_tag(err));
        }
        if (nc_err_get_path(err)) {
            FCGX_FPrintF(r->out, "        \"error-path\" : \"%s\",\n", nc_err_get_path(err));
        }
        if (nc_err_get_msg(err)) {
            FCGX_FPrintF(r->out, "        \"error-message\" : \"%s\"\n", nc_err_get_msg(err));
        }
        FCGX_FPrintF(r->out, "      }\n    ]\n  }\n");
    }

}

// according to nc_write_msg_io
int
rc_write_reply(FCGX_Request *r, int use_xml,  NC_MSG_TYPE type, ...)
{
    va_list ap;
    int ret = 0;
    char *model_data = NULL;
    struct nc_server_reply *reply;
    struct nc_server_reply_data *data_rpl;
    struct nc_server_reply_error *error_rpl;
    uint16_t i;

    VRB("%s", __FUNCTION__);

    va_start(ap, type);

    switch (type) {
    case NC_MSG_RPC: /* useless? */
    case NC_MSG_REPLY:
        // rpc_elem = va_arg(ap, struct lyxml_elem *);
        reply = va_arg(ap, struct nc_server_reply *);

        switch (reply->type) { /* according to cli_send_recv in commands.c */
        case NC_RPL_OK:
            FCGX_SetExitStatus(201, r->out); /* Created */
            FCGX_FPrintF(r->out, "Content-Type: text/plain\r\n");
            FCGX_FPrintF(r->out, "\r\n");
            break;
        case NC_RPL_DATA: /* use for GET */
            data_rpl = (struct nc_server_reply_data *)reply;
            if (use_xml){
                lyd_print_mem(&model_data, data_rpl->data, LYD_XML, LYP_FORMAT | LYP_WITHSIBLINGS);
            } else {
                lyd_print_mem(&model_data, data_rpl->data, LYD_JSON, LYP_FORMAT | LYP_WITHSIBLINGS);
            }
            FCGX_SetExitStatus(200, r->out); /* OK */
            FCGX_FPrintF(r->out, "Content-Type: application/yang-data+%s\r\n", use_xml?"xml":"json");
            FCGX_FPrintF(r->out, "\r\n");
            FCGX_FPrintF(r->out, "%s", model_data ? model_data:"");
            FCGX_FPrintF(r->out, "\n\n");
            break;
        case NC_RPL_ERROR:
            error_rpl = (struct nc_server_reply_error *)reply;
            int code = restconf_err2code(nc_err_get_tag(error_rpl->err[i]));
            const char *reason_phrase;
            if ((reason_phrase = restconf_code2reason(code)) == NULL){
                reason_phrase="";
            }
            FCGX_SetExitStatus(code, r->out);
            FCGX_FPrintF(r->out, "Status: %d %s\r\n", code, reason_phrase);
            FCGX_FPrintF(r->out, "Content-Type: application/yang-data+%s\r\n", use_xml?"xml":"json");
            FCGX_FPrintF(r->out, "\r\n");
            if (use_xml) {
                FCGX_FPrintF(r->out, "<errors xmlns=\"urn:ietf:params:xml:ns:yang:ietf-restconf\">\n");
            } else {
                FCGX_FPrintF(r->out, "{\n  \"ietf-restconf:errors\" : {\n");
            }
            /* error content */
            for (i = 0; i < error_rpl->count; ++i) {
                rc_write_error(r, use_xml, error_rpl->err[i]);
            }
            if (use_xml) {
                FCGX_FPrintF(r->out, "</errors>\r\n");
            } else {
                FCGX_FPrintF(r->out, "}\r\n");
            }
            break;
        default:
            EINT;
            ret = -1;
            goto cleanup;
        }
        break;
    case NC_MSG_NOTIF: /* not cencern now */
    default:
        ret = 0;
        goto cleanup;
    }

cleanup:
    va_end(ap);
    return ret;
}

/**
 * used by lyd_print_clb
 */
ssize_t
rc_write_clb(void *arg, const void *buf, size_t count)
{
    int ret = 0;
    struct buff *buff = (struct buff *)arg;

    memcpy(&buff->buf[buff->len], buf, count);
    buff->len += count;
    ret += count;

    return ret;
}

/*! REST OPTIONS method
 * According to restconf
 * @param[in]  r      Fastcgi request handle
 * @code
 *  curl -G http://localhost/restconf/data/interfaces/interface=eth0
 * @endcode
 * Minimal support:
 * 200 OK
 * Allow: HEAD,GET,PUT,DELETE,OPTIONS
 */
int
api_data_options(
            FCGX_Request *r)
{
    VRB("%s", __FUNCTION__);
    FCGX_SetExitStatus(200, r->out); /* OK */
    FCGX_FPrintF(r->out, "Allow: OPTIONS,HEAD,GET,POST,PUT,DELETE\r\n");
    FCGX_FPrintF(r->out, "\r\n");
    return 0;
}

/*! Generic GET (both HEAD and GET)
 * According to restconf
 * @param[in]  r      Fastcgi request handle
 * @param[in]  pcvec  Vector of path ie DOCUMENT_URI element
 * @param[in]  pi     Offset, where path starts
 * @param[in]  qvec   Vector of query string (QUERY_STRING)
 * @param[in]  use_xml Set to 0 for JSON and 1 for XML
 * @param[in]  head   If 1 is HEAD, otherwise GET
 * @code
 *  curl -G http://localhost/restconf/data/interfaces/interface=eth0
 * @endcode
 * See RFC8040 Sec 4.2 and 4.3
 * Request may contain
 *     Accept: application/yang.data+json,application/yang.data+xml
 * Response contains one of:
 *     Content-Type: application/yang-data+xml
 *     Content-Type: application/yang-data+json
 * NOTE: If a retrieval request for a data resource representing a YANG leaf-
 * list or list object identifies more than one instance, and XML
 * encoding is used in the response, then an error response containing a
 * "400 Bad Request" status-line MUST be returned by the server.
 * Netconf: <get-config>, <get>
 */
static int
api_data_get_base(struct ly_ctx    *ly_ctx,
                  sr_session_ctx_t *srs,
                  FCGX_Request *r,
                  rc_vec       *pcvec,
                  int           pi,
                  rc_vec       *qvec,
                  int           use_xml,
                  int           head)
{
    int  retval;
    struct lyd_node *root = NULL;
    struct nc_server_reply *reply = NULL;
    struct lyd_node *data = NULL, *node = NULL;
    const struct lys_module *ietfnc = NULL;
    const struct lys_module *ietfnc_nmda = NULL;
    const struct lys_module *ietf_ds = NULL;
    char xpath[80] = {'\0'};

    (void) qvec;

    VRB("%s", __FUNCTION__);
    /* copy from libnetconf2/src/session_client.c/nc_send_rpc */
    ietfnc = ly_ctx_get_module(ly_ctx, "ietf-netconf", NULL, 1);
    ietfnc_nmda = ly_ctx_get_module(ly_ctx, "ietf-netconf-nmda", NULL, 1);
    
    if (!ietfnc) {
        ERR("Session : missing \"ietf-netconf\" schema in the context.");
        return NC_MSG_ERROR;
    }

    if (!ietfnc_nmda) {
        ERR("Session : missing \"ietf-netconf-nmda\" schema in the context.");
        return NC_MSG_ERROR;
    }

    ietf_ds = ly_ctx_get_module(ly_ctx, "ietf-datastores", NULL, 1);
    if (!ietf_ds) {
        ERR("Session : missing \"ietf-datastores\" schema in the context.");
        return NC_MSG_ERROR;
    }

    /* convert pcvec to xpath */
    if ((retval = api_path2xpath(ly_ctx, pcvec, pi, xpath)))
        goto bail;
        
    VRB("%s xpath = %s", __FUNCTION__, xpath);

    /* Prepare lyd_node for netopeer2-cli to construct rpc
     * which is same to netopeer2-server decode rpc (without *priv)
     **/
    data = lyd_new(NULL, ietfnc_nmda, "get-data");
    node = lyd_new_leaf(data, ietfnc_nmda, "datastore", "ietf-datastores:operational");
    node = lyd_new_leaf(data, ietfnc_nmda, "max-depth", "unbounded");
    /*
      node = lyd_new_anydata(data, NULL, "subtree-filter", NULL, LYD_ANYDATA_CONSTSTRING);

    if (!node) {
        lyd_free(data);
        return NC_MSG_ERROR;
     }

    
    lyd_insert_attr(node, NULL, "type", "xpath");
    lyd_insert_attr(node, NULL, "select", xpath);
    */    
    root = lyd_new(NULL, ietfnc_nmda, "get-data");

    retval = np2srv_rpc_getdata_cb(srs, "/ietf-netconf:get-data", data, SR_EV_RPC, 0, root, NULL);

    //lyd_print_file(stdout, root, LYD_XML, LYP_FORMAT | LYP_WITHSIBLINGS);
    
 bail:

    if (retval)
    {
        struct nc_server_error *e = nc_err(NC_ERR_OP_FAILED,
                                           NC_ERR_TYPE_APP);
        reply = nc_server_reply_err(e);
        nc_err_free(e);
    }
    else
    {
        if (head)
        {
            reply = nc_server_reply_data(NULL, NC_WD_UNKNOWN, NC_PARAMTYPE_CONST);
        }
        else
        {
            reply = nc_server_reply_data(root, NC_WD_UNKNOWN, NC_PARAMTYPE_CONST);
        }
    }

    retval = rc_write_reply(r, use_xml, NC_MSG_REPLY, reply);
    nc_server_reply_free(reply);

    lyd_free_withsiblings(data);
    lyd_free_withsiblings(root);
    
    if (pcvec)
        rc_vec_free(pcvec);
    
    VRB("%s retval:%d", __FUNCTION__, retval);
    
    return retval;
}

/*! REST HEAD method
 * @param[in]  ly_ctx libyang context
 * @param[in]  sessions server <-> sysrepo session
 * @param[in]  r      Fastcgi request handle
 * @param[in]  pcvec  Vector of path ie DOCUMENT_URI element
 * @param[in]  pi     Offset, where path starts
 * @param[in]  qvec   Vector of query string (QUERY_STRING)
 * @param[in]  use_xml Set to 0 for JSON and 1 for XML
 *
 * The HEAD method is sent by the client to retrieve just the header fields
 * that would be returned for the comparable GET method, without the
 * response message-body.
 * Relation to netconf: none
 */
int
api_data_head(struct ly_ctx *ly_ctx,
          sr_session_ctx_t *srs,
	      FCGX_Request *r,
	      rc_vec       *pcvec,
	      int           pi,
          rc_vec       *qcvec,
	      int           use_xml)
{
    return api_data_get_base(ly_ctx, srs, r, pcvec, pi, qcvec, use_xml, 1);
}

/*! REST GET method
 * According to restconf
 * @param[in]  ly_ctx libyang context
 * @param[in]  sessions server <-> sysrepo session
 * @param[in]  r      Fastcgi request handle
 * @param[in]  pcvec  Vector of path ie DOCUMENT_URI element
 * @param[in]  pi     Offset, where path starts
 * @param[in]  qvec   Vector of query string (QUERY_STRING)
 * @param[in]  use_xml Set to 0 for JSON and 1 for XML
 * @code
 *  curl -G http://localhost/restconf/data/interfaces/interface=eth0
 * @endcode
 * XXX: cant find a way to use Accept request field to choose Content-Type
 *      I would like to support both xml and json.
 * Request may contain
 *     Accept: application/yang.data+json,application/yang.data+xml
 * Response contains one of:
 *     Content-Type: application/yang-data+xml
 *     Content-Type: application/yang-data+json
 * NOTE: If a retrieval request for a data resource representing a YANG leaf-
 * list or list object identifies more than one instance, and XML
 * encoding is used in the response, then an error response containing a
 * "400 Bad Request" status-line MUST be returned by the server.
 * Netconf: <get-config>, <get>
 */
int
api_data_get(struct ly_ctx *ly_ctx,
             sr_session_ctx_t *srs,
             FCGX_Request *r,
             rc_vec       *pcvec,
             int           pi,
             rc_vec       *qcvec,
             int           use_xml)
{
    return api_data_get_base(ly_ctx, srs, r, pcvec, pi, qcvec, use_xml, 0);
}

/*! GET restconf/operations resource
 * @param[in]  ly_ctx libyang context
 * @param[in]  sessions server <-> sysrepo session
 * @param[in]  r      Fastcgi request handle
 * @param[in]  path   According to restconf (Sec 3.5.1.1 in [draft])
 * @param[in]  pcvec  Vector of path ie DOCUMENT_URI element
 * @param[in]  pi     Offset, where path starts
 * @param[in]  qvec   Vector of query string (QUERY_STRING)
 * @param[in]  data   Stream input data
 * @param[in]  pretty Set to 1 for pretty-printed xml/json output
 * @param[in]  use_xml Set to 0 for JSON and 1 for XML
 *
 * @code
 *  curl -G http://localhost/restconf/operations
 * @endcode
 * RFC8040 Sec 3.3.2:
 * This optional resource is a container that provides access to the
 * data-model-specific RPC operations supported by the server.  The
 * server MAY omit this resource if no data-model-specific RPC
 * operations are advertised.
 * From ietf-restconf.yang:
 * In XML, the YANG module namespace identifies the module:
 *      <system-restart xmlns='urn:ietf:params:xml:ns:yang:ietf-system'/>
 * In JSON, the YANG module name identifies the module:
 *       { 'ietf-system:system-restart' : [null] }
 */
int
api_operations_get(struct ly_ctx *ly_ctx,
                sr_session_ctx_t *srs,
                FCGX_Request *r,
                rc_vec       *qvec,
                int           use_xml)
{
    int retval = -1;
#if 0
    cbuf *cbx = NULL;
    const struct lys_module *mod;
    struct lys_node *start, *snode, *next;
    sr_schema_t *schemas = NULL;
    size_t schema_count, i, j=0;

    VRB("%s", __FUNCTION__);
    if ((cbx = cbuf_new()) == NULL) {
        goto done;
    }
    if (np2srv_sr_list_schemas(srs, &schemas, &schema_count, NULL)) {
        return -1;
    }
    if (use_xml) {
        cprintf(cbx, "<operations>");
    } else {
        cprintf(cbx, "{\"operations\": {");
    }
    /* 1) use modules from sysrepo */
    for (i = 0; i < schema_count; i++) {
        if ((mod = ly_ctx_get_module(ly_ctx, schemas[i].module_name, schemas[i].revision.revision, 0))) {
            start = mod->data;
            LY_TREE_DFS_BEGIN(start, next, snode) {
                if (snode->nodetype & (LYS_RPC | LYS_ACTION)) {
                    if (use_xml) {
                        cprintf(cbx, "<%s xmlns=\"%s\"/>", snode->name, mod->ns);
                    } else {
                        if (j++) {
                            cprintf(cbx, ",");
                        }
                        cprintf(cbx, "\"%s:%s\": null", mod->name, snode->name);
                    }
                    goto dfs_nextsibling;
                }

                /* modified LY_TREE_DFS_END() */
                next = snode->child;
                /* child exception for leafs, leaflists and anyxml without children */
                if (snode->nodetype & (LYS_LEAF | LYS_LEAFLIST | LYS_ANYDATA)) {
                    next = NULL;
                }
                if (!next) {
                    /* no children */
        dfs_nextsibling:
                    /* try siblings */
                    next = snode->next;
                }
                while (!next) {
                    /* parent is already processed, go to its sibling */
                    snode = lys_parent(snode);
                    if (!snode) {
                        /* we are done, no next element to process */
                        break;
                    }
                    next = snode->next;
                }
            }
        }
    }
    if (use_xml) {
        cprintf(cbx, "</operations>");
    } else {
        cprintf(cbx, "}}");
    }


    FCGX_SetExitStatus(200, r->out); /* OK */
    FCGX_FPrintF(r->out, "Content-Type: application/yang-data+%s\r\n", use_xml?"xml":"json");
    FCGX_FPrintF(r->out, "\r\n");
    FCGX_FPrintF(r->out, "%s", cbx?cbuf_get(cbx):"");
    FCGX_FPrintF(r->out, "\r\n\r\n");
    retval = 0;
done:

    if (cbx)
        cbuf_free(cbx);
    
#endif    
    VRB("%s retval:%d", __FUNCTION__, retval);
    return retval;
}
///////////////////////////////////////////////////////////////
/* from netopeer2 op_editconfig */
static enum NP2_EDIT_OP
edit_get_op(struct lyd_node *node, enum NP2_EDIT_OP parentop, enum NP2_EDIT_DEFOP defop, enum NP2_EDIT_OP restconfop)
{
    enum NP2_EDIT_OP retval = NP2_EDIT_ERROR;
#if 0
    struct lyd_attr *attr;

    if (restconfop > -1) {
        retval = restconfop;
        return retval;
    }

    assert(node);

    /* TODO check conflicts between parent and current operations */
    for (attr = node->attr; attr; attr = attr->next) {
        if (!strcmp(attr->name, "operation") &&
                !strcmp(attr->annotation->module->name, "ietf-netconf")) {
            /* NETCONF operation attribute */
            if (!strcmp(attr->value_str, "create")) {
                retval = NP2_EDIT_CREATE;
            } else if (!strcmp(attr->value_str, "delete")) {
                retval = NP2_EDIT_DELETE;
            } else if (!strcmp(attr->value_str, "remove")) {
                retval = NP2_EDIT_REMOVE;
            } else if (!strcmp(attr->value_str, "replace")) {
                retval = NP2_EDIT_REPLACE;
            } else if (!strcmp(attr->value_str, "merge")) {
                retval = NP2_EDIT_MERGE;
            } /* else invalid attribute checked by libyang */

            goto cleanup;
        }
    }

    switch (parentop) {
    case NP2_EDIT_REPLACE:
        return NP2_EDIT_REPLACE_INNER;
    case NP2_EDIT_NONE:
        switch (defop) {
        case NP2_EDIT_DEFOP_NONE:
            return NP2_EDIT_NONE;
        case NP2_EDIT_DEFOP_MERGE:
            return NP2_EDIT_MERGE;
        case NP2_EDIT_DEFOP_REPLACE:
            return NP2_EDIT_REPLACE;
        default:
            EINT;
            return 0;
        }
    default:
        return parentop;
    }

cleanup:

    lyd_free_attr(node->schema->module->ctx, node, attr, 0);
#endif
    return retval;
}

static int
edit_get_move(struct lyd_node *node, const char *path, sr_move_position_t *pos, char **rel)
{
#if 0
    const char *name, *format;
    struct lyd_attr *attr_iter;

    if (node->schema->nodetype & LYS_LIST) {
        name = "key";
        format = "%s%s";
    } else {
        name = "value";
        format = "%s[.=\'%s\']";
    }

    for(attr_iter = node->attr; attr_iter; attr_iter = attr_iter->next) {
        if (!strcmp(attr_iter->annotation->module->name, "yang")) {
            if (!strcmp(attr_iter->name, "insert")) {
                if (!strcmp(attr_iter->value_str, "first")) {
                    *pos = SR_MOVE_FIRST;
                } else if (!strcmp(attr_iter->value_str, "last")) {
                    *pos = SR_MOVE_LAST;
                } else if (!strcmp(attr_iter->value_str, "before")) {
                    *pos = SR_MOVE_BEFORE;
                } else if (!strcmp(attr_iter->value_str, "after")) {
                    *pos = SR_MOVE_AFTER;
                }
            } else if (!strcmp(attr_iter->name, name)) {
                if (asprintf(rel, format, path, attr_iter->value_str) < 0) {
                    ERR("%s: memory allocation failed (%s) - %s:%d",
                        __func__, strerror(errno), __FILE__, __LINE__);
                    return -1;
                }
            }
        }
    }
#endif
    return 0;
}

static const char *
defop2str(enum NP2_EDIT_DEFOP defop)
{
    switch (defop) {
    case NP2_EDIT_DEFOP_MERGE:
        return "merge";
    case NP2_EDIT_DEFOP_REPLACE:
        return "replace";
    default:
        break;
    }

    return "none";
}

static const char *
op2str(enum NP2_EDIT_OP op)
{
    switch (op) {
    case NP2_EDIT_ERROR:
        return "error";
    case NP2_EDIT_MERGE:
        return "merge";
    case NP2_EDIT_CREATE:
        return "create";
    case NP2_EDIT_REPLACE:
        return "replace";
    case NP2_EDIT_REPLACE_INNER:
        return "inner replace";
    case NP2_EDIT_DELETE:
        return "delete";
    case NP2_EDIT_REMOVE:
        return "remove";
    default:
        break;
    }

    return "none";
}

struct nc_server_reply *
op_editconfig(struct lyd_node *rpc, sr_session_ctx_t *srs, enum NP2_EDIT_OP restconfop)
{
    struct nc_server_error *e = NULL;
    struct nc_server_reply *ereply = NULL;
#if 0
    sr_datastore_t ds = 0;
    sr_move_position_t pos = SR_MOVE_LAST;
    sr_val_t value;
    struct ly_set *nodeset;
    /* default value for default-operation is "merge" */
    enum NP2_EDIT_DEFOP defop = NP2_EDIT_DEFOP_MERGE;
    /* default value for test-option is "test-then-set" */
    enum NP2_EDIT_TESTOPT testopt = NP2_EDIT_TESTOPT_TESTANDSET;
    /* default value for error-option is "stop-on-error" */
    enum NP2_EDIT_ERROPT erropt = NP2_EDIT_ERROPT_STOP;
    struct lyd_node *config = NULL, *next, *iter;
    char *str, *path = NULL, *rel, *valbuf, quot;
    const char *cstr;
    enum NP2_EDIT_OP *op = NULL, *op_new;
    uint16_t *path_levels = NULL, *path_levels_new;
    uint16_t path_levels_index, path_levels_size = 0;
    int op_index, op_size, path_index = 0, missing_keys = 0, lastkey = 0, np_cont;
    int ret, path_len, new_len;
#ifdef NP2SRV_ENABLED_URL_CAPABILITY
    const char* urlval;
#endif


    if (np2srv_sr_check_exec_permission(srs, "/ietf-netconf:edit-config", &ereply)) {
        goto cleanup;
    }

    /* init */
    path_len = 128;
    path = malloc(path_len);
    if (!path) {
        EMEM;
        goto internalerror;
    }
    path[path_index] = '\0';

    /*
     * parse parameters
     */

    /* target */
    nodeset = lyd_find_path(rpc, "/ietf-netconf:edit-config/target/*");
    cstr = nodeset->set.d[0]->schema->name;
    ly_set_free(nodeset);

    if (!strcmp(cstr, "running")) {
        ds = SR_DS_RUNNING;
    } else if (!strcmp(cstr, "candidate")) {
        ds = SR_DS_CANDIDATE;
    }
    /* edit-config on startup is not allowed by RFC 6241 */
    if (ds != sessions->ds) {
        /* update sysrepo session */
        if (np2srv_sr_session_switch_ds(srs, ds, &ereply)) {
            goto cleanup;
        }
        sessions->ds = ds;
    }

    /* default-operation */
    nodeset = lyd_find_path(rpc, "/ietf-netconf:edit-config/default-operation");
    if (nodeset->number) {
        cstr = ((struct lyd_node_leaf_list*)nodeset->set.d[0])->value_str;
        if (!strcmp(cstr, "replace")) {
            defop = NP2_EDIT_DEFOP_REPLACE;
        } else if (!strcmp(cstr, "none")) {
            defop = NP2_EDIT_DEFOP_NONE;
        } else if (!strcmp(cstr, "merge")) {
            defop = NP2_EDIT_DEFOP_MERGE;
        }
    }
    ly_set_free(nodeset);

    /* test-option */
    nodeset = lyd_find_path(rpc, "/ietf-netconf:edit-config/test-option");
    if (nodeset->number) {
        cstr = ((struct lyd_node_leaf_list*)nodeset->set.d[0])->value_str;
        if (!strcmp(cstr, "set")) {
            testopt = NP2_EDIT_TESTOPT_SET;
        } else if (!strcmp(cstr, "test-only")) {
            testopt = NP2_EDIT_TESTOPT_TEST;
        } else if (!strcmp(cstr, "test-then-set")) {
            testopt = NP2_EDIT_TESTOPT_TESTANDSET;
        }
    }
    ly_set_free(nodeset);

    /* error-option */
    nodeset = lyd_find_path(rpc, "/ietf-netconf:edit-config/error-option");
    if (nodeset->number) {
        cstr = ((struct lyd_node_leaf_list*)nodeset->set.d[0])->value_str;
        if (!strcmp(cstr, "rollback-on-error")) {
            erropt = NP2_EDIT_ERROPT_ROLLBACK;
        } else if (!strcmp(cstr, "continue-on-error")) {
            erropt = NP2_EDIT_ERROPT_CONT;
        } else if (!strcmp(cstr, "stop-on-error")) {
            erropt = NP2_EDIT_ERROPT_STOP;
        }
    }
    ly_set_free(nodeset);


    /* config */
    nodeset = lyd_find_path(rpc, "/ietf-netconf:edit-config/config");
    if (nodeset->number) {
        config = op_import_anydata((struct lyd_node_anydata *)nodeset->set.d[0], LYD_OPT_EDIT | LYD_OPT_STRICT, &ereply);
        if (!config) {
            ly_set_free(nodeset);
            goto cleanup;
        }
    }
    ly_set_free(nodeset);

    if (config == NULL) {
        EINT;
        goto internalerror;
    }

    lyd_print_mem(&str, config, LYD_XML, LYP_WITHSIBLINGS | LYP_FORMAT);
    VRB("EDIT_CONFIG: ds %d, defop %s, testopt %d, config:\n%s", srs, defop2str(defop), testopt, str);
    free(str);
    str = NULL;

    if (sessions->ds != SR_DS_CANDIDATE) {
        /* update data from sysrepo */
        if (np2srv_sr_session_refresh(srs, &ereply)) {
            goto cleanup;
        }
    }

    /*
     * data manipulation
     */
    valbuf = NULL;
    path_levels_size = op_size = 16;
    op = malloc(op_size * sizeof *op);
    op[0] = NP2_EDIT_NONE;
    op_index = 0;
    path_levels = malloc(path_levels_size * sizeof *path_levels);
    path_levels_index = 0;
    LY_TREE_DFS_BEGIN(config, next, iter) {

        /* maintain list of operations */
        if (!missing_keys) {
            op_index++;
            if (op_index == op_size) {
                op_size += 16;
                op_new = realloc(op, op_size * sizeof *op);
                if (!op_new) {
                    EMEM;
                    goto internalerror;
                }
                op = op_new;
            }
            op[op_index] = edit_get_op(iter, op[op_index - 1], defop, restconfop);

            /* maintain path */
            if (path_levels_index == path_levels_size) {
                path_levels_size += 16;
                path_levels_new = realloc(path_levels, path_levels_size * sizeof *path_levels);
                if (!path_levels_new) {
                    EMEM;
                    goto internalerror;
                }
                path_levels = path_levels_new;
            }
            path_levels[path_levels_index++] = path_index;
            if (!iter->parent || lyd_node_module(iter) != lyd_node_module(iter->parent)) {
                /* with prefix */
                new_len = path_index + 1 + strlen(lyd_node_module(iter)->name) + 1 + strlen(iter->schema->name) + 1;
                if (new_len > path_len) {
                    path_len = new_len;
                    path = realloc(path, new_len);
                }
                path_index += sprintf(&path[path_index], "/%s:%s", lyd_node_module(iter)->name, iter->schema->name);
            } else {
                /* without prefix */
                new_len = path_index + 1 + strlen(iter->schema->name) + 1;
                if (new_len > path_len) {
                    path_len = new_len;
                    path = realloc(path, new_len);
                }
                path_index += sprintf(&path[path_index], "/%s", iter->schema->name);
            }

            /* erase value */
            memset(&value, 0, sizeof value);
        }

        /* specific work for different node types */
        ret = -1;
        rel = NULL;
        lastkey = 0;
        np_cont = 0;
        switch (iter->schema->nodetype) {
        case LYS_CONTAINER:
            if (!((struct lys_node_container *)iter->schema)->presence) {
                np_cont = 1;
            }
            if ((op[op_index] < NP2_EDIT_REPLACE) && np_cont) {
                /* do nothing, creating non-presence containers is not necessary */
                goto dfs_continue;
            }


            DBG("EDIT_CONFIG: %s container %s, operation %s", (!np_cont ? "presence" : ""), path, op2str(op[op_index]));
            break;
        case LYS_LEAF:
            if (missing_keys) {
                /* still processing list keys */
                missing_keys--;
                /* add key predicate into the list's path */
                new_len = path_index + 1 + strlen(iter->schema->name) + 2
                          + strlen(((struct lyd_node_leaf_list *)iter)->value_str) + 3;
                if (new_len > path_len) {
                    path_len = new_len;
                    path = realloc(path, new_len);
                }

                if (strchr(((struct lyd_node_leaf_list *)iter)->value_str, '\'')) {
                    quot = '\"';
                } else {
                    quot = '\'';
                }
                path_index += sprintf(&path[path_index], "[%s=%c%s%c]", iter->schema->name, quot,
                                      ((struct lyd_node_leaf_list *)iter)->value_str, quot);
                if (!missing_keys) {
                    /* the last key, create the list instance */
                    lastkey = 1;

                    DBG("EDIT_CONFIG: list %s, operation %s", path, op2str(op[op_index]));
                    break;
                }
                goto dfs_continue;
            }

            /* regular leaf */
            DBG("EDIT_CONFIG: leaf %s, operation %s", path, op2str(op[op_index]));
            break;
        case LYS_LEAFLIST:
            /* get info about inserting to a specific place */
            if (edit_get_move(iter, path, &pos, &rel)) {
                goto internalerror;
            }

            DBG("EDIT_CONFIG: leaflist %s, operation %s", path, op2str(op[op_index]));
            if (pos != SR_MOVE_LAST) {
                DBG("EDIT_CONFIG: moving leaflist %s, position %d (%s)", path, pos, rel ? rel : "absolute");
            }

            /* in leaf-list, the value is also the key, so add it into the path */
            new_len = path_index + 4 + strlen(((struct lyd_node_leaf_list *)iter)->value_str) + 3;
            if (new_len > path_len) {
                path_len = new_len;
                path = realloc(path, new_len);
            }
            if (strchr(((struct lyd_node_leaf_list *)iter)->value_str, '\'')) {
                quot = '\"';
            } else {
                quot = '\'';
            }
            path_index += sprintf(&path[path_index], "[.=%c%s%c]", quot, ((struct lyd_node_leaf_list *)iter)->value_str, quot);

            break;
        case LYS_LIST:
            /* get info about inserting to a specific place */
            if (edit_get_move(iter, path, &pos, &rel)) {
                goto internalerror;
            }

            if (op[op_index] < NP2_EDIT_DELETE) {
                /* set value for sysrepo, it will be used as soon as all the keys are processed */
                op_set_srval(iter, NULL, 0, &value, &valbuf);
            }

            /* the creation must be finished later when we get know keys */
            missing_keys = ((struct lys_node_list *)iter->schema)->keys_size;
            goto dfs_continue;
        case LYS_ANYXML:
        case LYS_ANYDATA:
            /* nothing special needed, not even supported by sysrepo */
            break;
        default:
            ERR("%s: Invalid node to process", __func__);
            goto internalerror;
        }

        if ((op[op_index] < NP2_EDIT_DELETE) && !lastkey) {
            /* set value for sysrepo */
            op_set_srval(iter, NULL, 0, &value, &valbuf);
        }

        /* apply change to sysrepo */
        switch (op[op_index]) {
        case NP2_EDIT_MERGE:
            /* create the node */
            if (!np_cont) {
                ret = np2srv_sr_set_item(srs, path, &value, 0, &ereply);
            }
            break;
        case NP2_EDIT_REPLACE_INNER:
        case NP2_EDIT_CREATE:
            /* create the node, but it must not exists */
            ret = np2srv_sr_set_item(srs, path, &value, SR_EDIT_STRICT, &ereply);
            break;
        case NP2_EDIT_DELETE:
            /* remove the node, but it must exists */
            ret = np2srv_sr_delete_item(srs, path, SR_EDIT_STRICT, &ereply);
            break;
        case NP2_EDIT_REMOVE:
            /* remove the node */
            ret = np2srv_sr_delete_item(srs, path, 0, &ereply);
            break;
        case NP2_EDIT_REPLACE:
            /* remove the node first */
            ret = np2srv_sr_delete_item(srs, path, 0, &ereply);
            /* create it again (but we removed all the children, sysrepo forbids creating NP containers as it's redundant) */
            if (!ret && !np_cont) {
                ret = np2srv_sr_set_item(srs, path, &value, 0, &ereply);
            }
            break;
        default:
            /* do nothing */
            ret = 0;
            break;
        }
        if (valbuf) {
            free(valbuf);
            valbuf = NULL;
        }

resultcheck:
        /* check the result */
        if (!ret) {
            DBG("EDIT_CONFIG: success (%s).", path);
        } else {
            switch (erropt) {
            case NP2_EDIT_ERROPT_CONT:
                DBG("EDIT_CONFIG: continue-on-error (%s).", nc_err_get_msg(nc_server_reply_get_last_err(ereply)));
                goto dfs_nextsibling;
            case NP2_EDIT_ERROPT_ROLLBACK:
                DBG("EDIT_CONFIG: rollback-on-error (%s).", nc_err_get_msg(nc_server_reply_get_last_err(ereply)));
                goto cleanup;
            case NP2_EDIT_ERROPT_STOP:
                DBG("EDIT_CONFIG: stop-on-error (%s).", nc_err_get_msg(nc_server_reply_get_last_err(ereply)));
                goto cleanup;
            }
        }

        /* move user-ordered list/leaflist */
        if (pos != SR_MOVE_LAST) {
            ret = np2srv_sr_move_item(srs, path, pos, rel, &ereply);
            free(rel);
            pos = SR_MOVE_LAST;
            goto resultcheck;
        }

        if ((op[op_index] == NP2_EDIT_DELETE) || (op[op_index] == NP2_EDIT_REMOVE)
                || ((op[op_index] == NP2_EDIT_CREATE) && (ret == SR_ERR_DATA_EXISTS))) {
            /* when delete, remove subtree, or failed create
             * no need to go into children */
            if (lastkey) {
                /* we were processing list's keys */
                goto dfs_parent;
            } else {
                goto dfs_nextsibling;
            }
        }

dfs_continue:
        /* where go next? - modified LY_TREE_DFS_END */
        if (iter->schema->nodetype & (LYS_LEAF | LYS_LEAFLIST | LYS_ANYXML)) {
            next = NULL;
        } else {
            next = iter->child;
        }
        if (!next) {
dfs_nextsibling:
            /* no children, try siblings */
            next = iter->next;

            /* maintain "stack" variables */
            if (!missing_keys && !lastkey) {
            	assert(op_index > 0);
                assert(path_levels_index > 0);
                op_index--;
                path_levels_index--;
                path_index = path_levels[path_levels_index];
                path[path_index] = '\0';
            }
        }
        while (!next) {
dfs_parent:
            iter = iter->parent;

            /* parent is already processed, go to its sibling */
            if (!iter) {
                /* we are done */
                break;
            }
            next = iter->next;

            /* maintain "stack" variables */
            if (!missing_keys) {
            	assert(op_index > 0);
                assert(path_levels_index > 0);
                op_index--;
                path_levels_index--;
                path_index = path_levels[path_levels_index];
                path[path_index] = '\0';
            }

        }
        /* end of modified LY_TREE_DFS_END */
    }

cleanup:
    /* cleanup */
    free(path);
    path = NULL;
    free(op);
    op = NULL;
    free(path_levels);
    path_levels = NULL;
    lyd_free_withsiblings(config);
    config = NULL;

    /* just rollback and return error */
    if ((erropt == NP2_EDIT_ERROPT_ROLLBACK) && ereply) {
        np2srv_sr_discard_changes(srs, NULL);
        return ereply;
    }

    switch (testopt) {
    case NP2_EDIT_TESTOPT_SET:
        VRB("edit-config test-option \"set\" not supported, validation will be performed.");
        /* fallthrough */
    case NP2_EDIT_TESTOPT_TESTANDSET:
        /* commit changes */
        if (np2srv_sr_commit(srs, &ereply)) {
            np2srv_sr_discard_changes(srs, NULL); /* rollback the changes */
        }
        if (sessions->ds == SR_DS_CANDIDATE) {
            /* mark candidate as modified */
            sessions->flags |= NP2S_CAND_CHANGED;
        }
        break;
    case NP2_EDIT_TESTOPT_TEST:
        np2srv_sr_discard_changes(srs, NULL);
        break;
    }

    if (ereply) {
        return ereply;
    }

    /* build positive RPC Reply */
    DBG("EDIT_CONFIG: success.");
    return nc_server_reply_ok();

internalerror:
    e = nc_err(NC_ERR_OP_FAILED, NC_ERR_TYPE_APP);
    nc_err_set_msg(e, np2log_lasterr(np2srv.ly_ctx), "en");
    if (ereply) {
        nc_server_reply_add_err(ereply, e);
    } else {
        ereply = nc_server_reply_err(e);
    }

    /* fatal error, so continue-on-error does not apply here,
     * instead we rollback */
    DBG("EDIT_CONFIG: fatal error, rolling back.");
    np2srv_sr_discard_changes(srs, NULL);

    free(path);
    free(op);
    free(path_levels);
    lyd_free_withsiblings(config);
#endif
    return ereply;
}

/** not right yet, do not use */
int
op_commit(struct lyd_node *UNUSED(rpc), struct nc_session *ncs)
{
#if 0
    sr_session_ctx_t *srs;
    struct nc_server_reply *ereply = NULL;

    /* get sysrepo connections for this session */
    sessions = (struct np2_sessions *)nc_session_get_data(ncs);

    if (np2srv_sr_check_exec_permission(srs, "/ietf-netconf:commit", &ereply)) {
        goto finish;
    }

    if (np2srv_sr_copy_config(srs, NULL, SR_DS_CANDIDATE, SR_DS_RUNNING, &ereply)) {
        goto finish;
    }

    /* remove modify flag */
    sessions->flags &= ~NP2S_CAND_CHANGED;

    // ereply = nc_server_reply_ok();

finish:
#endif
    return 0;
}

int api_data_post(struct ly_ctx *ly_ctx,
                sr_session_ctx_t *srs,
                FCGX_Request *r,
                rc_vec       *pcvec,
                char         *api_path,
                int           pi,
                rc_vec       *qvec,
                char*         content,
                int           use_xml)
{
    int ret = 0;
#if 0
    struct nc_server_reply *reply;
    int target_running = 1;
    char **pvec = NULL;
    int pn;
    struct lyd_node *node, *data;
    const struct lys_module *ietfnc = NULL;
    cbuf *cxml = NULL;
    VRB("%s", __FUNCTION__);

    ietfnc = ly_ctx_get_module(ly_ctx, "ietf-netconf", NULL, 1);
    if (!ietfnc) {
        ERR("Session : missing \"ietf-netconf\" schema in the context.");
        return NC_MSG_ERROR;
    }
    /**
     * If the NETCONF server supports **:writable-running**,
     * all edits to configuration nodes in {+restconf}/data are
     * performed in the running configuration datastore.
     */
    if (!lys_features_state(ietfnc, "writable-running") && lys_features_state(ietfnc,"candidate")) {
        target_running = 0;
    }

    data = lyd_new(NULL, ietfnc, "edit-config");
    node = lyd_new(data, ietfnc, "target");
    node = lyd_new_leaf(node, ietfnc, target_running ? "running":"candidate", NULL);
    if (!node) {
        lyd_free(data);
        return NC_MSG_ERROR;
    }
    node = lyd_new_leaf(data, ietfnc, "default-operation", "merge");
    if (!node) {
        lyd_free(data);
        return NC_MSG_ERROR;
    }
    node = lyd_new_leaf(data, ietfnc, "test-option", "test-then-set");
    if (!node) {
        lyd_free(data);
        return NC_MSG_ERROR;
    }
    node = lyd_new_leaf(data, ietfnc, "error-option", "stop-on-error");
    if (!node) {
        lyd_free(data);
        return NC_MSG_ERROR;
    }
    pvec = clicon_strsep(api_path, "/", &pn);
    /* Remove trailing '/'. Like in /a/ -> /a */
    if (pn > 1 && !strlen(pvec[pn-1])) {
        pn--;
    }
    if (pn > 3) { /* means that localhost/restconf/data/xxx:xxx */
        if ((cxml = cbuf_new()) == NULL)
        goto done;
        api_path2xml(ly_ctx, pcvec, pi, content, 1, use_xml, cxml);
        VRB("cxml: %s", cbuf_get(cxml));
        if (use_xml) {
            node = lyd_new_anydata(data, ietfnc, "config", cbuf_get(cxml), LYD_ANYDATA_SXML);
        } else {
            node = lyd_new_anydata(data, ietfnc, "config", cbuf_get(cxml), LYD_ANYDATA_JSON);
        }
    } else { /* datastore */
        VRB("%s", content);
        if (use_xml) {
            node = lyd_new_anydata(data, ietfnc, "config", content, LYD_ANYDATA_SXML);
        } else {
            node = lyd_new_anydata(data, ietfnc, "config", content, LYD_ANYDATA_JSON);
        }
    }
    if (!node) {
        lyd_free(data);
        return NC_MSG_ERROR;
    }
    if (!data) {
        /* error was already printed */
        return NC_MSG_ERROR;
    }

    if (lyd_validate(&data, LYD_OPT_RPC | LYD_OPT_NOEXTDEPS | LYD_OPT_STRICT, NULL)) {
        return NC_MSG_ERROR;
    }

    reply = op_editconfig(data, sessions, NP2_EDIT_CREATE);
    lyd_free(data);
    if (!reply) {
        reply = nc_server_reply_err(nc_err(NC_ERR_OP_FAILED, NC_ERR_TYPE_APP));
    }

    ret = rc_write_reply(r, use_xml, NC_MSG_REPLY, reply);
    nc_server_reply_free(reply);
    // /** not implemented yet, normally target_running will be 1
    //  * and will not run this commit code*/
    // if (!target_running) {
    //     data = lyd_new(NULL, ietfnc, "commit");
    //     lyd_new_leaf(data, ietfnc, "confirmed", NULL);
    //     op_commit(data, sessions->ncs);
    // }
done:
    if (cxml)
        cbuf_free(cxml);
#endif
    return ret;
}

int api_data_patch(struct ly_ctx *ly_ctx,
                sr_session_ctx_t *srs,
                FCGX_Request *r,
                rc_vec       *pcvec,
                char         *api_path,
                int           pi,
                rc_vec       *qvec,
                char*         content,
                int           use_xml)
{
    int ret = 0;
#if 0
    struct nc_server_reply *reply;
    int target_running = 1;
    char **pvec = NULL;
    int pn;
    struct lyd_node *node, *data;
    const struct lys_module *ietfnc = NULL;
    cbuf *cxml = NULL;
    VRB("%s", __FUNCTION__);

    ietfnc = ly_ctx_get_module(ly_ctx, "ietf-netconf", NULL, 1);
    if (!ietfnc) {
        ERR("Session : missing \"ietf-netconf\" schema in the context.");
        return NC_MSG_ERROR;
    }
    /**
     * If the NETCONF server supports **:writable-running**,
     * all edits to configuration nodes in {+restconf}/data are
     * performed in the running configuration datastore.
     */
    if (!lys_features_state(ietfnc, "writable-running") && lys_features_state(ietfnc,"candidate")) {
        target_running = 0;
    }

    data = lyd_new(NULL, ietfnc, "edit-config");
    node = lyd_new(data, ietfnc, "target");
    node = lyd_new_leaf(node, ietfnc, target_running ? "running":"candidate", NULL);
    if (!node) {
        lyd_free(data);
        return NC_MSG_ERROR;
    }
    node = lyd_new_leaf(data, ietfnc, "default-operation", "merge");
    if (!node) {
        lyd_free(data);
        return NC_MSG_ERROR;
    }
    node = lyd_new_leaf(data, ietfnc, "test-option", "test-then-set");
    if (!node) {
        lyd_free(data);
        return NC_MSG_ERROR;
    }
    node = lyd_new_leaf(data, ietfnc, "error-option", "stop-on-error");
    if (!node) {
        lyd_free(data);
        return NC_MSG_ERROR;
    }
    pvec = clicon_strsep(api_path, "/", &pn);
    /* Remove trailing '/'. Like in /a/ -> /a */
    if (pn > 1 && !strlen(pvec[pn-1])) {
        pn--;
    }
    if (pn > 3) { /* means that localhost/restconf/data/xxx:xxx */
        if ((cxml = cbuf_new()) == NULL)
        goto done;
        api_path2xml(ly_ctx, pcvec, pi, content, 0, use_xml, cxml);
        VRB("cxml: %s", cbuf_get(cxml));
        if (use_xml) {
            node = lyd_new_anydata(data, ietfnc, "config", cbuf_get(cxml), LYD_ANYDATA_SXML);
        } else {
            node = lyd_new_anydata(data, ietfnc, "config", cbuf_get(cxml), LYD_ANYDATA_JSON);
        }
    } else { /* datastore */
        VRB("%s", content);
        if (use_xml) {
            node = lyd_new_anydata(data, ietfnc, "config", content, LYD_ANYDATA_SXML);
        } else {
            node = lyd_new_anydata(data, ietfnc, "config", content, LYD_ANYDATA_JSON);
        }
    }
    if (!node) {
        lyd_free(data);
        return NC_MSG_ERROR;
    }
    if (!data) {
        /* error was already printed */
        return NC_MSG_ERROR;
    }

    if (lyd_validate(&data, LYD_OPT_RPC | LYD_OPT_NOEXTDEPS | LYD_OPT_STRICT, NULL)) {
        return NC_MSG_ERROR;
    }

    reply = op_editconfig(data, sessions, NP2_EDIT_MERGE);
    lyd_free(data);
    if (!reply) {
        reply = nc_server_reply_err(nc_err(NC_ERR_OP_FAILED, NC_ERR_TYPE_APP));
    }

    ret = rc_write_reply(r, use_xml, NC_MSG_REPLY, reply);
    nc_server_reply_free(reply);
done:
    if (cxml)
        cbuf_free(cxml);
#endif
    return 0;
}

int api_data_put(struct ly_ctx *ly_ctx,
                sr_session_ctx_t *srs,
                FCGX_Request *r,
                rc_vec       *pcvec,
                char         *api_path,
                int           pi,
                rc_vec       *qvec,
                char*         content,
                int           use_xml)
{
    int ret = 0;
#if 0
    int target_running = 1;
    struct nc_server_reply *reply;
    char **pvec = NULL;
    int pn;
    struct lyd_node *node, *data;
    const struct lys_module *ietfnc = NULL;
    cbuf *cxml = NULL;
    VRB("%s", __FUNCTION__);

    ietfnc = ly_ctx_get_module(ly_ctx, "ietf-netconf", NULL, 1);
    if (!ietfnc) {
        ERR("Session : missing \"ietf-netconf\" schema in the context.");
        return NC_MSG_ERROR;
    }
    if ((cxml = cbuf_new()) == NULL)
        goto done;

    if (!lys_features_state(ietfnc, "writable-running") && lys_features_state(ietfnc,"candidate")) {
        target_running = 0;
    }

    data = lyd_new(NULL, ietfnc, "edit-config");
    node = lyd_new(data, ietfnc, "target");
    node = lyd_new_leaf(node, ietfnc, target_running ? "running":"candidate", NULL);
    if (!node) {
        lyd_free(data);
        return NC_MSG_ERROR;
    }
    node = lyd_new_leaf(data, ietfnc, "default-operation", "merge");
    if (!node) {
        lyd_free(data);
        return NC_MSG_ERROR;
    }
    node = lyd_new_leaf(data, ietfnc, "test-option", "test-then-set");
    if (!node) {
        lyd_free(data);
        return NC_MSG_ERROR;
    }
    node = lyd_new_leaf(data, ietfnc, "error-option", "stop-on-error");
    if (!node) {
        lyd_free(data);
        return NC_MSG_ERROR;
    }

    pvec = clicon_strsep(api_path, "/", &pn);
    /* Remove trailing '/'. Like in /a/ -> /a */
    if (pn > 1 && !strlen(pvec[pn-1])) {
        pn--;
    }
    if (pn > 3) { /* means that localhost/restconf/data/xxx:xxx */
        if ((cxml = cbuf_new()) == NULL)
        goto done;
        api_path2xml(ly_ctx, pcvec, pi, content, 0, use_xml, cxml);
        VRB("cxml: %s", cbuf_get(cxml));
        if (use_xml) {
            node = lyd_new_anydata(data, ietfnc, "config", cbuf_get(cxml), LYD_ANYDATA_SXML);
        } else {
            node = lyd_new_anydata(data, ietfnc, "config", cbuf_get(cxml), LYD_ANYDATA_JSON);
        }
    } else { /* datastore */
        VRB("%s", content);
        if (use_xml) {
            node = lyd_new_anydata(data, ietfnc, "config", content, LYD_ANYDATA_SXML);
        } else {
            node = lyd_new_anydata(data, ietfnc, "config", content, LYD_ANYDATA_JSON);
        }
    }

    if (!node) {
        lyd_free(data);
        return NC_MSG_ERROR;
    }
    if (!data) {
        /* error was already printed */
        return NC_MSG_ERROR;
    }

    if (lyd_validate(&data, LYD_OPT_RPC | LYD_OPT_NOEXTDEPS | LYD_OPT_STRICT, NULL)) {
        return NC_MSG_ERROR;
    }

    reply = op_editconfig(data, sessions, NP2_EDIT_REPLACE);
    lyd_free(data);
    if (!reply) {
        reply = nc_server_reply_err(nc_err(NC_ERR_OP_FAILED, NC_ERR_TYPE_APP));
    }

    ret = rc_write_reply(r, use_xml, NC_MSG_REPLY, reply);
    nc_server_reply_free(reply);

done:
    if (cxml) {
        cbuf_free(cxml);
    }
#endif
    return ret;
}

int api_data_delete(struct ly_ctx *ly_ctx,
                sr_session_ctx_t *srs,
                FCGX_Request *r,
                rc_vec       *pcvec,
                int           pi,
                rc_vec       *qvec,
                int           use_xml)
{
    int ret = 0, rc;
#if 0
    struct nc_server_reply *ereply = NULL;
    sr_datastore_t ds = 0;
    char xpath[80]={'\0'};
    VRB("%s", __FUNCTION__);

    /* convert pcvec to xpath */
    if ((rc = api_path2xpath(ly_ctx, pcvec, pi, xpath)) < 0) {
        goto cleanup;
    }
    VRB("%s xpath = %s", __FUNCTION__, xpath);

    ds = SR_DS_RUNNING;
    if (ds != sessions->ds) {
        /* update sysrepo session */
        if (np2srv_sr_session_switch_ds(srs, ds, &ereply)) {
            goto cleanup;
        }
        sessions->ds = ds;
    }
    if (sessions->ds != SR_DS_CANDIDATE) {
        /* update data from sysrepo */
        if (np2srv_sr_session_refresh(srs, &ereply)) {
            goto cleanup;
        }
    }
    if (np2srv_sr_delete_item(srs, xpath, SR_EDIT_STRICT, &ereply)) {
        np2srv_sr_discard_changes(srs, NULL);
        goto cleanup;
    }
    if (np2srv_sr_commit(srs, &ereply)) {
        np2srv_sr_discard_changes(srs, NULL); /* rollback the changes */
        goto cleanup;
    }
    if (!ereply) {
        ereply = nc_server_reply_ok();
    }

cleanup:
    ret = rc_write_reply(r, use_xml, NC_MSG_REPLY, ereply);
    nc_server_reply_free(ereply);
#endif
    return ret;
}
