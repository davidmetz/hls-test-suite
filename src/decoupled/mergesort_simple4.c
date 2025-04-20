#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <math.h>
#include <malloc.h>

#define MIN(a, b) ((a)>(b)?(b):(a))

void sort_iteration(const uint32_t *table, uint32_t *result, uint32_t n, uint32_t width2, uint32_t width) {
    for (uint32_t i_outer = 0; i_outer < n; i_outer+=width2) {
        uint32_t i_left = i_outer;
        uint32_t i_right = MIN(i_outer + width, n);
        uint32_t i_end = MIN(i_outer + width2, n);
        uint32_t i = i_left;
        uint32_t j = i_right;
        for (uint32_t k = i_left; k < i_end; ++k) {
            if (i < i_right && (j >= i_end || table[i] <= table[j])) {
                result[k] = table[i];
                i = i + 1;
            } else {
                result[k] = table[j];
                j = j + 1;
            }
        }
    }
}

uint32_t * kernel(
        uint32_t *table,
        uint32_t *result,
        uint32_t n
) {
    uint32_t width2;
    uint32_t * rv;
    for (uint32_t width = 1; width < n; width = width2) {
        width2 = width << 1;
        sort_iteration(table, result, n, width2, width);
        rv = result;
        if(width2<n){
            width = width2;
            width2 = width << 1;
            sort_iteration(result, table, n, width2, width);
            rv = table;
        }
    }
    return rv;
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
    uint32_t* kernel_result = kernel(table, result, sort_elements);
    uint32_t prev_element = 0;
    for (size_t i = 0; i < sort_elements; ++i) {
        assert(kernel_result[i] == expected_result[i]);
        assert(expected_result[i] >= prev_element);
        prev_element = expected_result[i];
    }
}
