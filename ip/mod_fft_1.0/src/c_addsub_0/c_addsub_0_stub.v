// Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
// --------------------------------------------------------------------------------
// Tool Version: Vivado v.2022.1 (win64) Build 3526262 Mon Apr 18 15:48:16 MDT 2022
// Date        : Tue Jul 14 16:25:01 2026
// Host        : DESKTOP-NTUOAP1 running 64-bit major release  (build 9200)
// Command     : write_verilog -force -mode synth_stub
//               e:/WeiXin/b220_bkbase_v403_fixusbrest/ip/mod_fft/mod_fft_1.0/src/c_addsub_0/c_addsub_0_stub.v
// Design      : c_addsub_0
// Purpose     : Stub declaration of top-level module interface
// Device      : xc7z020clg400-1
// --------------------------------------------------------------------------------

// This empty module with port declaration file causes synthesis tools to infer a black box for IP.
// The synthesis directives are for Synopsys Synplify support to prevent IO buffer insertion.
// Please paste the declaration into a Verilog source file or add the file as an additional source.
(* x_core_info = "c_addsub_v12_0_14,Vivado 2022.1" *)
module c_addsub_0(A, B, CLK, CE, S)
/* synthesis syn_black_box black_box_pad_pin="A[63:0],B[63:0],CLK,CE,S[63:0]" */;
  input [63:0]A;
  input [63:0]B;
  input CLK;
  input CE;
  output [63:0]S;
endmodule
