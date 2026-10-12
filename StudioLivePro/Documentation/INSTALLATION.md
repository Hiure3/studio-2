# Instalação e build

## Usuário final
1. Execute `StudioLivePro-Setup.exe` (Windows 10/11 64-bit). Não exige Visual C++ Redistributable (CRT estático).
2. Atalhos: Menu Iniciar (e Área de Trabalho, opcional). Dados do usuário: `Documentos\StudioLivePro\` (`Logs`, `Projects`, `Presets`), preservados na desinstalação.
3. Desinstale em *Configurações > Aplicativos* ou pelo atalho "Uninstall".

## ASIO
O driver ASIO é fornecido pelo fabricante da interface (Focusrite, RME, Behringer...). Para outros usuários poderem escolher "ASIO" no app, o build precisa da SDK ASIO da Steinberg (licença própria, não redistribuída aqui): baixe em steinberg.net/developers e passe `-AsioSdk C:\caminho\ASIOSDK` ao `build.ps1`. Sem a SDK o app usa WASAPI (Shared/Exclusive).

## Build a partir do código (Windows)
Requisitos: Visual Studio 2022 Build Tools (workload C++), CMake ≥ 3.22, Ninja, Inno Setup 6, Git (JUCE 8.0.8 é baixado pelo CMake).
`build.ps1 -InstallDeps` instala tudo via `winget`. Etapas: configura → compila (Release x64) → roda `slp_tests.exe` → copia para `Release\` → gera o instalador.
Teste do instalador em Windows limpo: use uma VM Windows 10/11; instale, abra, desinstale e confira que `Program Files\Studio Live Pro` foi removida e `Documentos\StudioLivePro` foi mantida.

## Linux (somente núcleo/testes/app de desenvolvimento)
`cmake -S . -B Build/lin -G Ninja -DCMAKE_BUILD_TYPE=Release -DSLP_JUCE_DIR=<JUCE>` e `ninja -C Build/lin slp_tests` (use `-DSLP_BUILD_APP=OFF` para só testes).
