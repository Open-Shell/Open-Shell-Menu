# Open file location regression test

The `opencontaining` command must select a shortcut's target instead of selecting
the `.lnk` in its Start Menu folder. The separate shortcut-folder command keeps
its existing behavior.

The production fix in `MenuCommands.cpp` follows
[YellowNest/aaa8c635](https://github.com/YellowNest/Open-Shell-Menu/commit/aaa8c635abbc5e75533e6e76807370390409f59c).
It resolves `BHID_LinkTargetItem` and obtains the target PIDL inside the existing
worker STA. If item creation, link binding or PIDL extraction fails, the original
PIDL is used. The apartment-bound context-menu interface stays on its own thread.

Run from the project root:

```bat
python tests\run_open_containing.py
```

Requires the installed Visual Studio C++ toolset with ATL, Windows SDK and Python.
The runner extracts the actual production STA executor and both folder-opening
helpers, then compiles them with the production `ComHelper.h` PIDL ownership code.
Only the final `SHOpenFolderAndSelectItems` call is replaced by a capture function;
Shell shortcut creation, binding and PIDL extraction use real Windows APIs.

The test covers program links with spaces, Unicode and arguments; folder links;
ordinary files and folders; invalid shortcuts; invalid input; error propagation;
and the separate command that opens the shortcut's own folder. It also verifies
the worker's STA, unchanged selection arguments, and caller message pumping.

Test artifacts and private fixtures are written under `build/open-containing-tests`.
They are disposable. No Explorer windows are opened and no user files are changed.
Passing this test does not establish full Start Menu/Explorer UI verification.

Release build:

```bat
MSBuild OpenShell-VS2026.sln /m /p:Configuration=Release /p:Platform=x64
```
