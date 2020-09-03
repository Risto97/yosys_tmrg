module concat(
  input [5:0] i1,
  input [5:0] i2,
  input [5:0] i3,
  output [10:0] x1,
  output [7:0] x2
);
  assign x1 = { i2[0:4], i3[0:1]};
  assign x2 = i1;
  // assign {x2[0], x2[5], x2[1:4]} = {i1[0:2], i2[2:5]}; 
endmodule
//
// module m2(
//   input signed i1,
//   input signed [1:0] i2,i3,
//   output signed x);
//   assign x=|i1 & ^i3;
// endmodule

// module m3( x,y,z,y2);
//   input x,z;
//   output y;
//   output signed y2; 
//   wire [1:0] asd;
//   m1 inst1(.i1(x), .i2(asd), .i3({z,x}) , .x(y));
//   // m2 inst2(.i1(x), .i2({2'b10}), .i3({z,x}) , .x(y2));
// endmodule   


