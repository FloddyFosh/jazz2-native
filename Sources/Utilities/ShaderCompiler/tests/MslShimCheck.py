#!/usr/bin/env python3
"""
Type-checks the Metal Shading Language every shader lowers to, with an ordinary clang.

There is no MSL compiler outside Xcode, so this is the tool's stand-in for a `--msl-check`: it runs
`ShaderCompiler --msl` on every .shader, splits the dump into stages and compiles each stage as C++ against
the small `mslshim/metal_stdlib` header (vector types via clang's ext_vector_type, matrices, textures,
samplers, the built-in functions). Two rewrites make the emitted MSL plain C++: the constructor calls
(`float4(...)` -> `mk_float4(...)`, since an ext_vector has no constructor syntax) and the float literals
(`1.0` -> `1.0f`, which is what they are in MSL). The [[attributes]] are unknown to clang and ignored.

It catches undeclared names, arity and type mismatches, bad swizzles, wrong member names and malformed
declarations; it cannot catch MSL-only semantic rules (address spaces, attribute placement). A negative
control at the end proves the check rejects a real type error.

    python3 MslShimCheck.py <path/to/ShaderCompiler> [<shaders dir>]
"""
import glob, os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SHIM = os.path.join(HERE, 'mslshim')

if len(sys.argv) < 2:
    print(__doc__)
    sys.exit(2)
tool = sys.argv[1]
shaders = sys.argv[2] if len(sys.argv) > 2 else os.path.normpath(os.path.join(HERE, '..', '..', '..', 'Shaders'))

ctor = re.compile(r'\b(float|int|uint|bool)(2|3|4)?(x2|x3|x4)?\(')
flit = re.compile(r'(?<![\w.])(\d+\.\d*(?:[eE][-+]?\d+)?|\.\d+(?:[eE][-+]?\d+)?|\d+[eE][-+]?\d+)(?![\w.])')

def prepare(src):
    src = ctor.sub(lambda m: 'mk_' + m.group(1) + (m.group(2) or '') + (m.group(3) or '') + '(', src)
    src = flit.sub(lambda m: m.group(1) + 'f', src)
    return src

def compile_cpp(path):
    return subprocess.run(['clang++', '-std=c++17', '-fsyntax-only', '-x', 'c++', '-Wno-unknown-attributes', '-Wall',
        '-Wno-unused-variable', '-Wno-unused-but-set-variable', '-Wno-unused-parameter', '-Wno-unused-function',
        '-Wno-parentheses', '-I' + SHIM, path], capture_output=True, text=True)

total = fails = 0
failed = []
with tempfile.TemporaryDirectory() as work:
    for shader in sorted(glob.glob(os.path.join(shaders, '*.shader'))):
        dump = subprocess.run([tool, shader, '--msl'], capture_output=True, text=True)
        if dump.returncode != 0:
            fails += 1
            failed.append((os.path.basename(shader), '-', dump.stderr.strip()))
            continue
        parts = re.split(r'^=== (.+?) --- (vertex|fragment) \(msl\) ---\n', dump.stdout, flags=re.M)
        for i in range(1, len(parts), 3):
            name, stage, body = parts[i], parts[i + 1], parts[i + 2]
            total += 1
            if body.lstrip().startswith('// unsupported'):
                fails += 1
                failed.append((name, stage, body.strip().splitlines()[0]))
                continue
            cpp = os.path.join(work, re.sub(r'\W', '_', name) + '_' + stage + '.cpp')
            with open(cpp, 'w') as f:
                f.write(prepare(body))
            r = compile_cpp(cpp)
            if r.returncode != 0:
                fails += 1
                failed.append((name, stage, r.stderr))
    # Negative control: the shim must reject a real type error
    bad = os.path.join(work, '_negative.cpp')
    with open(bad, 'w') as f:
        f.write('#include <metal_stdlib>\nusing namespace metal;\n' + prepare('int f(float4 a){ float3 b = a; return 1; }'))
    negative_ok = (compile_cpp(bad).returncode != 0)

print(f'[MslShimCheck] {total - fails}/{total} stages type-check under the clang shim; negative control {"rejected" if negative_ok else "NOT rejected"}')
for name, stage, err in failed:
    print(f'--- FAIL {name} {stage}\n{err[:1500]}')
sys.exit(0 if (fails == 0 and negative_ok) else 1)
