`timescale 1ns/1ps

module testbench_aes();

//localparam integer ENCRYPTION = 0;
//localparam integer DECRYPTION = 1;
localparam integer OPERATION  = 1;
localparam real    CLK_PERIOD = 3.2;
localparam integer N_PIPES    = 4;
localparam integer MODE       = 2; // [0-ECB, 1-CTR, 2-CBC]

//localparam logic[127:0] DATA = 128'hFFEEDDCCBBAA99887766554433221100;
//localparam logic[127:0] KEY  = 128'h0F0E0D0C0B0A09080706050403020100;
localparam logic[31:0] ELEMENTS = 0'h4;

localparam logic[127:0] DATA    = 128'h00000001000000010000000100000001;//10000000100000001000000010000000; 00000000000000000000000000000000;00010001000100010001000100010004;//128'h00112233445566778899AABBCCDDEEFF;//256-128'h8ea2b7ca516745bfeafc49904b496089;128-128'h69c4e0d86a7b0430d8cdb78070b4c55a; 192-128'hdda97ca4864cdfe06eaf70a0ec0d7191//128'h00112233445566778899AABBCCDDEEFF;
localparam logic[127:0] DATA1   = 128'hfa90bc39d4f7c67f45ac8f3a5869ed29;//10000000100000001000000010000000; 00000000000000000000000000000000;00010001000100010001000100010004;//128'h00112233445566778899AABBCCDDEEFF;//256-128'h8ea2b7ca516745bfeafc49904b496089;128-128'h69c4e0d86a7b0430d8cdb78070b4c55a; 192-128'hdda97ca4864cdfe06eaf70a0ec0d7191//128'h00112233445566778899AABBCCDDEEFF;
localparam logic[127:0] DATA2   = 128'h8f632700324aff6674e31e5713c638d1;//10000000100000001000000010000000; 00000000000000000000000000000000;00010001000100010001000100010004;//128'h00112233445566778899AABBCCDDEEFF;//256-128'h8ea2b7ca516745bfeafc49904b496089;128-128'h69c4e0d86a7b0430d8cdb78070b4c55a; 192-128'hdda97ca4864cdfe06eaf70a0ec0d7191//128'h00112233445566778899AABBCCDDEEFF;
localparam logic[127:0] DATA3   = 128'h03bc9bc96c99e9c27afc75853d6cf2bc;//10000000100000001000000010000000; 00000000000000000000000000000000;00010001000100010001000100010004;//128'h00112233445566778899AABBCCDDEEFF;//256-128'h8ea2b7ca516745bfeafc49904b496089;128-128'h69c4e0d86a7b0430d8cdb78070b4c55a; 192-128'hdda97ca4864cdfe06eaf70a0ec0d7191//128'h00112233445566778899AABBCCDDEEFF;
localparam logic[127:0] DATA4   = 128'h25fee3acd9dd07ff6c1eec2e1ca01f20;//10000000100000001000000010000000; 00000000000000000000000000000000;00010001000100010001000100010004;//128'h00112233445566778899AABBCCDDEEFF;//256-128'h8ea2b7ca516745bfeafc49904b496089;128-128'h69c4e0d86a7b0430d8cdb78070b4c55a; 192-128'hdda97ca4864cdfe06eaf70a0ec0d7191//128'h00112233445566778899AABBCCDDEEFF;

localparam logic[127:0] DATA1d  = 128'hfe2045deffb16a75036385903ca15918;//10000000100000001000000010000000; 00000000000000000000000000000000;00010001000100010001000100010004;//128'h00112233445566778899AABBCCDDEEFF;//256-128'h8ea2b7ca516745bfeafc49904b496089;128-128'h69c4e0d86a7b0430d8cdb78070b4c55a; 192-128'hdda97ca4864cdfe06eaf70a0ec0d7191//128'h00112233445566778899AABBCCDDEEFF;
localparam logic[127:0] DATA2d  = 128'h2cc0aa9b00bc319a63fe600893abbd29;//10000000100000001000000010000000; 00000000000000000000000000000000;00010001000100010001000100010004;//128'h00112233445566778899AABBCCDDEEFF;//256-128'h8ea2b7ca516745bfeafc49904b496089;128-128'h69c4e0d86a7b0430d8cdb78070b4c55a; 192-128'hdda97ca4864cdfe06eaf70a0ec0d7191//128'h00112233445566778899AABBCCDDEEFF;
localparam logic[127:0] DATA3d  = 128'hd5cd92ca2a15d626e170647b83afac96;//10000000100000001000000010000000; 00000000000000000000000000000000;00010001000100010001000100010004;//128'h00112233445566778899AABBCCDDEEFF;//256-128'h8ea2b7ca516745bfeafc49904b496089;128-128'h69c4e0d86a7b0430d8cdb78070b4c55a; 192-128'hdda97ca4864cdfe06eaf70a0ec0d7191//128'h00112233445566778899AABBCCDDEEFF;
localparam logic[127:0] DATA4d  = 128'h729368483be32f0a9e2fedea29c03a67;//10000000100000001000000010000000; 00000000000000000000000000000000;00010001000100010001000100010004;//128'h00112233445566778899AABBCCDDEEFF;//256-128'h8ea2b7ca516745bfeafc49904b496089;128-128'h69c4e0d86a7b0430d8cdb78070b4c55a; 192-128'hdda97ca4864cdfe06eaf70a0ec0d7191//128'h00112233445566778899AABBCCDDEEFF;
                                       
localparam logic[255:0] KEY_128 = 256'h00000000000000000000000000000000000102030405060708090A0B0C0D0E0F;          
localparam logic[255:0] KEY_192 = 256'h0000000000000000000102030405060708090A0B0C0D0E0F1011121314151617;                               
localparam logic[255:0] KEY_256 = 256'h0000000100000001FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF00000000;
                                  //256'h000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F;
                                  //256'h0000000100000001FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF00000000;
                                  //256'h000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F;
                                                                                           
localparam logic[N_PIPES*128-1:0] CONF_DATA = {160'b0, 64'h00000000, KEY_256, ELEMENTS};  //C59BCF35
localparam logic[N_PIPES*128-1:0] FULL_DATA = {DATA4d, DATA3d, DATA2d, DATA1d};

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
      s_up_valid    <= 1'b0;
      s_down_ready  <= 1'b1;
          
      if (s_up_ready && s_elem_counter!=ELEMENTS && s_key_ready) begin
        s_up_valid  <= 1'b1;
        
        if(s_elem_counter==0)
            s_data_in   <= FULL_DATA;// + s_elem_counter;
        if(s_elem_counter==1)
            s_data_in   <= 512'h0b789a4905b1a3d5a6d3ebf90eedae9511ba75cbb8c7dc4bf4486d87384eed1e9263d530d3f2e5e2c59b9b89c122045dcc1ffe7ec74be22a11cf1c0fc6813f90;
        if(s_elem_counter==2)
            s_data_in   <= 512'h36dc9689f330fa6437eec7e89ab46f38dab74a1eb41bee700fe0e0dd59bb00fbb8b43a68453d66c2a9f915d0e717bf872b0272740b98d0eec242305b158dbbf4;
        if(s_elem_counter==3)
            s_data_in   <= 512'h4081b9e2f50007c1ef6fbf115bec9bfbd6047af9d15eee49c5fcd24ecacc74313129bad96e45f609e8e5b221fdbe806487bb170e6c82601dcf6b8d8c6c685640;
        
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