#include "kernel/sigtools.h"
#include "kernel/yosys.h"
#include <cstddef>
#include <iterator>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

std::vector<std::string> split(const std::string &s, char delim) {
  std::vector<std::string> result;
  std::stringstream ss(s);
  std::string item;

  while (getline(ss, item, delim)) {
    result.push_back(item);
  }

  return result;
}

bool TMR_wire_exist(RTLIL::Module *mod, RTLIL::IdString id, std::string sufix) {
  if (mod->wires_.count(id.str() + sufix))
    return true;
  else
    return false;
}

RTLIL::Wire *addWire(RTLIL::Module *mod, RTLIL::SigSpec sig,
                     std::string sufix) {
  RTLIL::Wire *wire;
  if (!sig.is_wire())
    return NULL;

  if (!TMR_wire_exist(mod, sig.as_wire()->name, sufix))
    wire =
        mod->addWire(sig.as_wire()->name.str() + sufix, sig.as_wire()->width);
  else
    wire = mod->wire(sig.as_wire()->name.str() + sufix);
  return wire;
}

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

  pool<RTLIL::Wire *> remove_wires_list;
  std::set<RTLIL::Wire *> voter_wires;
  std::set<RTLIL::Wire *> fanout_wires;

  /* Get a list of do not tmrg from attribute */
  if (orig->has_attribute("\\tmrg_do_not_triplicate")) {
    std::string attr = orig->get_string_attribute("\\tmrg_do_not_triplicate");
    std::vector<std::string> v = split(attr, ' ');
    for (auto s : v) {
      std::string wn = "\\" + s;
      if (orig->wires_.count(wn))
        dont_tmrg.second.emplace(orig->wire(wn));
    }
  }

  /* Get list of do not tmrg Cells and Wires */
  for (auto w : orig->selected_wires()) {
    if (dont_tmrg.second.count(w)) {
      if (!w->port_input && !w->port_output) {
        std::vector<RTLIL::Wire *> stmnt_w = group_statement_wires(w, orig);
        std::vector<RTLIL::Cell *> stmnt_c = group_statement_cells(w, orig);

        for (auto cl : stmnt_c)
          dont_tmrg.first.emplace(cl);

        for (auto p : stmnt_w) {
          // Add statement wires to dont tmrg list if it is private
          if (!obj_is_public(p->name))
            dont_tmrg.second.emplace(p);
          else
            voter_wires.emplace(p);
        }
      }
    }
  }

  /* Find wires that need a fanout or a voter */
  for (auto w : dont_tmrg.second) {
    if (w->port_output)
      voter_wires.emplace(w);
    if (w->port_input ||
        (!w->port_input && !w->port_output && obj_is_public(w->name)))
      fanout_wires.emplace(w);
  }

  /* Get list of connections */

  std::vector<std::pair<std::string, std::string>> conn_names;

  std::vector<RTLIL::SigSig> connection_list;
  for (auto con : orig->connections()) {
    RTLIL::SigSpec first;
    RTLIL::SigSpec second;

    for (auto s : {"A", "B", "C"}) {
      if (con.first.is_wire()) {
        RTLIL::Wire *wire = addWire(orig, con.first, s);
        first = wire;

      } else {
        RTLIL::SigSpec sig;
        for (auto c : con.first.chunks()) {
          if (c.wire == NULL) {
            for (auto state : c.data)
              sig.append(state);
          } else {
            RTLIL::Wire *wire = addWire(orig, c.wire, s);
            sig.append(RTLIL::SigChunk(wire, c.offset, c.width));
          }
        }
        first = sig;
      }

      if (con.second.is_wire()) {
        RTLIL::Wire *wire = addWire(orig, con.second, s);
        second = wire;

      } else {
        RTLIL::SigSpec sig;
        for (auto c : con.second.chunks()) {
          if (c.wire == NULL) {
            for (auto state : c.data)
              sig.append(state);
          } else {
            RTLIL::Wire *wire = addWire(orig, c.wire, s);
            sig.append(RTLIL::SigChunk(wire, c.offset, c.width));
          }
        }
        second = sig;
      }

      RTLIL::SigSig connection;
      connection.first = first;
      connection.second = second;
      connection_list.push_back(connection);
    }
  }

  /* Fixup ports */
  std::vector<std::string> port_names;
  for (auto p : orig->ports) {
    port_names.push_back(p.str());
  }

  for (auto w : port_names) {
    RTLIL::Wire *port = orig->wire(w);
    for (auto s : {"A", "B", "C"}) {
      RTLIL::Wire *wire;
      if (orig->wires_.count(w + s) == 0)
        wire = orig->addWire(w + s, port->width);
      else
        wire = orig->wire(w + s);
      wire->port_input = port->port_input;
      wire->port_output = port->port_output;
      orig->fixup_ports();
    }
    port->port_input = false;
    port->port_output = false;
    /* If wire is not on these lists, it is safe to delete */
    if (!voter_wires.count(port) && !dont_tmrg.second.count(port) &&
        !fanout_wires.count(port))
      remove_wires_list.emplace(port);
    orig->fixup_ports();
  }

  // orig->connections_.clear();
  /* Connecting Wires and Ports */
  for (auto pair : connection_list) {
    orig->connect(pair.first, pair.second);
  }

  // Add fanouts
  for (auto w : fanout_wires) {
    for (auto s : {"A", "B", "C"}) {
      RTLIL::Wire *wire = addWire(orig, w, s);
      if (wire->port_input)
        w->port_input = true;
      wire->port_input = false;
      w->port_output = false;
      wire->port_output = false;
      orig->fixup_ports();
      // Delete old connections that were in place of fanout input
    for(auto c = orig->connections_.begin(); c < orig->connections_.end(); c++)
      if(c->first == wire || c->second == wire)
        orig->connections_.erase(c);
    }

    RTLIL::Cell *fn = orig->addCell(w->name.str() + "_fanout", "\\fanout");
    dont_tmrg.first.emplace(fn);
    fn->setParam("\\WIDTH", 1);
    fn->setPort("\\in", w);
    for (auto s : {"A", "B", "C"})
      fn->setPort((std::string) "\\out" + s, orig->wire(w->name.str() + s));

  }

  // Adding voters
  for (auto w : voter_wires) {
    for (auto s : {"A", "B", "C"}) {
      RTLIL::Wire *wire = addWire(orig, w, s);
      if (wire->port_output) {
        w->port_output = true;
        w->port_input = false;
        wire->port_output = false;
        wire->port_input = false;
      }
      if (w->port_input) {
        wire->port_input = true;
        wire->port_output = false;
        w->port_input = false;
        w->port_output = false;
      }
      orig->fixup_ports();
    }

    RTLIL::Cell *vt = orig->addCell(w->name.str() + "_voter", "\\majorityVoter");
    dont_tmrg.first.emplace(vt);
    vt->setParam("\\WIDTH", 1);
    vt->setPort("\\out", w);
    for (auto s : {"A", "B", "C"})
      vt->setPort((std::string) "\\in" + s, orig->wire(w->name.str() + s));
    // Delete old connection that was connected to fanout output wire
    for(auto c = orig->connections_.begin(); c < orig->connections_.end(); c++)
      if(c->first == w || c->second == w)
        orig->connections_.erase(c);
  }

  /* Triplicate yosys cells */
  for (auto c : orig->selected_cells()) {
    if (c->type.str()[0] == '\\' || dont_tmrg.first.count(c)) {
      continue;
    } else {
      for (auto s : {"A", "B", "C"}) {
        RTLIL::Cell *cell = orig->addCell(NEW_ID, c->type);

        for (auto p : c->parameters) {
          cell->setParam(p.first, p.second.as_int());
        }
        for (auto p : c->connections()) {
          if (p.second.is_wire()) {
            RTLIL::Wire *wire = addWire(orig, p.second, s);
            cell->setPort(p.first.str(), wire);
          } else if (p.second.is_fully_const()) {
            RTLIL::SigSpec sig;
            sig.append(p.second.as_const());
            cell->setPort(p.first.str(), sig);
          } else {
            RTLIL::SigSpec sig;
            for (auto cn : p.second.chunks()) {
              if (cn.wire == NULL) {
                for (auto state : cn.data)
                  sig.append(state);
              } else {
                RTLIL::Wire *wire = addWire(orig, cn.wire, s);
                sig.append(RTLIL::SigChunk(wire, cn.offset, cn.width));
              }
            }
            cell->setPort(p.first.str(), sig);
          }
        }
      }
      orig->remove(c);
    }
  }

  /* Instantiate triplicated user modules as cells */
  for (auto c : orig->selected_cells()) {
    if (c->type.str()[0] == '\\' && c->type.str() != "\\majorityVoter" && c->type.str() != "\\fanout") {
      //
      RTLIL::Cell *newcell = orig->addCell(NEW_ID, c->type);
      for (auto p : c->connections()) {
        for (auto s : {"A", "B", "C"}) {

          if (p.second.is_wire()) {
            RTLIL::Wire *wire = addWire(orig, p.second, s);
            newcell->setPort(p.first.str() + s, wire);
          } else if (p.second.is_fully_const()) {
            RTLIL::SigSpec sig;
            sig.append(p.second.as_const());
            newcell->setPort(p.first.str() + s, sig);
          } else {
            RTLIL::SigSpec sig;
            for (auto cn : p.second.chunks()) {
              if (cn.wire == NULL) {
                for (auto state : cn.data)
                  sig.append(state);
              } else {
                RTLIL::Wire *wire = addWire(orig, cn.wire, s);
                sig.append(RTLIL::SigChunk(wire, cn.offset, cn.width));
              }
            }
            newcell->setPort(p.first.str() + s, sig);
          }
        }
      }
      std::string cell_name = c->name.str();
      orig->remove(c);
      orig->rename(newcell, cell_name);
    }
  }

  /* Remove old wires */
  for (auto w : remove_wires_list) {
    for (auto c = orig->connections_.begin(); c < orig->connections_.end(); c++) {
      if (c->first == w) {
        orig->connections_.erase(c);
      }
      if (c->second == w) {
        orig->connections_.erase(c);
      }
    }
  }

  orig->remove(remove_wires_list);

  return true;
}

struct TmrgPass : public Pass {
  TmrgPass() : Pass("tmrg_pass", "just a simple test") {}
  void execute(std::vector<std::string>, RTLIL::Design *design) override {

    log("#### Running TMRG PASS ####\n");
    std::pair<std::set<RTLIL::Cell *>, std::set<RTLIL::Wire *>> dont_tmrg_list;

    for (auto mod : design->selected_modules()) {
      //   // TODO USE SELECTION to exclude voter and fanout???
      if (mod->name.str() != "\\majorityVoter" && mod->name.str() != "\\fanout")
        TMRModule(mod, dont_tmrg_list);
    }

    // run_pass("opt_clean");
    // run_pass("rmports");
    // run_pass("opt_clean");
  }
} TmrgPass;

PRIVATE_NAMESPACE_END
