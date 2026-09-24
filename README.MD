# 🌕MoonShell

> [!WARNING]
> THIS SHELL IS CURRENTLY WORK IN PROGRESS! ANYTHING CAN CHANGE AT ANY MOMENT WITHOUT ANY NOTICE! USE THIS SHELL AT YOUR OWN RISK!

MoonShell is a native Windows shell with a Linux aesthetic (larp). It's fully native to Windows — there's no POSIX layer underneath, so it won't run Unix programs on its own. If you want that, you'll need something like a pre-built MinGW environment from [MSYS2](https://www.msys2.org/) (or similar) already on your system PATH.

## Features

- **Feels like a real terminal, not a wrapper.** MoonShell takes over the console directly (raw input, its own cursor and redraw handling) instead of leaning on `cmd.exe`'s line editing.
- **Familiar line editing** — arrow keys, Home/End, Delete, and Backspace all work the way you'd expect.
- **Command history** — browse previous commands with the Up/Down arrows.
- **Readline-style kill shortcuts** — delete a word, clear the line, or kill to the start/end of it without reaching for the mouse.
- **Typo suggestions** — mistype a command and MoonShell will point you at the closest real one (`la` → "Did you mean \"ls\"?").
- **Interactive `help` menu** — built-in commands are grouped by category in a simple numbered menu, similar to a phone's call menu.
- **Runs anything on your PATH** — if a command isn't built in, MoonShell looks for it on PATH the same way `cmd.exe` would (`.exe`, `.com`, `.bat`, `.cmd`, respecting `PATHEXT`) and runs it — so `code .`, `git status`, `python`, etc. all work normally.
- **Pipelines and redirection** — chain commands with `|`, and send output to a file with `>` (overwrite) or `>>` (append), or read a file in as input with `<`. Works for built-in commands too, not just external programs, so `ls | findstr foo` or `ls > files.txt` work the same way `cmd1 | cmd2 > out.txt` would.

## Built-in commands

### Basic

| Command | Description |
| --- | --- |
| `echo` | Print text back to the screen |
| `date` | Show the current date and time |
| `exit` | Quit the shell |
| `clear` / `clr` | Clear the screen and scrollback |
| `help` | Show the interactive help menu (or `help <command>` for a direct lookup) |
| `which` | Show what a command name runs: `name: shell built-in command`, or the full path of the file on PATH it resolves to |

### File & Directory

| Command | Description |
| --- | --- |
| `cd` | Change the current directory |
| `ls` | List files and folders in the current directory |
| `blank` | Create one or more new empty files (`blank a.txt b.txt c.txt`) |
| `mkdir` | Create a new directory |
| `rmdir` | Remove a directory |
| `rm` / `del` | Remove a file |
| `mv` | Move or rename a file |
| `cp` / `copy` | Copy a file |
| `pcd` | Print the current/working directory |

### Text

| Command | Description |
| --- | --- |
| `cat` | Print files (or piped input) to the screen; `-n` numbers the lines |
| `head` | Show the first lines of a file: `-n <count>` (default 10), also `-<count>` |
| `tail` | Show the last lines of a file: `-n <count>`, `-n +<line>` to start from a given line, `-f` to keep printing what gets added to a file (stop with `Ctrl+C` or `Esc`) |
| `grep` | Search for lines matching a pattern: `-i -v -n -c -l -o -w -x -q -r -E -F -e -H -h --color` (`grep --help` lists them) |

- All four take one or more files, or read piped/redirected input (`-` means "standard input"). They pass bytes through untouched, so `cat file > copy` gives you an identical file, line endings and all.
- With several files, `head` and `tail` print a `==> name <==` header before each.
- `grep` patterns are basic regex by default, like real grep (`\(`, `\|`, `\+` are the operators); `-E` switches to the extended flavour (`(`, `|`, `+`), `-F` treats the pattern as plain text. `\w \s \d \b \<` `\>` and `[[:alpha:]]`-style classes work, back-references (`\1`) don't. It exits `0` if anything matched, `1` if nothing did and `2` on an error, so `grep -q foo file && ...` works. `-r` skips symlinks and junctions.
- Wildcards aren't expanded by the shell yet, so `cat *.txt` won't work - list the files, or use `grep -r`.

Anything else is looked up on PATH before MoonShell gives up on it.

## Pipelines and redirection

```
$MShell C:\Users\you\projects> ls | findstr moon
$MShell C:\Users\you\projects> git log > log.txt
$MShell C:\Users\you\projects> git log >> log.txt
$MShell C:\Users\you\projects> sort < names.txt
$MShell C:\Users\you\projects> ls | findstr .c | findstr /v test > sources.txt
```

- `|` chains any number of commands, built-in or external, in any combination.
- Built-ins run one at a time, so a built-in on the *left* of a `|` can only hand over about 64 KB before the next stage has started reading. Small pipes like `ls | grep foo` are fine, but `cat huge.log | grep error` will hang - give the file to the command directly instead (`grep error huge.log`). Likewise `tail -f` can't feed a pipe (use `>` or run it on its own).
- `>` writes a command's output to a file, overwriting it; `>>` appends instead.
- `<` reads a file in as a command's input.
- Operators don't need spaces around them (`ls>out.txt` works exactly like `ls > out.txt`) — put them in quotes if you actually want one of these characters as a literal argument instead (`echo "a|b"`).
- Only stdout is redirected right now — there's no `2>` for stderr yet, and a small handful of commands that talk to the console directly rather than through stdout (`help`'s interactive menu, in particular) will still expect a real console no matter what you redirect.

