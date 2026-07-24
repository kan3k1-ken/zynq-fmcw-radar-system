`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Module Name: uart_excute
// Description: UART physical layer TX/RX engine
//              Logic strictly follows v403 bk_uart.v inline TX/RX engines
//////////////////////////////////////////////////////////////////////////////////

module uart_excute
(
    input  wire        clk,
    input  wire        rst_n,
    input  wire        DESR,
    input  wire [31:0] BAUD_DIV,
    input  wire [7:0]  tx_data,
    input  wire        tx_start,
    output wire        tx_busy,
    input  wire        loopback,
    input  wire        Rx,
    output wire        Tx,
    output wire        rx_done_pulse,
    output wire        rx_stop_ok,
    output wire [7:0]  rx_shift_reg
);


//=============================================================================
// TX Engine - timing-window approach (same as v403 bk_uart.v)
//=============================================================================
localparam TX_IDLE = 2'b00;
localparam TX_SEND = 2'b01;
localparam TX_DONE = 2'b10;

reg  [1:0]  tx_state;
reg  [31:0] tx_cnt;
reg         tx_busy_reg;

wire [31:0] BAUD_BYTE = BAUD_DIV * 10;

always @(posedge clk or negedge rst_n)
    if (!rst_n) begin
        tx_state    <= TX_IDLE;
        tx_cnt      <= 32'd0;
        tx_busy_reg <= 1'b0;
    end else begin
        case (tx_state)
            TX_IDLE: begin
                if (tx_start && DESR) begin
                    tx_state    <= TX_SEND;
                    tx_cnt      <= 32'd1;
                    tx_busy_reg <= 1'b1;
                end
            end
            TX_SEND: begin
                if (tx_cnt == BAUD_BYTE) begin
                    tx_state <= TX_DONE;
                    tx_cnt   <= 32'd0;
                end else begin
                    tx_cnt <= tx_cnt + 1'd1;
                end
            end
            TX_DONE: begin
                tx_busy_reg <= 1'b0;
                tx_state    <= TX_IDLE;
            end
            default: tx_state <= TX_IDLE;
        endcase
    end

assign tx_busy = tx_busy_reg;

wire tx_pre_bit   = (tx_cnt > 0)                  && (tx_cnt <= BAUD_DIV);
wire tx_bit0      = (tx_cnt > BAUD_DIV)           && (tx_cnt <= BAUD_DIV * 2);
wire tx_bit1      = (tx_cnt > BAUD_DIV * 2)       && (tx_cnt <= BAUD_DIV * 3);
wire tx_bit2      = (tx_cnt > BAUD_DIV * 3)       && (tx_cnt <= BAUD_DIV * 4);
wire tx_bit3      = (tx_cnt > BAUD_DIV * 4)       && (tx_cnt <= BAUD_DIV * 5);
wire tx_bit4      = (tx_cnt > BAUD_DIV * 5)       && (tx_cnt <= BAUD_DIV * 6);
wire tx_bit5      = (tx_cnt > BAUD_DIV * 6)       && (tx_cnt <= BAUD_DIV * 7);
wire tx_bit6      = (tx_cnt > BAUD_DIV * 7)       && (tx_cnt <= BAUD_DIV * 8);
wire tx_bit7      = (tx_cnt > BAUD_DIV * 8)       && (tx_cnt <= BAUD_DIV * 9);
wire tx_stop_bit  = (tx_cnt > BAUD_DIV * 9)       && (tx_cnt <= BAUD_BYTE);

reg txd_reg;
always @(posedge clk or negedge rst_n)
    if (!rst_n)
        txd_reg <= 1'b1;
    else if (tx_state == TX_SEND) begin
        if      (tx_pre_bit)  txd_reg <= 1'b0;
        else if (tx_bit0)     txd_reg <= tx_data[0];
        else if (tx_bit1)     txd_reg <= tx_data[1];
        else if (tx_bit2)     txd_reg <= tx_data[2];
        else if (tx_bit3)     txd_reg <= tx_data[3];
        else if (tx_bit4)     txd_reg <= tx_data[4];
        else if (tx_bit5)     txd_reg <= tx_data[5];
        else if (tx_bit6)     txd_reg <= tx_data[6];
        else if (tx_bit7)     txd_reg <= tx_data[7];
        else if (tx_stop_bit) txd_reg <= 1'b1;
        else                  txd_reg <= 1'b1;
    end else
        txd_reg <= 1'b1;

assign Tx = txd_reg;


//=============================================================================
// RX Engine - 4-state per-byte re-sync from START falling edge
// IDLE -> START (verify) -> DATA (8 bits) -> STOP (check, pulse)
// Continuous frame: STOP centre sampled, then rx_falling -> restart START
// No level-based triggering; no fixed baud timeout
// Sampling: BAUD_DIV*n + BAUD_HALF  (bit centre)
// Same as v403 bk_uart.v
//=============================================================================
localparam RX_IDLE  = 2'b00;
localparam RX_START = 2'b01;
localparam RX_DATA  = 2'b10;
localparam RX_STOP  = 2'b11;

wire rx_signal = loopback ? Tx : Rx;

reg rxd_d1, rxd_d2;
always @(posedge clk or negedge rst_n)
    if (!rst_n) begin
        rxd_d1 <= 1'b0;
        rxd_d2 <= 1'b0;
    end else begin
        rxd_d1 <= rx_signal;
        rxd_d2 <= rxd_d1;
    end

wire rx_falling  = !rxd_d1 && rxd_d2;

reg  [1:0]  rx_state;
reg  [31:0] rx_cnt;
reg  [7:0]  rx_shift_reg_int;
reg         rx_stop_ok_int;
reg         rx_done_pulse_int;

wire [31:0] BAUD_HALF = BAUD_DIV >> 1;

wire rx_sample_bit0  = (rx_cnt == BAUD_DIV     + BAUD_HALF);
wire rx_sample_bit1  = (rx_cnt == BAUD_DIV * 2 + BAUD_HALF);
wire rx_sample_bit2  = (rx_cnt == BAUD_DIV * 3 + BAUD_HALF);
wire rx_sample_bit3  = (rx_cnt == BAUD_DIV * 4 + BAUD_HALF);
wire rx_sample_bit4  = (rx_cnt == BAUD_DIV * 5 + BAUD_HALF);
wire rx_sample_bit5  = (rx_cnt == BAUD_DIV * 6 + BAUD_HALF);
wire rx_sample_bit6  = (rx_cnt == BAUD_DIV * 7 + BAUD_HALF);
wire rx_sample_bit7  = (rx_cnt == BAUD_DIV * 8 + BAUD_HALF);
wire rx_sample_stop  = (rx_cnt == BAUD_DIV * 9 + BAUD_HALF);

always @(posedge clk or negedge rst_n)
    if (!rst_n) begin
        rx_state           <= RX_IDLE;
        rx_cnt             <= 32'd0;
        rx_shift_reg_int   <= 8'd0;
        rx_stop_ok_int     <= 1'b1;
        rx_done_pulse_int  <= 1'b0;
    end else begin
        rx_done_pulse_int <= 1'b0;

        case (rx_state)

            RX_IDLE: begin
                rx_cnt <= 32'd0;
                if (rx_falling && DESR) begin
                    rx_state <= RX_START;
                    rx_cnt   <= 32'd1;
                end
            end

            RX_START: begin
                if (rx_cnt < BAUD_HALF) begin
                    rx_cnt <= rx_cnt + 1'd1;
                end else begin
                    if (rxd_d1 == 1'b0) begin
                        rx_state <= RX_DATA;
                        rx_cnt   <= rx_cnt + 1'd1;
                    end else begin
                        rx_state <= RX_IDLE;
                        rx_cnt   <= 32'd0;
                    end
                end
            end

            RX_DATA: begin
                rx_cnt <= rx_cnt + 1'd1;

                if      (rx_sample_bit0) rx_shift_reg_int[0] <= rxd_d1;
                else if (rx_sample_bit1) rx_shift_reg_int[1] <= rxd_d1;
                else if (rx_sample_bit2) rx_shift_reg_int[2] <= rxd_d1;
                else if (rx_sample_bit3) rx_shift_reg_int[3] <= rxd_d1;
                else if (rx_sample_bit4) rx_shift_reg_int[4] <= rxd_d1;
                else if (rx_sample_bit5) rx_shift_reg_int[5] <= rxd_d1;
                else if (rx_sample_bit6) rx_shift_reg_int[6] <= rxd_d1;
                else if (rx_sample_bit7) rx_shift_reg_int[7] <= rxd_d1;

                if (rx_sample_bit7) begin
                    rx_shift_reg_int[7] <= rxd_d1;
                    rx_state <= RX_STOP;
                end
            end

            RX_STOP: begin
                if (rx_cnt < BAUD_DIV * 9 + BAUD_HALF) begin
                    rx_cnt <= rx_cnt + 1'd1;
                end else if (rx_cnt == BAUD_DIV * 9 + BAUD_HALF) begin
                    rx_stop_ok_int    <= rxd_d1;
                    rx_done_pulse_int <= 1'b1;
                    rx_cnt            <= rx_cnt + 1'd1;
                end else if (rx_falling && DESR) begin
                    rx_state <= RX_START;
                    rx_cnt   <= 32'd1;
                end else begin
                    rx_cnt <= rx_cnt + 1'd1;
                end
            end

            default: rx_state <= RX_IDLE;

        endcase
    end

assign rx_shift_reg  = rx_shift_reg_int;
assign rx_stop_ok    = rx_stop_ok_int;
assign rx_done_pulse = rx_done_pulse_int;


endmodule