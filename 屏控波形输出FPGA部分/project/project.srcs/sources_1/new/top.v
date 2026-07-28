`timescale 1ns / 1ps

module top(
    input        Clk,
    input        Rst,
    input        uart_rx,
    input        Key_state,
    input  [11:0] ad1_in,
    output       ad1_clk,

    output reg [13:0] DataA,
    output       ClkA,
    output       WRTA,

    output reg [13:0] DataB,
    output       ClkB,
    output       WRTB,

    output reg   Led_1,
    output reg   Led_2,
    output reg   Led_3,
    output reg   Led_uart
);

localparam [7:0]  FRAME_SOF      = 8'hA5;
localparam [7:0]  FRAME_CMD      = 8'h41;
localparam [7:0]  FRAME_DATA_LEN = 8'd28;
localparam [7:0]  FRAME_EOF      = 8'h5A;
localparam [7:0]  PROTO_VERSION  = 8'd1;

localparam [1:0]  WAVE_SINE      = 2'd0;
localparam [1:0]  WAVE_SQUARE    = 2'd1;
localparam [1:0]  WAVE_TRIANGLE  = 2'd2;

localparam [13:0] DAC_MID        = 14'd8192;
localparam [13:0] DAC_MIN        = 14'd0;
localparam [13:0] DAC_MAX        = 14'd16383;
localparam [13:0] AMP_Q13_FULL   = 14'd8192;

localparam [2:0]  ST_SOF         = 3'd0;
localparam [2:0]  ST_CMD         = 3'd1;
localparam [2:0]  ST_LEN         = 3'd2;
localparam [2:0]  ST_DATA        = 3'd3;
localparam [2:0]  ST_CHECK       = 3'd4;
localparam [2:0]  ST_EOF         = 3'd5;

wire clk_125m;
wire mmcm_locked;

MMCM inst_MMCM(
    .clk_out1(clk_125m),
    .clk_out2(),
    .clk_out3(),
    .resetn(Rst),
    .locked(mmcm_locked),
    .clk_in1(Clk)
);

assign ClkA = clk_125m;
assign WRTA = clk_125m;
assign ClkB = clk_125m;
assign WRTB = clk_125m;
assign ad1_clk = 1'b0;

reg [1:0] uart_rx_sync;

always @(posedge clk_125m or negedge Rst) begin
    if (!Rst) begin
        uart_rx_sync <= 2'b11;
    end else begin
        uart_rx_sync <= {uart_rx_sync[0], uart_rx};
    end
end

wire [7:0] rx_byte;
wire       rx_valid;

uart_rx #(
    .DATA_WIDTH(8)
) inst_uart_rx (
    .clk(clk_125m),
    .rst(~Rst),
    .m_axis_tdata(rx_byte),
    .m_axis_tvalid(rx_valid),
    .m_axis_tready(1'b1),
    .rxd(uart_rx_sync[1]),
    .busy(),
    .overrun_error(),
    .frame_error(),
    .prescale(16'd136)
);

reg [2:0] parser_state;
reg [4:0] data_index;
reg [7:0] checksum;
reg [7:0] frame_data [0:27];

reg [1:0]  flags;
reg [1:0]  wave_a;
reg [1:0]  wave_b;
reg [31:0] fword_a;
reg [31:0] fword_b;
reg [13:0] amp_a_q13;
reg [13:0] amp_b_q13;
reg [31:0] duty_a_q32;
reg [31:0] duty_b_q32;
reg [31:0] phase_b_q32;
reg        config_seen;
reg        load_config;

function [13:0] clamp_amp_q13;
    input [15:0] value;
    begin
        if (value > {2'b00, AMP_Q13_FULL}) begin
            clamp_amp_q13 = AMP_Q13_FULL;
        end else begin
            clamp_amp_q13 = value[13:0];
        end
    end
endfunction

always @(posedge clk_125m or negedge Rst) begin
    if (!Rst) begin
        parser_state <= ST_SOF;
        data_index   <= 5'd0;
        checksum     <= 8'd0;
        flags        <= 2'b00;
        wave_a       <= WAVE_SINE;
        wave_b       <= WAVE_SINE;
        fword_a      <= 32'd0;
        fword_b      <= 32'd0;
        amp_a_q13    <= 14'd0;
        amp_b_q13    <= 14'd0;
        duty_a_q32   <= 32'h80000000;
        duty_b_q32   <= 32'h80000000;
        phase_b_q32  <= 32'd0;
        config_seen  <= 1'b0;
        load_config  <= 1'b0;
    end else begin
        load_config <= 1'b0;

        if (rx_valid) begin
            case (parser_state)
                ST_SOF: begin
                    if (rx_byte == FRAME_SOF) begin
                        checksum <= FRAME_SOF;
                        parser_state <= ST_CMD;
                    end
                end

                ST_CMD: begin
                    if (rx_byte == FRAME_CMD) begin
                        checksum <= checksum + rx_byte;
                        parser_state <= ST_LEN;
                    end else if (rx_byte == FRAME_SOF) begin
                        checksum <= FRAME_SOF;
                        parser_state <= ST_CMD;
                    end else begin
                        parser_state <= ST_SOF;
                    end
                end

                ST_LEN: begin
                    if (rx_byte == FRAME_DATA_LEN) begin
                        checksum <= checksum + rx_byte;
                        data_index <= 5'd0;
                        parser_state <= ST_DATA;
                    end else if (rx_byte == FRAME_SOF) begin
                        checksum <= FRAME_SOF;
                        parser_state <= ST_CMD;
                    end else begin
                        parser_state <= ST_SOF;
                    end
                end

                ST_DATA: begin
                    frame_data[data_index] <= rx_byte;
                    checksum <= checksum + rx_byte;
                    if (data_index == 5'd27) begin
                        parser_state <= ST_CHECK;
                    end else begin
                        data_index <= data_index + 5'd1;
                    end
                end

                ST_CHECK: begin
                    if (rx_byte == checksum) begin
                        parser_state <= ST_EOF;
                    end else if (rx_byte == FRAME_SOF) begin
                        checksum <= FRAME_SOF;
                        parser_state <= ST_CMD;
                    end else begin
                        parser_state <= ST_SOF;
                    end
                end

                ST_EOF: begin
                    if ((rx_byte == FRAME_EOF) &&
                        (frame_data[0] == PROTO_VERSION) &&
                        ((frame_data[1] & 8'hFC) == 8'h00) &&
                        (frame_data[2] <= 8'd2) &&
                        (frame_data[13] <= 8'd2)) begin

                        flags       <= frame_data[1][1:0];
                        wave_a      <= frame_data[2][1:0];
                        fword_a     <= {frame_data[6], frame_data[5], frame_data[4], frame_data[3]};
                        amp_a_q13   <= clamp_amp_q13({frame_data[8], frame_data[7]});
                        duty_a_q32  <= {frame_data[12], frame_data[11], frame_data[10], frame_data[9]};

                        wave_b      <= frame_data[13][1:0];
                        fword_b     <= {frame_data[17], frame_data[16], frame_data[15], frame_data[14]};
                        amp_b_q13   <= clamp_amp_q13({frame_data[19], frame_data[18]});
                        duty_b_q32  <= {frame_data[23], frame_data[22], frame_data[21], frame_data[20]};
                        phase_b_q32 <= {frame_data[27], frame_data[26], frame_data[25], frame_data[24]};

                        config_seen <= 1'b1;
                        load_config <= 1'b1;
                    end
                    parser_state <= ST_SOF;
                end

                default: begin
                    parser_state <= ST_SOF;
                end
            endcase
        end
    end
end

wire enable_a = flags[0];
wire enable_b = flags[1];

reg [31:0] phase_acc_a;
reg [31:0] phase_acc_b;

always @(posedge clk_125m or negedge Rst) begin
    if (!Rst) begin
        phase_acc_a <= 32'd0;
        phase_acc_b <= 32'd0;
    end else if (load_config) begin
        phase_acc_a <= 32'd0;
        phase_acc_b <= 32'd0;
    end else begin
        if (enable_a) begin
            phase_acc_a <= phase_acc_a + fword_a;
        end else begin
            phase_acc_a <= 32'd0;
        end

        if (enable_b) begin
            phase_acc_b <= phase_acc_b + fword_b;
        end else begin
            phase_acc_b <= 32'd0;
        end
    end
end

wire [31:0] phase_b_eff = phase_acc_b - phase_b_q32;
wire [11:0] rom_addr_a  = phase_acc_a[31:20];
wire [11:0] rom_addr_b  = phase_b_eff[31:20];

wire [13:0] sine_a;
wire [13:0] sine_b;
wire [13:0] tri_a;
wire [13:0] tri_b;

rom_sine inst_rom_sine_a (
    .clka(clk_125m),
    .addra(rom_addr_a),
    .douta(sine_a)
);

rom_sine inst_rom_sine_b (
    .clka(clk_125m),
    .addra(rom_addr_b),
    .douta(sine_b)
);

rom_triangular inst_rom_tri_a (
    .clka(clk_125m),
    .addra(rom_addr_a),
    .douta(tri_a)
);

rom_triangular inst_rom_tri_b (
    .clka(clk_125m),
    .addra(rom_addr_b),
    .douta(tri_b)
);

wire [13:0] square_a = (phase_acc_a < duty_a_q32) ? DAC_MAX : DAC_MIN;
wire [13:0] square_b = (phase_b_eff < duty_b_q32) ? DAC_MAX : DAC_MIN;

wire [13:0] raw_a =
    (wave_a == WAVE_SQUARE)   ? square_a :
    (wave_a == WAVE_TRIANGLE) ? tri_a :
                                sine_a;

wire [13:0] raw_b =
    (wave_b == WAVE_SQUARE)   ? square_b :
    (wave_b == WAVE_TRIANGLE) ? tri_b :
                                sine_b;

function [13:0] scale_sample;
    input [13:0] sample;
    input [13:0] amp_q13;
    reg signed [15:0] centered;
    reg signed [15:0] amp_signed;
    reg signed [31:0] mixed;
    reg signed [31:0] scaled;
    begin
        centered = $signed({2'b00, sample}) - 16'sd8192;
        amp_signed = $signed({2'b00, amp_q13});
        mixed = centered * amp_signed;
        scaled = (mixed >>> 13) + 32'sd8192;

        if (scaled < 32'sd0) begin
            scale_sample = DAC_MIN;
        end else if (scaled > 32'sd16383) begin
            scale_sample = DAC_MAX;
        end else begin
            scale_sample = scaled[13:0];
        end
    end
endfunction

always @(posedge clk_125m or negedge Rst) begin
    if (!Rst) begin
        DataA <= DAC_MID;
        DataB <= DAC_MID;
    end else begin
        DataA <= enable_a ? scale_sample(raw_a, amp_a_q13) : DAC_MID;
        DataB <= enable_b ? scale_sample(raw_b, amp_b_q13) : DAC_MID;
    end
end

always @(posedge clk_125m or negedge Rst) begin
    if (!Rst) begin
        Led_1    <= 1'b1;
        Led_2    <= 1'b1;
        Led_3    <= 1'b1;
        Led_uart <= 1'b1;
    end else begin
        Led_1 <= ~enable_a;
        Led_2 <= ~enable_b;
        Led_3 <= ~(config_seen & mmcm_locked);
        if (rx_valid) begin
            Led_uart <= ~Led_uart;
        end
    end
end

endmodule
