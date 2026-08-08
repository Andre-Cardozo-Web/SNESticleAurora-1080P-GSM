# SNESticle Revive PS2 v1.0.4

Changelog acumulado da versão 1.0.4, comparado com a tag **v1.0.3**.

Data deste pacote de teste: **8 de agosto de 2026**  
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
- A fonte de 240p ganhou um atlas próprio para CRT, com traços verticais de
  duas scanlines; 480i, 480p e 1080i mantêm o desenho original em 2x.
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
- SRAM de SNES e NES agora fica separada em `SNESticle/SNES/` e
  `SNESticle/NES/`, mantendo leitura e migração segura dos saves antigos.
- Finalizados a SRAM de bateria e os **save states de cartuchos NES**, incluindo
  CPU, PPU, áudio, CHR RAM e estado privado dos mappers.
- Substituído o sintetizador base do **NES/2A03** por
  **Nes_Snd_Emu + Blip_Buffer**: os cinco canais recebem cada escrita no ciclo
  correto, sem perder ataques/notas curtas entre quadros.
- A saída do NES agora nasce diretamente em 32 kHz, eliminando o antigo bloco
  PCM de 44,1 kHz e a segunda reamostragem que podia segurar ou estourar notas.
- Reset e load state calculam também a scanline inclusiva até o próximo VSync,
  evitando encurtar o primeiro bloco de áudio depois da retomada.
- Corrigido o congelamento introduzido pela r7 ao abrir ROMs NES que consultam
  o status do APU no primeiro ciclo de uma janela de áudio.
- Corrigido o segundo congelamento da r7/r8 no PS2: `Nes_Apu` e `Blip_Buffer`
  agora são construídos explicitamente, sem depender de `.init_array` no
  startup antigo do PS2SDK.
- A paleta antiga e excessivamente saturada do InfoNES foi substituída pela
  paleta NTSC 2C02 padrão do **Mesen2**, preservada em RGBA8.

---

## Revisão NES r9: inicialização correta do APU no PS2

- Corrigida a causa do congelamento que ainda permanecia na r8 ao abrir
  qualquer jogo de NES, antes mesmo de o primeiro quadro ser apresentado.
- O GCC atual colocava os construtores globais de `Nes_Apu` e `Blip_Buffer` em
  `.init_array`, mas o script/startup do PS2SDK usado pelo projeto só percorre
  a lista antiga `.ctors`. Assim, no PS2, os objetos ficavam apenas zerados e
  o primeiro `output()` do APU acessava ponteiros de osciladores nulos.
- Os dois objetos agora usam armazenamento estático alinhado e são construídos
  explicitamente por `InfoNES_pAPUInit()`. Eles continuam vivos entre trocas
  de ROM, sem depender de suporte implícito do runtime C++.
- A inspeção do ELF confirmou que `_GLOBAL__sub_I_pAPUSoundRegs` desapareceu e
  que `.init_array` encolheu exatamente uma entrada de quatro bytes.
- Validada também a sequência real de memória inicialmente zerada, construção
  explícita, leitura de `$4015` no ciclo zero e 600 quadros de geração/drenagem
  de áudio (320.002 amostras).
- Compilação limpa do ELF do PS2 concluída sem avisos nem erros. O conserto é
  restrito à inicialização do áudio NES; SNES, Final Fight, vídeo, navegador,
  mappers, paleta e saves não foram alterados nesta revisão.

---

## Revisão NES r8: hotfix do congelamento ao abrir jogos

- Corrigida a leitura de `$4015` exatamente no ciclo zero. Esse acesso é
  válido no NES, mas a rotina importada tentava avançar o APU até o ciclo `-1`.
- Como os `asserts` de consistência estão ativos na compilação do PS2, o tempo
  negativo encerrava a execução dentro da biblioteca; no console/emulador isso
  aparecia apenas como o homebrew congelado ao iniciar determinados jogos.
- A leitura agora usa diretamente o estado corrente quando não existe um ciclo
  anterior e conserva a ordem original dos eventos em todos os demais ciclos.
- Adicionado ao processo de validação um teste específico para leituras no
  ciclo zero, depois de escritas no mesmo ciclo e logo após `end_frame()`.
- O conserto é restrito ao APU do NES. Vídeo e áudio do SNES, Final Fight,
  mappers, paleta, navegador e saves não foram alterados nesta revisão.

---

