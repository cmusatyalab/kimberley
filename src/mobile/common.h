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

#ifndef _COMMON_H_
#define _COMMON_H_

#define KCM_DBUS_SERVICE_NAME		"edu.cmu.cs.kimberley.kcm"
#define KCM_DBUS_SERVICE_PATH		"/edu/cmu/cs/kimberley/kcm"
#define LAUNCHER_KCM_SERVICE_NAME	"_launcher_kcm._tcp"
#define VNC_KCM_SERVICE_NAME		"_vnc_kcm._tcp"

int            log_init(void);
int            log_message(char *message);
int            log_append_file(char *filename);
void           log_deinit(void);

char *         compress_file(const char *filename);
char *         decompress_file(const char *filename);

#endif
