# SNESticle Revive PS2 v1.0.4

Changelog acumulado da versão 1.0.4, comparado com a tag **v1.0.3**.

Data deste pacote de teste: **7 de agosto de 2026**  
Versão exibida pelo programa: **SNESticle Revive PS2 v1.0.4**

> Esta é uma source de teste. A compilação foi validada, mas modos de vídeo,
> CDFS e diferentes dispositivos ainda precisam de confirmação em PS2 real e
> nos emuladores usados pela comunidade antes de uma release ser marcada como
> final.

---

## Destaques

- Expandido o core **SuperFX/GSU**, incluindo conjunto de instruções, pipeline,
  cache de código, acesso à ROM/RAM e caminho gráfico `PLOT`/`RPIX`.
- Corrigida a causa de corrupção e flicker da **Issue #19**: texturas do
  emulador não usam mais endereços fixos que podiam sobrepor o framebuffer de
  480p.
- Refeito o dimensionamento de **240p/288p, 480i, 480p e 1080i**, com
  framebuffer adequado para cada modo.
- Corrigida a fonte pequena/comprimida de **240p** relatada na **Issue #26**.
- Corrigido o widescreen quebrado de **480p** com uma apresentação `16:9 Safe`
  que não ultrapassa a janela válida do PCRTC nem lê VRAM fora do framebuffer.
- Adicionados perfis de cor SNES **Original** e **Composite**, selecionáveis e
  salvos nas configurações.
- Removido o limite prático de **255/256 itens** do navegador e do CDFS de ISO.
- A listagem de diretórios passou a usar os registros retornados pelos drivers,
  evitando uma consulta `stat` separada para cada ROM.
- Corrigida a regressão em que pastas CDFS apareciam na lista, mas não abriam.
- Pastas agora aparecem como **`> NOME/`**, com marcador e barra sempre visíveis.
- Capas Libretro agora incluem **boxart, título, snap e logo**, com download
  automático opcional por `COVER=y` na criação da ISO.

---

## Core SNES: SuperFX / GSU

- A implementação do GSU deixou de ser apenas infraestrutura mínima e passou a
  cobrir o conjunto funcional de opcodes e seus prefixos `ALT`, `TO`, `FROM` e
  `WITH`.
- Implementados ou corrigidos, entre outros:
  - branches condicionais, `LOOP`, `JMP` e `LJMP`;
  - aritmética, comparação, multiplicação, shifts e rotates;
  - leitura/escrita de ROM e Game Pak RAM;
  - `CACHE`, `GETB`, `GETBH`, `GETBL`, `MERGE`, `LM`, `SM`, `SBK` e famílias
    relacionadas;
  - caminho gráfico `COLOR`, `CMODE`, `PLOT` e `RPIX`.
- Modelado o pipeline real de um byte do GSU, incluindo delay slot quando uma
  instrução altera `R15` e o caso em que opcode e operandos ficam separados
  entre origem e destino de um salto.
- Implementados cache de código de 512 bytes, validade por linha, alinhamento
  por `CBR` e invalidação coerente.
- Implementados prefetch de ROM por `R14`, troca de banco e atualização dos
  registradores associados.
- O cache de pixels agora possui os dois buffers usados pelo chip, permitindo
  alternância entre blocos sem perder pixels pendentes.
- Corrigidos os layouts gráficos de 2/4/8 bpp e o modo objeto de 256 pixels.
- O GSU agora avança em fatias por scanline, em vez de bloquear a EE até um
  trabalho inteiro terminar; o orçamento varia conforme o clock selecionado.
- IRQ de término é propagado ao 65C816 e baixado pelo acesso correspondente ao
  registrador de status.
- Adicionado watchdog defensivo para devolver o controle ao emulador se um
  programa GSU não terminar.

### Mapeamento dos cartuchos SuperFX

- Adicionada classificação de placas **Mario Chip 1**, **GSU1** e **GSU2**.
- `Star Fox`/`Starwing` usam o mapa de RAM do Mario Chip 1; `Star Fox 2` usa o
  mapa GSU2.
- Cartuchos GSU1 recebem seus espelhos adicionais de Game Pak RAM.
- Adicionadas as visões de Program ROM de 32 KiB e 64 KiB por banco usadas pelo
  coprocessador, incluindo os espelhos altos.
- Tipos de cartucho SuperFX `13h`, `14h`, `15h` e `1Ah` passam a ser detectados
  explicitamente.
- O tamanho da Game Pak RAM usa o campo do header estendido quando disponível,
  com fallback seguro para headers antigos.
