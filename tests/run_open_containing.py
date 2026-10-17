"""Test the exact production STA executor and opencontaining helper with real Shell links."""
from pathlib import Path
import os
import subprocess

root = Path(__file__).resolve().parent.parent
source = (root / 'Src/StartMenu/StartMenuDLL/MenuCommands.cpp').read_text(encoding='utf-8')

def function(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        if source[end] == '{': depth += 1
        if source[end] == '}': depth -= 1
        end += 1
    return source[start:end]

functions = '\n\n'.join(function(signature) for signature in [
    'template <typename TFunc>\nstatic auto ExecuteOnSTAThread(',
    'static HRESULT SHOpenFolderAndSelectItemsSafe(',
    'static HRESULT OpenContainingItemSafe(',
])
output = root / 'build/open-containing-tests'
output.mkdir(parents=True, exist_ok=True)
template = (root / 'tests/test_open_containing.cpp').read_text(encoding='utf-8')
(output / 'test.cpp').write_text(template.replace('// PRODUCTION_FUNCTIONS', functions), encoding='utf-8')
vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
vs = Path(subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-property', 'installationPath'], text=True).strip())
toolsets = sorted(p for p in (vs / 'VC/Tools/MSVC').iterdir() if (p / 'atlmfc/include/atlbase.h').exists())
command = ('call "{}" -vcvars_ver={} >nul && cl /nologo /std:c++20 /EHsc /W4 '
           '/DUNICODE /D_UNICODE /I"{}" test.cpp /Fe:test.exe '
           'user32.lib ole32.lib shell32.lib uuid.lib && test.exe "{}"').format(
               vs / 'VC/Auxiliary/Build/vcvars64.bat', toolsets[-1].name, root / 'Src/Lib', output)
subprocess.run(command, shell=True, cwd=output, check=True, timeout=45)
