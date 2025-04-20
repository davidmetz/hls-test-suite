#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>

#define TYPE float
//#define TYPE int
#define LATENCY 100
#define ITERATIONS 10
#define FACTOR 0.0001f

extern void hls_decouple_request_32(uint32_t channel, const uint32_t * addr);
extern uint32_t hls_decouple_response_32(uint32_t channel, uint32_t buffer_slots);

extern void hls_decouple_request_TYPE(uint32_t channel, const TYPE * addr);
extern TYPE hls_decouple_response_TYPE(uint32_t channel, uint32_t buffer_slots);

enum decoupled_channels{
    vals_dec_channel,
    cols_dec_channel,
    rows_dec_channel,
    vec_dec_channel,
    out_dec_channel
};

void kernel(
        const TYPE *restrict vals,
        const uint32_t *restrict cols,
        const uint32_t *restrict rowDelimiters,
        uint32_t startElement,
        uint32_t endElement,
        uint32_t firstDelim,
        TYPE *restrict vec,
        TYPE *restrict out,
        uint32_t nrows,
        TYPE factor,
        uint32_t iterations
){
    for (uint32_t k = 0; k < iterations; ++k) {
        for (uint32_t j = startElement; j < endElement; j++) {
            hls_decouple_request_32(cols_dec_channel, &cols[j]);
        }
        for (uint32_t j = startElement; j < endElement; j++) {
            uint32_t i = hls_decouple_response_32(cols_dec_channel, LATENCY);
            hls_decouple_request_TYPE(vec_dec_channel, &vec[i]);
        }
        for (uint32_t j = startElement; j < endElement; j++) {
            hls_decouple_request_TYPE(vals_dec_channel, &vals[j]);
        }
        for (uint32_t j = 0; j < nrows; j++) {
            hls_decouple_request_32(rows_dec_channel, &rowDelimiters[j+2]);
        }
        TYPE sum = 0;
        uint32_t r = 0;
        uint32_t nextDelim = firstDelim;
        uint32_t rowStart = startElement;
        for(uint32_t i = 0; i < nrows; i++){
            TYPE sum = 0;
            for (uint32_t j = rowStart; j < nextDelim; j++){
                TYPE cval = hls_decouple_response_TYPE(vals_dec_channel, LATENCY);
                TYPE vval = hls_decouple_response_TYPE(vec_dec_channel, LATENCY);
                TYPE Si = cval * vval;
                sum += Si;
            }
            out[i] = sum;
            rowStart = nextDelim;
            nextDelim = hls_decouple_response_32(rows_dec_channel, LATENCY);
        }
        // the loop bounds of these two loops are different to get correct decoupling - otherwise
        // they are in the same gamma as the spmv loop after tgi and decouple-mem-state fails
        for (uint32_t l = 1; l <= nrows; ++l) {
            hls_decouple_request_TYPE(out_dec_channel, &out[l-1]);
        }
        for (uint32_t l = 1; l <= nrows; ++l) {
            TYPE oval = hls_decouple_response_TYPE(out_dec_channel, LATENCY);
            vec[l-1] = oval*factor;
        }
    }
}

void multi_spmv(
        const TYPE *restrict vals,
        const uint32_t *restrict cols,
        const uint32_t *restrict rowDelimiters,
        uint32_t nrows,
        TYPE *restrict vec,
        TYPE *restrict out
) {
    kernel(vals, cols, rowDelimiters, rowDelimiters[0], rowDelimiters[nrows], rowDelimiters[1], vec, out, nrows, FACTOR, ITERATIONS);
}

void multi_spmv_ref(
        const TYPE *restrict vals,
        const uint32_t *restrict cols,
        const uint32_t *restrict rowDelimiters,
        uint32_t nrows,
        TYPE *restrict vec,
        TYPE *restrict out
) {

    for (uint32_t k = 0; k < ITERATIONS; ++k) {
        for (uint32_t i = 0; i < nrows; i++) {
            TYPE sum = 0;
            uint32_t tmp_begin = rowDelimiters[i];
            uint32_t tmp_end = rowDelimiters[i + 1];
            for (uint32_t j = tmp_begin; j < tmp_end; j++) {
                TYPE Si = vals[j] * vec[cols[j]];
                sum = sum + Si;
            }
            out[i] = sum;
        }
        for (size_t l = 0; l < nrows; ++l) {
            vec[l] = out[l]*FACTOR;
        }
    }
}

uint32_t generate_random_square_csr(uint32_t nrows, double density, TYPE **restrict vals, uint32_t **cols, uint32_t **rowDelimiters){
    size_t expected_elements = (size_t)(nrows*nrows*density*1.5);
    TYPE *tmp_vals = malloc(sizeof(TYPE) * expected_elements);
    uint32_t *tmp_cols = malloc(sizeof(uint32_t) * expected_elements);
    *rowDelimiters = malloc(sizeof(uint32_t) * (nrows + 1));
    size_t nelements = 0;
    for (size_t i = 0; i < nrows; ++i) {
        (*rowDelimiters)[i] = nelements;
        for (size_t j = 0; j < nrows; ++j) {
            if(((double)rand())/((double)(RAND_MAX)) < density){
                tmp_vals[nelements] = nelements;
                tmp_cols[nelements] = j;
                nelements++;
            }
        }
    }
    (*rowDelimiters)[nrows] = nelements;
    *vals = malloc(sizeof(TYPE) * nelements);
    *cols = malloc(sizeof(uint32_t) * nelements);
    memcpy(*vals, tmp_vals, sizeof(TYPE) * nelements);
    memcpy(*cols, tmp_cols, sizeof(uint32_t) * nelements);
    free(tmp_vals);
    free(tmp_cols);
    return nelements;
}

int main() {
    uint32_t nrows = 128;
    double density = 0.1;//1.0/nrows;
    TYPE *vec = malloc(sizeof(TYPE) * nrows);
    TYPE *vec_ref = malloc(sizeof(TYPE) * nrows);
    for (uint32_t i = 0; i < nrows; ++i) {
        vec[i] = i;
        vec_ref[i] = i;
    }

    TYPE *vals;
    uint32_t *cols;
    uint32_t *rowDelimiters;
    srand(0);
    uint32_t nelements = generate_random_square_csr(nrows, density, &vals, &cols, &rowDelimiters);

    TYPE *out = malloc(sizeof(TYPE) * nrows);
    TYPE *out_ref = malloc(sizeof(TYPE) * nrows);
    multi_spmv_ref(vals, cols, rowDelimiters, nrows, vec_ref, out_ref);
    multi_spmv(vals, cols, rowDelimiters, nrows, vec, out);
    for (uint32_t i = 0; i < nrows; ++i) {
        assert(out_ref[i] == out[i]);
    }
    return 0;
}