## Revisão NES r7: áudio por ciclo e paleta 2C02

Esta revisão substitui o primeiro conserto incremental do pAPU descrito mais
abaixo. Os itens antigos permanecem no changelog como histórico da pré-release,
mas o renderer PCM antigo, suas LUTs e o conversor 44,1 → 32 kHz não fazem mais
parte do caminho executado.

### Cinco canais base sem notas perdidas

- Integrado o `Nes_Apu`/`Blip_Buffer` de Shay Green, usado como referência
  consolidada por emuladores e pelo projeto Game Music Emu.
- Escritas em `$4000-$4013`, `$4015` e `$4017` são aplicadas na posição de ciclo
  acumulada do 6502. Ataques e cortes que ocorram dentro do mesmo quadro não
  são mais reduzidos a uma única fotografia de registradores no VSync.
- Pulsos 1/2, triângulo, ruído e DPCM mantêm seus próprios timers, fases,
  envelopes, length counters, sweep, contador linear e sequenciador de quadro.
- A leitura de `$4015` consulta o APU já avançado até o ciclo da instrução,
  incluindo o término real do DPCM e os IRQs do frame counter.
- O DPCM lê diretamente o espaço do cartucho pelo callback do 6502; assim o
  endereço, loop, tamanho e último bit do sample seguem o estado real do core.
- `Blip_Buffer` gera áudio band-limited diretamente em **32.000 Hz**. A razão
  CPU/áudio mantém a cadência alternada de 533/534 samples por quadro e a saída
  continua chegando ao conversor 32 → 48 kHz do PS2 em lotes de quatro.
- O snapshot do APU foi refeito sem ponteiros: preserva canais, envelopes,
  fases, ruído, DPCM, frame counter e IRQs. States r5/r6 ainda são aceitos;
  nesses arquivos antigos os registradores são reaplicados e o áudio retoma
  com uma nova fase, pois o formato PCM anterior não possui tradução exata.
- Esta alteração é exclusiva do NES. O SPC700, mixer e áudio do SNES não foram
  modificados.

### Cores menos saturadas

- Trocada a antiga tabela FCEUX/InfoNES reduzida a RGB555 pela paleta padrão
  **NTSC 2C02 do Mesen2**, com componentes completas de 8 bits em RGBA8.
- Índices escritos na palette RAM agora são sempre mascarados para os seis bits
  existentes no hardware (`0x00-0x3F`), evitando leitura fora da tabela.
- O verde excessivamente neon observado em jogos como **Side Pocket** passa a
  usar os mesmos valores-base do Mesen2. Um único bit vermelho continua
  reservado internamente pelo InfoNES como marcador de prioridade do fundo;
  isso altera no máximo um nível de 0-255 e não muda visualmente a cor.

### Escopo ainda separado

- O núcleo novo cobre os cinco canais **base** do 2A03. VRC6, VRC7, MMC5, FDS
  e Sunsoft 5B continuam sem ligação ao mixer; jogos que realmente dependem de
  expansão podem continuar sem esses instrumentos.
- O resultado foi compilado e validado estruturalmente, mas tom, carga da EE e
  comportamento do SPU2 ainda precisam de confirmação em PS2 real/NetherSX2.

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
- Após o teste em CRT mostrar que os traços horizontais de uma única scanline
  ainda pareciam cortados, `FontInit()` passou a gerar em RAM um atlas
  específico para 240p: cada pixel de tinta é repetido uma linha abaixo.
- A dilatação usa a linha vazia já existente entre os glifos e é feita uma vez
  no upload da fonte, sem duplicar centenas de primitivas por quadro.
- O atlas embarcado continua intacto; 480i, 480p e 1080i usam a versão normal,
  portanto a correção não engrossa a fonte nas resoluções que já estavam boas.
- Esta revisão visual não altera áudio, ritmo de emulação, SRAM, PPU nem lógica
  específica de jogos.

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

## Áudio e desempenho do NES (InfoNES) — primeira revisão, substituída pela r7

> Histórico da primeira tentativa desta pré-release. O caminho ativo atual é
> o `Nes_Snd_Emu + Blip_Buffer` documentado na seção r7 acima.

### Canais e temporização do pAPU

