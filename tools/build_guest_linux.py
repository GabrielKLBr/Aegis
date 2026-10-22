"""Cross-build the owned freestanding x64 guest using native Linux LLVM.

No Windows executable, CRT, Windows SDK, Wine or QEMU is used. PE/COFF is the
guest image format, independent of the host executable format (ELF).
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--llvm', type=pathlib.Path, default=os.environ.get('GUARDIAN_LLVM_ROOT'))
    parser.add_argument('--config', choices=['Debug', 'Release'], default='Release')
    parser.add_argument('--output', type=pathlib.Path)
    parser.add_argument('--image',action='append',help='Build only a named image contract (repeatable); omitted builds all.')
    args = parser.parse_args()
    if sys.platform != 'linux':
        parser.error('This builder runs on Linux; use the existing CMake/MSVC build on Windows.')
    if args.llvm is None:
        matches = sorted((pathlib.Path.home()/'.cache/guardian-native-deps').glob('llvm-mingw-*/bin/llvm-ml'))
        if not matches:
            parser.error('Set GUARDIAN_LLVM_ROOT to a native LLVM installation with clang, lld-link and llvm-ml.')
        args.llvm = matches[-1].parent.parent
    bindir = args.llvm.resolve()/'bin'
    for name in ['clang', 'llvm-ml', 'lld-link']:
        if not (bindir/name).is_file():
            parser.error(f'Missing native tool: {bindir/name}')
    out = (args.output or ROOT/'build-guest-linux'/args.config).resolve()
    out.mkdir(parents=True, exist_ok=True)
    generated = out/'generated'; generated.mkdir(exist_ok=True)
    objects = out/'objects'; objects.mkdir(exist_ok=True)
    log = (out/'build.log').open('w', encoding='utf-8')
    def run(cmd):
        p = subprocess.run(list(map(str, cmd)), cwd=ROOT, capture_output=True, text=True)
        log.write(json.dumps(list(map(str, cmd)))+'\n'+p.stdout+p.stderr);log.flush()
        if p.returncode:
            raise RuntimeError(p.stdout+p.stderr)
    optimization = '-O0' if args.config=='Debug' else '-O2'
    common = [bindir/'clang', '--target=x86_64-pc-windows-msvc', '-std=c++20',
              '-ffreestanding', '-funwind-tables', '-fno-exceptions', '-fno-rtti', '-fno-stack-protector',
              '-fno-builtin', '-nostdinc++', '-mno-avx', '-mno-avx2', '-Wall', '-Werror', '-Wno-misleading-indentation',
              '-Wno-deprecated-volatile', '-Wno-ignored-pragma-intrinsic', '-Wno-unused-variable', '-Wno-unused-function', '-Wno-unused-but-set-variable', optimization, '-I', ROOT/'shared', '-I', ROOT/'guest', '-I', generated]
    # This warning name was added after LLVM 18; probe it without disabling
    # unknown-warning errors for the rest of the freestanding build.
    warning = '-Wno-unused-but-set-global'
    probe = subprocess.run([str(bindir/'clang'), '-Werror', warning, '-x', 'c++', '-fsyntax-only', '-'], input='', capture_output=True, text=True)
    if probe.returncode == 0:
        common.append(warning)
    cache = {}
    def compile_one(source, defines=()):
        key = (source, tuple(defines))
        if key in cache:
            return cache[key]
        path = ROOT/'guest'/source
        suffix = hashlib.sha256(repr(key).encode()).hexdigest()[:12]
        obj = objects/(path.stem+'-'+suffix+'.obj')
        # llvm-ml omits these MASM surface spellings. Translate only directives
        # and labels, preserving every instruction and data expression.
        if path.suffix=='.asm':
            text = path.read_text()
            text = text.replace('INCLUDE GraphicsUser.inc', (ROOT/'guest/GraphicsUser.inc').read_text() if 'GUARDIAN_GRAPHICS_KERNEL=1' in defines else '')
            text = re.sub(r'^option casemap:none\s*\n', '', text, flags=re.M|re.I)
            text = re.sub(r'^(\w+)::', r'PUBLIC \1\n\1:', text, flags=re.M)
            text = re.sub(r'^(\w+) LABEL (?:BYTE|NEAR)', r'\1:', text, flags=re.M)
            text = re.sub(r'^\s*jmp \$\s*$', 'GuardianLocalSpin:\n    jmp GuardianLocalSpin', text, flags=re.M)
            if '.const' in text:
                text = text.replace('.const', ".rdata SEGMENT READONLY 'DATA'")
                text = re.sub(r'^END\s*$', '.rdata ENDS\nEND', text, flags=re.M)
            asm = objects/(path.stem+'-'+suffix+'.asm');asm.write_text(text)
            # Expand MASM macros into textual assembler before assembling COFF.
            # llvm-ml currently treats numeric FS/GS offsets as RIP-relative;
            # make those operands explicitly absolute in the textual dialect.
            # No object/image bytes are patched.
            text = re.sub(r'^(\w+) PROC', r'PUBLIC \1\n\1 PROC', text, flags=re.M)
            asm.write_text(text)
            gas = obj.with_suffix('.s')
            run([bindir/'llvm-ml', '-m64', '--filetype=s', '--output-att-asm',
                 *['/D'+d for d in defines], '/Fo', gas, asm])
            expanded = gas.read_text()
            expanded = re.sub(r'%([fg]s):(-?[0-9]+)\(%rip\)', r'%\1:\2', expanded)
            expanded = re.sub(r'%([fg]s):\(%rip\)', r'%\1:0', expanded)
            expanded = re.sub(r';[^\n]*', '', expanded)
            gas.write_text(expanded)
            run([bindir/'clang','--target=x86_64-pc-windows-msvc','-c',gas,'-o',obj])
        else:
            run(common+['-D'+d for d in defines]+['-c', path, '-o', obj])
        cache[key] = obj
        return obj
    def compile_all(sources, defines=()):
        # Fixed bounded jobs; every returned object is linked explicitly.
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            return list(pool.map(lambda source: compile_one(source, defines), sources))
    def pe(name, sources, dll=False, imports=(), definition=None):
        output = out/(name+('.dll' if dll else '.exe'))
        objs = compile_all(sources+['ClangIntrinsics.asm'])
        command = [bindir/'lld-link', '/nodefaultlib', '/incremental:no', '/manifest:no',
                   '/dynamicbase', '/fixed:no', '/include:_tls_used', '/debug:none',
                   '/out:'+str(output), '/entry:'+('DllEntry' if dll else 'GuardianEntry')]
        if dll:
            command += ['/dll', '/implib:'+str(out/(name+'.lib'))]
        else:
            command += ['/subsystem:console']
        if definition:
            command += ['/def:'+str(ROOT/'guest'/definition)]
        run(command+objs+list(imports))
        return output
    dll = pe('guardian-pe-runtime', ['fixtures/PeDll.cpp'], True, definition='fixtures/PeDll.def')
    app = pe('guardian-pe-console', ['fixtures/PeConsole.cpp'], imports=[out/'guardian-pe-runtime.lib'])
    run([sys.executable, ROOT/'tools/embed_pe_fixtures.py', generated/'PeFixtures.h', app, dll])
    rt = pe('guardian-runtime-api', ['fixtures/PeRuntimeDll.cpp','fixtures/PeRuntime.cpp','fixtures/PeSystemRuntime.cpp',
            'fixtures/PeRuntimeCall.asm','fixtures/PeExceptionRuntime.cpp','fixtures/PeLibraryRuntime.cpp','Memory.cpp','StackProbe.asm'],True,definition='fixtures/PeRuntimeDll.def')
    app = pe('guardian-runtime-console', ['fixtures/PeRuntimeConsole.cpp','fixtures/PeRuntimeCall.asm','Memory.cpp','StackProbe.asm'], imports=[out/'guardian-runtime-api.lib'])
    run([sys.executable, ROOT/'tools/embed_runtime_fixtures.py', generated/'PeRuntimeFixtures.h', app, rt])
    app = pe('guardian-lifetime-console', ['fixtures/PeLifetimeConsole.cpp','fixtures/PeRuntimeCall.asm','Memory.cpp','StackProbe.asm'], imports=[out/'guardian-runtime-api.lib'])
    child = pe('guardian-lifetime-child', ['fixtures/PeLifetimeChild.cpp','fixtures/PeRuntimeCall.asm','Memory.cpp','StackProbe.asm'], imports=[out/'guardian-runtime-api.lib'])
    run([sys.executable, ROOT/'tools/embed_lifetime_fixtures.py', generated/'PeLifetimeFixtures.h', app, child])
    complete = pe('guardian-complete-console',['fixtures/PeCompleteConsole.cpp','fixtures/PeExceptionProbe.asm','fixtures/PeRuntimeCall.asm','Memory.cpp','StackProbe.asm'],imports=[out/'guardian-runtime-api.lib'])
    complete_child = pe('guardian-complete-child',['fixtures/PeCompleteChild.cpp','fixtures/PeExceptionProbe.asm','fixtures/PeRuntimeCall.asm','Memory.cpp','StackProbe.asm'],imports=[out/'guardian-runtime-api.lib'])
    plugin = pe('guardian-dynamic',['fixtures/PeDynamicDll.cpp'],True,imports=[out/'guardian-runtime-api.lib'],definition='fixtures/PeDynamicDll.def')
    failing = pe('guardian-failing',['fixtures/PeFailingDll.cpp'],True,imports=[out/'guardian-runtime-api.lib'],definition='fixtures/PeFailingDll.def')
    run([sys.executable,ROOT/'tools/embed_complete_fixtures.py',generated/'PeCompleteFixtures.h',complete,complete_child,plugin,failing])
    conformance = pe('guardian-conformance',['fixtures/PeConformance.cpp','fixtures/PeExceptionProbe.asm','fixtures/PeRuntimeCall.asm','Memory.cpp','StackProbe.asm'],imports=[out/'guardian-runtime-api.lib'])
    run([sys.executable,ROOT/'tools/embed_conformance_fixtures.py',generated/'PeConformanceFixtures.h',conformance])
    session = pe('guardian-session',['fixtures/PeSession.cpp','fixtures/PeExceptionProbe.asm','fixtures/PeRuntimeCall.asm','Memory.cpp','StackProbe.asm'],imports=[out/'guardian-runtime-api.lib'])
    run([sys.executable,ROOT/'tools/embed_session_fixtures.py',generated/'PeSessionFixtures.h',session])
    environment = pe('guardian-environment',['fixtures/PeEnvironment.cpp','fixtures/PeRuntimeCall.asm','Memory.cpp','StackProbe.asm'],imports=[out/'guardian-runtime-api.lib'])
    docs = pe('guardian-docs',['fixtures/PeEnvironmentDocs.cpp','fixtures/PeRuntimeCall.asm','Memory.cpp','StackProbe.asm'],imports=[out/'guardian-runtime-api.lib'])
    run([sys.executable,ROOT/'tools/embed_environment_fixtures.py',generated/'PeEnvironmentFixtures.h',environment,docs])
    analysis = pe('guardian-analysis',['fixtures/PeAnalysis.cpp','fixtures/PeRuntimeCall.asm','Memory.cpp','StackProbe.asm'],imports=[out/'guardian-runtime-api.lib'])
    analysis_child = pe('guardian-analysis-child',['fixtures/PeAnalysisChild.cpp','fixtures/PeRuntimeCall.asm','Memory.cpp','StackProbe.asm'],imports=[out/'guardian-runtime-api.lib'])
    run([sys.executable,ROOT/'tools/embed_analysis_fixtures.py',generated/'PeAnalysisFixtures.h',analysis,analysis_child])
    drivers=out/'drivers';drivers.mkdir(exist_ok=True)
    api=drivers/'ntoskrnl.exe'
    run([bindir/'lld-link','/dll','/nodefaultlib','/subsystem:native','/entry:ApiEntry',
         '/base:0x1C0000','/dynamicbase','/fixed:no','/incremental:no','/manifest:no',
         '/def:'+str(ROOT/'guest/fixtures/DriverKernelApi.def'),'/implib:'+str(drivers/'ntoskrnl.lib'),
         '/out:'+str(api)]+compile_all(['fixtures/DriverKernelApi.cpp','Memory.cpp']))
    for kind,defines in [('guardian-hardware-probe',()),('guardian-hardware-probe-failing',('GUARDIAN_DRIVER_FAIL_ENTRY=1',))]:
        run([bindir/'lld-link','/nodefaultlib','/subsystem:native','/driver','/entry:DriverEntry',
             '/base:0x1A0000','/dynamicbase','/fixed:no','/incremental:no','/manifest:no',
             '/out:'+str(drivers/(kind+'.sys'))]+compile_all(['fixtures/HardwareProbeDriver.cpp','ClangIntrinsics.asm'],defines)+[drivers/'ntoskrnl.lib'])
    (drivers/'guardian-hardware-probe.inf').write_bytes((ROOT/'guest/fixtures/guardian-hardware-probe.inf').read_bytes())
    cpu = ['TelemetryKernel.cpp','CpuKernel.cpp','Cpu.asm','Memory.cpp','ClangIntrinsics.asm']
    memory = ['MemoryManager.cpp']
    processes = memory+['Scheduler.cpp','ProcessManager.cpp','ProcessChecks.cpp','ProcessKernel.cpp','ProcessUser.asm','ThreadUser.asm','StackProbe.asm']
    io = ['IoManager.cpp','IoChecks.cpp','IoKernel.cpp']
    storage = ['Volume.cpp','StorageChecks.cpp','StorageKernel.cpp']
    loader = ['PeLoader.cpp','PeKernel.cpp']
    defs = ['GUARDIAN_PROCESS_KERNEL=1']
    matrix = [('', ['Kernel.cpp','Memory.cpp','ClangIntrinsics.asm'], []), ('cpu',cpu,[]),
        ('memory',cpu+memory+['MemoryKernel.cpp','MemoryUser.asm'],['GUARDIAN_MEMORY_KERNEL=1']),
        ('threads',cpu+memory+['Scheduler.cpp','SchedulerChecks.cpp','ThreadKernel.cpp','ThreadUser.asm'],['GUARDIAN_THREAD_KERNEL=1'])]
    for name, extra, additional in [('processes',[],[]),('io',io,['GUARDIAN_IO_KERNEL=1']),
        ('storage',io+storage,['GUARDIAN_IO_KERNEL=1','GUARDIAN_STORAGE_KERNEL=1']),
        ('loader',io+storage+loader,['GUARDIAN_IO_KERNEL=1','GUARDIAN_STORAGE_KERNEL=1','GUARDIAN_PE_KERNEL=1']),
        ('runtime',io+storage+loader+['PeServices.cpp'],['GUARDIAN_IO_KERNEL=1','GUARDIAN_STORAGE_KERNEL=1','GUARDIAN_PE_KERNEL=1','GUARDIAN_RUNTIME_KERNEL=1']),
        ('lifetime',io+storage+loader+['PeServices.cpp','Unwind.cpp'],['GUARDIAN_IO_KERNEL=1','GUARDIAN_STORAGE_KERNEL=1','GUARDIAN_PE_KERNEL=1','GUARDIAN_RUNTIME_KERNEL=1','GUARDIAN_LIFETIME_KERNEL=1'])]:
        matrix.append((name,cpu+processes+extra,defs+additional))
    matrix.append(('complete',cpu+processes+io+storage+loader+['PeServices.cpp','Unwind.cpp','ExceptionDispatch.cpp','PeExceptions.cpp'],defs+['GUARDIAN_IO_KERNEL=1','GUARDIAN_STORAGE_KERNEL=1','GUARDIAN_PE_KERNEL=1','GUARDIAN_RUNTIME_KERNEL=1','GUARDIAN_LIFETIME_KERNEL=1','GUARDIAN_COMPLETE_KERNEL=1']))
    matrix.append(('conformance',matrix[-1][1],matrix[-1][2]+['GUARDIAN_CONFORMANCE_KERNEL=1']))
    matrix.append(('session',matrix[-1][1],matrix[-1][2][:-1]+['GUARDIAN_SESSION_KERNEL=1']))
    matrix.append(('environment',matrix[-1][1],matrix[-1][2][:-1]+['GUARDIAN_ENVIRONMENT_KERNEL=1']))
    matrix.append(('analysis',matrix[-1][1]+['NetworkStack.cpp','NetworkDriver.cpp'],matrix[-1][2][:-1]+['GUARDIAN_ANALYSIS_KERNEL=1','GUARDIAN_NETWORK_KERNEL=1']))
    matrix.append(('graphics',cpu+['Graphics.cpp','GraphicsDriver.cpp','StackProbe.asm'],['GUARDIAN_GRAPHICS_KERNEL=1']))
    matrix.append(('network',cpu+['NetworkStack.cpp','NetworkDriver.cpp','StackProbe.asm'],['GUARDIAN_NETWORK_KERNEL=1']))
    matrix.append(('hardware',['Kernel.cpp','Memory.cpp','TelemetryKernel.cpp','DriverKernel.cpp','PeLoader.cpp','StackProbe.asm','ClangIntrinsics.asm'],['GUARDIAN_DRIVER_KERNEL=1']))
    matrix.append(('resources',['ResourceKernel.cpp','ScalableVolume.cpp','Memory.cpp','StackProbe.asm','ClangIntrinsics.asm'],[]))
    matrix += [('cpu-fatal',cpu,['GUARDIAN_CPU_TEST_FATAL=1']),('cpu-double-fault',cpu,['GUARDIAN_CPU_TEST_FATAL=2'])]
    if args.image:
        selected=set(args.image)
        valid={name or "basic" for name,_,_ in matrix}
        if selected-valid:parser.error("Unknown image contract")
        matrix=[item for item in matrix if (item[0] or "basic") in selected]
    images = {}
    for name,sources,defines in matrix:
        target = out/('guardian-kernel'+('-'+name if name else '')+'.gk')
        objs = compile_all(sources, defines)
        run([bindir/'lld-link','/nodefaultlib','/subsystem:native','/entry:'+('GuardianResourceEntry' if name=='resources' else 'GuardianKernelEntry' if name in ('','hardware') else 'GuardianCpuEntry'),
             '/base:0x200000','/fixed','/dynamicbase:no','/incremental:no','/manifest:no','/debug:none', '/out:'+str(target)]+objs)
        run([sys.executable, ROOT/'tools/normalize_kernel_image.py', target])
        images[target.name] = hashlib.sha256(target.read_bytes()).hexdigest()
        print(f'{args.config}: {target.name}', flush=True)
    version = subprocess.check_output([bindir/'clang','--version'], text=True)
    (out/'build-manifest.json').write_text(json.dumps({'host':'linux','compiler':version,'configuration':args.config,
        'wine':False,'qemu':False,'images':images,'drivers':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in drivers.iterdir() if p.suffix in ('.exe','.sys','.inf')}},indent=2)+'\n')
    log.close()
    return 0

if __name__=='__main__':
    try:
        sys.exit(main())
    except Exception as exc:
        print(str(exc), file=sys.stderr)
        sys.exit(1)
