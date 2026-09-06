#!/usr/bin/env python3
"""
Compiles the generated Metal Shading Language with Apple's own `metal` compiler, for macOS and for iOS.

The complement of MslShimCheck.py: that one type-checks the MSL with a plain clang and a shim header on
any machine, this one needs a Mac with Xcode (the Command Line Tools have no `metal`) and is the real thing -
the same front end the device runs at load time, against the real <metal_stdlib>, including the MSL-only rules
the shim cannot know (address spaces, attribute placement, what each stage may declare). Warnings are not
promoted to errors, because the load-time compile does not do that either. Every stage of
`MetalGeneratedShaders.h` is compiled to AIR once per requested SDK; the default is both `macosx` and
`iphoneos`, so a construct that only one platform's Metal accepts is caught as well.

    python3 MslMetalCheck.py [<path/to/MetalGeneratedShaders.h>] [--sdk macosx,iphoneos]
"""
import os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
header = None
sdks = ['macosx', 'iphoneos']
args = sys.argv[1:]
while args:
    a = args.pop(0)
    if a == '--sdk':
        sdks = args.pop(0).split(',')
    elif a in ('-h', '--help'):
        print(__doc__)
        sys.exit(2)
    else:
        header = a
if header is None:
    header = os.path.normpath(os.path.join(HERE, '..', '..', '..', 'Shaders', 'Generated', 'MetalGeneratedShaders.h'))

with open(header, encoding='utf-8') as f:
    text = f.read()

# `inline constexpr char <Name>_<Vs|Fs>Msl[] = R"__SHDR__(...)__SHDR__";`
stages = re.findall(r'inline constexpr char (\w+?)_(Vs|Fs)Msl\[\] =\s*R"__SHDR__\((.*?)\)__SHDR__";', text, flags=re.S)
if not stages:
    print(f'[MslMetalCheck] no MSL stages found in {header}')
    sys.exit(1)

exit_code = 0
with tempfile.TemporaryDirectory() as work:
    for sdk in sdks:
        total = fails = 0
        failed = []
        for name, stage, body in stages:
            total += 1
            src = os.path.join(work, f'{name}_{stage}.metal')
            with open(src, 'w', encoding='utf-8') as f:
                f.write(body)
            r = subprocess.run(['xcrun', '-sdk', sdk, 'metal', '-c', src, '-o', os.path.join(work, 'out.air')],
                capture_output=True, text=True)
            if r.returncode != 0:
                fails += 1
                failed.append((name, stage, r.stderr))
        print(f'[MslMetalCheck] {sdk}: {total - fails}/{total} stages compile with Apple\'s metal compiler')
        for name, stage, err in failed:
            print(f'--- FAIL {sdk} {name} {stage}\n{err[:2000]}')
        if fails:
            exit_code = 1
sys.exit(exit_code)
