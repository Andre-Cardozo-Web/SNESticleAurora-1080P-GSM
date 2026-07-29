# SNESticle Revive PS2 v1.0.2

## Português (Brasil)

### Save states

- Restaurados os atalhos do SNESticle original: `L2 + X` salva o estado e
  `L2 + Círculo` carrega o estado. `L2 + R2` continua reservado para abrir ou
  fechar o menu e salvar a SRAM alterada.
- Adicionada uma tela mostrada somente no primeiro save para escolher o destino:
  Auto, USB, Memory Card, MMCE ou HDD interno. A escolha fica salva e pode ser
  redefinida posteriormente no menu Save States.
- O modo Auto usa sempre o quick slot 1 e, quando somente um Memory Card está
  disponível, utiliza o cartão conectado, dando preferência ao `mc0:`.
- Adicionados cinco quick slots para destinos escolhidos explicitamente.
- O formato de save state agora possui identificação da ROM, versão, geração e
  CRC. Cada slot mantém dois bancos para recuperar automaticamente o banco
  anterior caso o mais novo esteja incompleto ou corrompido.
- Novos estados usam compactação deflate rápida e são gravados sem releituras
  integrais desnecessárias, reduzindo bastante o tempo de salvar e carregar.
- Save states antigos, não compactados e no formato versão 1, continuam
  compatíveis.

### Gerenciamento e interface

- A tela Save States agora funciona como gerenciador de arquivos tanto na tela
  inicial do homebrew quanto durante um jogo pausado.
- É possível navegar pelos estados de USB, Memory Card, MMCE e HDD interno e
  excluir um estado pelo menu de arquivos. A exclusão remove os dois bancos do
  slot correspondente.
- O título Save States foi alinhado ao topo sem alterar a posição original das
  opções e dos textos de ajuda.

### Memory Card, SRAM e áudio

- Memory Cards não formatados são detectados antes do uso. O emulador pergunta
  se o usuário deseja formatar o cartão, mostra que todo o conteúdo será
  apagado e seleciona **Não / Cancelar** por padrão.
- Depois de uma formatação confirmada, a pasta e os ícones do SNESticle são
  recriados e a operação pendente de save ou navegação é retomada.
- A verificação de alterações da SRAM é forçada ao abrir o menu para evitar que
  uma gravação recente do jogo seja ignorada.
- O áudio é silenciado durante operações bloqueantes e restaurado corretamente
  ao voltar ao jogo, evitando repetição do último som e travamentos na retomada.

### Compatibilidade

- Save states estão disponíveis para o hardware base do SNES.
- Jogos com DSP, SuperFX, CX4, OBC1, S-DD1, S-RTC ou Super Game Boy continuam
  bloqueados até que o estado completo desses chips seja serializado.

---

## English

### Save states

- Restored the original SNESticle shortcuts: `L2 + Cross` saves a state and
  `L2 + Circle` loads it. `L2 + R2` remains reserved for opening or closing the
  menu and saving modified SRAM.
- Added a one-time destination chooser shown on the first save: Auto, USB,
  Memory Card, MMCE, or Internal HDD. The choice is remembered and can be reset
  later from the Save States menu.
- Auto always uses quick slot 1 and, when only a Memory Card is available, uses
  the connected card with preference for `mc0:`.
- Added five quick slots for explicitly selected destinations.
- The save-state format now includes ROM identity, version, generation, and CRC
  checks. Each slot keeps two banks so the previous bank can be recovered
  automatically if the newest one is incomplete or corrupt.
- New states use fast deflate compression and avoid unnecessary full-payload
  rereads, substantially reducing save and load times.
- Existing uncompressed version-1 save states remain compatible.

### Management and interface

- The Save States screen now works as a file manager both on the initial
  homebrew screen and while a game is paused.
- States stored on USB, Memory Card, MMCE, and Internal HDD can be browsed and
  deleted through the file menu. Deleting a state removes both banks belonging
  to its slot.
- The Save States title is aligned near the top without moving the original
  option and help-text positions.

### Memory Card, SRAM, and audio

- Unformatted Memory Cards are detected before use. The emulator asks whether
  to format the card, clearly warns that all contents will be erased, and
  selects **No / Cancel** by default.
- After confirmed formatting, the SNESticle directory and icons are recreated
  and the pending save or browse operation resumes.
- SRAM changes are checked immediately when opening the menu so a recent
  in-game write is not missed.
- Audio is muted during blocking operations and restored correctly when
  returning to the game, preventing repeated tail audio and resume deadlocks.

### Compatibility

- Save states are available for base SNES hardware.
- Games using DSP, SuperFX, CX4, OBC1, S-DD1, S-RTC, or Super Game Boy remain
  blocked until the complete state of those chips is serialized.
