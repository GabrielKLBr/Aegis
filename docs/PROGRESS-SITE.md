# Progresso público do MiniNT

GitHub Pages publica três páginas a partir de `main`:

- `index.html`: entrada com links para os dois relatórios;
- `progress.html`: inventários existentes de vídeo, interface e áudio;
- `windows-apis.html`: catálogo geral por DLL, incluindo objetivos do roadmap.

Gere localmente com Python, sem dependências externas:

```text
python -B tools/windows_api_progress.py --output build-progress
```

O gerador executa também `tools/progress.py`, preservando `progress.html` e
`progress.json`. O catálogo geral usa `.def`, declarações dos adaptadores,
inventários literais dos geradores de exports e matrizes versionadas de
contratos gráficos e de áudio. Inclui o recorte gerenciado da 14.11.
Não importa nem executa os geradores de runtimes para extrair seus inventários.

Uma declaração de export recebe **Presente no código — validar contrato**.
Ela não vira automaticamente uma API implementada ou validada. As matrizes
explícitas têm prioridade sobre as declarações brutas. Variantes de fases
anteriores são reunidas pela identidade DLL/símbolo, com links para os fontes;
isso não garante disponibilidade simultânea em um único pacote/perfil NT.

Os objetivos iniciais em `PLAN`, no gerador, são uma lista extensível de APIs
necessárias aos cenários planejados. O site não é um inventário exaustivo do
Windows SDK, não lista todos os métodos COM/WinRT e não afirma que terminar essa
lista fará todos os programas Windows funcionarem. Assinaturas, semântica,
dependências e diferenças por versão NT precisam de contratos e testes próprios.

O fluxo `.github/workflows/progress-pages.yml` regenera e publica o site quando
os inventários ou fontes relevantes mudam. Somente HTML e JSON gerados são
publicados; binários e referências Microsoft não fazem parte do site.
