
#include "images.h"

#include <string.h>
#include <stdbool.h>
#include <lzma.h>

#include "util.h"

extern const char imgs_xz[];
extern unsigned int imgs_xz_len;

static bool strm_init = false;
static lzma_stream strm = LZMA_STREAM_INIT;

extern "C" {

#define NUM_IMAGES  (100)
#define IMAGE_SIZE  (sizeof(float) * 3 * 224 * 224)

void *images[NUM_IMAGES];

int init_lzma() {
    int i;
    lzma_ret ret = lzma_stream_decoder(&strm, UINT64_MAX, LZMA_CONCATENATED);
    if (ret != LZMA_OK) {
        return -1;
    }

    srand(0xDEADBEEF);
    strm.next_in = (uint8_t*)imgs_xz;
    strm.avail_in = imgs_xz_len;

    // Pre-decompress everything
    for(i = 0; i < NUM_IMAGES; i++) {
        images[i] = malloc(IMAGE_SIZE);
        if(images[i]) {
            strm.next_out = (uint8_t *) images[i];
            strm.avail_out = IMAGE_SIZE;
            ret = lzma_code(&strm, LZMA_RUN);
            if (ret != LZMA_OK) {
                return -1;
            }
        } else {
            return -1;
        }
    }

    strm_init = true;
    return 0;
}

int get_next_image(void *buf, size_t len) {
    int i;

    if (!strm_init) {
        if (init_lzma() != 0) {
            return -1;
        }
    }

    if(len != IMAGE_SIZE) {
        return -1;
    }

    i = rand() % NUM_IMAGES;
    memcpy(buf, images[i], len);
    return i;
}

}