/**
 * @file restconf_path.c
 * @author Hongcheng Zhong <spartazhc@gmail.com>
 * @brief tool functions to process url to xml or xpath etc.
 * some code are from clixon_xml_map.c in clixon project.
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

/*
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

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "log.h"
#include "restconf_lib.h"

/*! Split colon-separated node identifier into prefix and name
 * @param[in]  node-id
 * @param[out] prefix  Malloced string. May be NULL.
 * @param[out] id      Malloced identifier.
 * @retval     0       OK
 * @retval    -1       Error
 * @code
 *    char      *prefix = NULL;
 *    char      *id = NULL;
 *    if (nodeid_split(nodeid, &prefix, &id) < 0)
 *	 goto done;
 *    if (prefix)
 *	 free(prefix);
 *    if (id)
 *	 free(id);
 * @note caller need to free id and prefix after use
 */
int
nodeid_split(char  *nodeid,
	     char **prefix,
	     char **id)
{
    int   retval = -1;
    char *str;

    if ((str = strchr(nodeid, ':')) == NULL){
	if ((*id = strdup(nodeid)) == NULL){
	    goto done;
	}
    }
    else{
	if ((*prefix = strdup(nodeid)) == NULL){
	    goto done;
	}
	(*prefix)[str-nodeid] = '\0';
	str++;
	if ((*id = strdup(str)) == NULL){
	    goto done;
	}
    }
    retval = 0;
 done:
    return retval;
}

/*! Split string into a vector based on character delimiters. Using malloc
 *
 * The given string is split into a vector where the delimiter can be
 * _any_ of the characters in the specified delimiter string.
 *
 * The vector returned is one single memory block that must be freed
 * by the caller
 *
 * @code
 * char **vec = NULL;
 * char  *v;
 * int    nvec;
 * if ((vec = clicon_strsep("/home/user/src/clixon", "/", &nvec)) == NULL)
 *    err;
 * for (i=0; i<nvec; i++){
 *    v = vec[i++];
 *    ...
 * }
 * free(vec);
 * @endcode
 * @param[in]   string     String to be split
 * @param[in]   delim      String of delimiter characters
 * @param[out]  nvec       Number of entries in returned vector
 * @retval      vec        Vector of strings. NULL terminated. Free after use
 * @retval      NULL       Error *
 */
char **
clicon_strsep(char *string,
	      char *delim,
	      int  *nvec0)
{
    char **vec = NULL;
    char  *ptr;
    char  *p;
    int   nvec = 1;
    int   i;
    size_t siz;
    char *s;
    char *d;

    if ((s = string)==NULL)
	goto done;
    while (*s){
	if ((d = index(delim, *s)) != NULL)
	    nvec++;
	s++;
    }
    /* alloc vector and append copy of string */
    siz = (nvec+1)* sizeof(char*) + strlen(string)+1;
    if ((vec = (char**)malloc(siz)) == NULL){
	ERR("malloc.");
	goto done;
    }
    memset(vec, 0, siz);
    ptr = (char*)vec + (nvec+1)* sizeof(char*); /* this is where ptr starts */
    strncpy(ptr, string, strlen(string)+1);
    i = 0;
    while ((p = strsep(&ptr, delim)) != NULL)
	vec[i++] = p;
    *nvec0 = nvec;
 done:
    return vec;
}

/*! Percent decoding according to RFC 3986 URI Syntax
 * need #include <ctype.h>
 * @param[in]   enc    Encoded input string
 * @param[out]  strp   Decoded malloced output string. Deallocate with free()
 * @retval      0      OK
 * @retval     -1      Error
 * @see RFC 3986 Uniform Resource Identifier (URI): Generic Syntax
 * @see uri_percent_encode
 */
int
uri_percent_decode(char  *enc,
		   char **strp)
{
    int   retval = -1;
    char *str = NULL;
    int   j;
    char  hstr[3];
    int   len;
    char *ptr;

    /* This is max */
    len = strlen(enc)+1;
    if ((str = malloc(len)) == NULL){
	// clicon_err(OE_UNIX, errno, "malloc");
	goto done;
    }
    memset(str, 0, len);
    j = 0;
    for (size_t i = 0; i < strlen(enc); i++){
	if (enc[i] == '%' && strlen(enc)-i > 2 &&
	    isxdigit(enc[i+1]) && isxdigit(enc[i+2])){
	    hstr[0] = enc[i+1];
	    hstr[1] = enc[i+2];
	    hstr[2] = 0;
	    str[j] = strtoul(hstr, &ptr, 16);
	    i += 2;
	}
	else
	    str[j] = enc[i];
	j++;
    }
    str[j++] = '\0';
    *strp = str;
    retval = 0;
 done:
    if (retval < 0 && str)
	free(str);
    return retval;
}

