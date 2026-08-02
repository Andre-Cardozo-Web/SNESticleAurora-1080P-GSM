# SNESticle Revive PS2 v1.0.3

Final changelog consolidating the storage/boot work, the menu-music rewrite,
and the SNES compatibility fixes validated during v1.0.3 testing.

Base release: **v1.0.2**  
Version shown in the program: **SNESticle Revive PS2 v1.0.3**

---

## Português (Brasil)

### Destaques

- Reconstruída a inicialização de armazenamento/SIO2 para ampliar a
  compatibilidade com PS2 Slim **Deckard** (`SCPH-7700x` em diante) e evitar
  estados falsos de módulo carregado.
- Corrigida a integração MMCE para **MemCard PRO 2** e **SD2PSX**, incluindo
  detecção real do dispositivo antes de mostrar `mmce0:` ou `mmce1:`.
- Refeito o BGM do menu com **libxmp-lite**, melhorando efeitos, instrumentos,
  tom, tempo e loops de módulos ProTracker/FastTracker.
- Corrigido o BGM dentro de ISO sem bloquear o boot ou congelar a tela de
  seleção de dispositivo.
- Estabilizado o boot de ELF direto, tanto packed quanto unpacked, inclusive
  quando o launcher não fornece um `argv[0]` utilizável.
- Corrigidos **Captain Commando (USA)** e os jogos testados de **Mighty Morphin
  Power Rangers** por meio de correções gerais no core do SNES, sem hacks por
  nome de ROM.
- Removidos os rastreadores e logs verbosos usados durante os testes.

### Boot, IOP e modelos Deckard

- Agora existe um único reset do IOP para todos os dispositivos de boot.
  Foi removido o segundo reset exclusivo do Memory Card, que apagava módulos
  já carregados e deixava o EE acreditando que os respectivos serviços RPC
  ainda estavam ativos.
- O reset deixou de depender de `rom0:EELOADCNF`, reduzindo diferenças entre
  versões de BIOS e modelos antigos/novos do PS2.
- Após o reset, `SifLoadFileInit`, caches e o estado interno dos módulos
  embutidos são reinicializados em uma ordem consistente.
- O caminho do ELF agora fica em buffers próprios e sempre válidos. Se o
  launcher omitir o caminho, `host:` é usado como fallback seguro em vez de
  acessar um ponteiro nulo antes da inicialização de vídeo.
- O CD/DVD é iniciado com `SCECdINoD`, sem esperar mecanicamente pelo disco no
  boot. Isso evita o bloqueio visto em algumas combinações de console, ISO e
  launcher.
- O código evita o RPC legado de CDVD que não é fornecido pela pilha moderna
  `cdfs.irx` usada pelo projeto.
- As mudanças foram desenhadas para boot por ELF, ISO, OPL e wLaunchELF. Elas
  removem dependências conhecidas de BIOS, mas testes comunitários em hardware
  real continuam importantes; não se declara compatibilidade universal sem
  validar cada revisão física.

### MMCE, MX4SIO e armazenamento

- `sio2man`, `mcman`, `mcserv`, `padman`, `mtapman`, `mmceman` e
  `mx4sio_bd` agora vêm de um conjunto fixado e coerente de IRX em `irx/`.
  Uma build oficial não mistura mais versões incompatíveis desses módulos.
- O carregador de IRX agora verifica também o resultado de `_start()`. Um ID de
  módulo retornado pelo LOADFILE não é mais tratado como sucesso quando o IRX
  respondeu `MODULE_NO_RESIDENT_END` e não permaneceu residente.
- MMCE não é mais considerado presente apenas porque `mmceman.irx` abriu. O
  programa envia um PING às duas portas físicas e só lista os slots que
  realmente responderam.
- O BGM do menu agora procura `.mod` e `.xm` em `mmce0:` e `mmce1:`. A busca
  só acessa slots confirmados pelo PING e também funciona quando MMCE é ativado
  no Video Config depois que o índice inicial de músicas já foi criado.
- O HDD interno também virou uma fonte de BGM. Com **HDD Support** ligado, o
  programa enumera as partições APA/PFS principais e usa a primeira que tiver
  `SNESticle/bgm` ou `bgm`.
