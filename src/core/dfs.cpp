// dfs.cpp — DataFirst Script (.dfs) interpreter
// =============================================================================
#include "dfs.hpp"
#include "globals.hpp"
#include "exec.hpp"
#include "strutil.hpp"
#include "debug.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

// =============================================================================
//  Data model
//
//  A DfsTable is a list of rows. Each row is a map of field->value strings.
//  The schema (field order) is stored separately so output columns are stable.
// =============================================================================

struct DfsTable {
    std::vector<std::string>              schema;  // column order
    std::vector<std::map<std::string,std::string>> rows;

    bool empty() const { return rows.empty(); }
    size_t size() const { return rows.size(); }

    void addRow(const std::vector<std::string>& values) {
        std::map<std::string,std::string> row;
        for (size_t i = 0; i < schema.size() && i < values.size(); ++i)
            row[schema[i]] = values[i];
        // Extra columns beyond schema get numbered names
        for (size_t i = schema.size(); i < values.size(); ++i) {
            std::string col = "col" + std::to_string(i);
            if (std::find(schema.begin(),schema.end(),col) == schema.end())
                schema.push_back(col);
            row[col] = values[i];
        }
        rows.push_back(std::move(row));
    }

    std::string field(const std::map<std::string,std::string>& row,
                      const std::string& name) const {
        auto it = row.find(name);
        if (it != row.end()) return it->second;
        // Try col0, col1... by position
        for (size_t i = 0; i < schema.size(); ++i)
            if (schema[i] == name) { auto it2=row.find(schema[i]); return it2!=row.end()?it2->second:""; }
        return "";
    }

    void print() const {
        for (auto& row : rows) {
            bool first = true;
            for (auto& col : schema) {
                if (!first) std::cout << '\t';
                auto it = row.find(col);
                std::cout << (it != row.end() ? it->second : "");
                first = false;
            }
            std::cout << '\n';
        }
    }
};

// =============================================================================
//  Capture shell command output into DfsTable
// =============================================================================

static DfsTable dfsCaptureCommand(const std::string& cmd) {
    DfsTable tbl;
    FILE* f = popen(cmd.c_str(), "r");
    if (!f) { std::cerr << "[dfs] popen failed: " << cmd << "\n"; return tbl; }

    char buf[4096];
    std::string acc;
    while (fgets(buf, sizeof(buf), f)) acc += buf;
    pclose(f);

    // Parse into rows: split by newline, each line into fields by whitespace
    std::istringstream ss(acc);
    std::string line;
    bool first = true;
    while (std::getline(ss, line)) {
        std::vector<std::string> fields;
        std::istringstream ls(line);
        std::string fld;
        while (ls >> fld) fields.push_back(fld);
        if (fields.empty()) continue;

        if (first) {
            // Heuristic: if the first row looks like a header (all-alpha, no ./ chars), use it
            bool looksHeader = true;
            for (auto& f2 : fields)
                if (f2.find('/') != std::string::npos || std::isdigit((unsigned char)f2[0]))
                    { looksHeader = false; break; }
            if (looksHeader) {
                tbl.schema = fields;
                first = false;
                continue;
            }
            // No header — auto-generate schema
            for (size_t i=0;i<fields.size();++i)
                tbl.schema.push_back("col"+std::to_string(i));
            first = false;
        }
        tbl.addRow(fields);
    }
    return tbl;
}

[[maybe_unused]] static DfsTable fromLines(const std::string& text) {
    DfsTable tbl;
    tbl.schema = {"line"};
    std::istringstream ss(text);
    std::string ln;
    while (std::getline(ss, ln)) {
        if (!ln.empty()) tbl.rows.push_back({{"line", ln}});
    }
    return tbl;
}

// =============================================================================
//  Verb implementations
// =============================================================================

