test: tmrg_pass.so
	yosys -ql test1.log -m ./tmrg_pass.so -p "proc; debug tmrg_pass; show fsm01; write_verilog out.v" fsm01.v voter.v fanout.v
	cat test1.log

test2: tmrg_pass.so
	yosys -ql test1.log -m ./tmrg_pass.so -p "proc; tmrg_pass; show simple_mod; write_verilog out.v" fsm01.v
	cat test1.log

test3: tmrg_pass.so
	yosys -ql test1.log -m ./tmrg_pass.so -p "proc; tmrg_pass; show picorv32_axi_adapter; write_verilog out.v" picorv32.v
	cat test1.log

tmrg_pass.so: tmrg_pass.cc
	yosys-config --exec --cxx --cxxflags --ldflags -o $@ -shared $^ --ldlibs

clean:
	rm -f test1.log
	rm -f tmrg_pass.so tmrg_pass.d
