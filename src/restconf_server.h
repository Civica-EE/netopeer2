/* -*- mode:c; indent-tabs-mode:nil; tab-width:4; c-basic-offset:4 -*- */
/*     vi: set ts=4 sw=4 expandtab:                                    */

/*@COPYRIGHT@*/

#ifndef __RESTCONF_SERVER_H__
#define __RESTCONF_SERVER_H__

#include "config.h"

/**
 * Global initialization for restconf operation. Must be called prior to any
 * of the other functions below.
 */
int restconf_server_init (int backlog);

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
int restconf_worker_thread (int idx, ATOMIC_T loop_continue);



#endif /* __RESTCONF_SERVER_H__ */