static bool compareOp(const std::string& cell, const std::string& op, const std::string& val) {
    if (op == "~=") {
        try { return std::regex_search(cell, std::regex(val)); } catch (...) { return false; }
    }
    double cv=0, fv=0; bool num=false;
    try { cv=std::stod(cell); fv=std::stod(val); num=true; } catch (...) {}
    if (num) {
        if (op=="==") return cv==fv;
        if (op=="!=") return cv!=fv;
        if (op==">") return cv>fv;
        if (op=="<")  return cv<fv;
        if (op==">=") return cv>=fv;
        if (op=="<=") return cv<=fv;
    } else {
        if (op=="==") return cell==val;
        if (op!="!=") return cell!=val;
    }
    return false;
}

// verb: filter <field> <op> <value>
static DfsTable verbFilter(const DfsTable& in, const std::vector<std::string>& args) {
    if (args.size() < 3) { std::cerr << "[dfs] filter needs: field op value\n"; return in; }
    const std::string& field = args[0];
    const std::string& op    = args[1];
    const std::string& val   = args[2];
    DfsTable out; out.schema = in.schema;
    for (auto& row : in.rows)
        if (compareOp(in.field(row, field), op, val)) out.rows.push_back(row);
    return out;
}

// verb: sort <field> [asc|desc]
static DfsTable verbSort(const DfsTable& in, const std::vector<std::string>& args) {
    if (args.empty()) return in;
    const std::string& field = args[0];
    bool desc = (args.size() >= 2 && (args[1]=="desc" || args[1]=="DESC"));
    DfsTable out = in;
    std::stable_sort(out.rows.begin(), out.rows.end(),
        [&](const auto& a, const auto& b) {
            std::string av = in.field(a, field), bv = in.field(b, field);
            double an=0, bn=0; bool num=false;
            try { an=std::stod(av); bn=std::stod(bv); num=true; } catch (...) {}
            bool less = num ? an < bn : av < bv;
            return desc ? !less : less;
        });
    return out;
}

// verb: take <n>
static DfsTable verbTake(const DfsTable& in, const std::vector<std::string>& args) {
    if (args.empty()) return in;
    size_t n = (size_t)std::atoi(args[0].c_str());
    DfsTable out; out.schema = in.schema;
    for (size_t i = 0; i < std::min(n, in.size()); ++i) out.rows.push_back(in.rows[i]);
    return out;
}

// verb: skip <n>
static DfsTable verbSkip(const DfsTable& in, const std::vector<std::string>& args) {
    if (args.empty()) return in;
    size_t n = (size_t)std::atoi(args[0].c_str());
    DfsTable out; out.schema = in.schema;
    for (size_t i = n; i < in.size(); ++i) out.rows.push_back(in.rows[i]);
    return out;
}

// verb: count  (terminal — prints and returns empty)
static DfsTable verbCount(const DfsTable& in, const std::vector<std::string>&) {
    std::cout << in.size() << "\n";
    return DfsTable{};
}

// verb: unique <field>
static DfsTable verbUnique(const DfsTable& in, const std::vector<std::string>& args) {
    std::string field = args.empty() ? (in.schema.empty() ? "" : in.schema[0]) : args[0];
    DfsTable out; out.schema = in.schema;
    std::map<std::string,bool> seen;
    for (auto& row : in.rows) {
        std::string v = in.field(row, field);
        if (!seen[v]) { seen[v]=true; out.rows.push_back(row); }
    }
    return out;
}

// verb: format <template>  — e.g. "{NAME} runs as {PID}"
static DfsTable verbFormat(const DfsTable& in, const std::vector<std::string>& args) {
    if (args.empty()) { in.print(); return DfsTable{}; }
    std::string tmpl = args[0];
    for (auto& row : in.rows) {
        std::string out = tmpl;
        for (auto& [k,v] : row) {
            std::string ph = "{" + k + "}";
            size_t p;
            while ((p = out.find(ph)) != std::string::npos) out.replace(p, ph.size(), v);
        }
        std::cout << out << "\n";
    }
    return DfsTable{};
}

