#include "kernel/sigtools.h"
#include "kernel/yosys.h"
#include <cstddef>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

struct TmrgPass : public Pass {
  std::pair<std::set<RTLIL::Cell *>, std::set<RTLIL::Wire *>> dont_tmrg;
  pool<RTLIL::Wire *> remove_wires_list;
  std::set<RTLIL::Wire *> voter_wires;
  std::set<RTLIL::Wire *> fanout_wires;

  bool TMRModule(RTLIL::Module *orig);
  bool addVoter(RTLIL::Module *mod, RTLIL::Wire *w);
  bool addFanout(RTLIL::Module *mod, RTLIL::Wire *w);

  TmrgPass() : Pass("tmrg_pass", "just a simple test") {}
  void execute(std::vector<std::string>, RTLIL::Design *design) override {

    log("#### Running TMRG PASS ####\n");

    std::map<RTLIL::IdString, std::map<RTLIL::IdString, std::string>>
        cell_attributes;
    for (auto mod : design->selected_modules()) {
      for (auto attr : mod->attributes)
        cell_attributes[mod->name][attr.first] =
            mod->get_string_attribute(attr.first);
    }
    for (auto mod : design->selected_modules()) {
      for (auto c : mod->cells())
        if (c->type.str()[0] == '\\' && c->type.str() != "\\majorityVoter" &&
            c->type.str() != "\\fanout")
          for (auto attr : cell_attributes[c->type])
            c->set_string_attribute(attr.first, attr.second);
    }

    for (auto mod : design->selected_modules()) {
      //   // TODO USE SELECTION to exclude voter and fanout???
      if (mod->name.str() != "\\majorityVoter" && mod->name.str() != "\\fanout")
        TMRModule(mod);
    }

    // run_pass("opt_clean");
    // run_pass("rmports");
    // run_pass("opt_clean");
  }
} TmrgPass;

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

template <class T>
pool<std::string> get_string_attributes(T *obj, std::string attr) {
  if (obj->has_attribute(attr)) {
    return obj->get_strpool_attribute(attr);
  }
  return pool<std::string>();
}