- A opção de destino MMCE dos save states também usa o resultado desse PING,
  eliminando destinos vazios/fantasmas.
- MMCE e MX4SIO são tratados como backends SIO2 mutuamente exclusivos. Ativar
  um desativa a configuração do outro; se o driver oposto já estiver residente,
  a interface pede reinício e aplica a troca no próximo boot limpo.
- USB continua usando a pilha BDM moderna, com múltiplos `massN:`, FAT16,
  FAT32, exFAT, MBR e GPT.
- O HDD interno continua com carregamento preguiçoso e desligado por padrão:
  `dev9`, `ps2atad`, `ps2hdd` e PFS só sobem no primeiro uso pelo navegador ou
  pela busca de BGM, depois que o usuário habilita **HDD Support**.
- Resultados essenciais de importação/boot dos módulos permanecem visíveis para
  diagnosticar uma falha que ocorra antes de a interface abrir.

### Música do menu e Issue #16

- Os players parciais `jar_mod` e `jar_xm` foram substituídos pelo port PS2 de
  **libxmp-lite** em modo core.
- O novo player respeita com muito mais fidelidade efeitos de tracker, tempo,
  instrumentos, mudanças de padrão e pontos de loop de `.mod` e `.xm`.
- A saída do tracker é reamostrada continuamente para os 48 kHz do SPU2 sem
  acumular erro de arredondamento entre frames.
- A taxa padrão de síntese passou para **24 kHz**, reduzindo picos de CPU. O
  usuário ainda pode escolher de 16 a 48 kHz no Video Config.
- A quantidade produzida por frame é limitada para evitar engasgos em módulos
  pesados, sem alterar o tempo musical acumulado.
- A troca de faixa respeita o loop completo informado pelo libxmp, descarta o
  reinício indevido do módulo e insere um pequeno intervalo limpo entre faixas.
- Arquivos inválidos são retirados da playlist um por frame, impedindo que uma
  pasta com várias tracks ruins congele o menu por muito tempo.
- Pastas locais são indexadas imediatamente. O CD/DVD recebe um período de
  espera e consultas não bloqueantes; `cdfs:/BGM` só é aberto depois de o disco
  responder pronto de forma estável.
- A busca aceita subpastas de BGM até quatro níveis e evita entradas duplicadas.
- Uma pasta `bgm` ao lado do ELF é encontrada automaticamente. Caminhos
  `cdrom0:` recebidos pelo launcher são normalizados para `cdfs:`.
- Em MMCE, são aceitas as pastas `mmce0:/SNESticle/bgm`, `mmce0:/bgm`,
  `mmce1:/SNESticle/bgm` e `mmce1:/bgm`, incluindo subpastas até quatro
  níveis. Cada ativação faz somente uma sondagem e uma varredura por sessão.
- No HDD interno, a playlist conserva o caminho lógico completo
  `hdd0:/PARTIÇÃO/...` e remonta automaticamente a partição correta antes de
  carregar cada faixa. Navegar ou carregar uma ROM de outra partição não
  invalida as músicas já indexadas.
- `make iso ... bgm=/pasta/das/tracks` preserva as subpastas em `BGM/` e agora
  interrompe a build com uma mensagem clara se não encontrar nenhum `.mod` ou
  `.xm`.
- A música continua mutando ao abrir um jogo e uma faixa diferente é escolhida
  quando o usuário volta ao menu, se houver mais de uma disponível.

### Compatibilidade do core do SNES

- O reset completo do 65C816 agora inicia o stack pointer em **`$01FF`**, como
  no hardware. Antes o byte baixo permanecia em `$00`, deslocando os primeiros
  pushes e quebrando jogos que dependem do estado correto da pilha após reset.
- A instrução reservada **WDM (`$42`)** agora consome seu byte de assinatura e
  continua na instrução seguinte, em vez de cair no tratamento de STP.
- O espelhamento de ROM deixou de usar módulo simples e passou a reproduzir as
  linhas de endereço do cartucho. Isso corrige especialmente ROMs cujo tamanho
  não é potência de dois, como cartuchos LoROM de **12 Mbit / 1,5 MiB**.
