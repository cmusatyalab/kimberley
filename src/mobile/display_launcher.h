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

int create_dcm_service(char *name, char *port);

#endif
