# Problem Notes

This is a list of known problems within TShell that i plan to fix eventually.

- Subshell grouping (`(a && b)`) currently does not work:
```
┌ [User@XenithDevEnv] ~ [~]
└ $ (cd / && ls)
(cd: command not found
error: command '(cd' not found
  Did you mean: cd?
```

Problem with current splitter, parsing the individual text as a command and not a subshell expansion:
```
# example via bash.
(User:XenithDevEnv) - [ ~ ]
-$ (cd / && ls)
bin  boot  dev  etc  home  lib  lib64  lost+found  mnt  opt  proc  root  run  sbin  srv  swapfile  sys  tmp  usr  var
(User:XenithDevEnv) - [ ~ ]
-$ ```
Will (probably) be fixed in 1.1.* with AST implementation.
(Note: will be fixed in 1.1.4, not 1.1.0)

- Environment scoping problem:
```
┌ [User@XenithDevEnv] ~ [~/Documents/Proj/Koukuro/TShell]
└ $ VAR=000 echo $VAR

┌ [User@XenithDevEnv] ~ [~/Documents/Proj/Koukuro/TShell]
└ $ 
```
Example speaks for itself. Planned to be fixed in v1.1.0-4 (v1.1.0 to v1.1.4)

