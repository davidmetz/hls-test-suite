module op_FPOP_sub_I32W_I32W_O32W(	// git/chisel-template/src/main/scala/hls_float/hls_float.scala:111:7
  output        i0_ready,	// git/chisel-template/src/main/scala/hls_float/hls_float.scala:112:16
  input         i0_valid,	// git/chisel-template/src/main/scala/hls_float/hls_float.scala:112:16
  input  [31:0] i0_data,	// git/chisel-template/src/main/scala/hls_float/hls_float.scala:112:16
  output        i1_ready,	// git/chisel-template/src/main/scala/hls_float/hls_float.scala:113:16
  input         i1_valid,	// git/chisel-template/src/main/scala/hls_float/hls_float.scala:113:16
  input  [31:0] i1_data,	// git/chisel-template/src/main/scala/hls_float/hls_float.scala:113:16
  input         o0_ready,	// git/chisel-template/src/main/scala/hls_float/hls_float.scala:114:16
  output        o0_valid,	// git/chisel-template/src/main/scala/hls_float/hls_float.scala:114:16
  output [31:0] o0_data,	// git/chisel-template/src/main/scala/hls_float/hls_float.scala:114:16
  input         clk,	// git/chisel-template/src/main/scala/hls_float/hls_float.scala:115:18
                reset	// git/chisel-template/src/main/scala/hls_float/hls_float.scala:116:19
);

  wire [31:0] neg = {~(i1_data[31]), i1_data[30:0]};	// git/chisel-template/src/main/scala/floatingpoint/FloatingPoint.scala:205:21, git/chisel-template/src/main/scala/hls_float/hls_float.scala:119:{40,61}
  op_FPOP_add_I32W_I32W_O32W_int add (	// git/chisel-template/src/main/scala/hls_float/hls_float.scala:121:21
    .i0_ready (i0_ready),
    .i0_valid (i0_valid),
    .i0_data  (i0_data),
    .i1_ready (i1_ready),
    .i1_valid (i1_valid),
    .i1_data  (neg),	// git/chisel-template/src/main/scala/hls_float/hls_float.scala:119:61
    .o0_ready (o0_ready),
    .o0_valid (o0_valid),
    .o0_data  (o0_data),
    .clk      (clk),
    .reset    (reset)
  );	// git/chisel-template/src/main/scala/hls_float/hls_float.scala:121:21
endmodule