- O registrador de versão do GSU é configurado conforme a placa e preservado
  durante reset.
- A classificação padrão da ROM foi inicializada de forma determinística,
  evitando flags aleatórias em tipos de cartucho desconhecidos.

### PPU e diagnóstico do core

- Corrigida a rotação de prioridade da OAM ao escrever em `$2102/$2103`;
  desligar a rotação volta corretamente ao primeiro sprite e invalida a lista
  renderizada quando necessário.
- A bancada `superfxtest` ganhou testes para pipeline, branches, cache, VCR,
  operações aritméticas, acesso a ROM, dois buffers de pixel e modos gráficos.
- Instrumentação opcional de CPU, PPU, GSU, DMA, HDMA, APU, mixer e sprites foi
  ampliada sob `SNDBG_LOG`; continua fora do caminho normal quando o log está
  desativado.

---

## Vídeo e renderização do PS2

### Framebuffers por modo

- **240p/288p:** framebuffer físico de `256x240`, mantendo os 256 samples
  horizontais nativos do SNES/NES. Isso elimina o redimensionamento digital
  anterior de 256 para 640 pixels e reduz shimmer em rolagem horizontal.
- **480i:** framebuffer de `640x480`; as 240 linhas lógicas passam a usar escala
  vertical exata de 2x em vez de 240 para 448.
- **480p:** framebuffer completo de `640x480` progressivo.
- **1080i:** fonte de `640x480` apresentada em uma janela 4:3 de `1280x960`,
  centralizada no raster 1080i, em vez de esticar automaticamente para 16:9.
- Em console PAL, o modo CRT continua usando o raster apropriado, apresentado
  como 240p/288p na interface.

### Issue #19: corrupção, bandas e flicker em 480p

- Removido o layout fixo de VRAM que colocava `_OutTex` em `0x2400`.
- Em `640x480`, esse endereço ficava dentro da área ocupada pelo segundo
  framebuffer; a textura sobrescrevia aproximadamente as últimas linhas de
  quadros alternados, causando bandas repetidas, imagem quebrada e flicker.
- Framebuffers são reservados primeiro pelo gsKit; depois `_OutTex`, fonte,
  textura de capa e área temporária do blender são alocadas dinamicamente e
  alinhadas.
- A inicialização falha de forma controlada se as quatro regiões não couberem
  nos 4 MiB de VRAM, em vez de continuar com endereços sobrepostos.
- A textura de capa e a fonte também deixaram de depender de posições fixas.
- O framebuffer físico inteiro é limpo a cada quadro, evitando pixels antigos
  em barras, bordas e regiões fora do canvas lógico.

### Issue #26: fonte em 240p

- A fonte passa a ser desenhada em 1x no framebuffer nativo de 240p e em 2x nos
  modos superiores.
- Posição e avanço usam a mesma transformação lógica/física, mantendo textos
  centralizados e colunas alinhadas.
- O desenho dos glifos evita amostragem fracionária com `NEAREST`, reduzindo
  letras cortadas ou com largura variável.

### Widescreen, overscan e offsets

- Adicionada transformação com escala e offset no nível de primitivas.
- Corrigida a aplicação de widescreen/overscan/offset durante uma
  reinicialização de vídeo no boot; antes a rotina podia retornar cedo.
- **480p widescreen** usa canvas `640x360` centralizado no framebuffer
  `640x480`, com barras pretas limpas. Isso evita o `StartX` negativo e o wrap
  para 4095 que faziam NetherSX2 identificar/renderizar a imagem incorretamente.
- Os demais modos mantêm o caminho PCRTC já usado para apresentação 16:9.
- A interface identifica o caso especial como **`16:9 Safe`**.

---

## Cores do SNES

- Corrigido um erro antigo na calibração: os valores RGB eram passados por
  valor e, portanto, o cálculo YIQ nunca alterava a paleta final.
- Adicionados dois perfis:
  - **Original:** comportamento visual das versões anteriores e padrão inicial;
  - **Composite:** aplica a calibração YIQ/brightness existente no core.
- A troca é feita ao vivo em **Video Config > SNES Colors**.
- O perfil selecionado é salvo no cartão de memória.
- Configurações v16 são migradas para v17 sem perder modo de vídeo, offsets,
  widescreen, volumes ou dispositivos; na migração, o perfil fica em Original.

---

## Capas de SNES e NES

### Formatos, tipos e navegação

- Mantido o suporte à PNG simples com o mesmo nome base da ROM e aos extras
  manuais `Game-1.png` até `Game-9.png`.
