`timescale 1ns / 1ps

module key_user_intel #(
    parameter OPERATION = 0,             // 0-encryption; 1-decryption
    parameter N_PIPES   = 4,
    parameter KEY_WIDTH = 256
)(
    // Clock and reset
    input logic           clock ,
    input logic           resetn,
    
    // upstream module communication
    input  logic          ivalid,
    output logic          oready,
    input  logic[256-1:0] datain,
    input  logic[7:0]     flagin,

    // downstream module communication
    output logic           ovalid,
    input  logic           iready,
    output logic[1024-1:0] dataout
);

 
logic[256-1:0]  s_key;
logic[2048-1:0] s_key_full;
logic[7:0]      s_flags;

logic s_key_in_valid;
logic s_key_out_valid;

logic [31:0] s_cntr;

assign oready = 1'b1;

// key pipelines
key_top #(
    .NPAR(N_PIPES),
    .KEY_WIDTH(KEY_WIDTH),
    .OPERATION(OPERATION)
) inst_key_top (
    .clk(clock),
    .reset_n(resetn),
    .stall(1'b0),     // 1'b0
    .key_in(s_key),
    .keyVal_in(s_key_in_valid),
    .keyVal_out(s_key_out_valid),
    .key_out(s_key_full)
);
  
always_ff @(posedge clock) begin
  if(~resetn) begin
    s_key          <= '0;
    s_cntr         <= '0;
    s_flags        <= '0;
    s_key_in_valid <= 1'b0;

    ovalid         <= 1'b0;
    dataout        <= '0;

  end
  else begin
    s_key_in_valid <= 1'b0;
    ovalid         <= 1'b0;

    if(ivalid) begin
        s_key   <= datain;
        s_flags <= flagin;

        if(s_cntr==0)begin
            s_key_in_valid <= 1'b1;
            s_cntr         <= s_cntr+1;
        end           
    end

    if(s_key_out_valid)begin      
        ovalid <= 1'b1;

        if(s_flags==8'h01) 
           dataout <= s_key_full[1024-1:0];
        else 
            if(s_flags==8'h02)
                dataout <= s_key_full[2048-1:1024];    
    end

  end    
end 

endmodule
