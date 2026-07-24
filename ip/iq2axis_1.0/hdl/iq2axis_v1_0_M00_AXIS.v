`timescale 1 ns / 1 ps

	module iq2axis_v1_0_M00_AXIS #
	(
		// Users to add parameters here

		// User parameters ends
		// Do not modify the parameters beyond this line

		// Width of S_AXIS address bus. The slave accepts the read and write addresses of width C_M_AXIS_TDATA_WIDTH.
		parameter integer C_M_AXIS_TDATA_WIDTH	= 32,
		// Start count is the number of clock cycles the master will wait before initiating/issuing any transaction.
		parameter integer C_M_START_COUNT	= 32
	)
	(
		// Users to add ports here

		// User ports ends
		// Do not modify the ports beyond this line

		// Global ports
		input wire  M_AXIS_ACLK,
		// 
		input wire  M_AXIS_ARESETN,
		// Master Stream Ports. TVALID indicates that the master is driving a valid transfer, A transfer takes place when both TVALID and TREADY are asserted. 
		output wire  M_AXIS_TVALID,
		// TDATA is the primary payload that is used to provide the data that is passing across the interface from the master.
		output wire [C_M_AXIS_TDATA_WIDTH-1 : 0] M_AXIS_TDATA,
		// TSTRB is the byte qualifier that indicates whether the content of the associated byte of TDATA is processed as a data byte or a position byte.
		output wire [(C_M_AXIS_TDATA_WIDTH/8)-1 : 0] M_AXIS_TSTRB,
		// TLAST indicates the boundary of a packet.
		output wire  M_AXIS_TLAST,
		// TREADY indicates that the slave can accept a transfer in the current cycle.
		input wire  M_AXIS_TREADY,
		
		// Users to add ports here
		// IQ data input from nco_gen (32-bit: I[15:0] + Q[15:0])
		input wire [C_M_AXIS_TDATA_WIDTH-1 : 0] iq_i
	);

	// FFT frame size (NFFT_MAX = 2048)
	localparam FFT_SIZE = 2048;
	
	// function called clogb2
	function integer clogb2 (input integer bit_depth);
	  begin
	    for(clogb2=0; bit_depth>0; clogb2=clogb2+1)
	      bit_depth = bit_depth >> 1;
	  end
	endfunction

	// WAIT_COUNT_BITS is the width of the wait counter.
	localparam integer WAIT_COUNT_BITS = clogb2(C_M_START_COUNT);

	// Wait counter
	reg [WAIT_COUNT_BITS-1 : 0] count;
	// Active flag: becomes 1 after start count delay
	reg active;
	// Sample counter for frame boundary (0 to FFT_SIZE-1)
	reg [$clog2(FFT_SIZE)-1 : 0] sample_cnt;

	// I/O Connections assignments
	assign M_AXIS_TVALID = active;
	assign M_AXIS_TDATA  = iq_i;
	assign M_AXIS_TLAST  = (sample_cnt == FFT_SIZE - 1);
	assign M_AXIS_TSTRB  = {(C_M_AXIS_TDATA_WIDTH/8){1'b1}};

	// Control state machine: wait for start count, then continuously stream
	always @(posedge M_AXIS_ACLK) begin
		if (!M_AXIS_ARESETN) begin
			count      <= 0;
			active     <= 1'b0;
			sample_cnt <= 0;
		end else begin
			if (!active) begin
				// Wait for C_M_START_COUNT cycles before starting
				if (count == C_M_START_COUNT - 1) begin
					active <= 1'b1;
					count  <= 0;
				end else begin
					count <= count + 1;
				end
			end else begin
				// Continuously stream data, increment sample counter on valid transfer
				if (M_AXIS_TREADY) begin
					if (sample_cnt == FFT_SIZE - 1)
						sample_cnt <= 0;
					else
						sample_cnt <= sample_cnt + 1;
				end
			end
		end
	end

	endmodule