/*! Split a string into a vector using 1st and 2nd delimiter
 * Split a string first into elements delimited by delim1, then into
 * pairs delimited by delim2.
 * @param[in]  string String to split
 * @param[in]  delim1 First delimiter char that delimits between elements
 * @param[in]  delim2 Second delimiter char for pairs within an element
 * @param[out] cvp    Created vector, deallocate w rc_vec_free
 * @retval     0      OK
 * @retval    -1      error
 * @code
 * rc_vec  *cvv = NULL;
 * if (str2rc_vec("a=b&c=d", ';', '=', &cvv) < 0)
 *   err;
 * @endcode
 *
 * a=b&c=d    ->  [[a,"b"][c="d"]
 * kalle&c=d  ->  [[c="d"]]  # Discard elements with no delim2
 * XXX differentiate between error and null rc_vec.
 */
int
str2rc_vec(char  *string,
	 char   delim1,
	 char   delim2,
	 rc_vec **cvp)
{
    int     retval = -1;
    char   *s;
    char   *s0 = NULL;;
    char   *val;     /* value */
    char   *valu;    /* unescaped value */
    char   *snext; /* next element in string */
    rc_vec *cvv = NULL;
    rc_var *cv;

    if ((s0 = strdup(string)) == NULL){
        ERR("%s: strdup.", __FUNCTION__);
        goto err;
    }
    s = s0;
    if ((cvv = rc_vec_new(0)) ==NULL){
	    ERR("%s: rc_vec_new.", __FUNCTION__);
        goto err;
    }
    while (s != NULL) {
	/*
	 * In the pointer algorithm below:
	 * name1=val1;  name2=val2;
	 * ^     ^      ^
	 * |     |      |
	 * s     val    snext
	 */
	if ((snext = index(s, delim1)) != NULL)
	    *(snext++) = '\0';
	if ((val = index(s, delim2)) != NULL){
	    *(val++) = '\0';
	    if (uri_percent_decode(val, &valu) < 0)
            goto err;
	    if ((cv = rc_vec_add(cvv)) == NULL){
            ERR("%s: rc_vec_add.", __FUNCTION__);
            goto err;
	    }
	    while ((strlen(s) > 0) && isblank(*s)){
            s++;
        }
        rc_var_name_set(cv, s);
        rc_var_value_set(cv, valu);
	    free(valu); valu = NULL;
	}
	else{
	    if (strlen(s)){
            if ((cv = rc_vec_add(cvv)) == NULL){
                ERR("%s: rc_vec_add.", __FUNCTION__);
                goto err;
            }
            rc_var_name_set(cv, s);
            rc_var_value_set(cv, "");
	    }
	}
	s = snext;
    }
    retval = 0;
 done:
    *cvp = cvv;
    if (s0)
	free(s0);
    return retval;
 err:
    if (cvv){
        rc_vec_free(cvv);
	    cvv = NULL;
    }
    goto done;
}

/*! using url path to complete config message in data
 * @param[in]  ly_ctx libyang context
 * @param[in]  cvv    api-path as rc_vec
 * @param[in]  offset Offset of rc_vec, where api-path starts
 * @param[in]  data
 * @param[out] cxml   data + api-path
 * @retval     0      OK
 * @retval    -1      Fatal error, restconf_err called (TODO)
 * @example
 *   api_path: /restconf/data/example-jukebox:jukebox
 *             /library/artist=Foo%20Fighters/album=Wasting%20Light
 *   data:  <album xmlns="http://example.com/ns/example-jukebox">
 *              <name>Wasting Light</name>
 *              <year>2011</year>
 *          </album>
 *   cxml[out]:
 *      <jukebox xmlns="http://example.com/ns/example-jukebox">
 *          <library>
 *              <artist>
 *              <name>Foo Fighters</name>
 *              <album>
 *                  <name>Wasting Light</name>
 *                  <year>2011</year>
 *              </album>
 *              </artist>
 *          </library>
 *      </jukebox>
 * @note POST method is different from PUT and PATCH:
 *  as this function is write to deal with "data resource" case
 *  that some data is in url like localhost/restconf/data/modname:topcontainer/container/list=keyvalue
 *  The target resource for the POST method for resource creation is the parent of the new resource.
 *  The target resource for the PUT & PATCH method for resource creation is the new resource.
 * @note "api-path" is "URI-encoded path expression" definition in RFC8040 3.5.3
 * @see api_path2xpath For api-path to xml xpath translation
 */
