`timescale 1ns / 1ps

module aes_ctrl_intel #(
    parameter N_PIPES   = 4,
    parameter KEY_WIDTH = 128,
    parameter MODE      = 0,        // [0-ECB, 1-CTR, 2-CBC]
    parameter OPERATION = 0         // 0-encryption; 1-decryption
)(
    input logic                   clk,
    input logic                   resetn,
    // upstream module
    input logic                   ivalid,
    output logic                  oready,
    input logic[N_PIPES*128-1:0]  idata,
    input logic[256-1:0]          iconfig,
    input logic[1024-1:0]         ikeylsb,
    input logic[1024-1:0]         ikeymsb,
    // aes module 
    output logic[N_PIPES*128-1:0] odata,
    output logic                  ovalid,
    input  logic                  iready,
    output logic[N_PIPES*128/8-1:0] okeep,
    output logic                  olast,
    output logic[N_PIPES*128-1:0] ocntr,
    output logic[2048-1:0]        okey,
    input  logic[128-1:0]         ifeedbackiv,
    input  logic                  ifeedbackvalid
);

localparam logic[N_PIPES*128/8-1:0] keep_1 = ~('b0);
logic[63:0]  s_elements;
logic[63:0]  s_elements_cntr;
logic[63:0]  s_nonce;
logic[63:0]  s_element0;
logic[63:0]  s_element1;
logic[63:0]  s_element2;
logic[63:0]  s_element3;
logic[127:0] s_iv_en;     // s_iv for encryption
logic[127:0] s_iv_de;     // s_iv for decryption
logic[N_PIPES*128-1:0] s_data;
logic s_ctrl_ready;

generate
    if (MODE==2) begin
        if (OPERATION==0) begin
            assign oready = s_ctrl_ready;
        end
        else begin
            assign oready = 1'b1;
        end
    end
    else begin
        assign oready = 1'b1;
    end
endgenerate

always_ff @(posedge clk) begin
  if(~resetn) begin
    ovalid    <= 1'b0;
    olast     <= 1'b0;
    okey      <= 'bx;  
    s_nonce   <= 'bx;
    
    s_elements_cntr <= '0;
    s_data          <= '0;
    s_elements      <= 'b0;
    s_iv_en         <= 'b0;
    s_iv_de         <= 'b0;
    s_ctrl_ready    <= 1'b1;  
  end
  else begin
    ovalid    <= 1'b0;
    olast     <= 1'b0;

    if(ivalid && s_ctrl_ready) begin
        s_data         <= idata;
        okeep          <= keep_1;
        ovalid         <= 1'b1;
        s_elements_cntr<= s_elements_cntr + 1;
        
        if (MODE==2)
            if (OPERATION==0)
                s_ctrl_ready <= 1'b0;
            else
                s_ctrl_ready <= 1'b1;
        else
            s_ctrl_ready <= 1'b1;

        if(s_elements_cntr==0) begin
            s_nonce    <= iconfig[64-1:0];
            s_elements <= iconfig[128-1:64];
            s_iv_en    <= iconfig[256-1:128];
            s_iv_de    <= iconfig[256-1:128];

            okey       <= {ikeymsb,ikeylsb};
            if((s_elements_cntr+1)==iconfig[128-1:64]) 
                olast <= 1'b1;    
        end
        else begin
            if((s_elements_cntr+1)==s_elements) begin
                olast <= 1'b1;
            end 
            s_iv_de <= s_data[N_PIPES*128-1:(N_PIPES-1)*128];
        end    
    end

    if(ifeedbackvalid) begin
        s_iv_en      <= ifeedbackiv;
        s_ctrl_ready <= 1'b1;
    end 
  end    
end

always_comb begin

    odata = s_data;

    s_element0 = s_elements_cntr ^ 64'h00;
    s_element1 = s_elements_cntr ^ 64'h01;
    s_element2 = s_elements_cntr ^ 64'h02;
    s_element3 = s_elements_cntr ^ 64'h03;
    
    if(MODE==2)
        if (OPERATION==0)
            ocntr = {idata[383:0],s_iv_en};
        else
            ocntr = {idata[383:0],s_iv_de};
    else
        ocntr = {s_nonce,s_element3,s_nonce,s_element2,s_nonce,s_element1,s_nonce,s_element0};
end

endmodule 
