// stsc.cpp — Structured Script (.stsc) interpreter
// =============================================================================
//  A small, self-contained interpreter that integrates with TShell.
//  Commands that aren't recognized as STSC statements are forwarded to
//  the shell's runPipeline() so ordinary shell commands still work.
// =============================================================================
#include "stsc.hpp"
#include "globals.hpp"
#include "exec.hpp"
#include "strutil.hpp"
#include "debug.hpp"

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <glob.h>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <variant>

namespace fs = std::filesystem;

// =============================================================================
//  Value type
// =============================================================================

struct StscValue {
    enum class Kind { Nil, Int, Float, Bool, Str, List } kind = Kind::Nil;

    long long              ival = 0;
    double                 fval = 0.0;
    bool                   bval = false;
    std::string            sval;
    std::vector<StscValue> lval;

    // ── Constructors ──────────────────────────────────────────────────────
    static StscValue nil()                       { return StscValue{}; }
    static StscValue from(long long v)           { StscValue x; x.kind=Kind::Int;   x.ival=v; return x; }
    static StscValue from(double v)              { StscValue x; x.kind=Kind::Float; x.fval=v; return x; }
    static StscValue from(bool v)                { StscValue x; x.kind=Kind::Bool;  x.bval=v; return x; }
    static StscValue from(const std::string& v)  { StscValue x; x.kind=Kind::Str;   x.sval=v; return x; }
    static StscValue fromList(std::vector<StscValue> v) {
        StscValue x; x.kind=Kind::List; x.lval=std::move(v); return x;
    }

    bool isNil()   const { return kind == Kind::Nil;   }
    bool isInt()   const { return kind == Kind::Int;   }
    bool isFloat() const { return kind == Kind::Float; }
    bool isBool()  const { return kind == Kind::Bool;  }
    bool isStr()   const { return kind == Kind::Str;   }
    bool isList()  const { return kind == Kind::List;  }

    // Truthy: non-zero int/float, true bool, non-empty string/list
    bool truthy() const {
        switch (kind) {
            case Kind::Nil:   return false;
            case Kind::Int:   return ival != 0;
            case Kind::Float: return fval != 0.0;
            case Kind::Bool:  return bval;
            case Kind::Str:   return !sval.empty();
            case Kind::List:  return !lval.empty();
        }
        return false;
    }

    // Coerce to string
    std::string str() const {
        switch (kind) {
            case Kind::Nil:   return "";
            case Kind::Int:   return std::to_string(ival);
            case Kind::Float: {
                std::ostringstream s; s << fval; return s.str();
            }
            case Kind::Bool:  return bval ? "true" : "false";
            case Kind::Str:   return sval;
            case Kind::List: {
                std::string out;
                for (size_t i=0;i<lval.size();++i){
                    if(i) out+="\n";
                    out+=lval[i].str();
                }
                return out;
            }
        }
        return "";
    }

    // Coerce to integer
    long long toInt() const {
        switch (kind) {
            case Kind::Int:   return ival;
            case Kind::Float: return (long long)fval;
            case Kind::Bool:  return bval ? 1 : 0;
            case Kind::Str:   try { return std::stoll(sval); } catch (...) { return 0; }
            default:          return 0;
        }
    }

    // Coerce to float
    double toFloat() const {
        switch (kind) {
            case Kind::Int:   return (double)ival;
            case Kind::Float: return fval;
            case Kind::Bool:  return bval ? 1.0 : 0.0;
            case Kind::Str:   try { return std::stod(sval); } catch (...) { return 0.0; }
            default:          return 0.0;
        }
    }

    size_t len() const {
        if (isList()) return lval.size();
        if (isStr())  return sval.size();
        return 0;
    }
};

// =============================================================================
//  Environment (scoped variable map)
// =============================================================================

struct StscEnv {
    std::map<std::string, StscValue> vars;
    StscEnv* parent = nullptr;

    StscValue get(const std::string& name) const {
        auto it = vars.find(name);
        if (it != vars.end()) return it->second;
        if (parent) return parent->get(name);
        return StscValue::nil();
    }

    void set(const std::string& name, StscValue v) {
        // Update in nearest scope that has it, else set here
        StscEnv* e = this;
        while (e) {
            auto it = e->vars.find(name);
            if (it != e->vars.end()) { it->second = std::move(v); return; }
            e = e->parent;
        }
        vars[name] = std::move(v);
    }

    void setLocal(const std::string& name, StscValue v) {
        vars[name] = std::move(v);
    }
};

// =============================================================================
//  Lexer
// =============================================================================

enum class TokKind {
    // Literals
    Int, Float, Bool, Str, Ident,
    // Punctuation
    LParen, RParen, LBrace, RBrace, LBracket, RBracket,
    Comma, Dot, Pipe, Equals, Semicolon, Newline,
    // Operators
    Plus, Minus, Star, Slash, Percent,
    EqEq, BangEq, Lt, Gt, LtEq, GtEq,
    AmpAmp, PipePipe, Bang,
    TildeEq,        // ~=  regex match
    // Special
    Eof, Error
};

