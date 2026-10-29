"""Generate the GitHub Pages hub and a source-derived MiniNT API catalogue, offline."""
from pathlib import Path
import argparse,ast,collections,html,json,re
import progress

ROOT=Path(__file__).resolve().parents[1]
LABELS={**progress.LABELS,'declared':'Presente no código — validar contrato'}
PLAN={
 'ntdll.dll':'NtAllocateVirtualMemory NtCreateFile NtCreateProcessEx NtCreateUserProcess NtAlpcConnectPort NtQuerySystemInformation LdrLoadDll RtlCreateHeap',
 'kernel32.dll':'CreateFileW ReadFile WriteFile CreateProcessW CreateThread VirtualAlloc VirtualProtect WaitForSingleObject LoadLibraryW GetProcAddress CreateNamedPipeW CreateJobObjectW',
 'advapi32.dll':'RegOpenKeyExW RegQueryValueExW OpenSCManagerW StartServiceW OpenProcessToken AccessCheck',
 'user32.dll':'CreateWindowExW GetMessageW DispatchMessageW RegisterClassExW SetWindowsHookExW RegisterRawInputDevices',
 'gdi32.dll':'CreateCompatibleDC TextOutW BitBlt CreateFontIndirectW',
 'ole32.dll':'CoInitializeEx CoCreateInstance CoMarshalInterface CoUnmarshalInterface',
 'oleaut32.dll':'SysAllocString VariantClear SafeArrayCreate',
 'rpcrt4.dll':'RpcBindingFromStringBindingW NdrClientCall2 RpcServerListen',
 'shell32.dll':'ShellExecuteExW SHGetKnownFolderPath SHFileOperationW',
 'ws2_32.dll':'WSAStartup socket connect send recv getaddrinfo',
 'winhttp.dll':'WinHttpOpen WinHttpConnect WinHttpSendRequest WinHttpReadData',
 'bcrypt.dll':'BCryptOpenAlgorithmProvider BCryptGenRandom BCryptEncrypt BCryptDecrypt',
 'crypt32.dll':'CertOpenStore CertGetCertificateChain CryptProtectData CryptUnprotectData',
 'mscoree.dll':'_CorExeMain _CorDllMain CLRCreateInstance',
 'msi.dll':'MsiInstallProductW MsiConfigureProductExW',
 'setupapi.dll':'SetupDiGetClassDevsW SetupDiEnumDeviceInfo SetupDiGetDeviceRegistryPropertyW',
 'combase.dll':'RoInitialize RoActivateInstance RoGetActivationFactory WindowsCreateString',
 'd3d12.dll':'D3D12CreateDevice D3D12GetDebugInterface',
 'opengl32.dll':'wglCreateContext wglMakeCurrent',
 'mfplat.dll':'MFStartup MFCreateMediaType',
}

