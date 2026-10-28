# Painel de APIs mediadas no host

Gere a página e os dados sem instalar dependências:

    python -B tools/progress.py --output build-progress

Abra build-progress/progress.html ou index.html. A busca e os filtros funcionam
localmente, sem conexão. Os arquivos HTML e progress.json são gerados; não
precisam ser versionados.

O painel usa config/gdi-api-matrix.json e
config/software-graphics-api-matrix.json e config/audio-api-matrix.json. Funções restritas e métodos COM
com estado local são identificados separadamente das pendências e recusas.
O percentual se refere ao inventário listado, não ao SDK completo do Windows,
nem à porcentagem de programas compatíveis. Métodos herdados podem aparecer
em mais de uma interface.

Áudio tem um recorte PCM/WAV, WinMM e WASAPI compartilhado da 14.10G, com
estado local, operações nativas e limites indicados por entrada. Media Foundation
e mídia comprimida permanecem pendentes. O painel apenas informa o inventário;
não habilita a ponte de áudio nem captura de dispositivos.

O workflow .github/workflows/progress-pages.yml regenera e publica somente
o diretório build-progress pelo GitHub Pages ao mudar os inventários, o
gerador ou o próprio workflow na branch main. Builds, evidências de testes,
DLLs e arquivos locais não fazem parte desse diretório publicado.
