# Future Features

Just a list of features I plan to implement in the future.
(An asterisk ("*") before a feature means it's already implemented.)

* Command parsing.
* Redirection (stdout overwrite/append, stdin redirect, stderr, etc)
* Pipeline & Operators.
* Expansion.
* Quoting.
* Scripting constructs (if/elif/else/fi/while/do, etc).
* Variables & Assignments.
* Built-in commands.
* Config file generation + usage.
* Bash-like usage.
* Job Control.
* History.
* History: Frecency + deduplication + configurable size.
* Tab completion.
* Mod support.
* Custom theme support.
* Dynamic mod loading (.so).
* Dynamic mod interactions (dynamic theme registering, dynamic commands, etc).
* STSC Pipeline verbs (mini data-pipeline layer ontop of shell pipelines)
* tshell help commands.
* Alias expansion.
* ANSI color support.
* Typo/fuzzy suggestion for unknown commands (Levenshtein distance).

Interactive Pipeline visualiser: A TUI/ANSI display to show each stage of the last pipeline with timing, exitc, byte counts, etc.

Conditional prompt segments: prompt tokens are functions that return a string, A format like `%if GITBRANCH{%green[$GITBRANCH]%reset} would be a nice QoL

UI-bvased history search.

Mod hot-reload.

Config schema validation.
