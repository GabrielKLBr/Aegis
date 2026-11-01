"""Generate a standalone, offline host API progress report from versioned inventories."""
from pathlib import Path
import argparse,collections,html,json

ROOT=Path(__file__).resolve().parents[1]
INTERFACES={'ID2D1':'d2d1.dll','IDWrite':'dwrite.dll','IWIC':'windowscodecs.dll','ID3D11':'d3d11.dll','IDXGI':'dxgi.dll'}
LABELS={'implemented':'Implemented','implemented_with_restrictions':'Has Limits','pending':'Pending','denied_by_policy':'Denied'}


def badge(output, name, label, rows):
    supported = sum(
        row['status'] in (
            'implemented',
            'implemented_with_restrictions'
        )
        for row in rows
    )

    total = len(rows)
    percent = 100 * supported / total if total else 0

    # Choose the color based on progress
    if percent >= 90:
        color = "brightgreen"
    elif percent >= 70:
        color = "green"
    elif percent >= 40:
        color = "yellow"
    elif percent >= 20:
        color = "orange"
    else:
        color = "red"

    # Official Shields.io endpoint format
    payload = {
        "schemaVersion": 1,
        "label": label,
        "message": f"{percent:.1f}%",
        "color": color,
        "namedLogo": "windows",
        "style": "flat"
    }

    folder = output / "badges"
    folder.mkdir(parents=True, exist_ok=True)

    (folder / f"{name}.json").write_text(
        json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8"
    )

    return {
        "supported": supported,
        "total": total,
        "percent": round(percent, 1)
    }


