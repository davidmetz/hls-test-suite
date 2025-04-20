#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>

extern void hls_stream_enq_uint32t(uint32_t channel, int data);
extern uint32_t hls_stream_deq_uint32t(uint32_t channel, uint32_t buffer_slots);

enum stream_channels{
    stream_channel0,
};
// this kernel is extracted from hashtable rif - it's tricky for buffer placement, because the stream enq is implicitly
// conditional on a stream deq
int kernel(int a, int b) {
    int c = 0;
    int d;
	for (int i=0; i<b; i++) {
        bool stream = true;
        if(i != 0){
            // not first iteration
            d = hls_stream_deq_uint32t(stream_channel0, 5);
            c += d;
            if(d == b){
                // last element reached
                stream = false;
            }
        }
        if(stream) {
            a++;
            hls_stream_enq_uint32t(stream_channel0, a);
        }
	}
	return c;
}

int main(int argc, char** argv) {
	int result = kernel(1, 5);
	printf("Result: %i\n", result);

	// Check if correct result
	if (result == 14)
		return 0;

	return 1;
}
