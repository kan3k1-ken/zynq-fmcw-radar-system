`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Company: 
// Engineer: 
// 
// Create Date: 2026/07/15 09:29:17
// Design Name: 
// Module Name: fake_pps
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


module fake_pps(
    input wire clk,
    input wire rst_n,
    output reg pps
    );

parameter PPS_INTERVAL = 100000000;

reg [31:0] cnt;

always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        cnt <= 32'd0;
        pps <= 1'b0;
    end
    else if (cnt == PPS_INTERVAL - 1) begin
        cnt <= 32'd0;
        pps <= 1'b1;
    end
    else begin
        cnt <= cnt + 1'b1;
        pps <= 1'b0;
    end
end

endmodule