struct Token {
    TokKind     kind;
    std::string text;
    int         line = 0;
};

class Lexer {
public:
    explicit Lexer(const std::string& src) : src_(src) {}

    std::vector<Token> tokenize() {
        std::vector<Token> out;
        while (pos_ < src_.size()) {
            skip_ws_and_comments();
            if (pos_ >= src_.size()) break;
            char c = src_[pos_];
            if (c == '\n') { out.push_back({TokKind::Newline, "\n", line_}); ++pos_; ++line_; continue; }
            Token t = nextToken();
            if (t.kind != TokKind::Error) out.push_back(t);
        }
        out.push_back({TokKind::Eof, "", line_});
        return out;
    }

private:
    std::string src_;
    size_t pos_ = 0;
    int    line_ = 1;

    char peek(size_t off=0) const {
        return (pos_+off < src_.size()) ? src_[pos_+off] : '\0';
    }
    char advance() { return src_[pos_++]; }

    void skip_ws_and_comments() {
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (c == ' ' || c == '\t' || c == '\r') { ++pos_; continue; }
            if (c == '#') { while (pos_ < src_.size() && src_[pos_] != '\n') ++pos_; continue; }
            break;
        }
    }

    Token nextToken() {
        char c = src_[pos_];
        int ln = line_;

        // String literal
        if (c == '"' || c == '\'') {
            char q = advance();
            std::string s;
            while (pos_ < src_.size() && src_[pos_] != q) {
                if (src_[pos_] == '\\' && pos_+1 < src_.size()) {
                    ++pos_;
                    switch (src_[pos_]) {
                        case 'n':  s += '\n'; break;
                        case 't':  s += '\t'; break;
                        default:   s += src_[pos_]; break;
                    }
                    ++pos_;
                } else {
                    s += src_[pos_++];
                }
            }
            if (pos_ < src_.size()) ++pos_; // closing quote
            return {TokKind::Str, s, ln};
        }

        // Number
        if (std::isdigit(c) || (c == '-' && std::isdigit(peek(1)))) {
            std::string s;
            if (c == '-') s += advance();
            while (pos_ < src_.size() && std::isdigit(src_[pos_])) s += advance();
            bool isf = false;
            if (pos_ < src_.size() && src_[pos_] == '.') {
                isf = true; s += advance();
                while (pos_ < src_.size() && std::isdigit(src_[pos_])) s += advance();
            }
            return {isf ? TokKind::Float : TokKind::Int, s, ln};
        }

        // Identifier / keyword / bool
        if (std::isalpha(c) || c == '_' || c == '$') {
            std::string s;
            if (c == '$') { advance(); } // $var -> var
            while (pos_ < src_.size() && (std::isalnum(src_[pos_]) || src_[pos_] == '_')) s += advance();
            if (s == "true" || s == "false") return {TokKind::Bool, s, ln};
            return {TokKind::Ident, s, ln};
        }

        // Two-char operators
        auto peek2 = [&](char a, char b) { return c == a && peek(1) == b; };
        if (peek2('=','=')) { pos_+=2; return {TokKind::EqEq,   "==", ln}; }
        if (peek2('!','=')) { pos_+=2; return {TokKind::BangEq, "!=", ln}; }
        if (peek2('<','=')) { pos_+=2; return {TokKind::LtEq,   "<=", ln}; }
        if (peek2('>','=')) { pos_+=2; return {TokKind::GtEq,   ">=", ln}; }
        if (peek2('&','&')) { pos_+=2; return {TokKind::AmpAmp, "&&", ln}; }
        if (peek2('|','|')) { pos_+=2; return {TokKind::PipePipe,"||",ln}; }
        if (peek2('~','=')) { pos_+=2; return {TokKind::TildeEq,"~=", ln}; }

        ++pos_;
        switch (c) {
            case '(':  return {TokKind::LParen,   "(", ln};
            case ')':  return {TokKind::RParen,   ")", ln};
            case '{':  return {TokKind::LBrace,   "{", ln};
            case '}':  return {TokKind::RBrace,   "}", ln};
            case '[':  return {TokKind::LBracket, "[", ln};
            case ']':  return {TokKind::RBracket, "]", ln};
            case ',':  return {TokKind::Comma,    ",", ln};
            case '.':  return {TokKind::Dot,      ".", ln};
            case '|':  return {TokKind::Pipe,     "|", ln};
            case '=':  return {TokKind::Equals,   "=", ln};
            case ';':  return {TokKind::Semicolon,";", ln};
            case '+':  return {TokKind::Plus,     "+", ln};
            case '-':  return {TokKind::Minus,    "-", ln};
            case '*':  return {TokKind::Star,     "*", ln};
            case '/':  return {TokKind::Slash,    "/", ln};
            case '%':  return {TokKind::Percent,  "%", ln};
            case '<':  return {TokKind::Lt,       "<", ln};
            case '>':  return {TokKind::Gt,       ">", ln};
            case '!':  return {TokKind::Bang,     "!", ln};
            default:   return {TokKind::Error,    std::string(1,c), ln};
        }
    }
};

