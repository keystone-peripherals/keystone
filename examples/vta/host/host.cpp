
#include <thread>

#include "edge/edge_call.h"
#include "host/keystone.h"

using namespace Keystone;
#define TIME_SINCE_UNIX_EPOCH 1709595745 // == Mon Mar  4 03:42:26 PM PST 2024

int main(int argc, char **argv) {
    Enclave encl;
    Params params;

    params.setFreeMemSize(1024 * 1024 * std::stol(argv[3]));
    params.setUntrustedMem(DEFAULT_UNTRUSTED_PTR, 2 * 1024 * 1024 );
    params.setTimeSinceUnixEpoch(TIME_SINCE_UNIX_EPOCH);

    printf("Loading eapp %s with rt %s\n", argv[1], argv[2]);
    encl.init(argv[1], argv[2], params, 0, true);

    encl.registerOcallDispatch(incoming_call_dispatch);
    edge_call_init_internals(
        (uintptr_t)encl.getSharedBuffer(), encl.getSharedBufferSize());

    encl.run();
    return 0;
}