def inventory(root):
    entries={}
    def add(dll,name,source,status='declared',contract=None):
        dll=dll.lower();dll=dll if dll.endswith(('.dll','.exe')) else dll+'.dll'
        key=(dll,name)
        if key not in entries:entries[key]={'dll':dll,'name':name,'status':status,'contract':contract or 'Símbolo encontrado nos fontes. Pode ser parcial, stub ou forwarder; exige contrato e validação no perfil NT escolhido.','sources':[]}
        if source not in entries[key]['sources']:entries[key]['sources'].append(source)
    for file in sorted((root/'guest/fixtures').glob('*.def')):
        text=file.read_text(encoding='utf-8-sig');library=re.search(r'^LIBRARY\s+["\']?([\w.-]+)',text,re.M|re.I)
        if not library:continue
        dll=library[1]
        if dll.startswith(('guardian','api-ms-win-guardian')):continue
        for line in text.split('EXPORTS',1)[-1].splitlines():
            match=re.match(r'\s*([A-Za-z_?][\w?$@]*)\s*(?:=|@|$)',line.split(';')[0])
            if match:add(dll,match[1],file.relative_to(root).as_posix())
    # Literal versioned export inventories are inspected without importing build tools.
    for name in ('component_runtime.py','internet_runtime.py'):
        file=root/'tools'/name
        for node in ast.parse(file.read_text(encoding='utf-8-sig')).body:
            if isinstance(node,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='DLL_EXPORTS' for t in node.targets):
                for dll,functions in ast.literal_eval(node.value).items():
                    for function in functions.split():add(dll,function,'tools/'+name)
    for file in sorted((root/'guest/fixtures').glob('*')):
        if file.suffix not in ('.cpp','.inl','.h'):continue
        text=file.read_text(encoding='utf-8-sig')
        for prefix,dlls in (('Kb',('kernel32','kernelbase')),('Av',('advapi32',))):
            for function in re.findall(r'extern "C" [^\n{]+?\b'+prefix+r'(\w+)\(',text):
                for dll in dlls:add(dll,function,file.relative_to(root).as_posix())
        if file.name=='NativeApi.h':
            for function in re.findall(r'NTAPI_EXPORT Status (Nt\w+)\(',text):
                for function in (function,'Zw'+function[2:]):add('ntdll',function,file.relative_to(root).as_posix())
    tree=ast.parse((root/'tools/win32_runtime.py').read_text(encoding='utf-8-sig'))
    for node in tree.body:
        if isinstance(node,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='FORWARDS' for t in node.targets):
            if not isinstance(node.value,ast.Call) or not isinstance(node.value.func,ast.Attribute) or node.value.func.attr!='split':raise ValueError('Unsupported forwarder inventory format')
            for function in ast.literal_eval(node.value.func.value).split():
                for dll in ('kernel32','kernelbase'):add(dll,function,'tools/win32_runtime.py')
    # Explicit graphics/audio contracts override raw declarations, never vice versa.
    for row in progress.inventory(root):
        add(row['dll'],row['name'],'tools/progress.py',row['status'],row['contract'])
        entries[(row['dll'].lower(),row['name'])].update(status=row['status'],contract=row['contract'])
    for dll,functions in PLAN.items():
        for function in functions.split():
            if (dll,function) not in entries:add(dll,function,'tools/windows_api_progress.py','pending','Objetivo de compatibilidade a implementar e validar. A ausência de um símbolo nesta lista não significa que ele seja dispensável.')
    for type,methods in {'Form':'.ctor Show Hide Close Dispose Text Visible IsDisposed Controls','Button':'.ctor PerformClick Click','Label':'.ctor Text','TextBox':'.ctor Text','Application':'Run Exit DoEvents'}.items():
        for method in methods.split():add('System.Windows.Forms.dll',type+'::'+method,'shared/guardian/FormsContracts.h','implemented_with_restrictions','Contrato gerenciado selecionado da 14.11; CIL no MiniNT e interface mediada no Windows. Não é o WinForms/CLR completo.')
    return sorted(entries.values(),key=lambda row:(row['dll'],row['name']))

