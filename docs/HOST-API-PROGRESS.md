# Painel de APIs mediadas no host

Gere a página e os dados sem instalar dependências:

    python -B tools/progress.py --output build-progress

Abra build-progress/progress.html ou index.html. A busca e os filtros funcionam
localmente, sem conexão. Os arquivos HTML e progress.json são gerados; não
precisam ser versionados.

O painel usa config/gdi-api-matrix.json e
config/software-graphics-api-matrix.json. Funções restritas e métodos COM
com estado local são identificados separadamente das pendências e recusas.
O percentual se refere ao inventário listado, não ao SDK completo do Windows,
nem à porcentagem de programas compatíveis. Métodos herdados podem aparecer
em mais de uma interface.

Áudio e mídia têm uma seleção inicial de funções planejadas, explicitamente
marcadas como pendentes. O painel não habilita áudio, captura ou qualquer
nova ponte com dispositivos do host.

O workflow .github/workflows/progress-pages.yml regenera e publica somente
o diretório build-progress pelo GitHub Pages ao mudar os inventários, o
gerador ou o próprio workflow na branch main. Builds, evidências de testes,
DLLs e arquivos locais não fazem parte desse diretório publicado.
