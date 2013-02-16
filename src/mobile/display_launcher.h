/*
 *  Kimberley
 *
 *  Copyright (c) 2008 Carnegie Mellon University
 *  All rights reserved.
 *
 *  Kimberley is free software: you can redistribute it and/or modify
 *  it under the terms of version 2 of the GNU General Public License
 *  as published by the Free Software Foundation.
 *
 *  Kimberley is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Kimberley. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _MOBILE_LAUNCHER_H_
#define _MOBILE_LAUNCHER_H_

typedef struct {
    char *vm_name;
    char *patch_file;
    char *overlay_location;
    char *encryption_key;
    char *persistent_state;
} kimberley_state_t;

extern const struct rpc_mobile_launcher_server_operations *server_ops;

int create_kcm_service(char *name, char *port);

#endif
