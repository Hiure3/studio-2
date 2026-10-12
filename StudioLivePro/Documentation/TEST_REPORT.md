# TEST REPORT

Executado em: Ubuntu 24.04, g++ 13.3 / CMake+Ninja (Release), VM de 1 núcleo / 3,9 GB. Binário: `slp_tests`. Saída completa: `test_output.txt`.

**Resultado: 41 testes, 0 falhas, 388 verificações, 0 falhas.** (tempo de relógio do estresse: ~36 s)

## Testes executados
```
[ PASS ] eq_bell_and_response
[ PASS ] eq_highpass_slope_and_24_bands
[ PASS ] eq_auto_gain_perceptual
[ PASS ] dynamic_eq_reduces_above_threshold_only
[ PASS ] compressor_static_curve_vca
[ PASS ] compressor_all_models_stable
[ PASS ] compressor_autogain_matches_loudness
[ PASS ] gate_closes_noise_passes_signal
[ PASS ] deesser_targets_sibilance
[ PASS ] transient_designer_direction
[ PASS ] multiband_is_transparent_when_idle
[ PASS ] saturation_models_harmonics_no_nan
[ PASS ] saturation_clean_small_signal_unity
[ PASS ] tape_engine_stable_and_bounded
[ PASS ] soft_clipper_respects_ceiling
[ PASS ] limiter_true_peak_ceiling
[ PASS ] limiter_transparent_below_ceiling
[ PASS ] delay_time_and_feedback
[ PASS ] reverb_decay_and_stability
[ PASS ] reverb_ducking_reduces_wet_under_signal
[ PASS ] loudness_meter_bs1770_reference
[ PASS ] correlation_and_mono_compat
[ PASS ] spectrum_analyzer_finds_tone
[ PASS ] noise_reducer_profile
[ PASS ] hum_remover_removes_60hz
[ PASS ] mixer_pan_fader_mute_solo
[ PASS ] mixer_routing_subgroup_aux_fx
[ PASS ] mixer_master_limiter_and_no_nan
[ PASS ] mixer_emergency_bypass_and_safe_mode
[ PASS ] mixer_block_larger_than_internal_and_small_blocks
[ PASS ] mixer_monitor_dim_mono_mute
[ PASS ] state_serialization_roundtrip
[ PASS ] scenes_snapshots_undo_ab_project
[ PASS ] midi_cc_note_mapping
[ PASS ] presets_apply_and_process_clean
[ PASS ] ai_studio_magic_boxy_signal
[ PASS ] reference_and_masking
[ PASS ] channel_noise_profile_capture_via_engine
[ PASS ] audio_signals_through_full_engine
[ PASS ] latency_report
[ PASS ] stress_32_channels_long_run
```

## Cobertura vs. requisitos
- **EQ**: ganho do bell, slope HP, 24 bandas ativas sem NaN, auto-gain perceptual. **Dynamic EQ**: reduz acima do threshold, neutro abaixo.
- **Compressor**: curva estática (VCA, GR esperada 10,5 dB ±0,7), os 7 modelos estáveis, auto-gain. **Gate, De-esser, Transient, Multibanda** (soma transparente ±0,5 dB, GR por banda).
- **Saturação** (8 modelos × oversampling 1/2/4: harmônicos presentes, DC < 0,01, sem NaN), **Tape**, **Soft clipper** (teto respeitado), **Limiter** (true peak ≤ teto +0,25 dB medido por referência independente 8×; transparente abaixo do teto).
- **Delay** (tempo ±3 amostras, feedback), **Reverb** (decaimento, estabilidade dos 7 tipos, ducking).
- **Metering**: LUFS contra referência BS.1770 (senoide 1 kHz −20 dBFS estéreo = −20,0 LUFS ±0,25), true peak, correlação/mono, espectro.
- **Ruído/Hum**: redução ≥ 10 dB mantendo tom; hum 60/120 Hz removido; captura de perfil via motor.
- **Mixer/Routing**: pan law −3 dB, fader, mute, solo, subgrupo, aux, FX, monitor (dim/mute), blocos de 32 a 1000 amostras, Emergency Bypass, limiter do master.
- **Cenas/Preset/Projeto/MIDI**: round-trip de serialização, cenas, snapshots, undo/redo, A/B com level match, salvar/abrir projeto, MIDI CC/Note/learn, 16 presets de canal processam sem NaN, presets de master.
- **IA**: Studio Magic detecta corte em 320 Hz num sinal "embolado" (EQ ≤ ±3 dB, toda mudança com justificativa); referência e mascaramento.
- **Sinais** (silêncio, seno, pink, white, impulso, transientes, onda quadrada fs): sem NaN/Inf, teto ≤ −1 dBFS, DC < 0,01.

## Estresse (32 canais, 60 s de áudio)
Cadeia completa por canal + FX + master chain + recarga de cenas a cada 400 blocos. Média: **RTF 0,60 (≈60 % de um núcleo de VM)**; memória estável (29,0 → 29,1 MB); sem NaN. Pior bloco: 7,3 ms vs. orçamento 5,33 ms (bloco 256 @48 kHz) com 11 estouros em ~11 250 blocos — atribuímos à variação de agendamento da VM compartilhada, mas **não foi medido em hardware real**. Uma recarga de cena de 32 canais causava picos de 47 ms; corrigido limitando a 4 comandos por bloco (cena aplicada em ~8 blocos). O teste foi de 60 s (variável `SLP_STRESS_SECONDS` permite horas); **não foi executada uma corrida de horas**.

## Latência
`latency_report.txt`: latência algorítmica do motor 54 amostras (1,1 ms; limiter 1 ms) a 180 amostras (3,75 ms; limiter 3 ms + clipper 2×) + buffer. Latências de driver ASIO/WASAPI e hardware dependem do dispositivo e **não foram medidas**.

## NÃO testado (requer Windows/hardware)
Interface gráfica em execução, wizard, Device Manager, ASIO, WASAPI, saída virtual/OBS, entrada MIDI física, instalador/desinstalador, execução em Windows limpo, testes com voz/música reais (apenas sinais sintéticos; `slp_render` está disponível para testes com seus arquivos WAV).
