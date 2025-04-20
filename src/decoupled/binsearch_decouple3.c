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
#define CHUNK_SIZE LATENCY-1

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
    for (uint32_t i = 0; i < table_elements;) {
        uint32_t i_p_chuck = i + CHUNK_SIZE;
        uint32_t chunk_end = MIN(i_p_chuck, table_elements);
        uint32_t sorted_extra = sorted_elements << EXTRA_ITERATIONS;
        for (uint32_t j = 1; sorted_extra >= j; j = j << 1) {
            bool first_iteration = j == 1;
            bool last_iteration = j << 1 > sorted_extra;
            for (uint32_t k = i; k < chunk_end; ++k) {
                uint32_t l, r, m;
                TYPE element;
                uint32_t res = -1;
                if (first_iteration) {
                    element = hls_decouple_response_32(table_channel, LATENCY);
                    l = 0;
                    r = sorted_elements - 1;
                } else {
                    element = hls_stream_deq_uint32t(element_stream, LATENCY);
                    l = hls_stream_deq_uint32t(l_stream, LATENCY);
                    r = hls_stream_deq_uint32t(r_stream, LATENCY);
                    TYPE tmp = hls_decouple_response_32(sorted_channel, LATENCY);
                    m = (r + l) >> 1;
                    if(tmp == element){
                        res = m;
                    } else if (tmp > element) {
                        r = m-1;
                    } else {
                        l = m+1;
                    }
                }
                m = (r + l) >> 1;
                if (last_iteration) {
                    result[k] = res;
                } else {
                    hls_stream_enq_uint32t(element_stream, element);
                    hls_stream_enq_uint32t(l_stream, l);
                    hls_stream_enq_uint32t(r_stream, r);
                    hls_decouple_request_32(sorted_channel, &sorted[m]);
                }
            }
        }
        i = i_p_chuck;
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
