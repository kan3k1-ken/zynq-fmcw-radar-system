`timescale 1 ns / 1 ps

module stream_padder_v1_0 #
(
    parameter integer C_S_AXIS_TDATA_WIDTH = 32,
    parameter integer C_M_AXIS_TDATA_WIDTH = 32,
    parameter integer DMA_BYTES  = 256,
    parameter integer TOTAL_BYTES = 8192
)
(
    input  wire s_axis_aclk,
    input  wire s_axis_aresetn,

    input  wire [C_S_AXIS_TDATA_WIDTH-1:0] s_axis_tdata,
    input  wire s_axis_tvalid,
    output wire s_axis_tready,
    input  wire s_axis_tlast,

    input  wire m_axis_aclk,
    input  wire m_axis_aresetn,

    output wire [C_M_AXIS_TDATA_WIDTH-1:0] m_axis_tdata,
    output wire m_axis_tvalid,
    input  wire m_axis_tready,
    output wire m_axis_tlast
);

    localparam DMA_BEATS   = DMA_BYTES  / (C_S_AXIS_TDATA_WIDTH / 8);
    localparam TOTAL_BEATS = TOTAL_BYTES / (C_M_AXIS_TDATA_WIDTH / 8);

    localparam IDLE      = 2'b00;
    localparam PASSTHRU  = 2'b01;
    localparam PAD       = 2'b10;

    reg [1:0] state;
    reg [31:0] beat_cnt;

    always @(posedge s_axis_aclk) begin
        if (!s_axis_aresetn) begin
            state <= IDLE;
            beat_cnt <= 0;
        end else begin
            case (state)
                IDLE: begin
                    beat_cnt <= 0;
                    if (s_axis_tvalid) begin
                        state <= PASSTHRU;
                    end
                end

                PASSTHRU: begin
                    if (s_axis_tvalid && s_axis_tready) begin
                        beat_cnt <= beat_cnt + 1;
                        if (s_axis_tlast || beat_cnt == DMA_BEATS - 1) begin
                            state <= PAD;
                            beat_cnt <= 0;
                        end
                    end
                end

                PAD: begin
                    if (m_axis_tvalid && m_axis_tready) begin
                        beat_cnt <= beat_cnt + 1;
                        if (beat_cnt == TOTAL_BEATS - DMA_BEATS - 1) begin
                            state <= IDLE;
                        end
                    end
                end

                default: state <= IDLE;
            endcase
        end
    end

    assign s_axis_tready = (state == PASSTHRU) && m_axis_tready;

    assign m_axis_tvalid = (state == PASSTHRU && s_axis_tvalid) ||
                           (state == PAD);

    assign m_axis_tdata = (state == PASSTHRU) ? s_axis_tdata : 0;

    assign m_axis_tlast = (state == PAD) && (beat_cnt == TOTAL_BEATS - DMA_BEATS - 1);

endmodule