// =============================================================================
//  Forward declarations
// =============================================================================
class StscInterp;
struct StscBreak  {};
struct StscReturn { StscValue value; };

// =============================================================================
//  Built-in functions
// =============================================================================

static StscValue builtinGlob(const std::vector<StscValue>& args) {
    if (args.empty()) return StscValue::nil();
    std::string pat = args[0].str();
    glob_t g{};
    std::vector<StscValue> out;
    if (::glob(pat.c_str(), GLOB_TILDE, nullptr, &g) == 0) {
        for (size_t i = 0; i < g.gl_pathc; ++i)
            out.push_back(StscValue::from(std::string(g.gl_pathv[i])));
    }
    ::globfree(&g);
    return StscValue::fromList(std::move(out));
}

static StscValue builtinLen(const std::vector<StscValue>& args) {
    if (args.empty()) return StscValue::from((long long)0);
    return StscValue::from((long long)args[0].len());
}

static StscValue builtinStr(const std::vector<StscValue>& args) {
    if (args.empty()) return StscValue::from(std::string{});
    return StscValue::from(args[0].str());
}

static StscValue builtinInt(const std::vector<StscValue>& args) {
    if (args.empty()) return StscValue::from((long long)0);
    return StscValue::from(args[0].toInt());
}

static StscValue builtinSplit(const std::vector<StscValue>& args) {
    if (args.empty()) return StscValue::fromList({});
    std::string s   = args[0].str();
    std::string del = (args.size() >= 2) ? args[1].str() : " ";
    std::vector<StscValue> out;
    size_t p = 0, f = 0;
    while ((f = s.find(del, p)) != std::string::npos) {
        out.push_back(StscValue::from(s.substr(p, f-p)));
        p = f + del.size();
    }
    out.push_back(StscValue::from(s.substr(p)));
    return StscValue::fromList(std::move(out));
}

static StscValue builtinRange(const std::vector<StscValue>& args) {
    long long start = 0, end_ = 0, step = 1;
    if (args.size() == 1)      { end_  = args[0].toInt(); }
    else if (args.size() >= 2) { start = args[0].toInt(); end_ = args[1].toInt(); }
    if (args.size() >= 3)      { step  = args[2].toInt(); if (!step) step = 1; }
    std::vector<StscValue> out;
    for (long long i = start; i < end_; i += step)
        out.push_back(StscValue::from(i));
    return StscValue::fromList(std::move(out));
}

// =============================================================================
//  Pipeline filter helpers for STSC
// =============================================================================

// A "row" in a pipeline is a list of fields (strings).
using PipeRow  = std::vector<std::string>;
using PipeData = std::vector<PipeRow>;

// Split a line of output into named fields.
// Heuristic: for output like "proc cpu mem", detect column count.
static PipeData captureRows(const std::string& cmd) {
    // Run command via shell, capture output
    std::string result;
    FILE* f = popen(cmd.c_str(), "r");
    if (!f) return {};
    char buf[512];
    while (fgets(buf, sizeof(buf), f)) result += buf;
    pclose(f);

    PipeData rows;
    std::istringstream ss(result);
    std::string line;
    while (std::getline(ss, line)) {
        PipeRow row;
        std::istringstream ls(line);
        std::string field;
        while (ls >> field) row.push_back(field);
        if (!row.empty()) rows.push_back(row);
    }
    return rows;
}

// =============================================================================
//  Interpreter
// =============================================================================

class StscInterp {
public:
    explicit StscInterp(StscEnv* env) : env_(env) {}

    // ── Main entry: run a vector of logical lines ──────────────────────────
    int runLines(const std::vector<std::string>& lines) {
        tokens_ = Lexer(joinLines(lines)).tokenize();
        pos_    = 0;
        try {
            while (!atEnd()) {
                skipNewlines();
                if (atEnd()) break;
                runStatement();
            }
        } catch (StscReturn& ret) {
            lastExit_ = (int)ret.value.toInt();
        } catch (const std::exception& e) {
            std::cerr << "[stsc] error: " << e.what() << "\n";
            lastExit_ = 1;
        }
        return lastExit_;
    }

private:
    std::vector<Token> tokens_;
    size_t             pos_ = 0;
    StscEnv*           env_;
    int                lastExit_ = 0;

    // ── Token navigation ──────────────────────────────────────────────────

    static std::string joinLines(const std::vector<std::string>& lines) {
        std::string out;
        for (auto& l : lines) { out += l; out += '\n'; }
        return out;
    }

    Token& cur()               { return tokens_[pos_]; }
    Token& peek(size_t off=1)  { size_t i=pos_+off; return i<tokens_.size()?tokens_[i]:tokens_.back(); }
    bool   atEnd()             { return cur().kind == TokKind::Eof; }

