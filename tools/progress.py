"""Generate a standalone, offline host API progress report from versioned inventories."""
from pathlib import Path
import argparse,collections,html,json

ROOT=Path(__file__).resolve().parents[1]
INTERFACES={'ID2D1':'d2d1.dll','IDWrite':'dwrite.dll','IWIC':'windowscodecs.dll','ID3D11':'d3d11.dll','IDXGI':'dxgi.dll'}
LABELS={'implemented':'Implementada','implemented_with_restrictions':'Com limites','pending':'Pendente','denied_by_policy':'Recusada'}

def inventory(root):
    rows=[]
    for e in json.loads((root/'config/gdi-api-matrix.json').read_bytes())['entries']:
        rows.append(dict(dll=e['dll'],name=e['function'],status=e['status'],contract=e['contract'],kind='Export',category='Interface e GDI',phase='14.10E',execution='Ponte delimitada; confira o contrato da operação'))
    for e in json.loads((root/'config/software-graphics-api-matrix.json').read_bytes())['entries']:
        dll=next(v for k,v in INTERFACES.items() if e['interface'].startswith(k))
        rows.append(dict(dll=dll,name=e['interface']+'::'+e['method'],status=e['status'],contract=e['contract'],kind='Método COM',category='Gráficos e imagens',phase='14.10F',execution='Objeto no convidado; operação nativa ou estado local conforme o método'))
    rows.extend(json.loads((root/'config/audio-api-matrix.json').read_bytes())['entries'])
    # Representative planning entries, deliberately not a full SDK inventory.
    planned={
        'd3d9.dll':['Direct3DCreate9','Direct3DCreate9Ex'],
        'd3d12.dll':['D3D12CreateDevice'],
        'opengl32.dll':['wglCreateContext','wglMakeCurrent']}
    for dll,names in planned.items():
        audio=dll in ('winmm.dll','mmdevapi.dll','mfplat.dll','mfreadwrite.dll')
        for name in names:
            rows.append(dict(dll=dll,name=name,status='pending',contract='Lista inicial de planejamento; implementação e fronteira do host ainda precisam de contrato e testes.' if audio else 'Backend indisponível neste recorte; não conta como implementação.',kind='Planejamento',category='Áudio e mídia' if audio else 'Backends futuros',phase='14.10G' if audio else 'Lote futuro',execution='Planejado; sem encaminhamento ao host habilitado'))
    keys=[(r['dll'],r['name']) for r in rows]
    if len(keys)!=len(set(keys)):raise ValueError('Duplicate inventory identity')
    if any(r['status'] not in LABELS for r in rows):raise ValueError('Unknown status')
    return sorted(rows,key=lambda r:(r['dll'],r['name']))

