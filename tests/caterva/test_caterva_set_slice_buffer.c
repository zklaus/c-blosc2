/*********************************************************************
  Blosc - Blocked Shuffling and Compression Library

  Copyright (C) 2021  The Blosc Developers <blosc@blosc.org>
  https://blosc.org
  License: BSD 3-Clause (see LICENSE.txt)

  See LICENSE.txt for details about copyright and rights to use.
**********************************************************************/

#include "test_common.h"

typedef struct {
    int8_t ndim;
    int64_t shape[CATERVA_MAX_DIM];
    int32_t chunkshape[CATERVA_MAX_DIM];
    int32_t blockshape[CATERVA_MAX_DIM];
    int64_t start[CATERVA_MAX_DIM];
    int64_t stop[CATERVA_MAX_DIM];
} test_shapes_t;


CUTEST_TEST_DATA(set_slice_buffer) {
    blosc2_context *ctx;
};


CUTEST_TEST_SETUP(set_slice_buffer) {
    blosc2_init();
    blosc2_cparams cparams = BLOSC2_CPARAMS_DEFAULTS;
    cparams.nthreads = 2;
    data->ctx = blosc2_create_cctx(cparams);

    // Add parametrizations
    CUTEST_PARAMETRIZE(itemsize, uint8_t, CUTEST_DATA(
            1,
            2,
            4,
            8,
    ));

    CUTEST_PARAMETRIZE(backend, _test_backend, CUTEST_DATA(
            {false, false},
            {true, false},
            {true, true},
            {false, true},
    ));


    CUTEST_PARAMETRIZE(shapes, test_shapes_t, CUTEST_DATA(
            {0, {0}, {0}, {0}, {0}, {0}}, // 0-dim
            {1, {5}, {3}, {2}, {2}, {5}}, // 1-idim
            {2, {20, 0}, {7, 0}, {3, 0}, {2, 0}, {8, 0}}, // 0-shape
            {2, {20, 10}, {7, 5}, {3, 5}, {2, 0}, {18, 0}}, // 0-shape
            {2, {14, 10}, {8, 5}, {2, 2}, {5, 3}, {9, 10}},
            {3, {12, 10, 14}, {3, 5, 9}, {3, 4, 4}, {3, 0, 3}, {6, 7, 10}},
            {4, {10, 21, 30, 5}, {8, 7, 15, 3}, {5, 5, 10, 1}, {5, 4, 3, 3}, {10, 8, 8, 4}},
            {2, {50, 50}, {25, 13}, {8, 8}, {0, 0}, {10, 10}},
            // The case below makes qemu-aarch64 (AARCH64 emulation) in CI (Ubuntu 22.04) to crash with a segfault.
            // Interestingly, this works perfectly well on both intel64 (native) and in aarch64 (emulated via docker).
            // Moreover, valgrind does not issue any warning at all when run in the later platforms.
            // In conclusion, this *may* be revealing a bug in the qemu-aarch64 binaries in Ubuntu 22.04.
            // {2, {143, 41}, {18, 13}, {7, 7}, {4, 2}, {6, 5}},
            // Replacing the above line by this one makes qemu-aarch64 happy.
            {2, {150, 45}, {15, 15}, {7, 7}, {4, 2}, {6, 5}},
            {2, {10, 10}, {5, 7}, {2, 2}, {0, 0}, {5, 5}},

    ));
}

CUTEST_TEST_TEST(set_slice_buffer) {
    CUTEST_GET_PARAMETER(backend, _test_backend);
    CUTEST_GET_PARAMETER(shapes, test_shapes_t);
    CUTEST_GET_PARAMETER(itemsize, uint8_t);

    char *urlpath = "test_set_slice_buffer.b2frame";
    blosc2_remove_urlpath(urlpath);

    caterva_params_t params;
    params.itemsize = itemsize;
    params.ndim = shapes.ndim;
    for (int i = 0; i < params.ndim; ++i) {
        params.shape[i] = shapes.shape[i];
    }

    caterva_storage_t storage = {0};
    if (backend.persistent) {
        storage.urlpath = urlpath;
    }
    storage.contiguous = backend.contiguous;
    for (int i = 0; i < params.ndim; ++i) {
        storage.chunkshape[i] = shapes.chunkshape[i];
        storage.blockshape[i] = shapes.blockshape[i];
    }

    /* Create dest buffer */
    int64_t shape[CATERVA_MAX_DIM] = {0};
    int64_t buffersize = itemsize;
    for (int i = 0; i < params.ndim; ++i) {
        shape[i] = shapes.stop[i] - shapes.start[i];
        buffersize *= shape[i];
    }

    uint8_t *buffer = malloc(buffersize);
    CUTEST_ASSERT("Buffer filled incorrectly", fill_buf(buffer, itemsize, buffersize / itemsize));

    /* Create caterva_array_t with original data */
    caterva_array_t *src;
    CATERVA_ERROR(caterva_zeros(data->ctx, &params, &storage, &src));


    CATERVA_ERROR(caterva_set_slice_buffer(data->ctx, buffer, shape, buffersize,
                                           shapes.start, shapes.stop, src));


    uint8_t *destbuffer = malloc((size_t) buffersize);

    /* Fill dest buffer with a slice*/
    CATERVA_TEST_ASSERT(caterva_get_slice_buffer(data->ctx, src, shapes.start, shapes.stop,
                                                 destbuffer,
                                                 shape, buffersize));

    for (uint64_t i = 0; i < (uint64_t) buffersize / itemsize; ++i) {
        uint64_t k = i + 1;
        switch (itemsize) {
            case 8:
                CUTEST_ASSERT("Elements are not equals!",
                              (uint64_t) k == ((uint64_t *) destbuffer)[i]);
                break;
            case 4:
                CUTEST_ASSERT("Elements are not equals!",
                              (uint32_t) k == ((uint32_t *) destbuffer)[i]);
                break;
            case 2:
                CUTEST_ASSERT("Elements are not equals!",
                              (uint16_t) k == ((uint16_t *) destbuffer)[i]);
                break;
            case 1:
                CUTEST_ASSERT("Elements are not equals!",
                              (uint8_t) k == ((uint8_t *) destbuffer)[i]);
                break;
            default:
                CATERVA_TEST_ASSERT(CATERVA_ERR_INVALID_ARGUMENT);
        }
    }

    /* Free mallocs */
    free(buffer);
    free(destbuffer);
    CATERVA_TEST_ASSERT(caterva_free(data->ctx, &src));
    blosc2_remove_urlpath(urlpath);

    return 0;
}

CUTEST_TEST_TEARDOWN(set_slice_buffer) {
    blosc2_free_ctx(data->ctx);
    blosc2_destroy();
}

int main() {
    CUTEST_TEST_RUN(set_slice_buffer);
}
