#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <malloc.h>


#define LATENCY 100

#define MIN(a, b) ((a)>(b)?(b):(a))
void kernel(
        uint32_t *table,
        uint32_t *result,
        uint32_t n
) {
    if (!n)
        return;
    uint32_t width = 1;
    do {
        uint32_t width2 = width << 1;
        uint32_t i_outer = 0;
        do {
            uint32_t i_left = i_outer;
            uint32_t i_right_max = i_outer + width;
            uint32_t i_right = MIN(i_right_max, n);
            uint32_t i_end_max = i_outer + width2;
            uint32_t i_end = MIN(i_end_max, n);
            uint32_t i = i_left;
            uint32_t j = i_right;
            uint32_t table_i = 0;
            uint32_t table_j = 0;
            bool update_table_i = true;
            bool update_table_j = true;
            uint32_t k = i_left;
            do {
                bool i_not_empty = i < i_right;
                bool set_table_i = update_table_i & i_not_empty;
                if (set_table_i) {
                    table_i = table[i];
                }
                update_table_i = false;
                bool j_not_empty = j < i_end;
                bool set_table_j = update_table_j & j_not_empty;
                if (set_table_j) {
                    table_j = table[j];
                }
                update_table_j = false;
                uint32_t rv;
                bool j_empty = j >= i_end;
                bool i_smaller = table_i <= table_j;
                bool i_smaller_or_j_empty = j_empty | i_smaller;
                bool pick_i = i_not_empty & i_smaller_or_j_empty;
                if (pick_i) {
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
            i_outer += width2;
        } while (i_outer < n);
        if (width2 < n) {
            uint32_t i = 0;
            do {
                table[i] = result[i];
                i++;
            } while (i < n);
        }
        width = width2;
    } while (width < n);
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
    kernel(table, result, sort_elements);
    uint32_t prev_element = 0;
    for (size_t i = 0; i < sort_elements; ++i) {
        assert(result[i] == expected_result[i]);
        assert(expected_result[i] >= prev_element);
        prev_element = expected_result[i];
    }
}
