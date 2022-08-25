`timescale 1ns/1ps

module clock_gen #(parameter INIT_DELAY=0)(
   input  real   clk_period,
   output logic  clk
);

 initial
   begin 
     clk = 0; #INIT_DELAY;
     forever begin
       clk = ~clk; #(clk_period/2.0);
     end
   end
 endmodule
