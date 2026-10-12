# Guia do usuário

## Primeira execução (wizard)
Welcome to Studio Live Pro → 1 driver (ASIO/WASAPI) → 2 interface → 3 sample rate → 4 buffer → 5 teste de áudio (tom 440 Hz) → 6 saídas (principal e monitor) → 7 streaming opcional. Final: **Studio Live Pro is ready.** Reabra em *Audio Setup* a qualquer momento.

## Drivers, sample rate e buffer
- **ASIO**: menor latência; exige SDK no build e driver do fabricante.
- **WASAPI Exclusive**: baixa latência com placas comuns; o app controla o dispositivo. **Shared**: convive com outros apps, maior latência.
- Sample rates 44.1/48/88.2/96 kHz; buffers 32–1024. Comece com 48 kHz / 256. Barra de status mostra latência, CPU, XRuns e overloads. Se aparecerem XRuns, aumente o buffer.
- A latência interna do motor (limiter + clipper) é exibida; o relatório algorítmico está em `TEST_REPORT.md`.

## Mixer
16 canais: nome (duplo clique), Mute/Solo, EQ on, CMP on, pan, fader (-100…+6 dB), medidor de pico. **edit** seleciona o canal para o editor inferior. Cada canal tem: ganho, fase, HPF 24 dB/oct, gate, EQ 24 bandas, Dynamic EQ, de-esser, compressor, saturação, transient, sends (4 aux, 2 FX), subgrupo/master. Canal estéreo usa duas entradas consecutivas.

Roteamento: saídas do motor = Main (1-2), Monitor/Headphone (3-4), Aux 1 Gravação (5-6), Aux 2 Streaming (7-8), Aux 3 (9-10). O wizard mapeia pares de saída físicos.

## Easy × Pro
**EASY / PRO** alterna o editor inferior. Easy: Clarity, Warmth, Punch, Presence, Space, Loudness (macros que ajustam EQ/saturação/transient/envio de reverb/limiter). Pro: todos os parâmetros do canal selecionado.

## Master
Cadeia: EQ → Dynamic EQ → Glue → Multiband → Saturation → Stereo (bass mono) → Soft Clipper → True Peak Limiter → medidores. Botões LIMITER/GLUE/CLIP, MONO e DIM de monitor, **EMERGENCY BYPASS** (pula todo processamento exceto ganho/fader e proteção de pico) e **SAFE MODE** (reduz automaticamente módulos pesados se a CPU passar de 85%; registra no log). Painel mostra LUFS M/S/I, True Peak, LRA, correlação, largura e aviso de compatibilidade mono.
Referências: YouTube ≈ -14 LUFS, rádio/TV (EBU R128) -23 LUFS; true peak ≤ -1 dBTP.

## Presets, cenas, snapshots
Combos de preset de canal (Vocal, Church, Podcast, Broadcast, YouTube, Drum, Bass, Guitar, Keyboard, Piano, Live) e master/live. Botões 1–5: clique carrega a cena (se vazia, grava a atual); **Shift+clique** grava. **Snap +/<** cria/carrega snapshots; **Undo/Redo**; **A/B** alterna com level match (compara por loudness); **Before/After** desliga todo o processamento para comparação.

## Projetos
New / Open / Save / Save As (`.slpproj`, texto legível): cena atual, 32 cenas, snapshots, parâmetros, roteamento.

## MIDI
Todas as entradas MIDI são abertas automaticamente. **MIDI learn** vincula o próximo CC/nota ao fader do canal selecionado. Alvos disponíveis no motor: fader/pan/mute/solo de canal, master fader, Emergency Bypass, recall de cena/snapshot (mapeamento via `MidiMapper`, salvável em texto).

## AI Studio Magic / VOCAL MAGIC
Toque/cante no canal selecionado por ≥3 s e clique **VOCAL MAGIC**. O app analisa espectro, ruído, dinâmica e sibilância, propõe correções moderadas (EQ ≤ ±3 dB, HPF, gate, de-esser, compressor), mostra ANTES/DEPOIS e "Por quê?" para cada alteração; só aplica se você confirmar. **Capture noise** grava um perfil de ruído (silêncio) e ativa a redução.

## OBS Studio / streaming
Opção A: instale um cabo virtual (ex.: VB-Cable). No wizard (etapa 7) habilite "virtual output" e escolha o dispositivo "CABLE Input". No OBS: *Fontes > Captura de saída de áudio/entrada* → "CABLE Output". Opção B: use o Aux 2 (Streaming) numa interface com saídas físicas ligadas à entrada do OBS. Feche e abra o OBS após criar o dispositivo. Use 48 kHz em ambos.

## Solução de problemas
- **Sem som**: Audio Setup → verifique saída e canais ativos; wizard etapa 5 (tom de teste).
- **Estalos/XRuns**: aumente buffer; desative oversampling em saturação/clipper; ative Safe Mode.
- **ASIO não aparece**: build sem SDK ASIO (use WASAPI) ou driver do fabricante não instalado.
- **OBS sem áudio**: confirme dispositivo virtual e mesmo sample rate; veja "VirtOut underruns" na barra de status.
- **Logs**: `Documentos\StudioLivePro\Logs\` (dispositivos, XRuns, overloads, bypass, crashes com stack trace).

## Limitações conhecidas (v1.0)
EQ em fase linear, soft-clipper multibanda e compensação automática de latência entre canais (PDC) não estão implementados; Auto Unmask apenas sugere. Cadeias "Vocal/Drum/Bass/Guitar/Keys Studio" são fornecidas como presets de canal.
