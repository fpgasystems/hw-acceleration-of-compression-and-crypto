`timescale 1ns / 1ps

module aes_user_intel #(
    parameter OPERATION = 0,             // 0-encryption; 1-decryption
    parameter N_PIPES   = 4,
    parameter KEY_WIDTH = 256,
    parameter MODE      = 2              // 0-ECB; 1-CTR; 2-CBC
)(
    // Clock and reset
    input logic                   clock ,
    input logic                   resetn,
    
    // upstream module communication
    input  logic                  ivalid,
    output logic                  oready,
    input  logic[N_PIPES*128-1:0] datain,
    input  logic[256-1:0]         configin,
    input  logic[1024-1:0]        keylsbin,
    input  logic[1024-1:0]        keymsbin,

    // downstream module communication
    output logic                  ovalid,
    input  logic                  iready,
    output logic[N_PIPES*128-1:0] dataout
);

 
logic[2048-1:0]          s_key;
logic[N_PIPES*128-1:0]   s_plaintxt_data;
logic[N_PIPES*128/8-1:0] s_plaintxt_keep;
logic[N_PIPES*128-1:0]   s_cntr;
logic[N_PIPES*128-1:0]   s_dataout;

logic s_plaintxt_valid;
logic s_plaintxt_ready;
logic s_plaintxt_last;
logic s_dataout_valid;
logic s_rqst;

aes_ctrl_intel #(
    .KEY_WIDTH(KEY_WIDTH),
    .N_PIPES(N_PIPES),
    .OPERATION(OPERATION),
    .MODE(MODE)
) inst_aes_ctrl_intel(
    .clk(clock),
    .resetn(resetn),
    .ivalid(ivalid),
    .oready(oready),
    .idata(datain),
    .iconfig(configin),
    .ikeylsb(keylsbin),
    .ikeymsb(keymsbin),
    .odata(s_plaintxt_data),
    .ovalid(s_plaintxt_valid),
    .iready(s_plaintxt_ready),
    .okeep(s_plaintxt_keep),
    .olast(s_plaintxt_last),
    .ocntr(s_cntr),
    .okey(s_key),
    .ifeedbackiv(s_dataout[N_PIPES*128-1:(N_PIPES-1)*128]),
    .ifeedbackvalid(s_dataout_valid)
);

assign s_plaintxt_ready = 1'b1;

assign ovalid  = s_dataout_valid;
assign dataout = s_dataout;

// AES pipelines
aes_top #(
    .NPAR(N_PIPES),
    .KEY_WIDTH(KEY_WIDTH),
    .MODE(MODE),
    .OPERATION(OPERATION)
) inst_aes_top (
    .clk(clock),
    .reset_n(resetn),
    .stall(1'b0),     // 1'b0
    .key_in(s_key),
    .last_in(s_plaintxt_last),
    .last_out(),
    .keep_in(s_plaintxt_keep),
    .keep_out(),
    .dVal_in(s_plaintxt_valid),
    .dVal_out(s_dataout_valid), 
    .data_in(s_plaintxt_data),
    .data_out(s_dataout),
    .cntr_in(s_cntr)
);
   
endmodule
