/**
 * @file restconf_server.h
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


#ifndef __RESTCONF_SERVER_H__
#define __RESTCONF_SERVER_H__

#include "config.h"

/**
 * Global initialization for restconf operation. Must be called prior to any
 * of the other functions below.
 */
int restconf_server_init (int backlog);

/**
 * Signals all the restconf workers to stop.
 */
int restconf_server_stop ();

/**
 * Global cleanup for restconf. Must be called after all previous calls to any
 * of the functions below have returned.
 */
int restconf_server_shutdown ();

/**
 * Main entry point for restconf workers.
 *
 * The function sits in a look accepting restconf requests until loop_continue
 * becomes 0. Returns 0 on success.
 */
int restconf_worker_thread (int idx);



#endif /* __RESTCONF_SERVER_H__ */
