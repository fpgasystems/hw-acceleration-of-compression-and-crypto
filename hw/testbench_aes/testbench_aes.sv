`timescale 1ns/1ps

module testbench_aes();

//localparam integer ENCRYPTION = 0;
//localparam integer DECRYPTION = 1;
localparam integer OPERATION  = 0;
localparam real    CLK_PERIOD = 3.2;
localparam integer N_PIPES    = 4;
localparam integer MODE       = 2; // [0-ECB, 1-CTR, 2-CBC]

//localparam logic[127:0] DATA = 128'hFFEEDDCCBBAA99887766554433221100;
//localparam logic[127:0] KEY  = 128'h0F0E0D0C0B0A09080706050403020100;
localparam logic[31:0] ELEMENTS = 0'h4;

localparam logic[127:0] DATA    = 128'h00000001000000010000000100000001;//10000000100000001000000010000000; 00000000000000000000000000000000;00010001000100010001000100010004;//128'h00112233445566778899AABBCCDDEEFF;//256-128'h8ea2b7ca516745bfeafc49904b496089;128-128'h69c4e0d86a7b0430d8cdb78070b4c55a; 192-128'hdda97ca4864cdfe06eaf70a0ec0d7191//128'h00112233445566778899AABBCCDDEEFF;
localparam logic[255:0] KEY_128 = 256'h00000000000000000000000000000000000102030405060708090A0B0C0D0E0F;          
localparam logic[255:0] KEY_192 = 256'h0000000000000000000102030405060708090A0B0C0D0E0F1011121314151617;                               
localparam logic[255:0] KEY_256 = 256'h0000000100000001FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF00000000;
                                  //256'h000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F;
                                  //256'h0000000100000001FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF00000000;
                                  //256'h000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F;

localparam logic[N_PIPES*128-1:0] CONF_DATA = {160'b0, 64'h00000000, KEY_256, ELEMENTS};  //C59BCF35
localparam logic[N_PIPES*128-1:0] FULL_DATA = {DATA, DATA, DATA, DATA};

logic reset_n;
logic clk;

logic s_up_valid;
logic s_up_ready;
logic s_down_ready;
logic s_down_valid;

logic s_key_up_valid_lsb;
logic s_key_up_ready_lsb;
logic s_key_down_ready_lsb;
logic s_key_down_valid_lsb;

logic s_key_up_valid_msb;
logic s_key_up_ready_msb;
logic s_key_down_ready_msb;
logic s_key_down_valid_msb;
logic s_key_start;

logic[256-1:0]  s_key_in;
logic[1024-1:0] s_key_out_lsb;
logic[1024-1:0] s_key_out_msb;
logic[2048-1:0] s_key;
logic           s_key_ready;

logic[N_PIPES*128-1:0] s_data_in;
logic[N_PIPES*128-1:0] s_data_out;
logic[256-1:0]         s_config;
logic[31:0]            s_elem_counter;

aes_user_intel #(
    .OPERATION(OPERATION),
    .MODE(MODE),
    .N_PIPES(N_PIPES),
    .KEY_WIDTH(256)
) inst_aes_user_intel (
    .clock  (clk),
    .resetn (reset_n),
    // upstream
    .ivalid (s_up_valid),
    .oready (s_up_ready),
    .datain (s_data_in),
    .configin(s_config),
    .keylsbin(s_key[1023:0]),    
    .keymsbin(s_key[2047:1024]),
    // downstream 
    .ovalid (s_down_valid),
    .iready (s_down_ready),
    .dataout(s_data_out)
);

key_user_intel #(
    .OPERATION(OPERATION),
    .N_PIPES(N_PIPES),
    .KEY_WIDTH(256)
) inst_key_user_intel_lsb (
    .clock  (clk),
    .resetn (reset_n),
    // upstream
    .ivalid (s_key_up_valid_lsb),
    .oready (s_key_up_ready_lsb),
    .datain (s_key_in),
    .flagin (8'h01),
    // downstream 
    .ovalid (s_key_down_valid_lsb),
    .iready (s_key_down_ready_lsb),
    .dataout(s_key_out_lsb)
);

key_user_intel #(
    .OPERATION(OPERATION),
    .N_PIPES(N_PIPES),
    .KEY_WIDTH(256)
) inst_key_user_intel_msb (
    .clock  (clk),
    .resetn (reset_n),
    // upstream
    .ivalid (s_key_up_valid_msb),
    .oready (s_key_up_ready_msb),
    .datain (s_key_in),
    .flagin (8'h02),
    // downstream 
    .ovalid (s_key_down_valid_msb),
    .iready (s_key_down_ready_msb),
    .dataout(s_key_out_msb)
);

clock_gen inst_clock_gen
(
  .clk_period(CLK_PERIOD),
  .clk(clk)    
);

  initial 
  begin 
    reset_n = 0; #50ns;
    reset_n = 1; //#20us;
  end // initial

//process for aes
always @(posedge clk) begin
    if (~reset_n) begin

      s_data_in      <= '0;
      s_up_valid     <= 1'b0;
      s_down_ready   <= 1'b0;
      s_elem_counter <= '0;
      s_config       <= '0;
       
    end
    else begin
      //s_up_valid    <= 1'b0;
      s_down_ready  <= 1'b1;
          
      if (s_up_ready && s_elem_counter!=ELEMENTS && s_key_ready) begin
        s_up_valid  <= 1'b1;
        s_data_in   <= FULL_DATA;// + s_elem_counter;
        s_elem_counter <= s_elem_counter + 1;  
        s_config    <= {128'h0,32'h0,ELEMENTS,64'h00000000};

      end

      if(s_elem_counter==ELEMENTS+1)
        s_up_valid <= 1'b0;
      
      if (s_down_valid) begin
        //$display("Data_OUT %h \n", s_data_out[511:384]);
        //$display("Data_OUT %h \n", s_data_out[383:256]);
        //$display("Data_OUT %h \n", s_data_out[255:128]);
        //$display("Data_OUT %h \n", s_data_out[127:0]);
        $display("Data_OUT %h%h%h%h\n", s_data_out[511:384], s_data_out[383:256], s_data_out[255:128], s_data_out[127:0]);
      end
    end
end

// process for key
 always @(posedge clk) begin
    if (~reset_n) begin
      s_key_up_valid_lsb   <= 1'b0;
      s_key_up_valid_msb   <= 1'b0;

      s_key_in             <= '0;
      s_key_down_ready_lsb <= 1'b0;
      s_key_down_ready_msb <= 1'b0; 

      s_key_start      <= 1'b0;     
      s_key_ready      <= 1'b0;
    end
    else begin
      s_key_up_valid_lsb   <= 1'b0;
      s_key_up_valid_msb   <= 1'b0;

      s_key_down_ready_lsb <= 1'b1;
      s_key_down_ready_msb <= 1'b1;

      if(~s_key_start) begin
         $display("Key %h \n", KEY_256);
         s_key_up_valid_lsb <= 1'b1;
         s_key_up_valid_msb <= 1'b1;
         s_key_in       <= KEY_256;
         s_key_start    <= 1'b1;
      end
       
      if (s_key_down_valid_lsb) begin
        //$display("Data_OUT %h \n", s_data_out[511:384]);
        //$display("Data_OUT %h \n", s_data_out[383:256]);
        //$display("Data_OUT %h \n", s_data_out[255:128]);
        //$display("Data_OUT %h \n", s_data_out[127:0]);
        $display("KEY_OUT_lsb %h\n", s_key_out_lsb);
        s_key[1024-1:0] <= s_key_out_lsb;
      end

      if (s_key_down_valid_msb) begin
        //$display("Data_OUT %h \n", s_data_out[511:384]);
        //$display("Data_OUT %h \n", s_data_out[383:256]);
        //$display("Data_OUT %h \n", s_data_out[255:128]);
        //$display("Data_OUT %h \n", s_data_out[127:0]);
        $display("KEY_OUT_msb %h\n", s_key_out_msb);
        s_key[2048-1:1024] <= s_key_out_msb;
        s_key_ready <= 1'b1;
      end
    end
end

endmodule // testbench_aes