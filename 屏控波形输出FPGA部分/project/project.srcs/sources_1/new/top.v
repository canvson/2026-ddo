`timescale 1ns / 1ps
module top(
    input        Clk,
    input        Rst,
	input        uart_rx,
    input        Key_state,
	input  [11:0]ad1_in,
	output 		 ad1_clk,

    output [13:0]DataA,
    output       ClkA,
    output       WRTA,

    output [13:0]DataB,
    output       ClkB,
    output       WRTB,
    
	output reg   Led_1,
	output reg   Led_2,
	output reg   Led_3,
	output reg   Led_uart
    
);
/////////////////////////////////PLL//////////////////////////////////////
wire CLK125M;//DA输出时钟
wire CLK50M;//暂时没有用到
wire CLK8M;//待分频时�?
wire CLK4M;//分频结，用于AD采集
wire locked;
divider u_clk_divider (
    .clk_in      (CLK8M),     
    .rst_n       (Rst),       
    .div_ratio   (16'd2), //二分�?(8M->4M)
    .duty_cycle  (16'd1),   
    .clk_out     (CLK4M)        
);
MMCM inst_MMCM(
        .clk_out1(CLK125M),
        .clk_out2(CLK50M),
		.clk_out3(CLK8M),
        .resetn(Rst), 
        .locked(locked),
        .clk_in1(Clk)
);
/////////////////////////////////AD采集//////////////////////////////////////
wire [11:0]ad_ch1;
assign ad1_clk=CLK4M;//硬件接口
adc inst_adc(
    .ad_clk(CLK4M),////////////？？？？？？�?//////
    .Rst(Rst),
    .ad1_in(ad1_in),
    .ad_ch1(ad_ch1)
);
//输入为偏移码，无符号数，�?高位取反即可,//要被拿去做IIR滤波
//ad_ch1_toIIR={~ad_ch1[11],ad_ch1[10:0]};
/////////////////////////////////状�?�机//////////////////////////////////////
parameter Basic_two    =3'd0;//基本�???2
parameter Basic_three  =3'd1;//基本�???3
parameter Basic_four   =3'd2;//基本�???3
parameter Develop_one  =3'd3;//发挥�???1
parameter Develop_two  =3'd4;//发挥�???2
reg [2:0] state;
reg [2:0] key_cnt;
wire Key_state_valid;
key_filter inst_Key_state(//按键消抖
    .clk(CLK125M), 
    .rst(~Rst), 
    .button_in(Key_state),
    .button_posedge(Key_state_valid),
    .button_negedge(),
    .button_out()   
);  
always @(posedge CLK125M or negedge Rst) begin//按键计数
    if(!Rst)
        key_cnt<=3'd0;
    else if(Key_state_valid)begin
        if(key_cnt>3'd4)
            key_cnt<=3'd0;
        else key_cnt<=key_cnt+1;
    end
        
end
always @(posedge CLK125M or negedge Rst) begin//状�?��?�择
    if(!Rst)
        state<=Basic_two;
    else begin
        case(key_cnt)
        3'd0:state<=Basic_two;
        3'd1:state<=Basic_three;
        3'd2:state<=Basic_four;
        3'd3:state<=Develop_one;
        3'd4:state<=Develop_two;
        default:state<=Basic_two;
        endcase
    end
end
/////////////////////////////////状�?�指示灯//////////////////////////////////////
always @(posedge CLK125M or negedge Rst) begin
	if(!Rst)begin
		Led_1<=1;
		Led_2<=1;
		Led_3<=1;
	end
	else begin
		case(state)
		Basic_two:		begin  Led_1<=1;Led_2<=1;Led_3<=1;  end//全灭
		Basic_three:    begin  Led_1<=0;Led_2<=1;Led_3<=1;  end//1�???
		Basic_four:		begin  Led_1<=1;Led_2<=0;Led_3<=1;  end//2�???
		Develop_one:	begin  Led_1<=1;Led_2<=1;Led_3<=0;  end//3�???
		Develop_two:	begin  Led_1<=0;Led_2<=0;Led_3<=0;  end//全亮
		default:		begin  Led_1<=1;Led_2<=1;Led_3<=1;  end//全灭
		endcase
	end 
end
/////////////////////////////////UART//////////////////////////////////////
wire uart_rxd_sync;
sync_signal #(
	.WIDTH(1),
	.N(2)
)
sync_signal_inst (
	.clk(CLK125M),
	.in({uart_rx}),
	.out({uart_rxd_sync})
);

	wire [7:0] uart_rx_axis_tdata;
	wire       uart_rx_axis_tvalid;

	uart uart0 (
		.clk(CLK125M),
		.rst(~Rst),
		// AXI input
		.s_axis_tdata(8'b0),
		.s_axis_tvalid(1'b0),
		.s_axis_tready(),
		// AXI output
		.m_axis_tdata(uart_rx_axis_tdata),
		.m_axis_tvalid(uart_rx_axis_tvalid),
		.m_axis_tready(1'b1),
		// uart
		.rxd(uart_rxd_sync),
		.txd(),
		// status
		.tx_busy(),
		.rx_busy(),
		.rx_overrun_error(),
		.rx_frame_error(),
		// configuration
		.prescale(16'd136/* clk/(baut*8) */) //125MHz, 115200bps
	);
    reg [7:0] Rx_data [0:9];//可能不够
	always @(posedge CLK125M or negedge Rst) begin
		if(!Rst) begin
			Rx_data[0] <= 0;//自己加的帧头
			Rx_data[1] <= 0;
			Rx_data[2] <= 0;
			Rx_data[3] <= 0;
			Rx_data[4] <= 0;
			Rx_data[5] <= 0;
			Rx_data[6] <= 0;
			Rx_data[7] <= 0;
			Rx_data[8] <= 0;
			Rx_data[9] <= 0;
			Led_uart<=1;
		end
		else begin
			if(uart_rx_axis_tvalid) begin
				Rx_data[0] <= uart_rx_axis_tdata;
				Rx_data[1] <= Rx_data[0];
				Rx_data[2] <= Rx_data[1];
				Rx_data[3] <= Rx_data[2];
				Rx_data[4] <= Rx_data[3];
				Rx_data[5] <= Rx_data[4];
				Rx_data[6] <= Rx_data[5];
				Rx_data[7] <= Rx_data[6];
				Rx_data[8] <= Rx_data[7];
				Rx_data[9] <= Rx_data[8];
				Led_uart<=~Led_uart;//LED指示
			end
		end
	end

/////////////////////////////////DDS//////////////////////////////////////
assign ClkA = CLK125M;
assign WRTA = CLK125M;
wire    [13:0]DDS_OUT;
wire Develop_two_flag;
DDS_AD9767 inst_DDS(
    .CLK125M(CLK125M),
    .Reset_n(Rst),
    .DataA(DDS_OUT),
    .Rx_Data_0(Rx_data[0]),
    .Rx_Data_1(Rx_data[1]),
    .Rx_Data_2(Rx_data[2]),
    .Rx_Data_3(Rx_data[3]),
    .Rx_Data_4(Rx_data[4]),
    .Rx_Data_5(Rx_data[5]),
    .Rx_Data_6(Rx_data[6]),
    .Rx_Data_7(Rx_data[7]),
    .Rx_Data_8(Rx_data[8]),
    .Rx_Data_9(Rx_data[9]),
	.uart_rx_axis_tvalid(uart_rx_axis_tvalid),
    .state(state),
	.Develop_two_flag(Develop_two_flag)
);
/////////////////////////////////IIR滤波//////////////////////////////////////
wire[13:0]IIR_OUT;
/*
	    	接线	[11:0]ad_ch1;
	   		输出	[13:0]IIR_OUT;
			reg    [?:?] para_1;
			reg    [?:?] para_2;
			reg    [?:?] para_3;输入
			reg    [?:?] para_4;
			reg    [?:?] para_5;
			reg    [?:?] para_6;
always @(posedge CLK125M or negedge Rst) begin
	if(!Rst)begin
		para_1<=0;
		'''
		para_6<=0;
	end
	else if((帧头==？？)&&(state==Develop_two))begin
		para_1<=Rx_data[?];
		'''
		para_6<=Rx_data[?];
	end
end

*/
/////////////////////////////////DA多路选择//////////////////////////////////////
reg da_sel;//0:作为DDS;1:作为纯DA
assign DataA=(!da_sel)?DDS_OUT:IIR_OUT;

always@(posedge CLK125M or negedge Rst)begin
    if(!Rst)
        da_sel <= 0;
    else begin
        if(Develop_two_flag == 1'b1)
            da_sel <= 1;
        else
            da_sel <= 0;
    end
end
/////////////////////////////////ILA//////////////////////////////////////
ila_0 inst_ila (
	.clk(CLK125M), // input wire clk

	.probe0(Rx_data[0]) // input wire [7:0] probe0
);
endmodule
