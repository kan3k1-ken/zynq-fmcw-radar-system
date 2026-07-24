`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Company: 
// Engineer: 
// 
// Create Date: 04/16/2024 05:28:22 PM
// Design Name: 
// Module Name: test_master
// Project Name: 
// Target Devices: 
// Tool Versions: 
// Description: 
// 
// Dependencies: 
// Revision:
// Revision 0.01 - File Created
// Additional Comments:
// 
//////////////////////////////////////////////////////////////////////////////////

module test_master (
	input wire clk,
	input wire rst_n,
	
	// the bk system port  MBKP
     
    output  wire                   bkt_ready_o,
    output  wire            [31:0] bkt_index_o,
    output  wire            [31:0] bkt_data_o,
    
    
    
    output wire tx

);
	
	localparam index_max = 4500; // base 
	localparam cnt0_delay = 20;
	

    reg [63:0]cnt_delay;
    always @(posedge clk)
	if(!rst_n)
	   cnt_delay <= 1'b0;
	else if(cnt_delay == 64'd4710)
	   cnt_delay <= cnt_delay;
	else
	   cnt_delay <= cnt_delay + 1'd1;
	   
	
	wire start;
	assign start =    (cnt_delay == 64'd4710) ? 1'b1 : 1'b0;
	
	
	reg start_z1 ,start_z2;
	always @(posedge clk)
	if(!rst_n)
	   begin
	       start_z1 <= 'd0;
	       start_z2 <= 'd0;
	   end 
	else
	   begin
	       start_z1 <= start;
	       start_z2 <= start_z1;
	   end 
	   
	 wire start_pedge;
	 assign   start_pedge = start_z1 & !start_z2;
 
    reg [31:0] cnt0;
    always @(posedge clk)
	if(!rst_n)
	   cnt0 <= 'd0;
	else if(cnt0 == cnt0_delay - 1'd1)
	   cnt0 <= 1'b0;
	else
	   cnt0 <= cnt0 + 1'd1;
	   
    
	reg cnt1_gate;
	always @(posedge clk)
	if(!rst_n)
		cnt1_gate <= 1'd0;
    else if(start_pedge)
        cnt1_gate <= 1'b1;
	else if(cnt0 == cnt0_delay - 1'd1)
	   begin
	       if(cnt1 == index_max)
	           cnt1_gate <= 1'b0;
	       else
	           cnt1_gate <= cnt1_gate;
	   end 
	else
		cnt1_gate <= cnt1_gate;
		
	reg [31:0] cnt1;
	always @(posedge clk)
	if(!rst_n)
		cnt1 <= 'd0;
    else if(cnt1_gate == 1'b0)
        cnt1 <= 'd0;
	else if(cnt0 == cnt0_delay - 1'd1)
		cnt1 <= cnt1 + 1'd1;
	else
		cnt1 <= cnt1;
  
    
	
	wire [31:0] bk_index_p1;	
	assign bk_index_p1 = cnt1 + 1;

	/*************************************************************************/
	// add tb.v  at here
	/***********************************bk_uart bk_base:1000**************************************/
    localparam base100_point = 100;
    localparam base120_point = 200;
    reg [31:0] bk_index_base1000;
    always @(*)
    begin
        if(bk_index_p1 >= base100_point  && bk_index_p1 <= base100_point)     
            bk_index_base1000 = 1000;
        else if(bk_index_p1 >= base100_point +1  && bk_index_p1 <= base100_point + 1)   
            bk_index_base1000 = 1001;
        else if(bk_index_p1 >= base100_point +2  && bk_index_p1 <= base100_point + 2)   
            bk_index_base1000 = 1002;
        else if(bk_index_p1 >= base100_point +3  && bk_index_p1 <= base100_point + 3)   
            bk_index_base1000 = 1004;
        else if(bk_index_p1 >= base100_point +4  && bk_index_p1 <= base100_point + 4)   
            bk_index_base1000 = 1005;
		else if(bk_index_p1 >= base100_point +5  && bk_index_p1 <= base100_point + 6)   
            bk_index_base1000 = 1006;
		else if(bk_index_p1 >= base100_point +7  && bk_index_p1 <= base100_point + 8)   
            bk_index_base1000 = 1003;
        else if(bk_index_p1 >= base120_point  && bk_index_p1 <= base120_point + 199)   
            bk_index_base1000 = 1204;
        else if(bk_index_p1 >= base120_point +200  && bk_index_p1 <= base120_point + 200)     
            bk_index_base1000 = 1200;
        else if(bk_index_p1 >= base120_point +201  && bk_index_p1 <= base120_point + 201)   
            bk_index_base1000 = 1201;
        else if(bk_index_p1 >= base120_point +202  && bk_index_p1 <= base120_point + 202)   
            bk_index_base1000 = 1202;
        else if(bk_index_p1 >= base120_point +203  && bk_index_p1 <= base120_point + 222)   
            bk_index_base1000 = 1203;
        else if(bk_index_p1 >= base120_point +223  && bk_index_p1 <= base120_point + 223)   
            bk_index_base1000 = 1205;
        else
            bk_index_base1000 = 'd0;    
    end
	
	
    reg [31:0] bk_data_base1000;
    always @(*)
    begin
		if(bk_index_base1000 == 0)			 // bk_mode
			bk_data_base1000 = 'd0;
        else if(bk_index_base1000 == 1000)     // uart_DESR 
			bk_data_base1000 = 1'b1;
		else if(bk_index_base1000 == 1001)     // BandRate_bit
			bk_data_base1000 = 100_000_000/1152000;
		else if(bk_index_base1000 == 1002)     // uart_send_buf
			bk_data_base1000 = 8'hcd;
		else if(bk_index_base1000 == 1003)     // uart_send_start
			begin
				if(bk_index_p1 == base100_point +7)
					bk_data_base1000 = 1'b1;
				else
					bk_data_base1000 = 1'b0;
			end 
		else if(bk_index_base1000 == 1004)     // rec_buf(1)
			bk_data_base1000 = 0;
		else if(bk_index_base1000 == 1005)     // rec_buf(2)
			bk_data_base1000 = 0;
		else if(bk_index_base1000 == 1006)     // rec_clean
			begin
				if(bk_index_p1 == base100_point +5)
					bk_data_base1000 = 1'b1;
				else
					bk_data_base1000 = 1'b0;
			end	
		else if(bk_index_base1000 == 1200)     // nco_gen DESR
			bk_data_base1000 = 1'b1;
		else if(bk_index_base1000 == 1201)     // nco_gen DDS_scale
			bk_data_base1000 = 8'd1;
		else if(bk_index_base1000 == 1202)     // nco_gen DDS_CH_enable
			bk_data_base1000 = 1'b1;
		else if(bk_index_base1000 == 1203)     // nco_gen init_phase
			bk_data_base1000 = 16'd0;
		else if(bk_index_base1000 == 1204)     // nco_gen DDS_D
			bk_data_base1000 = 32'd1000000;
		else if(bk_index_base1000 == 1205)     // nco_gen req_done
			bk_data_base1000 = 1'b1;
		else
			bk_data_base1000 = 'd0;
    end
	
	
	reg [31:0] sq_cnt;
	always @(posedge clk)
	if(!rst_n)
	   sq_cnt <= 'd0;
	else if(sq_cnt == 900)
	   sq_cnt <= 'd0;
	else
	   sq_cnt <= sq_cnt + 1'd1;
	
	
	reg tx_p;
	always @(posedge clk)
	if(!rst_n)
	   tx_p <= 1'b1;
	else if(sq_cnt == 900)
	   tx_p <= !tx_p;
	else
	   tx_p <= tx_p;
	   
	reg  rx_init_gate;
	always @(posedge clk)
	if(!rst_n)
	   rx_init_gate <= 1'b1;
	else if(rx_init_cnt == 10000)
	   rx_init_gate <= 1'b0;
	else
	   rx_init_gate <= rx_init_gate;
	   
	   
	reg [31:0] rx_init_cnt;
    always @(posedge clk)
	if(!rst_n)  
	   rx_init_cnt <= 'd0;
	else if(rx_init_gate) 
        rx_init_cnt <=  rx_init_cnt + 1'd1;
    else
        rx_init_cnt <= 1'b0;
	assign tx = tx_p | rx_init_gate;
	////
	
	
	
	///
	/*************************************************************************/
	//combine part
	assign bkt_ready_o = ((cnt1_gate == 1'b1) && (cnt0 >= 'd15)) ?  1'b1 : 1'b0;
    assign bkt_index_o = bk_index_base1000;
    assign bkt_data_o  = bk_data_base1000;
    
    
    endmodule