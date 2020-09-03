(* tmrg_do_not_triplicate = "in_buf" *)
module fsm01 (
  input in,
  input in2,
  // input in3,
  output data_out
  // output out2,
  // input clk
);
  // (* tmrg_triplicate = "default" *)
  // tmrg triplicate default
  // tmrg do_not_triplicate in_
  // tmrg do_not_triplicate clk_
  // tmrg do_not_triplicate data_out_
  // tmrg do_not_triplicate in_buf
  wire state;
  // reg stateNext;
    wire in_buf;
  //
  // always @(posedge clk)
  //   state <= stateNext;
  //
  // always @(state or in)
  //   stateNext = in_buf ^ state;
  //
  //   
    // assign in_buf = in;
    assign state = ~in2;
    assign in_buf = (~in) & (~in2) & state;
    // assign out2 = ~in3;
    // assign in_inv = ~in;
// assign data_out = in_buf;
simple_mod sm1(.din(in_buf), .dout(data_out));
// simple_mod2 sm2(.din1(state), .din2(in_buf), .dout(out2));

endmodule

module simple_mod (din, dout);
    input din;
    output dout;

    assign dout = din;
endmodule

// module simple_mod2(din1, din2, dout);
//     input din1;
//     input din2;
//     output dout;
//
//     assign dout = din1 & din2;
//
// endmodule