// verb: sum <field>
static DfsTable verbSum(const DfsTable& in, const std::vector<std::string>& args) {
    if (args.empty()) return in;
    const std::string& field = args[0];
    double s = 0;
    for (auto& row : in.rows) try { s += std::stod(in.field(row,field)); } catch(...) {}
    std::cout << s << "\n";
    return DfsTable{};
}

// verb: avg <field>
static DfsTable verbAvg(const DfsTable& in, const std::vector<std::string>& args) {
    if (args.empty() || in.empty()) return in;
    const std::string& field = args[0];
    double s = 0; size_t n = 0;
    for (auto& row : in.rows) try { s += std::stod(in.field(row,field)); ++n; } catch(...) {}
    std::cout << (n ? s/n : 0.0) << "\n";
    return DfsTable{};
}

// verb: max / min <field>
static DfsTable verbMax(const DfsTable& in, const std::vector<std::string>& args, bool doMax) {
    if (args.empty() || in.empty()) return in;
    const std::string& field = args[0];
    double best = doMax ? -1e300 : 1e300;
    for (auto& row : in.rows) try {
        double v = std::stod(in.field(row,field));
        if (doMax ? v>best : v<best) best=v;
    } catch(...) {}
    std::cout << best << "\n";
    return DfsTable{};
}

// verb: split — split each row's first field into words as new rows
static DfsTable verbSplit(const DfsTable& in, const std::vector<std::string>&) {
    DfsTable out; out.schema = {"word"};
    for (auto& row : in.rows) {
        std::string line = in.field(row, in.schema.empty() ? "" : in.schema[0]);
        std::istringstream ss(line);
        std::string w;
        while (ss >> w) out.rows.push_back({{"word", w}});
    }
    return out;
}

// verb: lines — convert raw multi-line field to one-row-per-line
static DfsTable verbLines(const DfsTable& in, const std::vector<std::string>&) {
    DfsTable out; out.schema = {"line"};
    for (auto& row : in.rows) {
        std::string all;
        for (auto& col : in.schema) { all += in.field(row,col); all += " "; }
        std::istringstream ss(all);
        std::string ln;
        while (std::getline(ss,ln)) if (!trim(ln).empty()) out.rows.push_back({{"line",trim(ln)}});
    }
    return out;
}

// verb: first / last
static DfsTable verbFirst(const DfsTable& in, const std::vector<std::string>&) {
    DfsTable out; out.schema = in.schema;
    if (!in.empty()) out.rows.push_back(in.rows.front());
    return out;
}
static DfsTable verbLast(const DfsTable& in, const std::vector<std::string>&) {
    DfsTable out; out.schema = in.schema;
    if (!in.empty()) out.rows.push_back(in.rows.back());
    return out;
}

// verb: select <col1> [col2 ...]
static DfsTable verbSelect(const DfsTable& in, const std::vector<std::string>& args) {
    if (args.empty()) return in;
    DfsTable out; out.schema = args;
    for (auto& row : in.rows) {
        std::map<std::string,std::string> r;
        for (auto& col : args) r[col] = in.field(row, col);
        out.rows.push_back(r);
    }
    return out;
}

// =============================================================================
//  Pipeline parser and executor
// =============================================================================

struct DfsVerb {
    std::string              name;
    std::vector<std::string> args;
};

