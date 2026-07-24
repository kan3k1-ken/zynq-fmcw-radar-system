`timescale 1 ns / 1 ps

module mod_fft_v1_0_S00_AXIS #
(
	parameter integer C_S_AXIS_TDATA_WIDTH = 64
)
(
	input wire  S_AXIS_ACLK,
	input wire  S_AXIS_ARESETN,
	output wire S_AXIS_TREADY,
	input wire  [C_S_AXIS_TDATA_WIDTH-1 : 0] S_AXIS_TDATA,
	input wire  [(C_S_AXIS_TDATA_WIDTH/8)-1 : 0] S_AXIS_TSTRB,
	input wire  S_AXIS_TLAST,
	input wire  S_AXIS_TVALID,

	output wire [31:0] mod_data_o,
	output wire        mod_en_o,
	output wire        frame_sync_o
);

	assign S_AXIS_TREADY = 1'b1;

	wire signed [27:0] fft_i = S_AXIS_TDATA[27:0];
	wire signed [27:0] fft_q = S_AXIS_TDATA[59:32];
	wire signed [31:0] data_i = {{4{fft_i[27]}}, fft_i};
	wire signed [31:0] data_q = {{4{fft_q[27]}}, fft_q};

localparam integer FFT_COMPONENT_SHIFT = 6;
localparam [31:0] COMPLEX_FORMAT_MAGIC = 32'h43584936;

	wire signed [31:0] scaled_i = data_i >>> FFT_COMPONENT_SHIFT;
	wire signed [31:0] scaled_q = data_q >>> FFT_COMPONENT_SHIFT;
	wire [15:0] packed_i = (scaled_i > 32'sd32767)  ? 16'h7FFF :
	                         (scaled_i < -32'sd32768) ? 16'h8000 : scaled_i[15:0];
	wire [15:0] packed_q = (scaled_q > 32'sd32767)  ? 16'h7FFF :
	                         (scaled_q < -32'sd32768) ? 16'h8000 : scaled_q[15:0];

	assign mod_data_o   = S_AXIS_TLAST ? COMPLEX_FORMAT_MAGIC : {packed_q, packed_i};
	assign mod_en_o     = S_AXIS_TVALID;
	assign frame_sync_o = S_AXIS_TVALID && S_AXIS_TLAST;

endmodule