- O layout Libretro passa a reconhecer os quatro diretórios disponíveis para
  SNES e NES: `Named_Boxarts`, `Named_Titles`, `Named_Snaps` e `Named_Logos`.
- O botão □ alterna apenas as imagens existentes, nesta ordem: imagem simples,
  boxart, título, snap, logo e extras numerados.
- A lista de nomes de PNG agora cresce sob demanda, começando em 256 entradas e
  podendo chegar a 16.384. Isso evita que coleções com vários tipos por ROM
  sejam cortadas pelo antigo limite fixo de 2.048 nomes.
- O cache de imagens e o índice continuam sendo liberados antes de iniciar uma
  ROM, devolvendo a memória ao core.

### Download automático pelo Makefile

- Adicionado `COVER=y` e seu equivalente minúsculo `cover=y` ao alvo `make
  iso`. `COVER=n` permanece como padrão e não acessa a internet.
- Quando ativado, o build consulta as coleções Libretro oficiais de SNES e NES
  e adiciona os quatro tipos de PNG apenas à árvore temporária `cdfs:/ROMS/`.
  A ROM e sua pasta original não são modificadas.
- O sistema é detectado pelas extensões SNES/NES. Arquivos ZIP são inspecionados
  quando possível; `COVER_SYSTEM=snes` ou `COVER_SYSTEM=nes` permite resolver
  arquivos compactados ambíguos.
- A correspondência tenta o nome exato, a substituição Libretro de caracteres
  inválidos por `_`, remoção de tags GoodTools, abreviações `(U)/(E)/(J)/(W)` e
  o nome interno de uma ROM única em ZIP.
- Capas válidas que já existem são preservadas. Ausências, nomes não
  reconhecidos e falhas de rede são resumidos sem impedir a criação da ISO.
- Adicionado `make covers ROMS=/pasta`, que cria o mesmo layout `Named_*`
  diretamente em uma pasta destinada a USB, MX4SIO, MMCE, HDD, memory card ou
  `host:` sem precisar gerar ISO.
- `COVER_JOBS` controla o paralelismo e `COVER_BASE_URL` permite espelho ou
  teste local do downloader.

### Documentação

- A seção de capas do README foi reescrita em português com exemplos completos
  de nomes para `.sfc`, `.nes` e `.zip`.
- Adicionados os links separados das coleções Libretro de SNES e NES, uma
  tabela explicando cada `Named_*`, exemplos de várias imagens para a mesma ROM
  e instruções para CDFS e todos os demais dispositivos.
- Documentado explicitamente que a automação adiciona PNGs, mas nunca injeta
  arquivos dentro da ROM, renomeia o jogo ou altera seus bytes.

---

## Navegador, dispositivos e CDFS

### Velocidade e compatibilidade entre dispositivos

- CDFS, USB/mass, cartões de memória, `host:`, MMCE e PFS/HDD usam
  `fileXioDopen/fileXioDread` como caminho comum de enumeração.
- O registro de diretório já contém nome, modo e tamanho; ROMs reconhecidas não
  geram um `stat` ou seek óptico adicional por item.
- Tipos de arquivo retornados por drivers ioman antigos são consumidos no
  formato `FIO_S_*` já normalizado pelo iomanX.
- Corrigida a regressão da primeira otimização CDFS: o navegador aplicava
  novamente a conversão antiga `FIO_SO_*`, classificando `ROMS` e `BGM` como
  arquivos. Agora essas entradas voltam a abrir como diretórios.
- Drivers de terceiros que não informam tipo recebem fallback por caminho
  completo (`getstat` e, se necessário, uma tentativa segura de `dopen`).
- O fallback só ocorre para tipo desconhecido; não reduz a velocidade da lista
  normal de ROMs.

### Mais de 256 ROMs em ISO

- O array do navegador no EE agora cresce geometricamente: 256 é apenas a
  reserva inicial, não um limite.
- O limite real passa a ser a memória disponível, com tratamento de falha de
  alocação e sem escrever além do buffer.
- O driver CDFS embutido é um fork do CDFS do PS2SDK que transmite registros
  ISO9660/Joliet por uma janela pequena de setores.
- Foi removida do caminho `dopen/dread` a tabela fixa `TocEntry[256]` do driver
  padrão, permitindo listar pastas grandes sem truncar a partir do item 256.
- O novo `cdfs_stream.irx` é embutido no ELF, carregado no boot e reinicializado
  corretamente após reset do IOP.

### Interface do navegador

- Pastas são renderizadas como **`> NOME/`**.
- O marcador `>` e a barra final `/` ficam fixos; apenas o nome intermediário
  recebe reticências ou marquee quando ultrapassa a largura disponível.