    Token consume() { Token t = cur(); ++pos_; return t; }
    Token expect(TokKind k, const std::string& what="") {
        if (cur().kind != k) {
            throw std::runtime_error("expected " + what + " got '" + cur().text
                                   + "' at line " + std::to_string(cur().line));
        }
        return consume();
    }

    bool check(TokKind k)   { return cur().kind == k; }
    bool match(TokKind k)   { if (check(k)) { consume(); return true; } return false; }
    void skipNewlines()      { while (check(TokKind::Newline) || check(TokKind::Semicolon)) consume(); }
    void skipToEOL()         { while (!atEnd() && !check(TokKind::Newline)) consume(); }

    // ── Statement dispatcher ──────────────────────────────────────────────

    void runStatement() {
        skipNewlines();
        if (atEnd()) return;

        Token& t = cur();

        // if
        if (t.kind == TokKind::Ident && t.text == "if") {
            consume(); runIf(); return;
        }
        // while
        if (t.kind == TokKind::Ident && t.text == "while") {
            consume(); runWhile(); return;
        }
        // for
        if (t.kind == TokKind::Ident && t.text == "for") {
            consume(); runFor(); return;
        }
        // return
        if (t.kind == TokKind::Ident && t.text == "return") {
            consume();
            StscValue v = StscValue::from((long long)0);
            if (!check(TokKind::Newline) && !check(TokKind::Semicolon) && !atEnd())
                v = parseExpr();
            throw StscReturn{v};
        }
        // break
        if (t.kind == TokKind::Ident && t.text == "break") {
            consume(); throw StscBreak{};
        }
        // echo (shorthand, forwards to shell)
        if (t.kind == TokKind::Ident && t.text == "echo") {
            consume();
            std::string msg;
            // collect rest of line as expression values
            bool first = true;
            while (!atEnd() && !check(TokKind::Newline) && !check(TokKind::Semicolon)) {
                if (!first) msg += " ";
                msg += parseExpr().str();
                first = false;
                if (check(TokKind::Comma)) consume();
            }
            std::cout << msg << "\n";
            skipToEOL();
            return;
        }

        // Assignment: ident = expr  (lookahead)
        if (t.kind == TokKind::Ident && peek().kind == TokKind::Equals) {
            std::string name = consume().text;
            consume(); // =
            StscValue val = parseExpr();
            env_->set(name, std::move(val));
            skipToEOL();
            return;
        }

        // Pipeline expression (shell command or data pipeline)
        // Collect remaining tokens on this logical line and decide
        std::string line = collectLine();
        if (!line.empty()) runLineAsCommand(line);
    }

    // Collect current line up to newline/semicolon
    std::string collectLine() {
        std::string out;
        while (!atEnd() && !check(TokKind::Newline) && !check(TokKind::Semicolon)) {
            out += cur().text;
            // Add space between tokens (except before/after . ( ) |)
            auto k = cur().kind;
            consume();
            if (!atEnd() && !check(TokKind::Newline) && !check(TokKind::Semicolon)) {
                auto nk = cur().kind;
                bool nosp = (k == TokKind::Dot || k == TokKind::LParen
                          || nk == TokKind::Dot || nk == TokKind::RParen
                          || nk == TokKind::LParen || k == TokKind::RParen
                          || nk == TokKind::Comma || k == TokKind::Comma);
                if (!nosp) out += ' ';
            }
        }
        return trim(out);
    }

    // Run a line that is either a pipeline-with-stsc-verbs or a plain shell cmd
    void runLineAsCommand(const std::string& line) {
        // Check for STSC pipeline verbs: filter(...) | count | take(n) | sort(...)
        if (hasStscPipelineVerbs(line)) {
            runStscPipeline(line);
        } else {
            // Expand $vars in line before passing to shell
            std::string expanded = expandVars(line);
            lastExit_ = runPipeline(expanded);
        }
    }

    // Detect if a pipeline contains STSC-specific verbs
    static bool hasStscPipelineVerbs(const std::string& line) {
        static const std::vector<std::string> verbs = {
            "filter(", "count", "take(", "sort(", "map("
        };
        for (auto& v : verbs)
            if (line.find(v) != std::string::npos) return true;
        return false;
    }

    // Expand $ident in a string using env
    std::string expandVars(const std::string& s) const {
        std::string out;
        size_t i = 0;
        while (i < s.size()) {
            if (s[i] == '$' && i+1 < s.size() && (std::isalpha(s[i+1]) || s[i+1]=='_')) {
                ++i;
                std::string name;
                while (i < s.size() && (std::isalnum(s[i]) || s[i]=='_')) name += s[i++];
                out += env_->get(name).str();
            } else {
                out += s[i++];
            }
        }
        return out;
    }

    // ── STSC pipeline execution ───────────────────────────────────────────
    //  Syntax: <shell-cmd> | <verb> | <verb> ...
    //  Verbs: filter(field op val)  sort(field [asc|desc])  take(n)  count
    //         map(field)   select(col1,col2,...)