def generate(root,output):
    rows=inventory(root);groups=collections.defaultdict(list)
    for r in rows:groups[r['dll']].append(r)
    summary=[]
    for dll,entries in sorted(groups.items()):
        counts=collections.Counter(r['status'] for r in entries)
        supported=counts['implemented']+counts['implemented_with_restrictions']
        summary.append(dict(dll=dll,total=len(entries),supported=supported,percent=round(100*supported/len(entries),1),counts=dict(counts)))
    payload={'schema':1,'title':'Aegis — APIs mediadas no host','scope':'Inventário declarado; não representa todo o Windows nem garante execução nativa de cada método. Métodos COM incluem estado local e herança repetida. Áudio cobre um recorte PCM delimitado; mídia comprimida permanece planejada.','libraries':summary,'entries':rows}
    esc=html.escape
    sections=[]
    for group in summary:
        dll=group['dll'];body=[]
        for r in groups[dll]:
            body.append('<tr data-status="'+r['status']+'"><td><code>'+esc(r['name'])+'</code><small>'+esc(r['kind']+' · '+r['phase'])+'</small></td><td><span class="badge '+r['status']+'">'+LABELS[r['status']]+'</span></td><td>'+esc(r['contract'])+'<small>'+esc(r['execution'])+'</small></td></tr>')
        sections.append('<details class="library"><summary><strong>'+esc(dll)+'</strong><span>'+str(group['supported'])+' / '+str(group['total'])+' no inventário</span><progress max="100" value="'+str(group['percent'])+'"></progress><b>'+str(group['percent'])+'%</b></summary><div class="scroll"><table><thead><tr><th>Função / método</th><th>Estado</th><th>Contrato e execução</th></tr></thead><tbody>'+''.join(body)+'</tbody></table></div></details>')
    totals=collections.Counter(r['status'] for r in rows)
    cards=''.join('<div class="card"><b>'+str(totals[s])+'</b><span>'+LABELS[s]+'</span></div>' for s in LABELS)
    document='''<!doctype html><html lang="pt-BR"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Aegis — Progresso das APIs do host</title><style>
:root{color-scheme:dark;--bg:#0c111b;--panel:#151e2d;--line:#29374c;--muted:#b3c0d4;--accent:#72d9c2}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:#edf3ff;font:16px/1.6 system-ui,sans-serif}main{max-width:1280px;margin:auto;padding:50px 24px}a{color:var(--accent)}header{max-width:950px}h1{font-size:clamp(2rem,5vw,3.4rem);line-height:1.15;margin:12px 0 22px}.eyebrow{color:var(--accent);letter-spacing:.18em;font-size:.8rem}p,small{color:var(--muted)}.cards{display:grid;grid-template-columns:repeat(4,1fr);gap:14px;margin:30px 0}.card{background:var(--panel);padding:20px;border:1px solid var(--line);border-radius:12px}.card b{font-size:2rem;display:block}.card span{color:var(--muted)}.filters{display:flex;gap:12px;flex-wrap:wrap;margin:28px 0}input,select,button{font:inherit;color:inherit;background:var(--panel);border:1px solid var(--line);padding:12px;border-radius:8px}input{flex:1;min-width:200px}button{cursor:pointer}details{border:1px solid var(--line);border-radius:10px;background:var(--panel);margin:12px 0;overflow:hidden}summary{cursor:pointer;display:flex;align-items:center;gap:20px;padding:18px}summary strong{min-width:175px}summary span{font-size:.85rem;color:var(--muted);flex:1}progress{width:160px;height:10px;accent-color:var(--accent)}summary b{min-width:65px;text-align:right}.scroll{overflow:auto}table{border-collapse:collapse;width:100%;text-align:left;font-size:.9rem}th,td{border-top:1px solid var(--line);padding:15px;vertical-align:top}th{color:var(--muted)}td:first-child{min-width:270px}small{display:block;margin-top:6px;font-size:.78rem}code{overflow-wrap:anywhere}.badge{display:inline-block;padding:3px 9px;border-radius:5px;white-space:nowrap;background:#253047}.implemented{color:#86efac}.implemented_with_restrictions{color:#7dd3fc}.pending{color:#fcd34d}.denied_by_policy{color:#fda4af}[hidden]{display:none!important}footer{margin:35px 0;color:var(--muted);font-size:.85rem}@media(max-width:700px){.cards{grid-template-columns:repeat(2,1fr)}summary{gap:8px;flex-wrap:wrap}summary span{flex-basis:50%}progress{flex:1}main{padding:28px 15px}}
</style><main><nav><a href="index.html">← Início</a> · <a href="windows-apis.html">APIs do Windows no MiniNT</a></nav><header><div class="eyebrow">AEGIS / COMPATIBILIDADE</div><h1>Progresso das APIs do host</h1><p>Interface, desenho, imagens e reprodução PCM; mídia comprimida permanece planejada. Abra uma DLL para consultar suas funções e os limites de cada contrato.</p><p>''' +esc(payload['scope'])+'''</p><p>“Com limites” conta como suporte parcial, incluindo métodos mantidos no convidado. Declarações, exports e métodos de recusa não provam compatibilidade. As porcentagens usam apenas as entradas listadas abaixo.</p></header><section class="cards" aria-label="Resumo">'''+cards+'''</section><div class="filters"><input id="search" type="search" aria-label="Buscar funções ou DLLs" placeholder="Buscar DLL, função ou contrato…"><select id="status" aria-label="Filtrar estado"><option value="">Todos os estados</option>'''+''.join('<option value="'+s+'">'+label+'</option>' for s,label in LABELS.items())+'''</select><button id="expand">Expandir listas</button></div><p id="results" aria-live="polite"></p>'''+''.join(sections)+'''<footer>Gerado por <a href="https://github.com/GabrielKLBr/Aegis/blob/main/tools/progress.py">tools/progress.py</a> a partir dos inventários versionados. <a href="progress.json">Dados JSON</a> · <a href="https://github.com/GabrielKLBr/Aegis/blob/main/docs/PHASE-14.10G.md">Contratos e limitações</a></footer></main>
<script>
const libs=[...document.querySelectorAll('.library')],search=document.querySelector('#search'),status=document.querySelector('#status');
function filter(){const q=search.value.toLocaleLowerCase();let n=0;for(const lib of libs){let count=0;for(const row of lib.querySelectorAll('tbody tr')){row.hidden=!!((status.value&&row.dataset.status!==status.value)||(q&&!(lib.querySelector('strong').textContent+' '+row.textContent).toLocaleLowerCase().includes(q)));if(!row.hidden)count++}lib.hidden=!count;n+=count;if(q||status.value)lib.open=!!count}document.querySelector('#results').textContent=n+' entradas visíveis';}
search.addEventListener('input',filter);status.addEventListener('change',filter);document.querySelector('#expand').addEventListener('click',()=>{const open=libs.some(l=>!l.hidden&&!l.open);for(const l of libs)if(!l.hidden)l.open=open});filter();
</script></html>'''
    output.mkdir(parents=True,exist_ok=True)
    for name in ('index.html','progress.html'):(output/name).write_text(document,encoding='utf-8')
    (output/'progress.json').write_text(json.dumps(payload,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    return {'libraries':len(groups),'entries':len(rows),'output':str(output)}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,default=ROOT);p.add_argument('--output',type=Path,default=ROOT/'build-progress');a=p.parse_args();print(json.dumps(generate(a.root,a.output)))
