/**
 * @file restconf_path.h
 * @author Hongcheng Zhong <spartazhc@gmail.com>
 * @brief tool functions to process url to xml or xpath etc.
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
#ifndef RESTCONF_PATH_H
#define RESTCONF_PATH_H

#include <libyang/libyang.h>
#include "restconf_lib.h"

int nodeid_split(char *nodeid, char **prefix, char **id);
char** clicon_strsep(char *string, char *delim, int *nvec0);
int uri_percent_decode(char *enc, char **strp);
int str2rc_vec(char  *string, char delim1, char delim2, rc_vec **cvp);
int api_path2xml(struct ly_ctx *ly_ctx, rc_vec  *cvv, int offset,
                    char *data, int put, int use_xml, cbuf *cxml);
int api_path2xpath(struct ly_ctx *ly_ctx, rc_vec *cvv, int offset, char *cpath);


#endif /* RESTCONF_PATH_H*/