    void runStscPipeline(const std::string& line) {
        // Split by | respecting parens
        auto stages = splitPipeStages(line);
        if (stages.empty()) return;

        // First stage: shell command -> rows
        std::string headCmd = expandVars(trim(stages[0]));
        PipeData rows = captureRows(headCmd);

        // Subsequent stages: STSC verbs
        for (size_t i = 1; i < stages.size(); ++i) {
            std::string verb = trim(stages[i]);
            rows = applyVerb(rows, verb);
        }

        // Print result
        for (auto& row : rows) {
            for (size_t j = 0; j < row.size(); ++j) {
                if (j) std::cout << '\t';
                std::cout << row[j];
            }
            std::cout << '\n';
        }
        lastExit_ = 0;
    }

    static std::vector<std::string> splitPipeStages(const std::string& line) {
        std::vector<std::string> out;
        std::string cur;
        int depth = 0;
        for (char c : line) {
            if (c == '(') ++depth;
            else if (c == ')') --depth;
            else if (c == '|' && depth == 0) { out.push_back(cur); cur.clear(); continue; }
            cur += c;
        }
        if (!cur.empty()) out.push_back(cur);
        return out;
    }

    PipeData applyVerb(const PipeData& rows, const std::string& verb) {
        // count
        if (verb == "count") {
            std::cout << rows.size() << "\n";
            return {};
        }
        // take(n)
        if (verb.rfind("take(", 0) == 0 && verb.back() == ')') {
            size_t n = (size_t)std::atoi(verb.c_str() + 5);
            PipeData out;
            for (size_t i = 0; i < std::min(n, rows.size()); ++i) out.push_back(rows[i]);
            return out;
        }
        // filter(field op value)
        if (verb.rfind("filter(", 0) == 0 && verb.back() == ')') {
            std::string inner = verb.substr(7, verb.size()-8);
            return applyFilter(rows, inner);
        }
        // sort(field [desc])
        if (verb.rfind("sort(", 0) == 0 && verb.back() == ')') {
            std::string inner = verb.substr(5, verb.size()-6);
            return applySort(rows, inner);
        }
        // map(col)  — extract a single column as a list
        if (verb.rfind("map(", 0) == 0 && verb.back() == ')') {
            std::string col = trim(verb.substr(4, verb.size()-5));
            size_t idx = colIndex(rows, col);
            PipeData out;
            bool first = true;
            for (auto& r : rows) {
                if (first) { first=false; if(idx<r.size() && r[idx]==col) continue; } // skip header?
                if (idx < r.size()) out.push_back({r[idx]});
            }
            return out;
        }
        // Not a known STSC verb — fall back to shell
        PipeData out;
        for (auto& r : rows) {
            std::string line2;
            for (size_t i=0;i<r.size();++i){if(i)line2+=' '; line2+=r[i];}
            FILE* f = popen((line2 + " | " + verb).c_str(), "r");
            if (!f) continue;
            char buf[512];
            std::string acc;
            while (fgets(buf, sizeof(buf), f)) acc += buf;
            pclose(f);
            std::istringstream ss(acc);
            std::string ln;
            while (std::getline(ss, ln)) {
                PipeRow row;
                std::istringstream ls(ln);
                std::string fld;
                while (ls >> fld) row.push_back(fld);
                if (!row.empty()) out.push_back(row);
            }
        }
        return out;
    }

    static size_t colIndex(const PipeData& rows, const std::string& name) {
        // Try to find field by header name in first row, else by 0-based index
        if (!rows.empty()) {
            const auto& hdr = rows[0];
            for (size_t i=0;i<hdr.size();++i)
                if (hdr[i] == name) return i;
        }
        // numeric index?
        try { return (size_t)std::stoul(name); } catch (...) {}
        return 0;
    }

    static double numericField(const PipeRow& row, size_t idx) {
        if (idx >= row.size()) return 0.0;
        try { return std::stod(row[idx]); } catch (...) { return 0.0; }
    }

    PipeData applyFilter(const PipeData& rows, const std::string& expr) {
        // Parse: field op value
        // Supports: == != < > <= >= ~=
        // e.g. "name ~= test"  "cpu > 10"
        std::istringstream ss(expr);
        std::string field, op, val;
        ss >> field >> op >> val;
        size_t colIdx = colIndex(rows, field);

        PipeData out;
        for (size_t ri=0; ri<rows.size(); ++ri) {
            const PipeRow& r = rows[ri];
            std::string cell = (colIdx < r.size()) ? r[colIdx] : "";
            bool pass = false;
            if (op == "~=") {
                try { pass = std::regex_search(cell, std::regex(val)); } catch (...) {}
            } else {
                double cv = 0.0, fv = 0.0;
                bool numeric = false;
                try { cv = std::stod(cell); fv = std::stod(val); numeric = true; } catch (...) {}
                if (numeric) {
                    if (op == "==") pass = cv==fv;
                    else if (op == "!=") pass = cv!=fv;
                    else if (op == ">")  pass = cv>fv;
                    else if (op == "<")  pass = cv<fv;
                    else if (op == ">=") pass = cv>=fv;
                    else if (op == "<=") pass = cv<=fv;
                } else {
                    if (op == "==") pass = cell==val;
                    else if (op == "!=") pass = cell!=val;
                    else pass = false;
                }
            }
            if (pass) out.push_back(r);
        }
        return out;
    }

