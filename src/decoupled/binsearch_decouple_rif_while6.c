#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <math.h>
#include <malloc.h>
#include <stdbool.h>

#define TYPE uint32_t

#define LATENCY 128
#define OUTSTANDING_READS LATENCY

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
    index_stream,
};

#define EXTRA_ITERATIONS 1
#define RIF (OUTSTANDING_READS-1)

void kernel(
        const TYPE *table,
        const TYPE *sorted,
        int32_t *result,
        uint32_t table_elements,
        uint32_t sorted_elements
) {
    uint32_t rif = 0; // requests in flight
    uint32_t table_i = 0;
    for (uint32_t i = 0; i < table_elements;) {
        uint32_t l, r, m, index;
        TYPE element;
        if (rif < RIF && table_i < table_elements) {
            element = table[table_i];
            index = table_i++;
            l = 0;
            r = sorted_elements - 1;
            rif++;
        } else {
            element = hls_stream_deq_uint32t(element_stream, OUTSTANDING_READS);
            l = hls_stream_deq_uint32t(l_stream, OUTSTANDING_READS);
            r = hls_stream_deq_uint32t(r_stream, OUTSTANDING_READS);
            index = hls_stream_deq_uint32t(index_stream, OUTSTANDING_READS);
            TYPE tmp = hls_decouple_response_32(sorted_channel, OUTSTANDING_READS);
            m = (r + l) >> 1;
            if (tmp > element) {
                r = m-1;
            } else {
                l = m+1;
            }
            bool equal = tmp == element;
            uint32_t res = -1;
            if(equal){
                res = m;
            }
            if(equal || !(l<=r)){
                result[index] = res;
                i++;
                rif--;
                continue;
            }
        }
        m = (r + l) >> 1;
        hls_stream_enq_uint32t(element_stream, element);
        hls_stream_enq_uint32t(l_stream, l);
        hls_stream_enq_uint32t(r_stream, r);
        hls_stream_enq_uint32t(index_stream, index);
        hls_decouple_request_32(sorted_channel, &sorted[m]);
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
