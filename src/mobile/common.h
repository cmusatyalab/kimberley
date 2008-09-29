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