- O mapeamento é feito por páginas de 8 KiB, incluindo regiões LoROM, HiROM e
  os espelhos usados por ExLoROM.
- Adicionada detecção e conversão de imagens de ROM **Type-1 interleaved**, com
  nova leitura dos headers após a normalização.
- O título interno de 21 bytes do cartucho agora é copiado para uma string
  própria, sanitizada e terminada em NUL. Isso impede que as informações da ROM
  continuem lendo bytes de outros campos e apareçam esticadas/corrompidas.
- As portas de comunicação CPU/APU `APUIO0-3` (`$2140-$2143`) agora possuem os
  espelhos de hardware completos até **`$217F`**, tanto para leitura quanto
  para escrita e fila de sincronização do SPC.

#### Jogos confirmados após as correções

| Jogo | Sintoma anterior | Resultado |
|---|---|---|
| Captain Commando (USA) | Informações da ROM apareciam por instantes e o jogo permanecia em tela preta | Inicialização e imagem corrigidas |
| Mighty Morphin Power Rangers: The Movie | Travava depois da logo da Bandai | Corrigido |
| Mighty Morphin Power Rangers | Travava durante a inicialização | Corrigido |
| Mighty Morphin Power Rangers: The Fighting Edition | Não avançava corretamente na inicialização | Corrigido |

As correções são de comportamento de hardware e podem beneficiar outros jogos
com os mesmos padrões de acesso; não existe lista de exceções baseada no nome
da ROM.

### Limpeza da build final

- Desativada a instrumentação temporária `SNDBG_LOG` de timing, PPU, DMA e
  S-DD1.
- Removidos os traces de instrução CPU/APU usados para localizar o loop dos
  Power Rangers.
- Removidos os logs ativos de GO/STOP/polling do SuperFX, a contagem diagnóstica
  de pixels, o dump de detecção de chip da ROM e as mensagens temporárias da
  carga de SRAM.
- Removidos os logs verbosos de varredura do BGM, navegador de arquivos e
  índice de capas.
- `PROFILE`, `DSP4_CAPTURE` e a tela de boot de debug continuam desligados por
  padrão. Os registros curtos de boot/importação de dispositivo foram mantidos
  porque são operacionais e permitem identificar falhas anteriores à UI.

### Build e distribuição

- A versão padrão da build e o rodapé da interface estão em **1.0.3**.
- O ELF linkado com símbolos, o ELF stripped/unpacked e o ELF packed são alvos
  separados; empacotar não modifica mais o arquivo original.
- `make elf` gera os dois formatos de distribuição:

  - `build/SNESticle.stripped.elf` — unpacked;
  - `build/SNESticle.packed.elf` — packed.

- O target ISO usa o ELF stripped quando `PACK=0` e o packed quando `PACK=1`.
- O Makefile pode reconstruir uma cópia local do `ps2-packer` e seus stubs se a
  ferramenta fornecida pelo ambiente estiver ausente ou quebrada.

Exemplos:

```bash
make elf

make iso \
  ROMS=/caminho/roms \
  bgm=/caminho/tracks \
  OUT=/caminho/saida

make iso PACK=0 \
  ROMS=/caminho/roms \
  bgm=/caminho/tracks \
  OUT=/caminho/saida
```

### Limitações conhecidas

- A nova pilha foi preparada para modelos Deckard e para diferentes launchers,
  mas ainda deve ser testada no maior número possível de consoles reais,
  revisões de MMCE, firmwares de MemCard PRO 2/SD2PSX e configurações de OPL.
- O novo BGM é consideravelmente mais fiel, porém módulos tracker incomuns ou
  malformados ainda podem apresentar diferenças.
- Save states continuam disponíveis apenas para o hardware base do SNES.
  Estados de jogos com DSP, SuperFX, CX4, OBC1, S-DD1, S-RTC ou Super Game Boy
  permanecem bloqueados até que o estado completo desses chips seja
  serializado.

---

## English

### Highlights

- Rebuilt the storage/SIO2 initialization path for broader **Deckard** slim
  compatibility (`SCPH-7700x` and later) and reliable IRX residency checks.
