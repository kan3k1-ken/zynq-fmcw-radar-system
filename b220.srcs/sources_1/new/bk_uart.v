`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Module Name: bk_uart
// Description: BK-UART wrapper with 256-byte RX FIFO, TX hold buffer, frame/overflow detect
//              TX/RX physical layer delegated to uart_excute (logic strictly follows v403)
//////////////////////////////////////////////////////////////////////////////////
   /*********************************bk_uart bk_base:1000*********************************************/
     // ascription: shell
     // type: RTL
     // function: PL UART TX/RX with 256B RX FIFO, echo/loopback, radar data reception
     // update:  2026.07.10
     //  0          bit0            DESR                     bit0=ENABLE, bit1=LOOPBACK
     //  1          bit31-0         BAUD_DIV                 clk_freq / baud_rate
     //  2          bit7-0          TX_DATA                  transmit byte (write auto-triggers)
     //  3          reserved                                (was single-byte RX_DATA)
     //  4          bit7-0          RX_FIFO                  mode1: read+pop oldest FIFO byte
    /*** bk_status ***/
    // mode0: bit[20:13]=rx_count, bit[12]=frame_err, bit[11]=rx_overflow,
    //        bit[10]=tx_busy, bit[9]=tx_pending, bit[8]=rx_valid, bit[7:0]=rx_data
    // mode1: offset 4 -> read+pop oldest FIFO byte


     module bk_uart #(
        parameter BKP_BASE_index = 1000,
        parameter sys_clk_freq   = 100_000_000
    )
    (
        input  wire        clk,
        input  wire        rst_n,
        input  wire        bkt_ready_i,
        input  wire [31:0] bkt_index_i,
        input  wire [31:0] bkt_data_i,
        output wire [31:0] bk_status,
        output wire        Tx,
        input  wire        Rx
    );


/********* gen code start*********/
reg BKP_Ready_z1, BKP_Ready_z2;
always @(posedge clk)
    if (!rst_n) begin
        BKP_Ready_z1 <= 1'b0;
        BKP_Ready_z2 <= 1'b0;
    end else begin
        BKP_Ready_z1 <= bkt_ready_i;
        BKP_Ready_z2 <= BKP_Ready_z1;
    end

wire BKP_Ready;
assign BKP_Ready = BKP_Ready_z1 & ~BKP_Ready_z2;

wire [31:0] bk_data_index;
assign bk_data_index = bkt_index_i;

wire [31:0] bk_data;
assign bk_data = bkt_data_i;

reg [2:0] bk_mode;
always @(posedge clk)
    if (!rst_n)
        bk_mode <= 3'd0;
    else if (BKP_Ready && bk_data_index == 0)
        bk_mode <= bk_data[2:0];

// DESR register (offset 0): bit0=ENABLE, bit1=LOOPBACK
reg DESR;
reg loopback;

always @(posedge clk)
    if (!rst_n) begin
        DESR     <= 1'b0;
        loopback <= 1'b0;
    end else if (BKP_Ready && bk_data_index == BKP_BASE_index) begin
        DESR     <= bk_data[0];
        loopback <= bk_data[1];
    end else begin
        DESR     <= DESR;
        loopback <= loopback;
    end

// BAUD_DIV register (offset 1), default 868 = 115200 @ 100MHz
reg [31:0] BAUD_DIV;
always @(posedge clk)
    if (!rst_n)
        BAUD_DIV <= 32'd868;
    else if (BKP_Ready && bk_data_index == BKP_BASE_index + 1)
        BAUD_DIV <= bk_data;
    else
        BAUD_DIV <= BAUD_DIV;

// TX_DATA register (offset 2) - write triggers TX, or holds if TX is busy
reg [7:0] tx_data_reg;
reg       tx_pending;
reg       tx_start;
always @(posedge clk)
    if (!rst_n) begin
        tx_data_reg <= 8'd0;
        tx_pending  <= 1'b0;
        tx_start    <= 1'b0;
    end else if (BKP_Ready && bk_data_index == BKP_BASE_index + 2 && DESR) begin
        tx_data_reg <= bk_data[7:0];
        if (tx_busy) begin
            tx_pending <= 1'b1;   // hold until current TX finishes
            tx_start   <= 1'b0;
        end else begin
            tx_pending <= 1'b0;
            tx_start   <= 1'b1;   // trigger immediately
        end
    end else if (!tx_busy && tx_pending) begin
        tx_start   <= 1'b1;       // delayed trigger
        tx_pending <= 1'b0;
    end else begin
        tx_start   <= 1'b0;
    end

//=============================================================================
// RX FIFO: 256 bytes deep, shift register
// Push LSB: {rx_fifo[2039:0], rx_shift_reg}  -- left shift by 8 bits, insert new byte at LSB
// Pop:       rx_count decrement only           -- oldest_pos shifts to [ (count-1)*8 +: 8 ]
// Read (FIFO): rx_fifo[oldest_pos -: 8]        -- oldest_pos = (rx_count*8)-1
//=============================================================================
reg [2047:0] rx_fifo;
reg  [7:0]   rx_count;
reg          rx_overflow;
reg          frame_err;

wire         tx_busy;
wire         rx_done_pulse;
wire         rx_stop_ok;
wire [7:0]   rx_shift_reg;

wire [7:0] rx_data_wire = rx_fifo[7:0];  // newest byte at LSB (mode0 real-time)

// Dynamic read of oldest: position (rx_count*8-1) down to (rx_count*8-8)
wire [10:0] oldest_pos  = {rx_count, 3'd0} - 1'd1;
wire [7:0] rx_oldest_byte = (rx_count == 8'd0) ? 8'd0 : rx_fifo[oldest_pos -: 8];
wire rx_valid_wire = (rx_count > 0);

always @(posedge clk or negedge rst_n)
    if (!rst_n) begin
        rx_fifo     <= 2048'd0;
        rx_count    <= 8'd0;
        rx_overflow <= 1'b0;
        frame_err   <= 1'b0;
    end else begin
        if (BKP_Ready && bk_data_index == BKP_BASE_index + 4 && bk_mode == 3'd1 && rx_count > 0) begin
            if (rx_done_pulse) begin
                // Both pop and push on same cycle: shift FIFO, count unchanged
                rx_fifo  <= {rx_fifo[2039:0], rx_shift_reg};
                frame_err <= ~rx_stop_ok;
            end else begin
                // Pop only: decrement count
                rx_count <= rx_count - 1'd1;
            end
        end else if (rx_done_pulse) begin  
            if (&rx_count) begin
                rx_overflow <= 1'b1;
            end else begin
                // Push only: shift left, insert new byte at LSB
                rx_fifo  <= {rx_fifo[2039:0], rx_shift_reg};
                rx_count <= rx_count + 1'd1;
            end
            frame_err <= ~rx_stop_ok;
        end
    end

// bk_status assembly
// mode1: rx_pop_latch holds the byte returned by the most recent FIFO pop.
// This latch is registered on BKP_Ready and stays stable between BK transactions,
// so get_bk_status() always reads the correct popped byte regardless of AXI timing.
reg [7:0] rx_pop_latch;

// Debug: total RX bytes received by uart_excute (rx_done_pulse count)
reg [31:0] rx_total_count;
always @(posedge clk or negedge rst_n)
    if (!rst_n)
        rx_total_count <= 32'd0;
    else if (rx_done_pulse)
        rx_total_count <= rx_total_count + 1'd1;

always @(posedge clk or negedge rst_n)
    if (!rst_n)
        rx_pop_latch <= 8'd0;
    else if (BKP_Ready && bk_data_index == BKP_BASE_index + 4 && bk_mode == 3'd1 && rx_count > 0)
        rx_pop_latch <= rx_oldest_byte;  // latch the oldest byte before pop (MSB)

reg [31:0] bk_status_p;
always @(posedge clk)
    if (!rst_n)
        bk_status_p <= 32'd0;
    else
        case (bk_mode)
            3'd0: bk_status_p <= {11'd0, rx_count[7:0],
                                   frame_err, rx_overflow,
                                   tx_busy, tx_pending, rx_valid_wire, rx_data_wire};
            // bit[20:13]=rx_count, bit[12]=frame_err, bit[11]=rx_overflow,
            // bit[10]=tx_busy, bit[9]=tx_pending, bit[8]=rx_valid, bit[7:0]=rx_data
            3'd1: bk_status_p <= {24'd0, rx_pop_latch};
            3'd2: bk_status_p <= rx_total_count;
            default: bk_status_p <= 32'd0;
        endcase

assign bk_status = bk_status_p;


//=============================================================================
// uart_excute instantiation - TX/RX physical layer
// Logic strictly follows v403 bk_uart.v inline TX/RX engines
//=============================================================================
uart_excute u_uart_excute (
    .clk            (clk),
    .rst_n          (rst_n),
    .DESR           (DESR),
    .BAUD_DIV       (BAUD_DIV),
    .tx_data        (tx_data_reg),
    .tx_start       (tx_start),
    .tx_busy        (tx_busy),
    .loopback       (loopback),
    .Rx             (Rx),
    .Tx             (Tx),
    .rx_done_pulse  (rx_done_pulse),
    .rx_stop_ok     (rx_stop_ok),
    .rx_shift_reg   (rx_shift_reg)
);

 
endmodule