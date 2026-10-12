# STUDIO LIVE PRO — *Som de estúdio. Em tempo real.*

Mixer e processador de áudio profissional para Windows 10/11 x64 (C++20 + JUCE), voltado a lives, igrejas, bandas, podcast, rádio, YouTube e OBS.

## O que há de real neste projeto
Todo controle exposto na UI está ligado a DSP real (`Source/DSP`). O motor (`Source/Mixer/MixerEngine.h`) é independente da interface e realtime-safe: sem alocação, sem locks e sem I/O na thread de áudio; alterações chegam por fila lock-free (SPSC).

| Módulo | Conteúdo |
|---|---|
| DSP | EQ 24 bandas (+auto-gain perceptual), Dynamic EQ, 7 modelos de compressor, multibanda 4 bandas (LR4), gate/expansor, de-esser, transient designer, 8 saturações, tape engine, soft clipper, limiter true-peak, delay (6 tipos), reverb FDN (7 tipos), redução de ruído por perfil, remoção de hum 50/60 Hz |
| Metering | LUFS M/S/I (BS.1770-4), LRA, true peak, pico, RMS, crest, correlação, largura, compatibilidade mono, espectro FFT |
| Mixer | 16 canais (arquitetura até 64), subgrupos, 4 aux (monitor/gravação/streaming), 2 FX, master chain completa, monitor (dim/mono/mute), Emergency Bypass, Safe Mode |
| Controle | cenas (32), snapshots, undo/redo, A/B com level match, projetos, presets de fábrica e de usuário, MIDI CC/Note com learn |
| IA | AI Studio Magic (regras explicáveis, antes/depois, "por quê"), comparação com referência, detecção de mascaramento |
| App | Device Manager (ASIO/WASAPI via JUCE), wizard de primeira execução, modo Easy/Pro, saída virtual para OBS, logs |

## Estrutura
`Source/` código · `Tests/` testes · `Installer/` Inno Setup · `Tools/render.cpp` render offline · `build.ps1` build completo no Windows · `Release/` entregáveis.

## Início rápido (Windows)
`powershell -ExecutionPolicy Bypass -File .\build.ps1 -InstallDeps` — instala dependências, compila Release x64, roda os testes e gera `Release\StudioLivePro.exe` e `StudioLivePro-Setup.exe`. Detalhes em `INSTALLATION.md`. Estado real da validação: `BUILD_REPORT.md` e `TEST_REPORT.md`.