- O divisor 16.16 usado pelo DPCM em 44,1 kHz foi corrigido de `265664` para
  `2659741` (`1789773 / 44100 * 65536`). O valor anterior não era uma simples
  aproximação: faltava um dígito e samples/efeitos DPCM tocavam cerca de dez
  vezes mais devagar, alterando tom e duração.
- O DAC direto de `$4011` agora mantém sua saída mesmo com o DMA de `$4015`
  desligado, como no 2A03 real.
- O último bit de cada byte DPCM deixou de ser descartado; os deltas usam os
  passos corretos de dois níveis dentro da faixa de sete bits.
- A leitura de status em `$4015` foi separada do último valor escrito. O bit 4
  agora indica o tamanho DPCM realmente restante e volta a zero no fim do
  sample, permitindo que jogos que fazem polling iniciem o próximo efeito.
- Corrigido o caso conhecido de nota dos pulsos 1/2 permanecer tocando depois
  do contador de duração acabar quando a flag `halt/loop` estava ativa.
- Escritas no período baixo dos pulsos e do ruído não recarregam mais, por
  engano, seus contadores de duração; somente o registrador alto dispara a
  nova nota.
- Os divisores dos pulsos e do triângulo passaram a usar `timer + 1`, removendo
  o pequeno desvio sistemático de afinação do caminho antigo.
- Envelopes dos pulsos e do ruído agora distinguem corretamente volume
  constante de envelope, começam em 15 ao disparar uma nota e decaem a 240 Hz.
  Antes a lógica estava invertida e os acumuladores sem sinal impediam o
  decaimento correto.
- Sweep dos dois pulsos foi movido para 120 Hz e corrigido para a diferença de
  negação entre pulse 1 e pulse 2.
- O contador linear do triângulo deixou de diminuir uma vez por sample PCM.
  Agora é recarregado/contado pelo sequenciador de quadro; isso recupera linhas
  de baixo e outros instrumentos que desapareciam em poucos milissegundos.
- O ruído usa novamente um LFSR de 15 bits com seed não nulo, taps longo/curto
  corretos e volume/envelope correto.
- Quando um canal é silenciado ou recebe período inválido, todas as amostras
  restantes do quadro são escritas com zero. O código anterior saía do laço e
  deixava dados do quadro anterior, uma causa direta de nota presa e estalo.
- Escritas nos registradores do pAPU recebem timestamp do relógio acumulado do
  6502. O valor de overshoot de `K6502_Step()` não é mais confundido com tempo
  de quadro, evitando deslocar ataques e cortes de nota para o começo do bloco.
- A fila de eventos ganhou limite defensivo para impedir sobrescrita de memória
  em ROMs que escrevam nos registradores de áudio em excesso.

### Mixer e saída do PS2

- Os cinco canais base deixaram de ser somados com o mesmo peso e um centro DC
  fixo. O frontend usa duas tabelas pré-calculadas com as curvas não lineares
  de **pulse** e **triangle/noise/DPCM** do 2A03.
- Um bloqueador DC simples remove o offset do DAC sem o salto artificial que
  causava estouros em entradas/saídas de som.
- O conversor 44,1 kHz → taxa do mixer mantém posição 32.32 e a última amostra
  entre quadros. Ele não repete mais a borda nem reinicia a interpolação a cada
  VSync.
- A razão fracionária 44.100 → 32.000 produz a cadência correta de 533/534
  samples por quadro (média exata de 32 kHz), em vez de truncar sempre para
  533 e tocar lentamente com pequenas descontinuidades.
- A entrega ao `AudMixBuffer` usa lotes múltiplos de quatro, compatíveis com o
  conversor 32 → 48 kHz e sem perder a amostra ímpar em `Flush()`.
- Histórico do filtro/resampler é zerado ao resetar ROM ou carregar state. A
  imagem do pAPU passou à versão 2, mas states NES v1 desta pré-release ainda
  são aceitos e têm o DAC antigo convertido.
- Todas essas mudanças ficam no core/caminho do **NES**; mixer SPC700 e áudio
  do SNES não foram alterados.

### Custo e limites desta rodada

- Envelope, sweep e contadores deixaram os laços de 735 amostras e passam a
  rodar apenas nas quatro/duas batidas necessárias por quadro. A mistura usa
  LUTs e não faz divisões no caminho normal por sample.
- Cenas pesadas ainda podem ultrapassar 16,6 ms por causa de CPU/PPU/mappers e
  precisam de perfil/teste em PS2 real; esta rodada remove desperdício do
  áudio, mas não promete 60 fps em toda ROM.
