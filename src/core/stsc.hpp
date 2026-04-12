#pragma once
// =============================================================================
//  stsc.hpp  —  Structured Script (.stsc) interpreter for TShell
// =============================================================================
//
//  Syntax overview:
//
//    # comment
//    files = glob("*.cpp")          # variable assignment (Value type-inferred)
//    x = 42                         # integer literal
//    msg = "hello world"            # string literal
//    flag = true                    # boolean literal
//
//    if files.len() > 10 {          # block with optional braces on same line
//      echo "many files"
//    }
//
//    for f in files {               # iterate list values
//      echo $f
//    }
//
//    while x > 0 {
//      x = x - 1
//    }
//
//    ls | filter(name ~= "test") | count    # pipeline with filter/count verbs
//    ps | sort(cpu) | take(5)               # sort + take pipeline verbs
//
//  Built-in functions:  glob(pat)  len(v)  str(v)  int(v)  split(s,delim)
//  Built-in methods:    .len()  .str()  .lines()  .words()
//  Pipeline verbs:      filter(field op val)  sort(field)  take(n)  count
//  Operators:           + - * /  == != < > <= >=  && ||  ~= (regex match)
// =============================================================================

#include <string>
#include <vector>

// Run a .stsc file.  Returns exit code (0 = success).
int runStsc(const std::string& path, const std::vector<std::string>& args = {});