bool is_port_triplicated(RTLIL::IdString p, RTLIL::Cell *c) {
  pool<std::string> cell_attr =
      get_string_attributes(c, "\\tmrg_do_not_triplicate");
  if (cell_attr.size() == 0)
    return true;

  if (cell_attr.count(p.str().substr(1)) >
      0) // ignore first character (private, public)
    return false;

  return true;
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

bool TmrgPass::addFanout(RTLIL::Module *mod, RTLIL::Wire *w) {
  remove_wires_list.erase(w);
  fanout_wires.emplace(w);
  if(voter_wires.count(w))
      return false;

  for (auto s : {"A", "B", "C"}) {
    RTLIL::Wire *wire = addWire(mod, w, s);
    if (wire->port_input)
      w->port_input = true;
    wire->port_input = false;
    w->port_output = false;
    wire->port_output = false;
    mod->fixup_ports();
    // Delete old connections that were in place of fanout input
    for (auto c = mod->connections_.begin(); c < mod->connections_.end(); c++)
      if (c->first == wire || c->second == wire)
        mod->connections_.erase(c);
  }

  RTLIL::Cell *fn = mod->addCell(w->name.str() + "_fanout", "\\fanout");
  dont_tmrg.first.emplace(fn);
  fn->setParam("\\WIDTH", 1);
  fn->setPort("\\in", w);
  for (auto s : {"A", "B", "C"})
    fn->setPort((std::string) "\\out" + s, mod->wire(w->name.str() + s));
  return true;
}

bool TmrgPass::addVoter(RTLIL::Module *mod, RTLIL::Wire *w) {
  remove_wires_list.erase(w);
  voter_wires.emplace(w);
  if (fanout_wires.count(w))
    return false;
  for (auto s : {"A", "B", "C"}) {
    RTLIL::Wire *wire = addWire(mod, w, s);
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
    mod->fixup_ports();
  }

  RTLIL::Cell *vt = mod->addCell(w->name.str() + "_voter", "\\majorityVoter");
  dont_tmrg.first.emplace(vt);
  vt->setParam("\\WIDTH", 1);
  vt->setPort("\\out", w);
  for (auto s : {"A", "B", "C"})
    vt->setPort((std::string) "\\in" + s, mod->wire(w->name.str() + s));
  // Delete old connection that was connected to fanout output wire
  for (auto c = mod->connections_.begin(); c < mod->connections_.end(); c++)
    if (c->first == w || c->second == w)
      mod->connections_.erase(c);
  return true;
}

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

RTLIL::SigSpec tmr_SigSpec(RTLIL::Module *mod, RTLIL::SigSpec sg, std::string s,
                           pool<RTLIL::Wire *> rm_wl) {
  RTLIL::SigSpec sig;
  for (auto chunk : sg.chunks()) {
    if (chunk.wire == NULL) {
      for (auto state : chunk.data)
        sig.append(state);
    } else {
      RTLIL::Wire *wire = addWire(mod, chunk.wire, s);
      sig.append(RTLIL::SigChunk(wire, chunk.offset, chunk.width));
      rm_wl.emplace(wire);
    }
  }
  return sig;
}

bool TmrgPass::TMRModule(RTLIL::Module *orig) {

  dont_tmrg.first.clear();
  dont_tmrg.second.clear();
  remove_wires_list.clear();
  voter_wires.clear();
  fanout_wires.clear();

  /* Get a list of do not tmrg from attribute */
  pool<std::string> tmrg_do_not_triplicate_attr =
      get_string_attributes(orig, "\\tmrg_do_not_triplicate");
  for (auto s : tmrg_do_not_triplicate_attr) {
    dont_tmrg.second.emplace(orig->wire("\\" + s));
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

  std::vector<RTLIL::SigSig> connection_list;

  for (auto con : orig->connections()) {

    for (auto s : {"A", "B", "C"}) {
      RTLIL::SigSig connection;
      connection.first = tmr_SigSpec(orig, con.first, s, remove_wires_list);
      connection.second = tmr_SigSpec(orig, con.second, s, remove_wires_list);
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
    if(fanout_wires.count(port))
        continue;
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

  /* Connecting Wires and Ports */
  for (auto pair : connection_list) {
    orig->connect(pair.first, pair.second);
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
          RTLIL::SigSpec sig;
          if (p.second.is_fully_const()) {
            sig.append(p.second.as_const());
            cell->setPort(p.first.str(), sig);
          } else {
            for (auto cn : p.second.chunks()) {
              if (cn.wire == NULL) {
                for (auto state : cn.data)
                  sig.append(state);
              } else {
                RTLIL::Wire *wire = addWire(orig, cn.wire, s);
                sig.append(RTLIL::SigChunk(wire, cn.offset, cn.width));
                if (!dont_tmrg.second.count(cn.wire) &&
                    !voter_wires.count(cn.wire) && !fanout_wires.count(cn.wire))
                  remove_wires_list.emplace(cn.wire);
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
    if (c->type.str()[0] == '\\' && c->type.str() != "\\majorityVoter" &&
        c->type.str() != "\\fanout") {
      //
      RTLIL::Cell *newcell = orig->addCell(NEW_ID, c->type);
      get_string_attributes(c, "\\tmrg_do_not_triplicate");
      for (auto p : c->connections()) {
        if (is_port_triplicated(p.first, c) == false) {
          if (c->input(p.first))
              voter_wires.emplace(p.second.as_wire());
          else if (c->output(p.first))
              fanout_wires.emplace(p.second.as_wire());
          newcell->setPort(p.first, p.second);
          continue;
        }

        for (auto s : {"A", "B", "C"}) {
          RTLIL::SigSpec sig;
          if (p.second.is_fully_const()) {
            sig.append(p.second.as_const());
            newcell->setPort(p.first.str() + s, sig);
          } else {
            for (auto cn : p.second.chunks()) {
              if (cn.wire == NULL) {
                for (auto state : cn.data)
                  sig.append(state);
              } else {
                RTLIL::Wire *wire = addWire(orig, cn.wire, s);
                sig.append(RTLIL::SigChunk(wire, cn.offset, cn.width));
                if (!dont_tmrg.second.count(cn.wire) &&
                    !voter_wires.count(cn.wire) && !fanout_wires.count(cn.wire))
                  remove_wires_list.emplace(cn.wire);
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

  // Add fanouts
  for (auto w : fanout_wires) {
    addFanout(orig, w);
  }
  // Adding voters
  for (auto w : voter_wires) {
    addVoter(orig, w);
  }

  /* Remove old connections */

  for (auto c = orig->connections_.begin(); c < orig->connections_.end(); c++) {
    bool delete_conn = false;
    if (c->first.is_wire())
      if (remove_wires_list.count(c->first.as_wire()))
        delete_conn = true;

    if (c->second.is_wire())
      if (remove_wires_list.count(c->second.as_wire()))
        delete_conn = true;

    if (!c->first.is_wire()) {
      for (auto chunk : c->first.chunks()) {
        if (chunk.wire != NULL)
          if (remove_wires_list.count(chunk.wire))
            delete_conn = true;
      }
    }

    if (!c->second.is_wire()) {
      for (auto chunk : c->second.chunks()) {
        if (chunk.wire != NULL)
          if (remove_wires_list.count(chunk.wire))
            delete_conn = true;
      }
    }

    if (delete_conn) {
      orig->connections_.erase(c);
      c--;
    }
  }

  orig->remove(remove_wires_list);

  return true;
}

PRIVATE_NAMESPACE_END
