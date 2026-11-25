from pathlib import Path
import subprocess,tempfile,os
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as tmp:
    for name, flags in [('tempo', []), ('tempo-sdk-macro', ['-DAFX_TEST_SDK_SNPRINTF_MACRO'])]:
        binary=Path(tmp)/name
        subprocess.run(['g++','-std=c++17','-O2','-fsanitize=undefined','-fno-sanitize-recover=all',*flags,'-I'+str(root),str(root/'tests/cinematic/test_musical_timeline.cpp'),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
        print(f'{name}: passed')