// Split a pipeline line into stages, respecting quoted strings
static std::vector<std::string> splitPipeStages(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool inq = false; char qc = 0;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (!inq && (c=='"'||c=='\'')) { inq=true; qc=c; cur+=c; continue; }
        if (inq && c==qc) { inq=false; cur+=c; continue; }
        if (!inq && c=='|') { out.push_back(cur); cur.clear(); continue; }
        cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// Parse a verb and its args from a stage string (after the first stage)
static DfsVerb parseVerb(const std::string& stage) {
    DfsVerb v;
    std::istringstream ss(trim(stage));
    ss >> v.name;
    std::string a;
    while (ss >> a) v.args.push_back(a);
    return v;
}

// Execute one pipeline line
static DfsTable execPipeline(const std::string& line,
                              std::map<std::string,DfsTable>& vars) {
    auto stages = splitPipeStages(line);
    if (stages.empty()) return DfsTable{};

    std::string head = trim(stages[0]);

    // $var reference
    DfsTable tbl;
    if (!head.empty() && head[0] == '$') {
        std::string vname = head.substr(1);
        auto it = vars.find(vname);
        tbl = (it != vars.end()) ? it->second : DfsTable{};
    } else {
        tbl = dfsCaptureCommand(head);
    }

    // Apply verbs
    for (size_t i = 1; i < stages.size(); ++i) {
        DfsVerb v = parseVerb(stages[i]);
        if (v.name.empty()) continue;

        if (v.name == "filter")  tbl = verbFilter(tbl, v.args);
        else if (v.name == "sort")    tbl = verbSort(tbl, v.args);
        else if (v.name == "take")    tbl = verbTake(tbl, v.args);
        else if (v.name == "skip")    tbl = verbSkip(tbl, v.args);
        else if (v.name == "count")   tbl = verbCount(tbl, v.args);
        else if (v.name == "unique")  tbl = verbUnique(tbl, v.args);
        else if (v.name == "format")  tbl = verbFormat(tbl, v.args);
        else if (v.name == "sum")     tbl = verbSum(tbl, v.args);
        else if (v.name == "avg")     tbl = verbAvg(tbl, v.args);
        else if (v.name == "max")     tbl = verbMax(tbl, v.args, true);
        else if (v.name == "min")     tbl = verbMax(tbl, v.args, false);
        else if (v.name == "split")   tbl = verbSplit(tbl, v.args);
        else if (v.name == "lines")   tbl = verbLines(tbl, v.args);
        else if (v.name == "first")   tbl = verbFirst(tbl, v.args);
        else if (v.name == "last")    tbl = verbLast(tbl, v.args);
        else if (v.name == "select")  tbl = verbSelect(tbl, v.args);
        else {
            std::cerr << "[dfs] unknown verb: " << v.name << "\n";
        }
    }
    return tbl;
}

// =============================================================================
//  Script runner
// =============================================================================

int runDfs(const std::string& path, const std::vector<std::string>& args) {
    std::ifstream f(path);
    if (!f) { std::cerr << path << ": " << std::strerror(errno) << "\n"; return 1; }

    std::map<std::string,DfsTable> vars;
    // Expose positional args as $1, $2... in a pseudo-table
    for (size_t i=0;i<args.size();++i) {
        DfsTable t; t.schema={"value"};
        t.rows.push_back({{"value",args[i]}});
        vars[std::to_string(i+1)] = t;
    }

    int lineNo = 0, rc = 0;
    std::string line;
    while (std::getline(f, line)) {
        ++lineNo;
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;

        // set <name> = <pipeline>
        if (t.rfind("set ", 0) == 0) {
            std::string rest = trim(t.substr(4));
            size_t eq = rest.find('=');
            if (eq == std::string::npos) {
                std::cerr << "[dfs] line " << lineNo << ": set needs '='\n";
                rc = 1; continue;
            }
            std::string vname = trim(rest.substr(0, eq));
            std::string pipeline = trim(rest.substr(eq+1));
            vars[vname] = execPipeline(pipeline, vars);
            continue;
        }

        // echo / print — forward to shell
        if (t.rfind("echo ", 0) == 0 || t == "echo") {
            rc = runPipeline(t);
            continue;
        }

        // Regular pipeline line
        DfsTable result = execPipeline(t, vars);
        if (!result.empty()) result.print();
    }
    return rc;
}
