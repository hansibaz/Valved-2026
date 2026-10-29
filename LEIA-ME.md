# Valved v0.5.1 — simulador de amp, gabinete e IR

## O que tem nesta versão

- **4 amplificadores:**
  - Clean 120: limpo de transistor, com chorus estéreo (Rate e Depth funcionam só com o chorus ligado)
  - Brit 410: crunch britânico, com Resonance
  - 51 Block: high gain agressivo, com Resonance
  - Rec: high gain pesado, com chave Vintage/Modern
- **Knobs:** Gain, Bass, Mid, Treble, Presence, Master (+ Resonance, Rate/Depth conforme o amp)
- **Entrada:** Input, Noise Gate e **Transpose/Drop** de meio em meio tom (-12 a +12)
- **Gabinetes internos:** 4x12 Vintage, 4x12 Britânico e 2x12 Aberto
- **Microfone (aba CAB):** arraste o microfone sobre o alto-falante (centro = brilhante, borda = escuro) e use MIC DISTANCE para aproximar/afastar
- **Carregar IR:** aceita arquivos .wav/.aiff de IRs comprados ou gratuitos
- Formatos: **VST3** (Windows e Mac), **AU** (Mac) e **aplicativo independente** (para tocar sem abrir DAW)

## Novidades da v0.5

- **Aba PEDALS** (antes do amp): Green Drive, Yellow Drive, Gold Drive e Blue Drive, cada um com 3 knobs e footswitch. Ordem: Guitarra > Green > Yellow > Gold > Blue > Amp. Drive baixo + Level alto = boost.
- **Aba FX** (depois do gabinete):
  - **Delay** com 10 tipos: Digital, Tape, Analog, Dual, Ping-Pong, Multi-Tap, Reverse, Swell, Ducking e Lo-Fi. Tem Time, Repeats, Mix, Tone, Mod, um knob especial por tipo, sincronia com o BPM da DAW (divisões), TAP tempo e trails.
  - **Reverb** com 9 tipos: Room, Hall, Plate, Spring, Shimmer, Bloom, Cloud, Swell e Gated. Tem Decay, Pre-Delay, Mix, Tone, Mod, um knob especial por tipo e trails.

## Como gerar os instaladores (sem programar)

O código é compilado automaticamente nos servidores do GitHub, de graça.

1. Crie uma conta em github.com
2. Clique em **New repository**, dê o nome `valved`, marque **Private** e crie
3. Na página do repositório, clique em **uploading an existing file**
4. Arraste **todo o conteúdo** desta pasta (inclusive a pasta `.github`) e clique em **Commit changes**
   - No Mac, a pasta `.github` fica oculta: no Finder aperte **Cmd + Shift + .** para mostrá-la
5. Abra a aba **Actions**. A compilação leva uns 10–15 minutos
6. Quando ficar verde, clique na execução e baixe **Valved-Windows** ou **Valved-Mac** no fim da página

## Como instalar

**Windows**
- Copie `Valved.vst3` para `C:\Program Files\Common Files\VST3`
- Ou abra `Valved.exe` para tocar direto

**Mac**
- Copie `Valved.vst3` para `/Library/Audio/Plug-Ins/VST3`
- Copie `Valved.component` para `/Library/Audio/Plug-Ins/Components`
- Como o plugin ainda não tem assinatura da Apple, abra o **Terminal** e cole:
  ```
  sudo xattr -cr /Library/Audio/Plug-Ins/VST3/Valved.vst3 /Library/Audio/Plug-Ins/Components/Valved.component
  ```
- Para o app: clique com o botão direito em `Valved.app` → **Abrir**

**No aplicativo independente (Windows):** vá em **Options → Audio/MIDI Settings**. Em **Audio device type** escolha **ASIO** e selecione o driver da sua interface de áudio. Ajuste o **Audio buffer size** para 64 ou 128 samples (latência baixa) e desmarque **Mute audio input** (vem ligado para evitar microfonia).

Se a sua interface não tiver driver ASIO próprio, use **Windows Audio (Exclusive Mode)** ou instale o ASIO4ALL.

ASIO is a trademark and software of Steinberg Media Technologies GmbH.

## Limitações conhecidas da v0.1

- O Transpose usa um algoritmo simples de baixa latência; em acordes pode soar levemente "ondulado". Será trocado por um de qualidade maior
- Os amps são modelados por filtros e saturação, ainda não por captura de amps reais
- Sem presets ainda

## Fontes

A interface usa as fontes Barlow Condensed e Russo One, ambas com licença SIL Open Font License (permite uso comercial). Os textos das licenças estão em `Resources/Fonts`.

## ASIO e licença (antes de vender)

O build do Windows baixa automaticamente o ASIO SDK da Steinberg e ativa o ASIO no aplicativo independente. Para distribuir um programa fechado (pago) com ASIO, é preciso aceitar o contrato de licença proprietária da Steinberg; confirme os termos no site da Steinberg antes de começar a vender.
