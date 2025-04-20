#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <math.h>
#include <malloc.h>

#define TYPE uint32_t

void kernel(
        const TYPE *table,
        const TYPE *sorted,
        int32_t *result,
        uint32_t table_elements,
        uint32_t sorted_elements
) {

    for (uint32_t i = 0; i < table_elements; ++i) {
        TYPE element = table[i];
        uint32_t l = 0, r = sorted_elements - 1;
        int32_t res = -1;
        while (l<=r) {
            uint32_t m = (r + l) >> 1;
            TYPE tmp = sorted[m];
            if(tmp == element){
                res = m;
                break;
            }
            if (tmp > element) {
                r = m-1;
            } else {
                l = m+1;
            }
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
    TYPE *table = allocate(table_elements* sizeof(TYPE));
    TYPE *sorted = allocate(sorted_elements* sizeof(TYPE));
    int32_t *result = allocate(table_elements* sizeof(int32_t));
    int32_t *expected_result = allocate(table_elements* sizeof(int32_t));
    for (size_t i = 0; i < sorted_elements; ++i) {
        // every second number is contained
        sorted[i] = i*2;
    }
    for (size_t i = 0; i < table_elements; ++i) {
        TYPE tmp = rand()%(sorted_elements*2);
        table[i] = tmp;
        expected_result[i] = tmp%2==0?tmp/2:-1;
    }
    kernel(table, sorted, result, table_elements, sorted_elements);
    for (size_t i = 0; i < table_elements; ++i) {
        assert(result[i] == expected_result[i]);
    }
}