- Foram corrigidos os cinco canais **base** do 2A03. Áudio de expansão VRC6,
  VRC7, MMC5, FDS e Sunsoft 5B continua sendo uma etapa separada; portanto uma
  ROM japonesa que dependa desses chips ainda pode ter instrumentos ausentes.

---

## SRAM e save states de SNES/NES

### Organização e compatibilidade da SRAM

- A SRAM dentro do save do cartão de memória passou a ser separada por sistema:
  - `mc0:/SNESticle/SNES/<rom>.srm` para SNES;
  - `mc0:/SNESticle/NES/<rom>.srm` para NES.
- A pasta principal `mc0:/SNESticle/` continua contendo ícone, configurações e
  bancos de save state; as subpastas `SNES` e `NES` são exclusivas para SRAM.
- Saves antigos de SNES em `mc0:/SNESticle/<rom>.srm` continuam compatíveis. Se
  não existir o arquivo novo, o emulador lê o antigo, marca uma migração e grava
  uma cópia em `SNES/` na próxima abertura do menu. O original não é apagado.
- A criação das subpastas é feita sob demanda e volta a funcionar após troca de
  cartão, sem depender de um cache global que confundia SNES e NES.
- O checksum da SRAM agora é inicializado também quando ainda não existe arquivo,
  evitando herdar o estado sujo do jogo carregado anteriormente.

### SRAM de bateria do NES

- `NesSystem::GetSRAMBytes()` e `GetSRAMData()` foram implementados.
- ROMs iNES com a flag de bateria expõem os 8 KiB completos da SRAM do InfoNES;
  ROMs sem bateria não criam um `.srm` desnecessário.
- A SRAM é zerada ao inserir outro cartucho, antes da leitura do save, impedindo
  que bytes do jogo anterior vazem para a nova ROM.
- Trainers iNES de 512 bytes são copiados para `$7000-$71ff`, como exige o
  formato, sem interferir na restauração posterior da SRAM.

### Save state do NES

- O gerenciador existente passou a aceitar cartuchos `.nes` nos mesmos cinco
  slots e nos destinos Auto, USB, memory card, MMCE e HDD interno.
- Estados NES usam bancos `.n1a/.n1b` até `.n5a/.n5b`; estados SNES preservam
  os nomes `.s1a/.s1b` existentes. No cartão, ambos permanecem diretamente em
  `mcN:/SNESticle/`, conforme a opção de destino já existente.
- O navegador de manutenção reconhece os bancos `.nNa/.nNb`, remove o par de
  segurança ao apagar um slot e oculta as novas pastas de SRAM.
- `NesStateT` deixou de ser o placeholder de 64 KiB e agora serializa:
  - registradores, interrupções e clocks do 6502;
  - RAM, SRAM, PPU RAM, OAM, registradores, scroll, scanline e paleta;
  - CHR RAM e cache decodificado de padrões;
  - estado do pAPU, incluindo envelopes, fases, contadores e DPCM;
  - RAM, registradores, latches e contadores de IRQ privados de todos os mappers
    compilados no InfoNES;
  - bancos PRG/CHR/SRAM ativos e bases do renderer.
- Nenhum endereço cru é salvo: cada ponteiro de banco vira uma referência de
  região + offset e é validado/reconstruído ao carregar. Assim, o estado não
  depende do endereço em que ROM e buffers foram alocados após reiniciar.
- O container externo mantém versão, identidade/CRC da ROM, CRC do payload,
  compressão deflate e os dois bancos resistentes a queda de energia. Um ID de
  sistema adicionado ao campo reservado mantém compatibilidade com estados SNES
  v1 já existentes.
- Estados FDS continuam fora deste suporte; o menu informa que o recurso atual
  é destinado a cartuchos iNES.

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
- O índice em RAM agora é ordenado uma vez e consultado por busca binária, em
  vez de comparar cada nome pedido com todas as PNGs várias vezes por frame.
- O prefetch de capas vizinhas ganhou atraso inicial e intervalo entre
  decodificações, evitando várias leituras/decodificações pesadas em frames
  consecutivos.