    PipeData applySort(const PipeData& rows, const std::string& inner) {
        std::istringstream ss(inner);
        std::string field, order;
        ss >> field >> order;
        bool desc = (order == "desc" || order == "DESC");
        size_t colIdx = colIndex(rows, field);

        PipeData sorted = rows;
        std::stable_sort(sorted.begin(), sorted.end(),
            [&](const PipeRow& a, const PipeRow& b) {
                std::string av = colIdx<a.size()?a[colIdx]:"";
                std::string bv = colIdx<b.size()?b[colIdx]:"";
                double an=0, bn=0; bool num=false;
                try { an=std::stod(av); bn=std::stod(bv); num=true; } catch(...) {}
                bool less = num ? (an<bn) : (av<bv);
                return desc ? !less : less;
            });
        return sorted;
    }

    // ── Control flow ──────────────────────────────────────────────────────

    void runIf() {
        StscValue cond = parseExpr();
        skipNewlines();
        expect(TokKind::LBrace, "{");
        auto body = collectBlock();
        std::vector<std::string> elseBody;

        skipNewlines();
        if (cur().kind == TokKind::Ident && cur().text == "else") {
            consume();
            skipNewlines();
            expect(TokKind::LBrace, "{");
            elseBody = collectBlock();
        }

        if (cond.truthy()) {
            runBlock(body);
        } else if (!elseBody.empty()) {
            runBlock(elseBody);
        }
    }

    void runWhile() {
        size_t condStart = pos_;
        try {
            while (true) {
                pos_ = condStart;
                StscValue cond = parseExpr();
                skipNewlines();
                expect(TokKind::LBrace, "{");
                auto body = collectBlock();
                if (!cond.truthy()) break;
                try {
                    runBlock(body);
                } catch (StscBreak&) {
                    break;
                }
            }
        } catch (StscBreak&) {}
    }

    void runFor() {
        // for <var> in <expr> {
        std::string var = expect(TokKind::Ident, "variable name").text;
        if (cur().kind != TokKind::Ident || cur().text != "in")
            throw std::runtime_error("expected 'in' in for loop");
        consume();
        StscValue collection = parseExpr();
        skipNewlines();
        expect(TokKind::LBrace, "{");
        auto body = collectBlock();

        std::vector<StscValue> items;
        if (collection.isList()) {
            items = collection.lval;
        } else {
            // Iterate lines of a string
            std::istringstream ss(collection.str());
            std::string ln;
            while (std::getline(ss, ln)) items.push_back(StscValue::from(ln));
        }

        for (auto& item : items) {
            env_->set(var, item);
            try {
                runBlock(body);
            } catch (StscBreak&) {
                break;
            }
        }
    }

    // Collect tokens until matching } (handling nested braces)
    std::vector<std::string> collectBlock() {
        std::vector<std::string> lines;
        std::string cur_line;
        int depth = 1;
        while (!atEnd()) {
            if (cur().kind == TokKind::LBrace) { ++depth; cur_line += '{'; consume(); }
            else if (cur().kind == TokKind::RBrace) {
                --depth;
                if (depth == 0) { consume(); break; }
                cur_line += '}'; consume();
            } else if (cur().kind == TokKind::Newline || cur().kind == TokKind::Semicolon) {
                consume();
                if (!trim(cur_line).empty()) lines.push_back(cur_line);
                cur_line.clear();
            } else {
                cur_line += cur().text + " ";
                consume();
            }
        }
        if (!trim(cur_line).empty()) lines.push_back(cur_line);
        return lines;
    }

    void runBlock(const std::vector<std::string>& lines) {
        StscEnv child; child.parent = env_;
        StscInterp sub(&child);
        sub.runLines(lines);
        lastExit_ = sub.lastExit_;
    }

    // ── Expression parser (Pratt-style) ───────────────────────────────────

    StscValue parseExpr(int minPrec = 0) {
        StscValue left = parseUnary();
        while (true) {
            int prec = binPrec(cur().kind);
            if (prec < minPrec) break;
            TokKind op = cur().kind; consume();
            StscValue right = parseExpr(prec + 1);
            left = applyBinop(op, left, right);
        }
        return left;
    }

    static int binPrec(TokKind k) {
        switch (k) {
            case TokKind::PipePipe: return 1;
            case TokKind::AmpAmp:   return 2;
            case TokKind::EqEq:
            case TokKind::BangEq:
            case TokKind::TildeEq:  return 3;
            case TokKind::Lt:
            case TokKind::Gt:
            case TokKind::LtEq:
            case TokKind::GtEq:     return 4;
            case TokKind::Plus:
            case TokKind::Minus:    return 5;
            case TokKind::Star:
            case TokKind::Slash:
            case TokKind::Percent:  return 6;
            default:                return -1;
        }
    }

