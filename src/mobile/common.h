#ifndef _COMMON_H_
#define _COMMON_H_

#define DCM_DBUS_SERVICE_NAME		"edu.cmu.cs.kimberley.kcm"
#define DCM_DBUS_SERVICE_PATH		"/edu/cmu/cs/kimberley/kcm"
#define LAUNCHER_DCM_SERVICE_NAME	"_launcher_kcm._tcp"
#define VNC_DCM_SERVICE_NAME		"_vnc_kcm._tcp"

int            log_init(void);
int            log_message(char *message);
int            log_append_file(char *filename);
void           log_deinit(void);

char *         compress_file(const char *filename);
char *         decompress_file(const char *filename);

#endif