- As pastas `Named_Boxarts`, `Named_Titles`, `Named_Snaps`, `Named_Logos` e o
  arquivo `COVERS.IDX` agora ficam escondidos da lista de jogos.

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
- O downloader gera um `COVERS.IDX` compacto em cada pasta de ROMs. Em CDFS e
  outros dispositivos lentos, o emulador lê esse arquivo sequencial uma vez e
  evita enumerar separadamente os quatro diretórios `Named_*`; layouts manuais
  sem índice continuam usando o fallback compatível.
- Adicionado `make covers ROMS=/pasta`, que cria o mesmo layout `Named_*`
  diretamente em uma pasta destinada a USB, MX4SIO, MMCE, HDD, memory card ou
  `host:` sem precisar gerar ISO.
- `COVER_JOBS` controla o paralelismo e `COVER_BASE_URL` permite espelho ou
  teste local do downloader.

### Documentação

- A seção de capas do README foi reescrita em inglês com exemplos completos de
  nomes para `.sfc`, `.nes` e `.zip`.
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

- Compilação limpa da source extraída do ZIP com o toolchain PS2DEV: **153
  arquivos compilados**.
- Resultado desta source: **153/153 arquivos**, **0 erros** e **0 avisos**.
- O relógio DPCM foi conferido contra `1789773 / 44100`: o passo corrigido
  resulta em `40,5844269` ciclos/sample, contra `40,5844218` exatos; o antigo
  resultava em apenas `4,0537109`.
- Uma simulação de 600 quadros do resampler contínuo gerou **320.000 samples**
  em 10 segundos (32.000 Hz exatos), sem lote fora de múltiplo de quatro e sem
  acumular amostras pendentes.
- O downloader foi validado com uma coleção local: uma ROM SNES no formato
  GoodTools `(U) [!]` e uma ROM NES dentro de ZIP encontraram os nomes Libretro
  correspondentes e produziram **8/8 imagens** nas quatro categorias.
- Uma segunda execução de `make covers` reconheceu as oito PNGs existentes e
  não sobrescreveu nenhuma delas.
- O novo índice foi validado com quatro tipos para uma ROM GoodTools: gerou um
  `COVERS.IDX` de quatro entradas, e uma segunda execução preservou todas as
  PNGs existentes enquanto atualizava o índice.
- A preparação de `cdfs:/ROMS/` foi verificada com `COVER=n` sobre uma pasta já
  preparada: ROM, quatro diretórios `Named_*` e `COVERS.IDX` foram copiados
  integralmente para a árvore da ISO.
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
- Comparar em PS2 real músicas e efeitos dos cinco canais base, incluindo
  **Double Dragon** (nota presa), **Battletoads & Double Dragon** (DPCM) e
  **Castlevania III US** (polling de `$4015`). A edição japonesa de Castlevania
  III usa VRC6 e continua fora do áudio base desta rodada.
- Deixar um jogo NES tocando por vários minutos e testar pause/reset/load state
  para confirmar ausência de estalo, deriva de tom e amostra ímpar perdida.
- Validar jogos SuperFX de placas diferentes, incluindo Star Fox/Starwing,
  títulos GSU1 e títulos GSU2. O core recebeu testes unitários, mas compatibilidade
  jogo a jogo ainda depende de testes reais.

---

## Referências técnicas e créditos desta rodada

- Projeto original e layout de interface: iaddis/SNESticle PS2.
- PS2SDK: iomanX/fileXio e base do driver CDFS.
- PicoDrive PS2 de irixxxx: referência para resolução defensiva de entradas de
  diretório sem tipo conhecido.
- `fhoedemakers/pico-infonesPlus`: referência moderna do InfoNES para as
  correções de nota presa nos pulsos, DAC/status DPCM e efeitos ausentes
  (incluindo a Issue #111 daquele projeto).
- `jay-kumogata/InfoNES`: origem do pAPU integrado e base usada para comparar
  as mudanças locais do frontend PS2.
- `libgme/game-music-emu`: origem de `Nes_Snd_Emu` e `Blip_Buffer` (Shay
  Green, LGPL-2.1+), usados pela revisão r7 para os cinco canais base do 2A03.
- `SourMesen/Mesen2`: referência da paleta padrão NTSC 2C02 usada pela revisão
  r7 para remover a saturação excessiva do InfoNES.
- InfinityStation: referência anterior para limpeza de bandas e comportamento
  visual do navegador.
- Relatos das Issues #19 e #26 e testes enviados pela comunidade.
- Observações de jsr sobre escala 240p/480i/480p e amostragem horizontal.
