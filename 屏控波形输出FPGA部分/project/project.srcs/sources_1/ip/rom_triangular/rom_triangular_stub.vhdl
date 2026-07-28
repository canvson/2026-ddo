-- Copyright 1986-2018 Xilinx, Inc. All Rights Reserved.
-- --------------------------------------------------------------------------------
-- Tool Version: Vivado v.2018.3 (win64) Build 2405991 Thu Dec  6 23:38:27 MST 2018
-- Date        : Sun Jul 20 16:28:01 2025
-- Host        : LAPTOP-AKA16VM5 running 64-bit major release  (build 9200)
-- Command     : write_vhdl -force -mode synth_stub
--               d:/ZYNQ/DDS/project/project.srcs/sources_1/ip/rom_triangular/rom_triangular_stub.vhdl
-- Design      : rom_triangular
-- Purpose     : Stub declaration of top-level module interface
-- Device      : xc7z010clg400-1
-- --------------------------------------------------------------------------------
library IEEE;
use IEEE.STD_LOGIC_1164.ALL;

entity rom_triangular is
  Port ( 
    clka : in STD_LOGIC;
    addra : in STD_LOGIC_VECTOR ( 11 downto 0 );
    douta : out STD_LOGIC_VECTOR ( 13 downto 0 )
  );

end rom_triangular;

architecture stub of rom_triangular is
attribute syn_black_box : boolean;
attribute black_box_pad_pin : string;
attribute syn_black_box of stub : architecture is true;
attribute black_box_pad_pin of stub : architecture is "clka,addra[11:0],douta[13:0]";
attribute x_core_info : string;
attribute x_core_info of stub : architecture is "blk_mem_gen_v8_4_2,Vivado 2018.3";
begin
end;
