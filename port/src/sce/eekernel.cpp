#include <eekernel.h>

#include "runtime/mw_runtime.hpp"

// The host's caches are coherent with everything the game hands data to.
void FlushCache(int operation) {}

void iFlushCache(int operation) {}

void iSyncDCache(void *start, void *end) {}

void Exit(int status) {
    runtime::Exit(status);
}
