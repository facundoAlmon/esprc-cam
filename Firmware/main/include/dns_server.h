#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void dns_server_start(const char* ip);
void dns_server_stop(void);

#ifdef __cplusplus
}
#endif