    StscValue applyBinop(TokKind op, const StscValue& a, const StscValue& b) {
        // Arithmetic
        if (op == TokKind::Plus) {
            if (a.isStr() || b.isStr()) return StscValue::from(a.str() + b.str());
            if (a.isFloat() || b.isFloat()) return StscValue::from(a.toFloat() + b.toFloat());
            return StscValue::from(a.toInt() + b.toInt());
        }
        if (op == TokKind::Minus)   return StscValue::from(a.toFloat() - b.toFloat());
        if (op == TokKind::Star)    return StscValue::from(a.toFloat() * b.toFloat());
        if (op == TokKind::Slash) {
            double dv = b.toFloat();
            if (dv == 0.0) throw std::runtime_error("division by zero");
            return StscValue::from(a.toFloat() / dv);
        }
        if (op == TokKind::Percent) return StscValue::from(a.toInt() % b.toInt());
        // Comparison
        if (op == TokKind::TildeEq) {
            try { return StscValue::from(std::regex_search(a.str(), std::regex(b.str()))); }
            catch (...) { return StscValue::from(false); }
        }
        if (op == TokKind::EqEq)    return StscValue::from(a.str() == b.str());
        if (op == TokKind::BangEq)  return StscValue::from(a.str() != b.str());
        if (op == TokKind::Lt)      return StscValue::from(a.toFloat() <  b.toFloat());
        if (op == TokKind::Gt)      return StscValue::from(a.toFloat() >  b.toFloat());
        if (op == TokKind::LtEq)    return StscValue::from(a.toFloat() <= b.toFloat());
        if (op == TokKind::GtEq)    return StscValue::from(a.toFloat() >= b.toFloat());
        // Logical
        if (op == TokKind::AmpAmp)  return StscValue::from(a.truthy() && b.truthy());
        if (op == TokKind::PipePipe)return StscValue::from(a.truthy() || b.truthy());
        return StscValue::nil();
    }

    StscValue parseUnary() {
        if (match(TokKind::Bang)) return StscValue::from(!parseUnary().truthy());
        if (match(TokKind::Minus)) {
            StscValue v = parseUnary();
            if (v.isFloat()) return StscValue::from(-v.fval);
            return StscValue::from(-v.toInt());
        }
        return parsePostfix(parsePrimary());
    }

    StscValue parsePostfix(StscValue val) {
        while (true) {
            if (match(TokKind::Dot)) {
                // Method call
                std::string method = expect(TokKind::Ident, "method name").text;
                std::vector<StscValue> margs;
                if (match(TokKind::LParen)) {
                    while (!check(TokKind::RParen) && !atEnd()) {
                        margs.push_back(parseExpr());
                        if (!match(TokKind::Comma)) break;
                    }
                    expect(TokKind::RParen, ")");
                }
                val = callMethod(val, method, margs);
            } else if (match(TokKind::LBracket)) {
                // Index
                StscValue idx = parseExpr();
                expect(TokKind::RBracket, "]");
                if (val.isList()) {
                    size_t i = (size_t)idx.toInt();
                    val = (i < val.lval.size()) ? val.lval[i] : StscValue::nil();
                } else if (val.isStr()) {
                    size_t i = (size_t)idx.toInt();
                    val = (i < val.sval.size()) ? StscValue::from(std::string(1, val.sval[i]))
                                                : StscValue::nil();
                }
            } else {
                break;
            }
        }
        return val;
    }

    StscValue callMethod(const StscValue& v, const std::string& name,
                         const std::vector<StscValue>& args) {
        if (name == "len")   return StscValue::from((long long)v.len());
        if (name == "str")   return StscValue::from(v.str());
        if (name == "int")   return StscValue::from(v.toInt());
        if (name == "float") return StscValue::from(v.toFloat());
        if (name == "lines") {
            if (v.isList()) return v;
            std::vector<StscValue> out;
            std::istringstream ss(v.str());
            std::string ln;
            while (std::getline(ss, ln)) out.push_back(StscValue::from(ln));
            return StscValue::fromList(std::move(out));
        }
        if (name == "words") {
            std::vector<StscValue> out;
            std::istringstream ss(v.str());
            std::string w;
            while (ss >> w) out.push_back(StscValue::from(w));
            return StscValue::fromList(std::move(out));
        }
        if (name == "upper") {
            std::string s = v.str();
            for (char& c : s) c = (char)std::toupper((unsigned char)c);
            return StscValue::from(s);
        }
        if (name == "lower") {
            std::string s = v.str();
            for (char& c : s) c = (char)std::tolower((unsigned char)c);
            return StscValue::from(s);
        }
        if (name == "trim") {
            return StscValue::from(trim(v.str()));
        }
        if (name == "contains") {
            if (args.empty()) return StscValue::from(false);
            std::string needle = args[0].str();
            if (v.isStr())  return StscValue::from(v.sval.find(needle) != std::string::npos);
            if (v.isList()) {
                for (auto& e : v.lval) if (e.str() == needle) return StscValue::from(true);
                return StscValue::from(false);
            }
            return StscValue::from(false);
        }
        if (name == "split") {
            std::string del = args.empty() ? " " : args[0].str();
            return builtinSplit({v, StscValue::from(del)});
        }
        if (name == "join") {
            std::string del = args.empty() ? " " : args[0].str();
            if (!v.isList()) return v;
            std::string out;
            for (size_t i=0;i<v.lval.size();++i){if(i)out+=del; out+=v.lval[i].str();}
            return StscValue::from(out);
        }
        // List methods
        if (name == "first") {
            if (v.isList() && !v.lval.empty()) return v.lval.front();
            return StscValue::nil();
        }
        if (name == "last") {
            if (v.isList() && !v.lval.empty()) return v.lval.back();
            return StscValue::nil();
        }
        throw std::runtime_error("unknown method '" + name + "'");
    }

