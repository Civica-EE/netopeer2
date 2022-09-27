/**
 * @file restconf_methods.h
 * @author Hongcheng Zhong <spartazhc@gmail.com>
 * @brief HTTP methods
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

*** remove ***

#ifndef _RESTCONF_METHODS_H_
#define _RESTCONF_METHODS_H_

#include <fcgiapp.h>
#include <sysrepo.h>

int api_data_options(FCGX_Request *r);

int api_data_head(struct ly_ctx *ly_ctx,
                  sr_session_ctx_t *srs,
                  FCGX_Request *r,
                  rc_vec       *pcvec,
                  int           pi,
                  rc_vec       *qcvec,
                  int           use_xml);

int api_data_get(struct ly_ctx *ly_ctx,
                 sr_session_ctx_t *srs,
                 FCGX_Request *r,
                 rc_vec       *pcvec,
                 int           pi,
                 rc_vec       *qvec,
                 int           use_xml);

int api_data_post(struct ly_ctx *ly_ctx,
                  sr_session_ctx_t *srs,
                  FCGX_Request *r,
                  rc_vec       *pcvec,
                  char         *api_path,
                  int           pi,
                  rc_vec       *qvec,
                  char         *data,
                  int           use_xml);

int api_data_patch(struct ly_ctx *ly_ctx,
                   sr_session_ctx_t *srs,
                   FCGX_Request *r,
                   rc_vec       *pcvec,
                   char         *api_path,
                   int           pi,
                   rc_vec       *qvec,
                   char*         content,
                   int           use_xml);

int api_data_put(struct ly_ctx *ly_ctx,
                 sr_session_ctx_t *srs,
                 FCGX_Request *r,
                 rc_vec       *pcvec,
                 char         *api_path,
                 int           pi,
                 rc_vec       *qvec,
                 char*         content,
                 int           use_xml);

int api_data_delete(struct ly_ctx *ly_ctx,
                    sr_session_ctx_t *srs,
                    FCGX_Request *r,
                    rc_vec       *pcvec,
                    int           pi,
                    rc_vec       *qvec,
                    int           use_xml);

int api_operations_get(struct ly_ctx *ly_ctx,
                       sr_session_ctx_t *srs,
                       FCGX_Request *r,
                       rc_vec       *qvec,
                       int           use_xml);

// int api_operations_post(struct ly_ctx *ly_ctx,
//                 sr_session_ctx_t *srs,
//                 FCGX_Request *r,
//                 rc_vec       *pcvec,
//                 int           pi,
//                 rc_vec       *qvec,
//                 int           use_xml);

#endif /* _RESTCONF_METHODS_H_ */
