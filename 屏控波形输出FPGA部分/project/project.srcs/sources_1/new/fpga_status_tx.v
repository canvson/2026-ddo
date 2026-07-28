`timescale 1ns / 1ps
/*
 * FPGA -> STM32 status frame sender for 2025 G problem.
 *
 * Frame: A5 MSG LEN DATA CHECK 5A
 * CHECK = (A5 + MSG + LEN + sum(DATA)) & 8'hFF
 *
 * Connect tx_data/tx_valid/tx_ready to the existing uart module AXI input:
 *   .s_axis_tdata(tx_data), .s_axis_tvalid(tx_valid), .s_axis_tready(tx_ready)
 */
module fpga_status_tx(
    input  wire        clk,
    input  wire        rst_n,

    input  wire        ack_valid,
    input  wire [7:0]  ack_cmd,
    input  wire [7:0]  ack_status,

    input  wire        busy_valid,
    input  wire [7:0]  busy_op,
    input  wire [7:0]  busy_progress,

    input  wire        progress_valid,
    input  wire [7:0]  progress_percent,
    input  wire [7:0]  progress_step,

    input  wire        result_valid,
    input  wire [7:0]  result_filter_type,
    input  wire [15:0] result_r_ohm,
    input  wire [15:0] result_l_uH,
    input  wire [31:0] result_c_pF,

    input  wire        measure_valid,
    input  wire [31:0] measure_freq_hz,
    input  wire [15:0] measure_input_mVpp,
    input  wire [15:0] measure_output_mVpp,

    input  wire        error_valid,
    input  wire [15:0] error_code,
    input  wire [7:0]  error_source,

    output reg  [7:0]  tx_data,
    output reg         tx_valid,
    input  wire        tx_ready,
    output wire        busy
);

localparam [7:0] SOF = 8'hA5;
localparam [7:0] EOF = 8'h5A;

localparam [7:0] MSG_ACK            = 8'h80;
localparam [7:0] MSG_BUSY           = 8'h81;
localparam [7:0] MSG_LEARN_PROGRESS = 8'h82;
localparam [7:0] MSG_LEARN_RESULT   = 8'h83;
localparam [7:0] MSG_MEASURE        = 8'h84;
localparam [7:0] MSG_ERROR          = 8'h8F;

localparam [0:0] ST_IDLE = 1'b0;
localparam [0:0] ST_SEND = 1'b1;

reg        state;
reg [7:0]  frame [0:15];
reg [4:0]  frame_len;
reg [4:0]  frame_index;
reg [7:0]  sum;

assign busy = (state != ST_IDLE);

always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
        state <= ST_IDLE;
        frame_len <= 5'd0;
        frame_index <= 5'd0;
        tx_data <= 8'd0;
        tx_valid <= 1'b0;
        sum <= 8'd0;
    end else begin
        if (state == ST_SEND) begin
            if (tx_valid && tx_ready) begin
                if (frame_index + 1'b1 < frame_len) begin
                    frame_index <= frame_index + 1'b1;
                    tx_data <= frame[frame_index + 1'b1];
                    tx_valid <= 1'b1;
                end else begin
                    tx_valid <= 1'b0;
                    state <= ST_IDLE;
                end
            end
        end else begin
            tx_valid <= 1'b0;
            frame_index <= 5'd0;

            if (ack_valid) begin
                sum = SOF + MSG_ACK + 8'd2 + ack_cmd + ack_status;
                frame[0] <= SOF;
                frame[1] <= MSG_ACK;
                frame[2] <= 8'd2;
                frame[3] <= ack_cmd;
                frame[4] <= ack_status;
                frame[5] <= sum;
                frame[6] <= EOF;
                frame_len <= 5'd7;
                tx_data <= SOF;
                tx_valid <= 1'b1;
                state <= ST_SEND;
            end else if (error_valid) begin
                sum = SOF + MSG_ERROR + 8'd3 + error_code[7:0] + error_code[15:8] + error_source;
                frame[0] <= SOF;
                frame[1] <= MSG_ERROR;
                frame[2] <= 8'd3;
                frame[3] <= error_code[7:0];
                frame[4] <= error_code[15:8];
                frame[5] <= error_source;
                frame[6] <= sum;
                frame[7] <= EOF;
                frame_len <= 5'd8;
                tx_data <= SOF;
                tx_valid <= 1'b1;
                state <= ST_SEND;
            end else if (progress_valid) begin
                sum = SOF + MSG_LEARN_PROGRESS + 8'd2 + progress_percent + progress_step;
                frame[0] <= SOF;
                frame[1] <= MSG_LEARN_PROGRESS;
                frame[2] <= 8'd2;
                frame[3] <= progress_percent;
                frame[4] <= progress_step;
                frame[5] <= sum;
                frame[6] <= EOF;
                frame_len <= 5'd7;
                tx_data <= SOF;
                tx_valid <= 1'b1;
                state <= ST_SEND;
            end else if (result_valid) begin
                sum = SOF + MSG_LEARN_RESULT + 8'd9 +
                      result_filter_type +
                      result_r_ohm[7:0] + result_r_ohm[15:8] +
                      result_l_uH[7:0] + result_l_uH[15:8] +
                      result_c_pF[7:0] + result_c_pF[15:8] +
                      result_c_pF[23:16] + result_c_pF[31:24];
                frame[0] <= SOF;
                frame[1] <= MSG_LEARN_RESULT;
                frame[2] <= 8'd9;
                frame[3] <= result_filter_type;
                frame[4] <= result_r_ohm[7:0];
                frame[5] <= result_r_ohm[15:8];
                frame[6] <= result_l_uH[7:0];
                frame[7] <= result_l_uH[15:8];
                frame[8] <= result_c_pF[7:0];
                frame[9] <= result_c_pF[15:8];
                frame[10] <= result_c_pF[23:16];
                frame[11] <= result_c_pF[31:24];
                frame[12] <= sum;
                frame[13] <= EOF;
                frame_len <= 5'd14;
                tx_data <= SOF;
                tx_valid <= 1'b1;
                state <= ST_SEND;
            end else if (measure_valid) begin
                sum = SOF + MSG_MEASURE + 8'd8 +
                      measure_freq_hz[7:0] + measure_freq_hz[15:8] +
                      measure_freq_hz[23:16] + measure_freq_hz[31:24] +
                      measure_input_mVpp[7:0] + measure_input_mVpp[15:8] +
                      measure_output_mVpp[7:0] + measure_output_mVpp[15:8];
                frame[0] <= SOF;
                frame[1] <= MSG_MEASURE;
                frame[2] <= 8'd8;
                frame[3] <= measure_freq_hz[7:0];
                frame[4] <= measure_freq_hz[15:8];
                frame[5] <= measure_freq_hz[23:16];
                frame[6] <= measure_freq_hz[31:24];
                frame[7] <= measure_input_mVpp[7:0];
                frame[8] <= measure_input_mVpp[15:8];
                frame[9] <= measure_output_mVpp[7:0];
                frame[10] <= measure_output_mVpp[15:8];
                frame[11] <= sum;
                frame[12] <= EOF;
                frame_len <= 5'd13;
                tx_data <= SOF;
                tx_valid <= 1'b1;
                state <= ST_SEND;
            end else if (busy_valid) begin
                sum = SOF + MSG_BUSY + 8'd2 + busy_op + busy_progress;
                frame[0] <= SOF;
                frame[1] <= MSG_BUSY;
                frame[2] <= 8'd2;
                frame[3] <= busy_op;
                frame[4] <= busy_progress;
                frame[5] <= sum;
                frame[6] <= EOF;
                frame_len <= 5'd7;
                tx_data <= SOF;
                tx_valid <= 1'b1;
                state <= ST_SEND;
            end
        end
    end
end

endmodule
