#include "kernel/sigtools.h"
#include "kernel/yosys.h"
#include <iterator>
#include <regex>
#include <set>
#include <string>
#include <utility>
#include <vector>

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

struct obj_src {
  bool is_private;
  std::string fn;
  int ln;
};

bool obj_is_public(RTLIL::IdString obj) {
  if (obj.str()[0] == '$')
    return false;
  else
    return true;
}

obj_src find_obj_src(RTLIL::IdString obj) {
  obj_src objsrc;

  std::regex fn_ln_regex("\\$.*?\\$(.+):([0-9]+)\\$");
  std::smatch m;
  std::string line = obj.str();

  objsrc.is_private = !obj_is_public(obj);

  if (std::regex_search(line, m, fn_ln_regex)) {
    objsrc.fn = m[1].str();
    objsrc.ln = std::stoi(m[2].str());
  }

  return objsrc;
}

std::vector<RTLIL::Cell *> group_statement_cells(RTLIL::Wire *wire,
                                                 RTLIL::Module *mod) {
  std::vector<RTLIL::Cell *> statement_cells;

  for (auto c : mod->connections()) {
    obj_src wsrc = find_obj_src(c.second.as_wire()->name);
    if (c.first.as_wire() == wire && wsrc.is_private) {

      for (auto c : mod->selected_cells()) {
        obj_src csrc = find_obj_src(c->name);
        if (csrc.fn == wsrc.fn && csrc.ln == wsrc.ln) {
          statement_cells.push_back(c);
        }
      }
    }
  }
  return statement_cells;
}

std::vector<RTLIL::Wire *> group_cell_wires(RTLIL::Cell *cell) {
  std::vector<RTLIL::Wire *> cell_drivers;
  for (auto p : cell->connections())
    cell_drivers.push_back(p.second.as_wire());

  return cell_drivers;
}

std::vector<RTLIL::Wire *> group_statement_wires(RTLIL::Wire *wire,
                                                 RTLIL::Module *mod) {
  std::vector<RTLIL::Cell *> cells;
  std::vector<RTLIL::Wire *> wires;

  cells = group_statement_cells(wire, mod);
  for (auto c : cells) {
    std::vector<RTLIL::Wire *> c_drv = group_cell_wires(c);
    for (auto d : c_drv)
      wires.push_back(d);
  }
  return wires;
}