    StscValue parsePrimary() {
        Token t = cur();

        if (t.kind == TokKind::Int)   { consume(); return StscValue::from(std::stoll(t.text)); }
        if (t.kind == TokKind::Float) { consume(); return StscValue::from(std::stod(t.text));  }
        if (t.kind == TokKind::Str)   { consume(); return StscValue::from(t.text);              }
        if (t.kind == TokKind::Bool)  { consume(); return StscValue::from(t.text == "true");    }

        if (t.kind == TokKind::Ident) {
            consume();
            // Function call?
            if (check(TokKind::LParen)) {
                consume();
                std::vector<StscValue> args;
                while (!check(TokKind::RParen) && !atEnd()) {
                    args.push_back(parseExpr());
                    if (!match(TokKind::Comma)) break;
                }
                expect(TokKind::RParen, ")");
                return callFunction(t.text, args);
            }
            // Variable reference
            return env_->get(t.text);
        }

        // List literal [a, b, c]
        if (t.kind == TokKind::LBracket) {
            consume();
            std::vector<StscValue> items;
            while (!check(TokKind::RBracket) && !atEnd()) {
                items.push_back(parseExpr());
                if (!match(TokKind::Comma)) break;
            }
            expect(TokKind::RBracket, "]");
            return StscValue::fromList(std::move(items));
        }

        // Grouped
        if (t.kind == TokKind::LParen) {
            consume();
            StscValue v = parseExpr();
            expect(TokKind::RParen, ")");
            return v;
        }

        return StscValue::nil();
    }

    StscValue callFunction(const std::string& name, const std::vector<StscValue>& args) {
        if (name == "glob")  return builtinGlob(args);
        if (name == "len")   return builtinLen(args);
        if (name == "str")   return builtinStr(args);
        if (name == "int")   return builtinInt(args);
        if (name == "split") return builtinSplit(args);
        if (name == "range") return builtinRange(args);
        if (name == "env") {
            if (args.empty()) return StscValue::nil();
            const char* v = getenv(args[0].str().c_str());
            return v ? StscValue::from(std::string(v)) : StscValue::nil();
        }
        if (name == "exists") {
            if (args.empty()) return StscValue::from(false);
            std::error_code ec;
            return StscValue::from(fs::exists(args[0].str(), ec));
        }
        if (name == "isdir") {
            if (args.empty()) return StscValue::from(false);
            std::error_code ec;
            return StscValue::from(fs::is_directory(args[0].str(), ec));
        }
        if (name == "isfile") {
            if (args.empty()) return StscValue::from(false);
            std::error_code ec;
            return StscValue::from(fs::is_regular_file(args[0].str(), ec));
        }
        if (name == "read") {
            if (args.empty()) return StscValue::nil();
            std::ifstream f(args[0].str());
            if (!f) return StscValue::nil();
            std::ostringstream ss; ss << f.rdbuf();
            return StscValue::from(ss.str());
        }
        // Unknown — try running as shell command
        std::string cmd = name;
        for (auto& a : args) { cmd += ' '; cmd += a.str(); }
        int rc = runPipeline(cmd);
        return StscValue::from((long long)rc);
    }
};

// =============================================================================
//  Public entry point
// =============================================================================

int runStsc(const std::string& path, const std::vector<std::string>& args) {
    std::ifstream f(path);
    if (!f) {
        std::cerr << path << ": " << std::strerror(errno) << "\n";
        return 1;
    }
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(f, line)) lines.push_back(line);

    // Set positional variables
    StscEnv env;
    env.setLocal("0", StscValue::from(path));
    for (size_t i = 0; i < args.size(); ++i)
        env.setLocal(std::to_string(i+1), StscValue::from(args[i]));

    StscInterp interp(&env);
    return interp.runLines(lines);
}
