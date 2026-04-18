# Patchnotes.

This is just a list of updates I've done over time. 
...-f* in versions means a summary of patches not pushed to Github.
Versions that are Major(x.*.*) update or a Minor(*.x.*) update that jumps over 3 numbers (e.g., 1.x.y -> 2.x.y, 1.2.x -> 1.5.x) will likely have updates to the modding API. compat will be kept for up to 5 minor versions, or 1 major. (v1 mods work on v2 modloader, v1.2 mods work on 1.7, v1.1 will most likely not work on v1.7).
Versions present here that are not released are upcoming patches coming to the latest version. In the case you have an issue with the shell and you open a github issue, read the latest patchnotes on the next version release to see if the issue is already fixed before making an issue report.

pre-V1: private development, shared with friends on discord for feedback.

v1.0.0-release: first (public) release.

v1.0.1: minor patches that i forgot. 

read:

https://github.com/EsetrixArc/TShell/commit/0b0fc589b2b181d8f33deca8e238d28bdcff29f8 < patch errors with Modloader.

https://github.com/EsetrixArc/TShell/commit/8798c5bfbb11ca3d2e35aecbd85fe389e3d3f7ec < small patch

https://github.com/EsetrixArc/TShell/commit/e21eaf0d29c58f2373151721d6864417dcccb6f4 < added dependencies.

https://github.com/EsetrixArc/TShell/commit/d07c1f456fdc46bb8ed8811d6e33a3e1f8727579 < update readme, make + pkgbuild.

https://github.com/EsetrixArc/TShell/commit/5c3863bb4affe85307bc1836225fb78293ea2c1f < first private github release after large internal refactors.

To see individual commits/code changes.

## v1.1.0-f*:
---
Heredoc fork eliminated. Two new parent-side helpers (for future fork optim):

    · resolveHeredocFds(reds): called in the parent before any fork. Creates the pipe, writes the full content synchronously, closes the write end, stores the read-fd as "heredoc:fd:<n>" in the redirect target. Both ends are FD_CLOEXEC so unrelated children never inherit them.

    · closeHeredocFds(reds): releases the read-fd after all children that need it have been forked.
    
    · applyRedirects now just dup2s the pre-created fd instead of forking a writer grandchild

Terminal control(tcsetpgrp).

The single-command runCommand path was the only path that never had setpgid/tcsetpgrp. Fixed:

    · Child: setgpid(0,0) + tcsetpgrp(STDIN_FILENO, getpid()) before signal resets and exec: the child owns the terminal before it runs, so Ctrl+C hits it directly.
    
    · Parent: race-free setpgid(pid,pid) + tcsetpgrp(STDIN_FILENO, pid) after fork.
    
    · waitpid now uses WUNTRACED: Ctrl+Z is caught and registers a stopped Job. matching the pipeline path.
    
    · Terminal is reclaimed with tcsetpgrp(STDIN_FILENO, getpgrp()) after wait.

Heredoc wireing: resolveHeredocFds/closeHeredocFds called in both runCommand and the multistage pipeline loop. In the pipeline, a StageData struct pre-resolves all stages' redirects before the first 

fork; each child closes the other stages' fds before applyRedirects.

AST evaluator: execAST promoted from static to external linkage. Namespace alias tshp = tsh replaces the conflicting using tsh::AstNode. Declarations that were colliding with ::AstNode from ModdingAPI.

Real AST with operator precedence. Full recursive-descent parser:

· ParseList -> parseAndOr -> parsePipeline -> collectLeaf

· Precendence: |/|& binds tightest, then &&/||, then ;. so:
  a ; b && c || d
  parses as Seq(a, AndOr(b,c,d)). Not the flat left-to-right the old parseSections loop produced.

· Same tshp=tsh alias fix to resolve the AstNode name clash cleanly.

Interactive loop uses AST. The old parseSections + manual section.delimiter chain-breaking loop is replaced with parseAST + execAST. All operator precedence, short-circuit evaluation, and background 

handling are now driven by the tree, not string scanning.

PreParse hook hardening. Four new guarentees on every hook call:

1. Exception isolation: Each hook runs in try/catch; a throwing hook is autodisabled for the session, its partial mutation rolled back.

2. Growth cap: a rewrite that expands the line beyond max(16xoriginal, 4096) bytes is silently discarded.

3. Safe-mode block: when --safe is active, any rewrite that changes non-whitespace content is rejected with a warning.

4. Bad-hook GC: Disabled hook indices are erased from g_hooks[PreParse] in reverse order after each dispatch.

Made collectLeaf detect a leading `(` at the start of its first token and scan the raw source for the matching `)`, collecting the full balanced group as a single Cmd note text like `(cd / && ls)` to fix a bug where subshells wouldn't work because the lexer keeps both params as regular word characters:

`(cd / && ls)`

becomes:

```
Word:"(cd"
Word:"/"
&&
Word:"ls)"
```

the && is a real operation, so it splits into:

`And("(cd /", "ls))`

which made it so neither side passes isGroup() since "(cd /" doesn't start/end with parens.

Made it so the raw (unexpanded) cmdText is stored and only call `parseArgs`/expand inside the child after `applyInlineEnvChild`. because commands that would use `VAR=000 echo $VAR` wouldn't print correctly because it expands VAR early in the parent instead of the forked child.

Minor fix: g_interactive undefined symbol in ttyguard: the prebuilt Apr12 binary was compiled when mod.cpp still referenced g_interactive. rebuilt from source.
---