bool TMRModule(
    RTLIL::Module *orig,
    std::pair<std::set<RTLIL::Cell *>, std::set<RTLIL::Wire *>> dont_tmrg) {

  log("\n#####################################\nProcessing module %s\n\n",
      orig->name.str().c_str());
  /* Get list of do not tmrg Cells and Wires */
  for (auto w : orig->selected_wires()) {
    if (dont_tmrg.second.count(w)) {
      log("Wire: %s\n", w->name.str().c_str());
      if (!w->port_input && !w->port_output) {
        std::vector<RTLIL::Wire *> stmnt_w = group_statement_wires(w, orig);
        std::vector<RTLIL::Cell *> stmnt_c = group_statement_cells(w, orig);

        for (auto cl : stmnt_c) {
          dont_tmrg.first.emplace(cl);
          log("Add to dont_tmrg cell: %s\n", cl->name.str().c_str());
        }

        log("\n");
        for (auto p : stmnt_w) {
          // Add statement wires to dont tmrg list if it is private
          if (!obj_is_public(p->name)) {
            if (dont_tmrg.second.count(p) == 0)
              log("Add to dont_tmrg wire: %s\n", p->name.str().c_str());
            dont_tmrg.second.emplace(p);
          }
        }
      }
    }
  }

  log("\n");
  /* Get list of connections */
  std::vector<std::pair<std::string, std::string>> conn_names;
  for (auto con : orig->connections()) {

    std::pair<std::string, std::string> pair;
    if (dont_tmrg.second.count(con.first.as_wire()) == 0)
      for (auto s : {"A", "B", "C"}) {
        pair.first = con.first.as_wire()->name.str() + s;
        pair.second = con.second.as_wire()->name.str() + s;
        conn_names.push_back(pair);
      }
    else {
      pair.first = con.first.as_wire()->name.str();
      pair.second = con.second.as_wire()->name.str();
      conn_names.push_back(pair);
    }
  }
  for (auto con : conn_names) {
    log("Connection Second: %s, First: %s\n", con.second.c_str(),
        con.first.c_str());
  }

  orig->connections_.clear();
  log("\n");
  // Triplicating tmrg wires and ports not on the list
  for (auto w : orig->selected_wires()) {
    log("Selected wire: %s\n", w->name.str().c_str());
    if (dont_tmrg.second.count(w) == 0) {
      log("Triplicating Wire: %s\n", w->name.str().c_str());
      for (auto s : {"A", "B", "C"}) {
        RTLIL::Wire *wire = orig->addWire(w->name.str() + s, w->width);
        wire->port_input = w->port_input;
        wire->port_output = w->port_output;
        orig->fixup_ports();
      }
    }
  }

  log("\n");
  // Add voters and fanouts
  for (auto w : dont_tmrg.second) {
    if (obj_is_public(w->name)) {
      log("Triplicating dont tmrg Wire: %s\n", w->name.str().c_str());
      for (auto s : {"A", "B", "C"}) {
        RTLIL::Wire *wire = orig->addWire(w->name.str() + s, w->width);
        wire->port_input = false;
        wire->port_output = false;
      }
      if (w->port_input || (!w->port_input && !w->port_output)) {
        log("Adding fanout on: %s\n", w->name.str().c_str());
        RTLIL::Cell *fn = orig->addCell(NEW_ID, "\\fanout");
        dont_tmrg.first.emplace(fn);
        fn->setParam("\\WIDTH", 1);
        fn->setPort("\\in", w);
        for (auto s : {"A", "B", "C"})
          fn->setPort((std::string) "\\out" + s, orig->wire(w->name.str() + s));

      } else if (w->port_output) {
        log("Adding voter on: %s\n", w->name.str().c_str());
        RTLIL::Cell *vt = orig->addCell(NEW_ID, "\\majorityVoter");
        dont_tmrg.first.emplace(vt);
        vt->setParam("\\WIDTH", 1);
        vt->setPort("\\out", w);
        for (auto s : {"A", "B", "C"})
          vt->setPort((std::string) "\\in" + s, orig->wire(w->name.str() + s));
      }
    }
  }

  log("\n");
  /* Connecting Wires and Ports */
  for (auto pair : conn_names) {
    log("Connecting %s to %s\n", pair.second.c_str(), pair.first.c_str());
    orig->connect(orig->wire(pair.first), orig->wire(pair.second));
    // conn_names.pop_back();
  }

  log("\n");
  /* Triplicate yosys cells */
  for (auto c : orig->selected_cells()) {
    if (c->name.str()[0] == '\\' || dont_tmrg.first.count(c)) {
      continue;
    } else {
      log("\nTriplicating cell: %s\n", c->name.str().c_str());
      for (auto s : {"A", "B", "C"}) {
        RTLIL::Cell *cell = orig->addCell(NEW_ID, c->type);

        for (auto p : c->parameters) {
          cell->setParam(p.first, p.second.as_int());
        }
        for (auto p : c->connections()) {
          std::string conn_to = p.second.as_wire()->name.str() + s;
          log("First: %s, second %s\n", p.first.c_str(), conn_to.c_str());
          cell->setPort(p.first.str(), orig->wire(conn_to));
        }
      }
      orig->remove(c);
    }
  }

  log("\n");
  /* Instantiate triplicated user modules as cells */
  for (auto c : orig->selected_cells()) {
    if (c->name.str()[0] == '\\') {
      log("Cell is: %s\n", log_id(c->name));
      // log_cell(c);

      RTLIL::Cell *newcell = orig->addCell(NEW_ID, c->type);
      for (auto p : c->connections()) {
        for (auto s : {"A", "B", "C"}) {
          newcell->setPort(p.first.str() + s,
                           orig->wire(p.second.as_wire()->name.str() + s));
        }
      }
      std::string cell_name = c->name.str();
      orig->remove(c);
      orig->rename(newcell, cell_name);
    }
    // for (auto p : c->connections()) {
    //   log("Connection is: %s to: %s\n", log_id(p.first),
    //   log_signal(p.second));
    // }
  }

  return true;
}

struct TmrgPass : public Pass {
  TmrgPass() : Pass("tmrg_pass", "just a simple test") {}
  void execute(std::vector<std::string>, RTLIL::Design *design) override {

    log("\n\n\n########## STARTING TMRG PASS ####################\n\n\n");
    RTLIL::Module *orig = design->modules_["\\fsm01"];

    std::set<RTLIL::Wire *> dont_tmrg_wires;
    std::set<RTLIL::Cell *> dont_tmrg_cells;
    std::pair<std::set<RTLIL::Cell *>, std::set<RTLIL::Wire *>> dont_tmrg_list;
    dont_tmrg_wires.insert(orig->wire("\\in_buf"));
    dont_tmrg_wires.insert(orig->wire("\\in3"));
    dont_tmrg_wires.insert(orig->wire("\\data_out"));
    dont_tmrg_list.second = dont_tmrg_wires;
    dont_tmrg_list.first = dont_tmrg_cells;

    for (auto mod : design->selected_modules()) {
      //   // TODO USE SELECTION to exclude voter and fanout???
      if (mod->name.str() != "\\majorityVoter" && mod->name.str() != "\\fanout")
        TMRModule(mod, dont_tmrg_list);
    }
  // run_pass("rmports");
  // run_pass("opt_clean");
  }
} TmrgPass;

PRIVATE_NAMESPACE_END