def inventory(root):
    rows=[]
    for e in json.loads((root/'config/gdi-api-matrix.json').read_bytes())['entries']:
        rows.append(dict(dll=e['dll'],name=e['function'],status=e['status'],contract=e['contract'],kind='Export',category='Interface and GDI',phase='14.10E',execution='Bounded bridge; see the operation contract'))
    for e in json.loads((root/'config/software-graphics-api-matrix.json').read_bytes())['entries']:
        dll=next(v for k,v in INTERFACES.items() if e['interface'].startswith(k))
        rows.append(dict(dll=dll,name=e['interface']+'::'+e['method'],status=e['status'],contract=e['contract'],kind='COM method',category='Graphics and images',phase='14.10F',execution='Object in the guest; native operation or local state depending on the method'))
    rows.extend(json.loads((root/'config/audio-api-matrix.json').read_bytes())['entries'])
    # Representative planning entries, deliberately not a full SDK inventory.
    planned={
        'd3d9.dll':['Direct3DCreate9','Direct3DCreate9Ex'],
        'd3d12.dll':['D3D12CreateDevice'],
        'opengl32.dll':['wglCreateContext','wglMakeCurrent']}
    for dll,names in planned.items():
        audio=dll in ('winmm.dll','mmdevapi.dll','mfplat.dll','mfreadwrite.dll')
        for name in names:
            rows.append(dict(dll=dll,name=name,status='pending',contract='Initial planning list; implementation and host border still need contract and testing.' if audio else 'Backend unavailable in this subset; does not count as implemented.',kind='Planning',category='Audio and Media' if audio else 'Future Backends',phase='14.10G' if audio else 'Future Lot',execution='Planned; host forwarding is not enabled'))
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
    payload={'schema':1,'title':'Aegis — APIs mediadas no host','scope':'Declared inventory; does not represent all Windows nor ensures native execution of each method. COM methods include local status and restored inheritance. Audio covers a delimited PCM cutout; compressed media remains planned.','libraries':summary,'entries':rows}
    esc=html.escape
    sections=[]
    for group in summary:
        dll=group['dll'];body=[]
        for r in groups[dll]:
            body.append('<tr data-status="'+r['status']+'"><td><code>'+esc(r['name'])+'</code><small>'+esc(r['kind']+' · '+r['phase'])+'</small></td><td><span class="badge '+r['status']+'">'+LABELS[r['status']]+'</span></td><td>'+esc(r['contract'])+'<small>'+esc(r['execution'])+'</small></td></tr>')
        sections.append('<details class="library"><summary><strong>'+esc(dll)+'</strong><span>'+str(group['supported'])+' / '+str(group['total'])+' in the inventory</span><progress max="100"value="'+str(group['percent'])+'"></progress><b>'+str(group['percent'])+'%</b></summary><div class="scroll"><table><thead><tr><th>Function / method</th><th>Status</th><th>Contract and execution</th></tr></thead><tbody>'+''.join(body)+'</tbody></table></div></details>')
    totals=collections.Counter(r['status'] for r in rows)
    cards=''.join('<div class="card"><b>'+str(totals[s])+'</b><span>'+LABELS[s]+'</span></div>' for s in LABELS)
    document='''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Aegis — Host API Progress</title><style>
:root{color-scheme:dark;--bg:#0c111b;--panel:#151e2d;--line:#29374c;--muted:#b3c0d4;--accent:#72d9c2}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:#edf3ff;font:16px/1.6 system-ui,sans-serif}main{max-width:1280px;margin:auto;padding:50px 24px}a{color:var(--accent)}header{max-width:950px}h1{font-size:clamp(2rem,5vw,3.4rem);line-height:1.15;margin:12px 0 22px}.eyebrow{color:var(--accent);letter-spacing:.18em;font-size:.8rem}p,small{color:var(--muted)}.cards{display:grid;grid-template-columns:repeat(4,1fr);gap:14px;margin:30px 0}.card{background:var(--panel);padding:20px;border:1px solid var(--line);border-radius:12px}.card b{font-size:2rem;display:block}.card span{color:var(--muted)}.filters{display:flex;gap:12px;flex-wrap:wrap;margin:28px 0}input,select,button{font:inherit;color:inherit;background:var(--panel);border:1px solid var(--line);padding:12px;border-radius:8px}input{flex:1;min-width:200px}button{cursor:pointer}details{border:1px solid var(--line);border-radius:10px;background:var(--panel);margin:12px 0;overflow:hidden}summary{cursor:pointer;display:flex;align-items:center;gap:20px;padding:18px}summary strong{min-width:175px}summary span{font-size:.85rem;color:var(--muted);flex:1}progress{width:160px;height:10px;accent-color:var(--accent)}summary b{min-width:65px;text-align:right}.scroll{overflow:auto}table{border-collapse:collapse;width:100%;text-align:left;font-size:.9rem}th,td{border-top:1px solid var(--line);padding:15px;vertical-align:top}th{color:var(--muted)}td:first-child{min-width:270px}small{display:block;margin-top:6px;font-size:.78rem}code{overflow-wrap:anywhere}.badge{display:inline-block;padding:3px 9px;border-radius:5px;white-space:nowrap;background:#253047}.implemented{color:#86efac}.implemented_with_restrictions{color:#7dd3fc}.pending{color:#fcd34d}.denied_by_policy{color:#fda4af}[hidden]{display:none!important}footer{margin:35px 0;color:var(--muted);font-size:.85rem}@media(max-width:700px){.cards{grid-template-columns:repeat(2,1fr)}summary{gap:8px;flex-wrap:wrap}summary span{flex-basis:50%}progress{flex:1}main{padding:28px 15px}}
</style><main><nav><a href="index.html">← Home</a> · <a href="windows-apis.html">Windows APIs in MiniNT</a></nav><header><div class="eyebrow">AEGIS / COMPATIBILITY</div><h1>Host API Progress</h1><p>Interface, drawing, images, and PCM playback; compressed media remains planned. Expand a DLL to view its functions and the limits of each contract.</p><p>''' +esc(payload['scope'])+'''</p><p>“With limits” counts as partial support, including methods kept in the guest. Statements, exports and refusal methods do not prove compatibility. Percentages only use entries listed below.</p></header><section class="cards" aria-label="Summary">'''+cards+'''</section><div class="filters"><input id="search" type="search" aria-label="Search functions or DLLs" placeholder="Search DLL, function or contract..."><select id="status" aria-label="Filter status"><option value="">All states</option>'''+''.join('<option value="'+s+'">'+label+'</option>' for s,label in LABELS.items())+'''</select><button id="expand">Expand Lists</button></div><p id="results" aria-live="polite"></p>'''+''.join(sections)+'''<footer>Generated by <a href="https://github.com/GabrielKLBr/Aegis/blob/main/tools/progress.py">tools/progress.py</a> from the versioned inventories. <a href="progress.json">JSON Data</a> · <a href="https://github.com/GabrielKLBr/Aegis/blob/main/docs/PHASE-14.10G.md">Contracts and limitations</a></footer></main>
<script>
const libs=[...document.querySelectorAll('.library')],search=document.querySelector('#search'),status=document.querySelector('#status');
function filter(){const q=search.value.toLocaleLowerCase();let n=0;for(const lib of libs){let count=0;for(const row of lib.querySelectorAll('tbody tr')){row.hidden=!!((status.value&&row.dataset.status!==status.value)||(q&&!(lib.querySelector('strong').textContent+' '+row.textContent).toLocaleLowerCase().includes(q)));if(!row.hidden)count++}lib.hidden=!count;n+=count;if(q||status.value)lib.open=!!count}document.querySelector('#results').textContent=n+' visible entries';}
search.addEventListener('input',filter);status.addEventListener('change',filter);document.querySelector('#expand').addEventListener('click',()=>{const open=libs.some(l=>!l.hidden&&!l.open);for(const l of libs)if(!l.hidden)l.open=open});filter();
</script></html>'''
    output.mkdir(parents=True,exist_ok=True)
    badge(output,'interface','GUI APIs',rows)
    for name in ('index.html','progress.html'):(output/name).write_text(document,encoding='utf-8')
    (output/'progress.json').write_text(json.dumps(payload,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    return {'libraries':len(groups),'entries':len(rows),'output':str(output)}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,default=ROOT);p.add_argument('--output',type=Path,default=ROOT/'build-progress');a=p.parse_args();print(json.dumps(generate(a.root,a.output)))
