`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Company: 
// Engineer: 
// 
// Create Date: 2025/03/12 17:55:38
// Design Name: 
// Module Name: bkt_forwarder
// Project Name: 
// Target Devices: 
// Tool Versions: 
// Description: 
// 
// Dependencies: 
// 
// Revision:
// Revision 0.01 - File Created
// Additional Comments:
// 
//////////////////////////////////////////////////////////////////////////////////


module bkt_forwarder #(
    parameter BB_VERSION = 101,
    parameter SUBSYSTEM_NUMS = 1
)
   (

    input wire clk,
    input wire rst_n,

    input wire clk_bb,
    input wire bb_rst_n,

    output wire bkt_ready_o,
    output wire [31:0]bkt_index_o,
    output wire [31:0]bkt_data_o,
  

    input   [31:0] bk_status1_i,
    input   [31:0] bk_status2_i,
    input   [31:0] bk_status3_i,
    input   [31:0] bk_status4_i,
    input   [31:0] bk_status5_i,
    input   [31:0] bk_status6_i,
    input   [31:0] bk_status7_i,
    input   [31:0] bk_status8_i,
    input   [31:0] bk_status9_i,
    input   [31:0] bk_status10_i,
    input   [31:0] bk_status11_i,	
    input   [31:0] bk_status12_i,
    input   [31:0] bk_status13_i,
    input   [31:0] bk_status14_i,
    input   [31:0] bk_status15_i,
    input   [31:0] bk_status16_i,
    input   [31:0] bk_status17_i,	
    input   [31:0] bk_status18_i,
    input   [31:0] bk_status19_i,
    input   [31:0] bk_status20_i,
    input   [31:0] bk_status21_i,
    input   [31:0] bk_status22_i,
    input   [31:0] bk_status23_i,
    input   [31:0] bk_status24_i,
    input   [31:0] bk_status25_i,
    input   [31:0] bk_status26_i,
    input   [31:0] bk_status27_i,
    input   [31:0] bk_status28_i,
    input   [31:0] bk_status29_i,
    input   [31:0] bk_status30_i,
    input   [31:0] bk_status31_i,



    output wire   interrupt,
    input  wire   [11:0]s_axi_control_araddr,
    output wire   s_axi_control_arready,
    input  wire   s_axi_control_arvalid,
    input  wire   [11:0]s_axi_control_awaddr,
    output wire   s_axi_control_awready,
    input  wire   s_axi_control_awvalid,
    input  wire   s_axi_control_bready,
    output wire   [1:0]s_axi_control_bresp,
    output wire   s_axi_control_bvalid,
    output wire   [31:0]s_axi_control_rdata,
    input  wire   s_axi_control_rready,
    output wire   [1:0]s_axi_control_rresp,
    output wire   s_axi_control_rvalid,
    input  wire   [31:0]s_axi_control_wdata,
    output wire   s_axi_control_wready,
    input  wire   [3:0]s_axi_control_wstrb,
    input  wire   s_axi_control_wvalid

   );

    wire bkt_ready;
    wire [31:0] bkt_index;
    wire [31:0] bkt_data;

    
    wire bkt_ready_cdc;    
      xpm_cdc_array_single #(
      .DEST_SYNC_FF(8),   // DECIMAL; range: 2-10
      .INIT_SYNC_FF(0),   // DECIMAL; 0=disable simulation init values, 1=enable simulation init values
      .SIM_ASSERT_CHK(0), // DECIMAL; 0=disable simulation messages, 1=enable simulation messages
      .SRC_INPUT_REG(1),  // DECIMAL; 0=do not register input, 1=register input
      .WIDTH(1)           // DECIMAL; range: 1-1024
   )
   xpm_cdc_array_single_inst1 (
      .dest_out(bkt_ready_cdc), // WIDTH-bit output: src_in synchronized to the destination clock domain. This
                           // output is registered.
      .dest_clk(clk_bb), // 1-bit input: Clock signal for the destination clock domain.
      .src_clk(clk),   // 1-bit input: optional; required when SRC_INPUT_REG = 1
      .src_in(bkt_ready)      // WIDTH-bit input: Input single-bit array to be synchronized to destination clock
                           // domain. It is assumed that each bit of the array is unrelated to the others. This
                           // is reflected in the constraints applied to this macro. To transfer a binary value
                           // losslessly across the two clock domains, use the XPM_CDC_GRAY macro instead.
   );

wire [31:0] bkt_index_cdc;
      xpm_cdc_array_single #(
      .DEST_SYNC_FF(8),   // DECIMAL; range: 2-10
      .INIT_SYNC_FF(0),   // DECIMAL; 0=disable simulation init values, 1=enable simulation init values
      .SIM_ASSERT_CHK(0), // DECIMAL; 0=disable simulation messages, 1=enable simulation messages
      .SRC_INPUT_REG(1),  // DECIMAL; 0=do not register input, 1=register input
      .WIDTH(32)           // DECIMAL; range: 1-1024
   )
   xpm_cdc_array_single_inst2 (
      .dest_out(bkt_index_cdc), // WIDTH-bit output: src_in synchronized to the destination clock domain. This
                           // output is registered.
      .dest_clk(clk_bb), // 1-bit input: Clock signal for the destination clock domain.
      .src_clk(clk),   // 1-bit input: optional; required when SRC_INPUT_REG = 1
      .src_in(bkt_index)      // WIDTH-bit input: Input single-bit array to be synchronized to destination clock
                           // domain. It is assumed that each bit of the array is unrelated to the others. This
                           // is reflected in the constraints applied to this macro. To transfer a binary value
                           // losslessly across the two clock domains, use the XPM_CDC_GRAY macro instead.
   );

wire [31:0] bkt_data_cdc;
      xpm_cdc_array_single #(
      .DEST_SYNC_FF(8),   // DECIMAL; range: 2-10
      .INIT_SYNC_FF(0),   // DECIMAL; 0=disable simulation init values, 1=enable simulation init values
      .SIM_ASSERT_CHK(0), // DECIMAL; 0=disable simulation messages, 1=enable simulation messages
      .SRC_INPUT_REG(1),  // DECIMAL; 0=do not register input, 1=register input
      .WIDTH(32)           // DECIMAL; range: 1-1024
   )
   xpm_cdc_array_single_inst3 (
      .dest_out(bkt_data_cdc), // WIDTH-bit output: src_in synchronized to the destination clock domain. This
                           // output is registered.
      .dest_clk(clk_bb), // 1-bit input: Clock signal for the destination clock domain.
      .src_clk(clk),   // 1-bit input: optional; required when SRC_INPUT_REG = 1
      .src_in(bkt_data)      // WIDTH-bit input: Input single-bit array to be synchronized to destination clock
                           // domain. It is assumed that each bit of the array is unrelated to the others. This
                           // is reflected in the constraints applied to this macro. To transfer a binary value
                           // losslessly across the two clock domains, use the XPM_CDC_GRAY macro instead.
   );




wire [31:0] bk_status_cdc;
      xpm_cdc_array_single #(
      .DEST_SYNC_FF(8),   // DECIMAL; range: 2-10
      .INIT_SYNC_FF(0),   // DECIMAL; 0=disable simulation init values, 1=enable simulation init values
      .SIM_ASSERT_CHK(0), // DECIMAL; 0=disable simulation messages, 1=enable simulation messages
      .SRC_INPUT_REG(1),  // DECIMAL; 0=do not register input, 1=register input
      .WIDTH(32)           // DECIMAL; range: 1-1024
   )
   xpm_cdc_array_single_inst4 (
      .dest_out(bk_status_cdc), // WIDTH-bit output: src_in synchronized to the destination clock domain. This
                           // output is registered.
      .dest_clk(clk), // 1-bit input: Clock signal for the destination clock domain.
      .src_clk(clk_bb),   // 1-bit input: optional; required when SRC_INPUT_REG = 1
      .src_in(bk_status_p)      // WIDTH-bit input: Input single-bit array to be synchronized to destination clock
                           // domain. It is assumed that each bit of the array is unrelated to the others. This
                           // is reflected in the constraints applied to this macro. To transfer a binary value
                           // losslessly across the two clock domains, use the XPM_CDC_GRAY macro instead.
   );




    bk_gen_axi_lite #(.ready_bit(0))bk_gen_axi_lite_i
       (.ap_clk(clk),
        .ap_rst_n(rst_n),
        .BkpCfg_Ready_o(bkt_ready),
        .BkpCfg_DataIndex_o(bkt_index),
        .BkpCfg_DataValue_o(bkt_data),
        .BK_Status_i(bk_status_cdc),
  
        .interrupt(interrupt),
        .s_axi_control_araddr(s_axi_control_araddr),
        .s_axi_control_arready(s_axi_control_arready),
        .s_axi_control_arvalid(s_axi_control_arvalid),
        .s_axi_control_awaddr(s_axi_control_awaddr),
        .s_axi_control_awready(s_axi_control_awready),
        .s_axi_control_awvalid(s_axi_control_awvalid),
        .s_axi_control_bready(s_axi_control_bready),
        .s_axi_control_bresp(s_axi_control_bresp),
        .s_axi_control_bvalid(s_axi_control_bvalid),
        .s_axi_control_rdata(s_axi_control_rdata),
        .s_axi_control_rready(s_axi_control_rready),
        .s_axi_control_rresp(s_axi_control_rresp),
        .s_axi_control_rvalid(s_axi_control_rvalid),
        .s_axi_control_wdata(s_axi_control_wdata),
        .s_axi_control_wready(s_axi_control_wready),
        .s_axi_control_wstrb(s_axi_control_wstrb),
        .s_axi_control_wvalid(s_axi_control_wvalid));
    

    
	/********* gen code start*********/  
	reg bkt_ready_d1,bkt_ready_d2,bkt_ready_d3;
	always @(posedge clk_bb)
	if(!bb_rst_n)
    begin
        bkt_ready_d1 <= 1'b0;
        bkt_ready_d2 <= 1'b0;
        bkt_ready_d3 <= 1'b0;
    end
 	else
    begin
        bkt_ready_d1 <= bkt_ready_cdc;
        bkt_ready_d2 <= bkt_ready_d1;
        bkt_ready_d3 <= bkt_ready_d2;
    end 

	wire bk_ready_pedge;
	assign bk_ready_pedge = bkt_ready_d2 & ~bkt_ready_d3;

	reg [31:0] bk_index_d1,bk_index_d2,bk_index_d3;
	always @(posedge clk_bb)
	if(!bb_rst_n)
	   begin
	          bk_index_d1 <= 'd0;
	          bk_index_d2 <= 'd0;
	          bk_index_d3 <= 'd0;
	   end 
	else
	   begin
	          bk_index_d1 <= bkt_index_cdc;
	          bk_index_d2 <= bk_index_d1;
	          bk_index_d3 <= bk_index_d2;
	   end 

	reg [31:0] bk_data_d1,bk_data_d2,bk_data_d3;
	always @(posedge clk_bb)
	if(!bb_rst_n)
	   begin
	          bk_data_d1 <= 'd0;
	          bk_data_d2 <= 'd0;
	          bk_data_d3 <= 'd0;
	   end 
	else
	   begin
	          bk_data_d1 <= bkt_data_cdc;
	          bk_data_d2 <= bk_data_d1;
	          bk_data_d3 <= bk_data_d2;
	   end 


	wire [31:0] bk_index;
	assign bk_index = bk_index_d3;
	wire [31:0] bk_data;
	assign bk_data = bk_data_d3;
	
  
    reg  [31:0] status_sw;
    always @(posedge clk_bb)
    if(!bb_rst_n)
        status_sw <= 'd0;
    else if(bk_ready_pedge && bk_index ==  1)
        status_sw <= bk_data;
    else
        status_sw <= status_sw;
        

    // Concatenate all input signals into a wide wire for indexing
    wire [31*32-1:0] all_inputs = {
        bk_status31_i, bk_status30_i, bk_status29_i, bk_status28_i, bk_status27_i, bk_status26_i, bk_status25_i, bk_status24_i,
        bk_status23_i, bk_status22_i, bk_status21_i, bk_status20_i, bk_status19_i, bk_status18_i, bk_status17_i, bk_status16_i,
        bk_status15_i, bk_status14_i, bk_status13_i, bk_status12_i, bk_status11_i, bk_status10_i, bk_status9_i,  bk_status8_i,
        bk_status7_i,  bk_status6_i,  bk_status5_i,  bk_status4_i,  bk_status3_i,  bk_status2_i,  bk_status1_i
    };

    wire[31:0] bk_status [0:30];
    genvar i;
    generate
        for (i = 0; i < 31; i = i + 1) begin : gen_bk_status_assign
            // Load input if i < SUBSYSTEM_NUMS, otherwise set to 0
            assign bk_status[i] = (i < SUBSYSTEM_NUMS) ? all_inputs[(i+1)*32-1:i*32] : 32'b0;
        end
    endgenerate

	reg [31:0] bk_status_p;
    always @(posedge clk_bb)
    if(!bb_rst_n)
        bk_status_p <= 'd0;
    else
    begin
    case(status_sw)
        0:   bk_status_p  <= BB_VERSION;     // Index 0: Module version
        1:   bk_status_p  <= bk_status[0];   // Index 1: bk_status1_i
        2:   bk_status_p  <= bk_status[1];   // Index 2: bk_status2_i
        3:   bk_status_p  <= bk_status[2];   // Index 3: bk_status3_i
        4:   bk_status_p  <= bk_status[3];
        5:   bk_status_p  <= bk_status[4];
        6:   bk_status_p  <= bk_status[5];
        7:   bk_status_p  <= bk_status[6];
        8:   bk_status_p  <= bk_status[7];
        9:   bk_status_p  <= bk_status[8];
        10:  bk_status_p  <= bk_status[9];
        11:  bk_status_p  <= bk_status[10];
        12:  bk_status_p  <= bk_status[11];
        13:  bk_status_p  <= bk_status[12];
        14:  bk_status_p  <= bk_status[13];
        15:  bk_status_p  <= bk_status[14];
        16:  bk_status_p  <= bk_status[15];
        17:  bk_status_p  <= bk_status[16];
        18:  bk_status_p  <= bk_status[17];
        19:  bk_status_p  <= bk_status[18];
        20:  bk_status_p  <= bk_status[19];
        21:  bk_status_p  <= bk_status[20];
        22:  bk_status_p  <= bk_status[21];
        23:  bk_status_p  <= bk_status[22];
        24:  bk_status_p  <= bk_status[23];
        25:  bk_status_p  <= bk_status[24];
        26:  bk_status_p  <= bk_status[25];
        27:  bk_status_p  <= bk_status[26];
        28:  bk_status_p  <= bk_status[27];
        29:  bk_status_p  <= bk_status[28];
        30:  bk_status_p  <= bk_status[29];
        31:  bk_status_p  <= bk_status[30];  // Index 31: bk_status31_i
        default:
            bk_status_p <= 32'b0;
     endcase
    end 
	 
	 
 assign     bkt_data_o = bkt_data_cdc;
 assign     bkt_index_o = bkt_index_cdc;
 assign     bkt_ready_o = bkt_ready_cdc;
 
        
endmodule