- Fixed MMCE integration for **MemCard PRO 2** and **SD2PSX**, including real
  device probing before exposing `mmce0:` or `mmce1:`.
- Replaced the menu BGM replay path with **libxmp-lite**, improving tracker
  effects, instruments, pitch, timing, and loops.
- Restored ISO BGM without blocking boot or freezing the device-selection
  screen.
- Stabilized direct packed and unpacked ELF boot, including launchers which do
  not provide a usable `argv[0]`.
- Fixed **Captain Commando (USA)** and the tested **Mighty Morphin Power
  Rangers** titles through general SNES hardware-emulation corrections rather
  than per-ROM hacks.
- Removed the verbose diagnostic instrumentation used by the test builds.

### Boot, IOP, and Deckard models

- Every boot device now follows one IOP reset. The old memory-card-only second
  reset, which discarded loaded modules while leaving stale EE-side state, was
  removed.
- Boot no longer depends on `rom0:EELOADCNF`, reducing BIOS/model-specific
  behavior.
- `SifLoadFileInit`, cache state, and embedded-module runtime state are reset in
  a consistent order after the IOP reboot.
- The ELF path is copied into owned, always-valid buffers. `host:` is used as a
  safe fallback when a launcher supplies no executable path.
- CD/DVD starts with `SCECdINoD`, avoiding a mechanical disc-ready wait during
  boot, and the obsolete CDVD RPC path is no longer used.
- The changes target direct ELF, ISO, OPL, and wLaunchELF boot paths. They
  remove known BIOS assumptions, but broad real-hardware testing is still
  required before claiming every physical revision is verified.

### MMCE, MX4SIO, and storage

- `sio2man`, `mcman`, `mcserv`, `padman`, `mtapman`, `mmceman`, and
  `mx4sio_bd` are pinned as one coherent in-tree IRX set.
- The embedded IRX loader now checks the module `_start()` result in addition
  to the LOADFILE module ID. `MODULE_NO_RESIDENT_END` is correctly treated as
  failure.
- MMCE support PINGs both physical ports and lists only slots which actually
  answer. Save-state destination selection uses the same result.
- Menu BGM now discovers `.mod` and `.xm` tracks on `mmce0:` and `mmce1:`.
  Only PING-confirmed slots are accessed, including when MMCE is enabled from
  Video Config after the initial music index has already been built.
- The internal HDD is also a BGM source. With **HDD Support** enabled, the
  player enumerates main APA/PFS partitions and uses the first one containing
  `SNESticle/bgm` or `bgm`.
- MMCE and MX4SIO are mutually exclusive SIO2 backends. If the opposite driver
  is already resident, the UI requests a restart and applies the change after
  the next clean IOP boot.
- USB keeps the modern multi-device BDM stack with FAT16/FAT32/exFAT and
  MBR/GPT support.
- Internal-HDD modules remain disabled by default and lazy-loaded on the first
  browser or BGM access after the user enables **HDD Support**.
- Essential boot/module import results remain available for failures which
  occur before the UI can appear.

### Menu music and Issue #16

- Replaced the partial `jar_mod`/`jar_xm` players with the PS2 port of
  **libxmp-lite** in core-player mode.
- Tracker effects, timing, instruments, pattern changes, and loop points for
  `.mod` and `.xm` files are reproduced much more accurately.
- Source audio is continuously resampled to the SPU2's 48 kHz output without
  accumulating per-frame rounding drift.
- The default synthesis rate is now **24 kHz**, with 16–48 kHz still available
  in Video Config.
- Per-frame synthesis is bounded to avoid CPU spikes on heavy modules while
  preserving accumulated musical timing.
- Playlist advancement follows libxmp's completed-loop state, prevents an
  unwanted fragment from the restarted loop, and inserts a clean short gap.
- Invalid tracks are removed one per frame so several bad files cannot stall
  the menu for a long time.
- Local folders are indexed immediately. CD/DVD uses a grace period and
  non-blocking readiness polls, and `cdfs:/BGM` is opened only after stable
  ready responses.