int api_path2xml(struct ly_ctx *ly_ctx,
            rc_vec  *cvv,
            int      offset,
            char    *data,
            int     post,
            int     usexml,
            cbuf    *cxml)
{
    int         vcount;
    rc_var     *cv;
    const struct lys_module *mod = NULL;
    struct      ly_set *nodeset;
    struct      lys_node *snode;
    struct      lys_node_leaf **keys;
    char       *val;
    char       *v;
    char       *modname, *topcontainer;
    cbuf       *xpath = NULL, *cbtail = NULL;
    if ((xpath = cbuf_new()) == NULL)
        goto done;
    if ((cbtail = cbuf_new()) == NULL)
        goto done;
    /* strip incoming data */
    if (post == 0) {
        if (usexml) {
            data = index(data, '>') + 1;
            v = data;
            v = rindex(v, '<');
            *v = '\0';
        } else {
            /* remove to second '{' and reversely second '}' */
            data = index(data + 2, '{') + 1;
            v = data;
            v = rindex(v, '}');
            *v = '\0';
            v = data;
            v = rindex(v, '}');
            *v = '\0';
        }
    }
    /* spectial case when use POST and JSON */
    if (post == 1 && usexml == 0) {
        data = index(data, ':') + 1;  // use '{' is ok, but use ':' will make it more pretty
        v = data;
        v = rindex(v, '}');
        *v = '\0';
    }
    VRB("%s striped data: [%s]", __FUNCTION__, data);

    int rc_len = rc_vec_len(cvv);
    for (int i = offset; i < rc_len; i++){
        cv = rc_vec_i(cvv, i);
        if (i == offset) {
            nodeid_split(cv->name, &modname, &topcontainer);
            mod = ly_ctx_get_module(ly_ctx, modname, NULL, 1);
            if (usexml) {
                cprintf(cxml, "<%s xmlns=\"%s\">", topcontainer, mod->ns);
            } else {
                cprintf(cxml, "{ \"%s:%s\" : { ", modname, topcontainer);
                cprintf(cbtail, "}}");  // for outside '}' plus one
            }
            cprintf(xpath, "/%s", cv->name);
            continue; // topcontainer is different
        }
        /**
         * Check if has value, means '=' , key region
         * whichmeans, only keys are allowed after this.
         * only when use /leaf=key-value, we will need to retrive key-name.
         */
        if (strlen(cv->value) >= 1 ){
            if (usexml) {
                cprintf(cxml, "<%s>", cv->name);
            } else {
                cprintf(cxml, "\"%s\" : [ { ", cv->name);
                cprintf(cbtail, "]}");
            }
            /* if last vec is a key, it should be skip because it is already in data */
            if (i == rc_len - 1 && post == 0){
                break;
            }
            /**
             * first construct xpath for searching node
             * [input]  "/restconf/data/building:storage/boxs/toys=1"
             * [output] keypath = /storage/boxs/toys
             * can reuse xpath part after ':' to construct this
             */
            cprintf(xpath, "/%s", cv->name);
            nodeset = lys_find_path(mod, NULL, cbuf_get(xpath));
            if (nodeset->number) {
                snode = (struct lys_node *)nodeset->set.d[0];
                keys = ((struct lys_node_list *)snode)->keys;
                ly_set_free(nodeset);
            }

            if ((val = strdup(cv->value)) == NULL)
                return -1;
            v = val;
            vcount = strlen(cv->value);
            /* XXX sync with yang */
            while((v=index(v, ',')) != NULL){
                *v = '\0';
                v++;
            }
            /* Iterate over individual yang keys  */
            v = val;
            int j = 0;
            while (keys[j] != NULL) {// or use for loop ((struct lys_node_list *)snode)->keys_size
                /* if there is lack of key-value in input, error should be reported  */
                if (vcount <= 0) return -1; //  TODO: should modify to http reply code
                if (usexml) {
                    cprintf(cxml, "<%s>%s</%s>", keys[j]->name, v, keys[j]->name);
                } else { //! there is a problem that whether ',' is needed (for last leaf it doesn't need ',')
                    cprintf(cxml, "\"%s\" : \"%s\", ", keys[j]->name, v);
                }
                v += strlen(v)+1;
                vcount -= (strlen(v) + 1);
                ++j;
            }
        } else { // if no key specified
            if (usexml) {
                cprintf(cxml, "<%s>", cv->name);
            } else {
                cprintf(cxml, "\"%s\" : { ", cv->name);
                cprintf(cbtail, "}");
            }

            cprintf(xpath, "/%s", cv->name);
        }
    }
    /* add data part */
    if (post == 1 && usexml == 0) { // or use '{' in the beginning then don't need this
        cprintf(cxml, " \"%s", data);
    } else {
        cprintf(cxml, "%s", data);
    }
    /* add tail part */
    if (usexml) {
        for (int i = rc_len - 1; i > offset; --i) {
            cv = rc_vec_i(cvv, i);
            cprintf(cxml, "</%s>", cv->name);
        }
        cprintf(cxml, "</%s>", topcontainer);
    } else {
        VRB("cbtail: %s", cbuf_get(cbtail));
        for (int i = cbuf_len(cbtail) - 1; i >= 0; --i) {
            cprintf(cxml, "%c ", (cbtail->cb_buffer)[i]);
        }
    }
    // VRB("%s cxml: [%s]", __FUNCTION__, cbuf_get(cxml));
    return 0;
done:
    if (xpath)
        cbuf_free(xpath);
    if (cbtail)
        cbuf_free(cbtail);
    return 0;
}

