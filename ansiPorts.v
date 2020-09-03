module m1(
  input i1,
  input [1:0] i2,i3,
  output x);
  assign x=|i1 & ^i3;
endmodule

module m2(
  input signed i1,
  input signed [1:0] i2,i3,
  output signed x);
  assign x=|i1 & ^i3;
endmodule

(* tmrg_do_not_triplicate = "y" *)
module concat( x,y,z,y2);
  input x,z;
  output y;
  output signed y2; 
  m1 inst1(.i1(x), .i2({2'b10}), .i3({z,x}) , .x(y));
  m2 inst2(.i1(x), .i2({2'b10}), .i3({z,x}) , .x(y2));
endmodule   