- BGM subfolders are scanned up to four levels and duplicate paths are ignored.
- A `bgm` folder beside a direct ELF is detected automatically; launcher
  `cdrom0:` paths are normalized to `cdfs:`.
- MMCE searches `mmce0:/SNESticle/bgm`, `mmce0:/bgm`,
  `mmce1:/SNESticle/bgm`, and `mmce1:/bgm`, including subfolders up to four
  levels, with one probe/scan per activation rather than per-frame polling.
- HDD playlist entries retain their complete logical
  `hdd0:/PARTITION/...` path and remount the correct PFS partition before each
  track load, so browsing or loading a ROM from another partition does not
  invalidate already indexed music.
- `make iso ... bgm=/tracks` preserves subfolders and fails clearly when the
  source contains no `.mod` or `.xm` files.

### SNES core compatibility

- A hard 65C816 reset now initializes the stack pointer to **`$01FF`**. Soft
  reset retains the low byte while restoring the emulation-mode stack page.
- Reserved instruction **WDM (`$42`)** consumes its signature byte and resumes
  at the following opcode instead of falling through into STP handling.
- ROM mirroring now follows cartridge address-line behavior instead of simple
  modulo wrapping, fixing non-power-of-two images such as 12-Mbit/1.5-MiB
  LoROM cartridges.
- ROM mapping is installed per 8-KiB page, including LoROM, HiROM, and ExLoROM
  mirror paths.
- Added detection and normalization of **Type-1 interleaved** ROM images.
- The fixed 21-byte internal ROM title is copied into a sanitized,
  NUL-terminated buffer, preventing UI/log reads into adjacent header fields.
- CPU/APU communication ports `APUIO0-3` (`$2140-$2143`) now mirror correctly
  through **`$217F`** for reads, writes, and queued SPC synchronization.

#### Confirmed games after the fixes

| Game | Previous symptom | Result |
|---|---|---|
| Captain Commando (USA) | ROM information flashed briefly, followed by a permanent black screen | Boots and renders correctly |
| Mighty Morphin Power Rangers: The Movie | Froze after the Bandai logo | Fixed |
| Mighty Morphin Power Rangers | Froze during startup | Fixed |
| Mighty Morphin Power Rangers: The Fighting Edition | Failed to progress correctly through startup | Fixed |

These are hardware-behavior fixes and may benefit other titles using the same
access patterns; no ROM-name compatibility exceptions were added.

### Final release cleanup

- Disabled temporary `SNDBG_LOG` timing/PPU/DMA/S-DD1 instrumentation.
- Removed the CPU/APU instruction traces used to locate the Power Rangers wait
  loop.
- Removed active SuperFX GO/STOP/poll traces, diagnostic pixel counters, ROM
  chip-detection dumps, and temporary SRAM-load messages.
- Removed verbose BGM scanning, file-browser, and cover-index logging.
- `PROFILE`, `DSP4_CAPTURE`, and the debug boot screen remain off by default.
  Short operational boot/device-import reports are intentionally retained for
  failures which happen before the UI exists.

### Build and distribution

- The default build version and UI footer are **1.0.3**.
- The symbol-bearing linked ELF, stripped/unpacked ELF, and packed ELF are now
  separate targets; packing no longer modifies the original executable.
- `make elf` produces:

  - `build/SNESticle.stripped.elf` — unpacked;
  - `build/SNESticle.packed.elf` — packed.

- ISO builds use the stripped ELF with `PACK=0` and the packed ELF with
  `PACK=1`.
- The Makefile can rebuild a local `ps2-packer` and its stubs when the host
  copy is missing or broken.

### Known limitations

- The storage changes target Deckard consoles and multiple launch paths, but
  wider tests are still needed across physical consoles, MMCE revisions,
  MemCard PRO 2/SD2PSX firmware versions, and OPL configurations.
- libxmp playback is substantially more accurate, but unusual or malformed
  tracker modules may still differ.
- Save states remain limited to base SNES hardware. Games using DSP, SuperFX,
  CX4, OBC1, S-DD1, S-RTC, or Super Game Boy hardware remain blocked until
  those coprocessor states are fully serialized.
