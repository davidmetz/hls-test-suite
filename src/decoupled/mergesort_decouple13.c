#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <malloc.h>


#define LATENCY 100

#define MIN(a, b) ((a)>(b)?(b):(a))

extern void hls_decouple_request_32(uint32_t channel, uint32_t *addr);

extern uint32_t hls_decouple_response_32(uint32_t channel, uint32_t buffer_slots);

extern void hls_stream_enq_uint32t(uint32_t channel, uint32_t data);

extern uint32_t hls_stream_deq_uint32t(uint32_t channel, uint32_t buffer_slots);

enum decoupled_channels {
    result_channel,
    i_channel,
    j_channel,
    i_channel2,
    j_channel2,
    rv_stream,
};

void merge(const uint32_t *table, uint32_t *result, uint32_t i_left, uint32_t i_right, uint32_t i_end) {
    uint32_t i = i_left;
    uint32_t j = i_right;
    uint32_t table_i;
    uint32_t table_j;
    bool update_table_i = true;
    bool update_table_j = true;
    uint32_t k = i_left;
    do {
        if (update_table_i && i < i_right) {
            table_i = hls_decouple_response_32(i_channel, LATENCY);
        }
        update_table_i = false;
        if (update_table_j && j < i_end) {
            table_j = hls_decouple_response_32(j_channel, LATENCY);
        }
        update_table_j = false;
        uint32_t rv;
        if (i < i_right && (j >= i_end || table_i <= table_j)) {
            rv = table_i;
            update_table_i = true;
            i = i + 1;
        } else {
            rv = table_j;
            update_table_j = true;
            j = j + 1;
        }
        result[k] = rv;
        k++;
    } while (k < i_end);
}

void sort_iteration(const uint32_t *table, uint32_t *result, uint32_t n, uint32_t width, uint32_t width2) {
    uint32_t i_outer2 = 0;
    do {
        uint32_t i_left = i_outer2;
        uint32_t i_right = MIN(i_outer2 + width, n);
        uint32_t i_end = MIN(i_outer2 + width2, n);
        uint32_t i_req = i_left;
        do {
            hls_decouple_request_32(i_channel, &table[i_req]);
            i_req++;
        } while (i_req < i_right);
        for (uint32_t j_req = i_right; j_req < i_end; j_req++) {
            hls_decouple_request_32(j_channel, &table[j_req]);
        }
        i_outer2 += width2;
    } while (i_outer2 < n);
    uint32_t i_outer = 0;
    do {
        uint32_t i_left = i_outer;
        uint32_t i_right = MIN(i_outer + width, n);
        uint32_t i_end = MIN(i_outer + width2, n);
        merge(table, result, i_left, i_right, i_end);
        i_outer += width2;
    } while (i_outer < n);
}

void merge2(const uint32_t *table, uint32_t *result, uint32_t i_left, uint32_t i_right, uint32_t i_end) {
    uint32_t i = i_left;
    uint32_t j = i_right;
    uint32_t table_i;
    uint32_t table_j;
    bool update_table_i = true;
    bool update_table_j = true;
    uint32_t k = i_left;
    do {
        if (update_table_i && i < i_right) {
            table_i = hls_decouple_response_32(i_channel2, LATENCY);
        }
        update_table_i = false;
        if (update_table_j && j < i_end) {
            table_j = hls_decouple_response_32(j_channel2, LATENCY);
        }
        update_table_j = false;
        uint32_t rv;
        if (i < i_right && (j >= i_end || table_i <= table_j)) {
            rv = table_i;
            update_table_i = true;
            i = i + 1;
        } else {
            rv = table_j;
            update_table_j = true;
            j = j + 1;
        }
        result[k] = rv;
        k++;
    } while (k < i_end);
}

void sort_iteration2(const uint32_t *table, uint32_t *result, uint32_t n, uint32_t width, uint32_t width2) {
    uint32_t i_outer2 = 0;
    do {
        uint32_t i_left = i_outer2;
        uint32_t i_right = MIN(i_outer2 + width, n);
        uint32_t i_end = MIN(i_outer2 + width2, n);
        uint32_t i_req = i_left;
        do {
            hls_decouple_request_32(i_channel2, &table[i_req]);
            i_req++;
        } while (i_req < i_right);
        for (uint32_t j_req = i_right; j_req < i_end; j_req++) {
            hls_decouple_request_32(j_channel2, &table[j_req]);
        }
        i_outer2 += width2;
    } while (i_outer2 < n);
    uint32_t i_outer = 0;
    do {
        uint32_t i_left = i_outer;
        uint32_t i_right = MIN(i_outer + width, n);
        uint32_t i_end = MIN(i_outer + width2, n);
        merge2(table, result, i_left, i_right, i_end);
        i_outer += width2;
    } while (i_outer < n);
}

bool kernel(
        uint32_t *table,
        uint32_t *result,
        uint32_t n
) {
    if (!n)
        return false;
    uint32_t width = 1;
    bool rv;
    do {
        uint32_t width2 = width << 1;
        sort_iteration(table, result, n, width, width2);
        width = width2;
        rv = false;
        if (width < n) {
            // instantiate a second sort to avoid the copy
            width2 = width << 1;
            sort_iteration2(result, table, n, width, width2);
            width = width2;
            rv = true;
        }
    } while (width < n);
    return rv;
}

uint32_t * kernel_wrapper(
        uint32_t *table,
        uint32_t *result,
        uint32_t n
){
    bool swapped = kernel(table, result, n);
    if(swapped){
        return table;
    }
    return result;
}

int comp(const void *elem1, const void *elem2) {
    int f = *((uint32_t *) elem1);
    int s = *((uint32_t *) elem2);
    if (f > s) return 1;
    if (f < s) return -1;
    return 0;
}

void sort_ref(uint32_t *arr, uint32_t size) {
    qsort(arr, size, sizeof(uint32_t), comp);
}

void *allocate(size_t size) {
    return memalign(4096, size + 4096);
}

int main() {
    srand(0);
    uint32_t sort_elements = 234;
    uint32_t *table = allocate(sort_elements * sizeof(uint32_t));
    uint32_t *result = allocate(sort_elements * sizeof(uint32_t));
    uint32_t *expected_result = allocate(sort_elements * sizeof(uint32_t));
    for (size_t i = 0; i < sort_elements; ++i) {
        uint32_t tmp = rand();
        table[i] = tmp;
        expected_result[i] = tmp;
    }
    sort_ref(expected_result, sort_elements);
    uint32_t* kernel_result = kernel_wrapper(table, result, sort_elements);
    uint32_t prev_element = 0;
    for (size_t i = 0; i < sort_elements; ++i) {
        assert(kernel_result[i] == expected_result[i]);
        assert(expected_result[i] >= prev_element);
        prev_element = expected_result[i];
    }
}