def generate(root,output):
    output.mkdir(parents=True,exist_ok=True);rows=inventory(root);counts=collections.Counter(r['status'] for r in rows)
    scope='Catálogo extraído dos fontes e dos contratos versionados, com objetivos iniciais do roadmap. Não é uma lista exaustiva do SDK Windows. Nenhuma porcentagem representa compatibilidade com todo o Windows: versões NT, assinaturas, COM, syscalls e comportamentos também precisam ser implementados e testados.'
    payload={'schema':1,'scope':scope,'counts':dict(counts),'entries':rows}
    (output/'windows-apis.json').write_text(json.dumps(payload,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    esc=html.escape;groups=collections.defaultdict(list)
    for row in rows:
        links=' · '.join('<a href="https://github.com/GabrielKLBr/Aegis/blob/main/'+esc(s)+'">'+esc(s)+'</a>' for s in row['sources'])
        groups[row['dll']].append('<tr data-status="'+row['status']+'"><td><code>'+esc(row['name'])+'</code></td><td>'+LABELS[row['status']]+'</td><td>'+esc(row['contract'])+'<small>'+links+'</small></td></tr>')
    content='<nav><a href="index.html">← Início</a> · <a href="progress.html">Vídeo, interface e áudio</a></nav><h1>APIs do Windows no MiniNT</h1><p>'+esc(scope)+'</p><p><b>Presente no código</b> significa declaração encontrada, sem comprovar implementação completa. <b>Com limites</b> identifica um contrato parcial documentado; recusas por política são separadas de pendências.</p><div class="cards">'+''.join('<div><b>'+str(counts[s])+'</b><br>'+label+'</div>' for s,label in LABELS.items())+'</div><label>Buscar DLL, função ou contrato <input id="search" type="search"></label> <label>Estado <select id="state"><option value="">Todos</option>'+''.join('<option value="'+s+'">'+label+'</option>' for s,label in LABELS.items())+'</select></label><p id="count" aria-live="polite"></p>'
    for dll,body in sorted(groups.items()):content+='<details><summary>'+esc(dll)+' · '+str(len(body))+' entradas</summary><div class="scroll"><table><thead><tr><th>API / método</th><th>Estado</th><th>Contrato e evidências</th></tr></thead><tbody>'+''.join(body)+'</tbody></table></div></details>'
    content+='<p><a href="windows-apis.json">Baixar dados JSON</a> · <a href="https://github.com/GabrielKLBr/Aegis/blob/main/docs/ROADMAP-COMPLETO.md">Roadmap completo</a></p>'
    script="""<script>const q=document.querySelector('#search'),s=document.querySelector('#state');function filter(){let n=0;for(const d of document.querySelectorAll('details')){let k=0;for(const r of d.querySelectorAll('tbody tr')){r.hidden=!!((s.value&&r.dataset.status!==s.value)||!(d.querySelector('summary').textContent+' '+r.textContent).toLowerCase().includes(q.value.toLowerCase()));if(!r.hidden)k++}d.hidden=!k;if(q.value||s.value)d.open=!!k;n+=k}document.querySelector('#count').textContent=n+' entradas visíveis'}q.addEventListener('input',filter);s.addEventListener('change',filter);filter();</script>"""
    (output/'windows-apis.html').write_text(page('APIs do Windows no MiniNT',content+script),encoding='utf-8')
    hub='<div class="eyebrow">AEGIS / PROGRESSO</div><h1>Construindo o MiniNT</h1><p>Acompanhe os contratos de compatibilidade, o que já está disponível e o que falta implementar.</p><div class="links"><a href="progress.html"><h2>Vídeo, interface e áudio →</h2><p>GDI, gráficos, imagens, controles e reprodução de som mediados no host.</p></a><a href="windows-apis.html"><h2>APIs do Windows →</h2><p>Catálogo do MiniNT por DLL: sistema, memória, arquivos, segurança, rede, componentes e frameworks.</p></a></div><p>Dados gerados dos fontes do <a href="https://github.com/GabrielKLBr/Aegis">Aegis</a>. Cada página informa seus limites de cobertura.</p>'
    (output/'index.html').write_text(page('Aegis — Progresso do MiniNT',hub),encoding='utf-8')
    (output/'.nojekyll').write_text('',encoding='utf-8')
    return {'entries':len(rows),'libraries':len(groups),'counts':dict(counts)}

def page(title,content):
    return '<!doctype html><html lang="pt-BR"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>'+html.escape(title)+'</title><style>:root{color-scheme:dark}*{box-sizing:border-box}body{background:#0c111b;color:#edf3ff;font:16px/1.65 system-ui;margin:0}main{max-width:1280px;margin:auto;padding:48px 24px}h1{font-size:clamp(2rem,5vw,3.4rem);line-height:1.2}a{color:#72d9c2}p,small{color:#b3c0d4}small{display:block;font-size:.8rem;margin-top:10px}input,select{font:inherit;background:#151e2d;color:inherit;border:1px solid #39465d;border-radius:8px;padding:10px;max-width:100%}details,.cards>div,.links>a{background:#151e2d;border:1px solid #29374c;border-radius:12px;margin:12px 0;padding:18px}summary{cursor:pointer;font-weight:700}.cards,.links{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:16px}.cards b{font-size:2rem}.links>a{display:block;text-decoration:none}.links>a:hover{border-color:#72d9c2}table{border-collapse:collapse;width:100%;text-align:left}td,th{border-top:1px solid #29374c;padding:14px;vertical-align:top}td:first-child{min-width:250px}code{overflow-wrap:anywhere}.scroll{overflow:auto}[hidden]{display:none!important}.eyebrow{color:#72d9c2;letter-spacing:.16em}</style></head><body><main>'+content+'</main></body></html>'

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--root',type=Path,default=ROOT);parser.add_argument('--output',type=Path,default=ROOT/'build-progress');args=parser.parse_args();progress.generate(args.root,args.output);print(json.dumps(generate(args.root,args.output)))