## Chaining commands

```
$MShell C:\Users\you\projects> mkdir build && cd build && cmake ..
$MShell C:\Users\you\projects> git pull || echo "pull failed, staying on old code"
$MShell C:\Users\you\projects> cd build ; ls
```

- `&&` runs the next command only if the previous one succeeded.
- `||` runs the next command only if the previous one failed.
- `;` runs the next command regardless of what the previous one did.
- These combine with pipes and redirection normally, and follow the same precedence real shells use: `|` binds tighter than `&&`/`||`/`;`, so `a | b && c` means "run the pipeline `a | b`, then run `c` if that pipeline succeeded" — not "run `a`, piped into `(b && c)`".
- A pipeline's success/failure is based on its *last* command, same as bash's default (`false | true` counts as success).
- Built-in commands participate too — `cd somewhere && ls` only lists the directory if `cd` actually got there.
- Background jobs (a trailing `&`) aren't supported — MoonShell reports it as a syntax error rather than silently ignoring it or hanging.

## Keyboard shortcuts

| Shortcut | Action |
| --- | --- |
| `↑` / `↓` | Browse command history |
| `←` / `→` | Move the cursor |
| `Home` / `End` | Jump to the start / end of the line |
| `Delete` | Delete the character ahead of the cursor |
| `Backspace` | Delete the character behind the cursor |
| `Ctrl+W` | Delete the word behind the cursor |
| `Ctrl+U` | Kill from the cursor to the start of the line |
| `Ctrl+K` | Kill from the cursor to the end of the line |
| `Alt+C` | Clear the whole line |

## Building from source

You'll need [CMake](https://cmake.org/), [Ninja](https://ninja-build.org/), and [LLVM/Clang](https://releases.llvm.org/) for Windows.

```bat
build.bat debug     REM or: build.bat release
```

This configures and builds with CMake + Ninja + Clang, and drops the binary at:

```
build\<debug|release>\bin\mshell.exe
```

If Clang is installed somewhere other than `C:\Program Files\LLVM\bin`, update `CLANG_PATH` near the top of `build.bat` first.

## Usage

```
$MShell C:\Users\you\projects> ls
$MShell C:\Users\you\projects> cd moonshell
$MShell C:\Users\you\projects\moonshell> code .
$MShell C:\Users\you\projects\moonshell> git status
$MShell C:\Users\you\projects\moonshell> la
Unknown Command! : la
  Did you mean "ls"?
$MShell C:\Users\you\projects\moonshell> help
=== MoonShell Help ===
Pick a category:

  1) Basic
  2) File & Directory

  0) Exit help

help>
```

## License

MIT License — see [LICENSE](LICENSE) for the full text.
