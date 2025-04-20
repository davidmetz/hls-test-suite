#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <math.h>
#include <malloc.h>
#include <stdbool.h>

#define TYPE uint32_t

//#define LATENCY 128
#define LATENCY 128
#define CHUNK_SIZE LATENCY

extern void hls_decouple_request_32(uint32_t channel, uint32_t *addr);
extern uint32_t hls_decouple_response_32(uint32_t channel, uint32_t buffer_slots);

extern void hls_stream_enq_uint32t(uint32_t channel, uint32_t data);
extern uint32_t hls_stream_deq_uint32t(uint32_t channel, uint32_t buffer_slots);

enum decoupled_channels {
    table_channel,
    sorted_channel,
    r_stream,
    l_stream,
    element_stream,
    element_stream2,
    r_stream2,
    l_stream2,
    r_sorted_channel,
    l_sorted_channel,
};

#define EXTRA_ITERATIONS 1
#define MIN(a, b) (((a)<(b))?(a):(b))
void kernel(
        const TYPE *table,
        const TYPE *sorted,
        int32_t *result,
        uint32_t table_elements,
        uint32_t sorted_elements
) {

    for (uint32_t i = 0; i < table_elements; i++) {
        hls_decouple_request_32(table_channel, &table[i]);
    }
    for (uint32_t i = 0; i < table_elements; i += CHUNK_SIZE) {
        uint32_t elements_left = table_elements - i;
        uint32_t chunk = MIN(CHUNK_SIZE, elements_left);
        for (uint32_t j = 1; sorted_elements >= (j>>EXTRA_ITERATIONS); j = j << 1) {
            bool first_iteration = j == 1;
            bool last_iteration = (j>>EXTRA_ITERATIONS) << 1 > sorted_elements;
            for (uint32_t k = 0; k < chunk; ++k) {
                uint32_t l, r, m;
                TYPE element;
                if (first_iteration) {
                    element = hls_decouple_response_32(table_channel, CHUNK_SIZE);
                    l = 0;
                    r = sorted_elements - 1;
                } else {
                    element = hls_stream_deq_uint32t(element_stream, CHUNK_SIZE);
                    l = hls_stream_deq_uint32t(l_stream, CHUNK_SIZE);
                    r = hls_stream_deq_uint32t(r_stream, CHUNK_SIZE);
                    TYPE tmp = hls_decouple_response_32(sorted_channel, CHUNK_SIZE);
                    m = (r + l) >> 1;
                    if (tmp > element) {
                        r = m;
                    } else {
                        l = m;
                    }
                }
                m = (r + l) >> 1;
                if (last_iteration) {
                    hls_stream_enq_uint32t(element_stream2, element);
                    hls_stream_enq_uint32t(l_stream2, l);
                    hls_stream_enq_uint32t(r_stream2, r);
                    hls_decouple_request_32(r_sorted_channel, &sorted[r]);
                    hls_decouple_request_32(l_sorted_channel, &sorted[l]);
//                    assert(r - l < 2);
                } else {
                    hls_stream_enq_uint32t(element_stream, element);
                    hls_stream_enq_uint32t(l_stream, l);
                    hls_stream_enq_uint32t(r_stream, r);
                    hls_decouple_request_32(sorted_channel, &sorted[m]);
                }
            }
        }
    }
    for (uint32_t i = 0; i < table_elements; i++) {
        int32_t res = -1;
        TYPE tmp_l = hls_decouple_response_32(l_sorted_channel, CHUNK_SIZE);
        TYPE tmp_r = hls_decouple_response_32(r_sorted_channel, CHUNK_SIZE);
        uint32_t l = hls_stream_deq_uint32t(l_stream2, CHUNK_SIZE);
        uint32_t r = hls_stream_deq_uint32t(r_stream2, CHUNK_SIZE);
        TYPE element = hls_stream_deq_uint32t(element_stream2, CHUNK_SIZE);
        if (tmp_r == element) {
            res = r;
        } else if (tmp_l == element) {
            res = l;
        }
        result[i] = res;
    }
}

void *allocate(size_t size) {
    return memalign(4096, size + 4096);
}

int main() {
    srand(0);
    uint32_t sorted_elements = 12345;
    uint32_t table_elements = 1000;
    TYPE *table = allocate(table_elements * sizeof(TYPE));
    TYPE *sorted = allocate(sorted_elements * sizeof(TYPE));
    int32_t *result = allocate(table_elements * sizeof(int32_t));
    int32_t *expected_result = allocate(table_elements * sizeof(int32_t));
    for (size_t i = 0; i < sorted_elements; ++i) {
        // every second number is contained
        sorted[i] = i * 2;
    }
    for (size_t i = 0; i < table_elements; ++i) {
        TYPE tmp = rand() % (sorted_elements * 2);
        table[i] = tmp;
        expected_result[i] = tmp % 2 == 0 ? tmp / 2 : -1;
    }
    kernel(table, sorted, result, table_elements, sorted_elements);
    for (size_t i = 0; i < table_elements; ++i) {
        assert(result[i] == expected_result[i]);
    }
}