/*! Translate from restconf api-path in cvv form to xml xpath
 * eg a/b=c -> a/[b=c]
 * eg example:a/b -> ex:a/b
 * @param[in]  ly_ctx libyang context
 * @param[in]  cvv    api-path as rc_vec
 * @param[in]  offset Offset of rc_vec, where api-path starts
 * @param[out] xpath  The xpath as char*
 * @retval     0      OK
 * @retval    -1      Fatal error, restconf_err called (TODO)
 *
 * @note both retval 0 and -1 set clicon_err, but the later is fatal
 * @note Not proper namespace translation from api-path 2 xpath
 * It works like this:
 * Assume origin incoming path is
 * "www.foo.com/restconf/a/b=c",  pi is 2 and pcvec is:
 *   ["www.foo.com" "restconf" "a" "b=c"]
 * which means the api-path is ["a" "b=c"] corresponding to "a/b=c"
 * @code
 *   rc_vec *cvv = NULL;
 *   if (str2rc_vec("www.foo.com/restconf/a/b=c", '/', '=', &cvv) < 0)
 *      err;
 *   if ((ret = api_path2xpath(yspec, cvv, 0, cxpath)) < 0)
 *      err;
 * @endcode
 * @note "api-path" is "URI-encoded path expression" definition in RFC8040 3.5.3
 * @see api_path2xml  For api-path to xml tree
 */
int
api_path2xpath(struct ly_ctx *ly_ctx,
            rc_vec  *cvv,
            int      offset,
            char    *xpath)
{
    int         vcount;
    int         key_flag = 0;
    rc_var     *cv;
    const struct lys_module *mod = NULL;
    struct      ly_set *nodeset;
    struct      lys_node *snode;
    struct      lys_node_leaf **keys;
    char       *val;
    char       *v;
    char       *ss;
    char        keypath[80] = {'\0'};
    char        tmp[80] = {'\0'};
    char       *modname;

    for (int i = offset; i < rc_vec_len(cvv); i++){
        cv = rc_vec_i(cvv, i);
        if (i == offset) {
            ss = strdup(cv->name);
            modname = strtok(ss, ":");
        }
        /**
         * Check if has value, means '=' , key region
         * whichmeans, only keys are allowed after this.
         * only when use /leaf=key-value, we will need to retrive key-name.
         */
        if (strlen(cv->value) >= 1 ){
            /**
             * first construct xpath for searching node
             * [input]  "/restconf/data/building:storage/boxs/toys=1"
             * [output] keypath = /storage/boxs/toys
             * can reuse xpath part after ':' to construct this
             */
            if (key_flag != 1) { /* only need to find keys lyd_node once */
                sprintf(keypath, "/%s/%s", xpath + strlen(modname)+2, cv->name);

                mod = ly_ctx_get_module(ly_ctx, modname, NULL, 1);
                nodeset = lys_find_path(mod, NULL, keypath);
                if (nodeset != NULL) {
                    if (nodeset->number) {
                        snode = (struct lys_node *)nodeset->set.d[0];
                        keys = ((struct lys_node_list *)snode)->keys;
                        ly_set_free(nodeset);
                    }
                } else {
                    /* lys_find_path error, add to xpath and return,
                                error will be reported later */
                    sprintf(tmp, "/%s", cv->name);
                    strcat(xpath, tmp);
                    return -1;
                }
            }

            if ((val = strdup(cv->value)) == NULL)
                return -1;
            v = val;
            vcount = strlen(cv->value);
            /* XXX sync with yang */
            while((v=index(v, ',')) != NULL){
                *v = '\0';
                v++;
            }
            /* Iterate over individual yang keys  */
            sprintf(tmp, "/%s", cv->name);
            strcat(xpath, tmp);
            v = val;
            int j = 0;
            while (keys[j] != NULL) {
                /* if there is lack of key-value in input, error should be reported  */
                if (vcount <= 0) return -1; //  TODO: should modify to http reply code
                sprintf(tmp, "[%s='%s']", keys[j]->name, v);
                strcat(xpath, tmp);
                v += strlen(v)+1;
                vcount -= (strlen(v) + 1);
                ++j;
            } // if key-calues are more than key number in yang, now will ignore extra key-values
        } else { // if no key specified
            sprintf(tmp, "/%s", cv->name);
            strcat(xpath, tmp);
        }
    }
    return 0;
}
