MOD_NAME := fsm01

tmrg_pass.so: tmrg_pass.cc
	yosys-config --exec --cxx --cxxflags --ldflags -o $@ -shared $^ --ldlibs

test: tmrg_pass.so
	yosys -ql test1.log -m ./tmrg_pass.so -p "proc; debug tmrg_pass; show fsm01; write_verilog -norename -noattr out.v" fsm01.v voter.v fanout.v
	cat test1.log

notmrg: tmrg_pass.so
	yosys -ql test1.log -m ./tmrg_pass.so -p "proc; dump; show ${MOD_NAME}; write_verilog -norename -noattr out.v" fsm01.v
	cat test1.log

concat: tmrg_pass.so
	yosys -ql test1.log -m ./tmrg_pass.so -p "proc; debug tmrg_pass; show concat; write_verilog -norename -noattr out.v" concat.v voter.v fanout.v
	cat test1.log

ansiports: tmrg_pass.so
	yosys -ql test1.log -m ./tmrg_pass.so -p "proc; debug tmrg_pass; show concat; write_verilog -norename -noattr out.v" ansiPorts.v voter.v fanout.v
	cat test1.log

assignment: tmrg_pass.so
	yosys -ql test1.log -m ./tmrg_pass.so -p "proc; debug tmrg_pass; show assigment02; write_verilog -norename -noattr out.v" tests/verilog/assigment02.v voter.v fanout.v
	cat test1.log

case01: tmrg_pass.so
	yosys -ql test1.log -m ./tmrg_pass.so -p "proc; debug tmrg_pass; show case01; write_verilog -norename -noattr out.v" tests/verilog/case01.v voter.v fanout.v
	cat test1.log

hier01: tmrg_pass.so
	yosys -ql test1.log -m ./tmrg_pass.so -p "proc; debug tmrg_pass; show hier01; write_verilog -norename -noattr out.v" tests/verilog/hier01.v voter.v fanout.v
	cat test1.log

generate: tmrg_pass.so
	yosys -ql test1.log -m ./tmrg_pass.so -p "proc; debug tmrg_pass; show gen; write_verilog -norename -noattr out.v" tests/verilog/generate01.v voter.v fanout.v
	cat test1.log


clean:
	rm -f test1.log
	rm -f tmrg_pass.so tmrg_pass.d
