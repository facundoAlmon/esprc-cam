#pragma once
#include "state.h"

void mjpeg_server_start(const CameraState* state);
void mjpeg_server_stop(void);
void mjpeg_server_get_stats(float* fps, uint32_t* bps);
bool mjpeg_server_has_client(void);
