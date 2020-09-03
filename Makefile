MOD_NAME := fsm01

test: tmrg_pass.so
	yosys -ql test1.log -m ./tmrg_pass.so -p "proc; debug tmrg_pass; show ${MOD_NAME}; write_verilog -norename -noattr out.v" fsm01.v voter.v fanout.v
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

tmrg_pass.so: tmrg_pass.cc
	yosys-config --exec --cxx --cxxflags --ldflags -o $@ -shared $^ --ldlibs

clean:
	rm -f test1.log
	rm -f tmrg_pass.so tmrg_pass.d