- A alteração é apenas visual e não modifica o nome enviado a `Chdir()` ou ao
  carregador de ROM.
- A quantidade de linhas visíveis é calculada a partir da altura real da fonte
  e termina antes do rodapé.
- O texto da lista não ultrapassa mais a barra inferior em diretórios grandes.
- Arquivos PNG usados como capas continuam escondidos da lista de ROMs.

---

## Interface e rodapé

- Removido o endereço IP do rodapé; essa informação já existe na tela de
  configuração de Host/rede.
- Restaurada a faixa inferior em verde-azulado escuro inspirada no visual
  original do iaddis.
- O rodapé é desenhado depois da tela ativa, impedindo que itens do navegador o
  cubram.
- O rodapé mantém a versão do GCC à esquerda e a versão do programa alinhada à
  direita.
- Os nomes dos modos de vídeo foram atualizados para descrever o sinal usado:
  `240p/288p (CRT)`, `480i (default)`, `480p` e `1080i`.

---

## Build e código-fonte

- `cdfs_stream.irx`, seu código-fonte e licença estão incluídos na árvore.
- O Makefile verifica e embute o novo IRX automaticamente.
- `tools/fetch_libretro_covers.py` implementa a preparação opcional de capas
  sem dependências Python externas.
- As regiões de vídeo deixaram de depender de constantes de endereço entre
  modos.
- O pacote ZIP preserva a pasta raiz `SNESticleRevive/` e exclui `.git`,
  resultados de build e ELFs gerados.
- A partir deste pacote de teste, a entrega solicitada é **source + changelog**;
  nenhum ELF pré-compilado acompanha os downloads enviados nesta conversa.

### Validação realizada neste ambiente

- Compilação completa com o toolchain PS2DEV: **153 arquivos compilados**.
- Resultado: **0 erros** e **2 avisos antigos** de possível truncamento de texto
  em caminhos, sem novos avisos causados por estas mudanças.
- O downloader foi validado com uma coleção local: uma ROM SNES no formato
  GoodTools `(U) [!]` e uma ROM NES dentro de ZIP encontraram os nomes Libretro
  correspondentes e produziram **8/8 imagens** nas quatro categorias.
- Uma segunda execução de `make covers` reconheceu as oito PNGs existentes e
  não sobrescreveu nenhuma delas.
- `COVER=y` e `cover=y` foram testados na preparação de `cdfs:/ROMS/`: as PNGs
  foram colocadas somente na árvore temporária da ISO e a pasta original
  permaneceu com zero PNGs.
- A imagem ISO final não foi produzida neste ambiente porque não há
  `mkisofs`/`genisoimage`/`xorriso`; a etapa completa anterior ao gerador de
  ISO, incluindo `SYSTEM.CNF`, ROMs e capas CDFS, foi validada.
- O ZIP da source é testado com `unzip -t`.
- O pacote é extraído em uma pasta temporária e recompilado para confirmar que
  não depende do diretório de trabalho original.

---

## Pontos que ainda exigem teste comunitário

- Confirmar 240p/288p e a fonte em CRT real, especialmente nas revisões FAT e
  Slim citadas nas Issues #19 e #26.
- Confirmar 480p normal e `16:9 Safe` em componente, PS2-to-HDMI, GSM, OPL e
  NetherSX2.
- Confirmar a proporção 4:3 e a opção widescreen em 1080i em diferentes TVs.
- Abrir `cdfs:/ROMS/`, subpastas e uma ISO com mais de 256 ROMs.
- Repetir a navegação em `mass0:`, `mass1:`, `mc0:`, `mc1:`, `host:`, MMCE e
  partições HDD/PFS.
- Comparar os perfis Original e Composite em jogos com gradientes, transparência
  e tons de pele; a preferência visual continua sendo subjetiva.
- Validar jogos SuperFX de placas diferentes, incluindo Star Fox/Starwing,
  títulos GSU1 e títulos GSU2. O core recebeu testes unitários, mas compatibilidade
  jogo a jogo ainda depende de testes reais.

---

## Referências técnicas e créditos desta rodada

- Projeto original e layout de interface: iaddis/SNESticle PS2.
- PS2SDK: iomanX/fileXio e base do driver CDFS.
- PicoDrive PS2 de irixxxx: referência para resolução defensiva de entradas de
  diretório sem tipo conhecido.
- InfinityStation: referência anterior para limpeza de bandas e comportamento
  visual do navegador.
- Relatos das Issues #19 e #26 e testes enviados pela comunidade.
- Observações de jsr sobre escala 240p/480i/480p e amostragem horizontal.
