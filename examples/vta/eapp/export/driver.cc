
// Standard includes
#include <stdio.h>

// Keystone includes
#include "app/callee.h"
#include "app/syscall.h"

// Internal includes
#include "../../util/util.h"

static const char VTA_DEVICE[] = "vta@60000000";
static const char CMA_DEVICE[] = "cma@C0000000";

int main() {
    int ret = claim_mmio(VTA_DEVICE, sizeof(VTA_DEVICE));
    if(ret < 0) {
        printf(ERROR_PREFIX "Couldn't claim VTA MMIO device\n");
        return ret;
    }

    ret = claim_mmio(CMA_DEVICE, sizeof(CMA_DEVICE));
    if(ret < 0) {
        printf(ERROR_PREFIX "Couldn't claim CMA MMIO device\n");
        return ret;
    }

    ret = spawn_callee_handler(NULL, CALL_MAPPED);
    if(ret < 0) {
        printf(ERROR_PREFIX "Couldn't spawn callee handler CALL_MAPPED\n");
        return ret;
    }

    while(1) {
        yield_thread();
    }
}