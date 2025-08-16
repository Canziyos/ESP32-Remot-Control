#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

// Persist a single bit indicating: “we rolled back last boot attempt”.
void bootflag_set_post_rollback(bool on);
bool bootflag_is_post_rollback(void);

#ifdef __cplusplus
}
#endif
