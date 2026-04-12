#pragma once
// =============================================================================
//  dfs.hpp  —  DataFirst Script (.dfs) interpreter for TShell
// =============================================================================
//
//  DataFirst is a pipeline-first scripting language where data always flows
//  left to right and every statement IS a pipeline.
//
//  Syntax:
//    # comment
//    ps | filter cpu > 10 | sort cpu desc | take 5
//    ls -la | filter size > 1000 | sort name | count
//    cat /etc/hosts | filter line ~= "local" | take 3
//    echo "hello world" | split | count
//
//  Verbs (no parens required in .dfs):
//    filter <field> <op> <value>    — filter rows by field value
//    sort <field> [asc|desc]        — sort rows by field
//    take <n>                       — keep first n rows
//    skip <n>                       — drop first n rows
//    count                          — print count and stop pipeline
//    unique <field>                 — deduplicate by field
//    format <template>              — render rows with {field} substitution
//    sum <field>                    — sum a numeric column
//    avg <field>                    — average a numeric column
//    max <field>  min <field>       — max/min of a numeric column
//    split                          — split each line into words
//    lines                          — treat output as one row per line
//    first  last                    — keep first/last row only
//
//  Variables:
//    set name = ps | filter cpu > 10    — store pipeline result as var
//    use $name | sort cpu               — reuse stored result
// =============================================================================

#include <string>
#include <vector>

int runDfs(const std::string& path, const std::vector<std::string>& args = {});
