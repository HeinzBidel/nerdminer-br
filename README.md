# Nerd Miner BR

Firmware em português para o **NerdMiner v2 (LilyGO T-Display S3)**. É um fork do
[NerdMiner v2](https://github.com/BitMaker-hub/NerdMiner_v2) (licença MIT) com interface própria, pool reserva,
preço em reais, painel na rede de casa e o SHA-256 espremido até perto do limite do chip.

O NerdMiner é uma **loteria de Bitcoin**: ele tenta achar um bloco sozinho. A chance é minúscula, e este
firmware diz isso na tela, com a conta (veja «A conta honesta», abaixo).

## Baixar e gravar (pelo Chrome, sem instalar nada)

1. Baixe `NerdminerV2_factory.bin` na página de [Releases](../../releases).
2. Ligue a placa no computador com um cabo USB **de dados**.
3. Abra **https://espressif.github.io/esptool-js/** no Chrome, Edge ou Brave e clique em **Connect**.
   Escolha a porta *USB JTAG/serial debug unit*.
4. **Primeira vez:** clique em **Erase Flash** e espere terminar (uns 40 s).
   **Atualização:** pule este passo; o Wi-Fi, a carteira e a pool continuam salvos.
5. Em *Flash Address* escreva **`0x0`**, escolha o arquivo `NerdminerV2_factory.bin` e clique em **Program**.
6. Quando aparecer *Hash of data verified*, tire o cabo e ligue de novo.

Para voltar a outro firmware, grave o `.bin` dele do mesmo jeito.

## Configurar

1. Na primeira vez, a tela mostra **AJUSTES** com um QR code. Aponte o celular: ele entra na rede
   `NerdMinerAP` (aberta, sem senha) e abre a página de ajuste. Se não abrir, vá em `192.168.4.1`.
2. Preencha:

   | Campo | Valor sugerido |
   |---|---|
   | Pool url / port | `public-pool.io` / `3333` |
   | Pool reserva / porta | `pool.nerdminers.org` / `3333` |
   | Your BTC address | o seu endereço (pode acrescentar `.nome` para ver o aparelho no site da pool) |
   | TimeZone | `-3` (Brasília) |

3. Salve. A rede de ajuste some e o aparelho entra no seu Wi-Fi.

Para mudar depois, ligue o aparelho segurando a tecla de cima.

## O que muda em relação ao NerdMiner v2

- **Interface própria, pixelada** (fonte de 8 bits, estilo videogame antigo), com 8 telas: Minerando, Sorte,
  Pool, Rede, Taxas, Relógio, Aparelho e Ajustes (com QR do painel). Tecla de cima troca de tela; tecla de
  baixo liga/desliga a tela, dois cliques giram.
- **Painel na rede de casa:** `http://<IP do aparelho>/` mostra tudo no celular ou no computador.
  Os dados saem em JSON em `/api/estado`.
- **Pool reserva:** se a principal falhar 3 vezes ou ficar 10 min sem mandar trabalho, troca sozinha e
  depois volta.
- **Preço em reais** com a variação de 24 h, fuso de Brasília e textos em português.
- **Melhor hash** achado pelo próprio aparelho (o original só contava o que a pool aceitava).
- **Velocidade de cada motor** (SHA em hardware e em software) e tempo estimado por parte aceita.
- **Correções:** o estado «minerando» nunca era ligado no original; a rede de ajuste abre sem senha;
  um `printf` recebia `String` onde esperava texto.

## Desempenho: medido, não prometido

Tudo medido na mesma placa (T-Display S3, ESP32-S3 a 240 MHz), na mesma pool, em 24/10/2026.

| O que foi feito | Antes | Depois |
|---|---|---|
| SHA em hardware: deixar de reescrever 6 registradores que valem zero nos dois blocos do hash duplo | 215–219 kH/s | 256–259 kH/s (**+18 a +20%**) |
| Tela: trocar a fonte suavizada (FreeType) pela fonte de 8 bits | ~262 kH/s no total | **287 kH/s** no total (~+10%) |

Hoje o aparelho faz **287 kH/s** minerando (hardware 242 + software 38).

**Estamos a ~3% do limite do chip.** Os dois motores, medidos sozinhos no autoteste da partida, somam
256 + 41 = **~297 kH/s**. Esse é o teto desta placa. Os 287 kH/s minerando são 96,6% disso; o resto se
perde dividindo o processador com o Wi-Fi, a pool e a tela.

O caminho rápido do SHA em hardware **não é ligado às cegas**: a cada partida, um autoteste compara os dois
caminhos com o SHA-256 do mbedtls, hash por hash, mede a velocidade de cada um e só liga o rápido se ele
estiver certo e for mais veloz. O resultado aparece na serial (`[AUTOTESTE]`) e na tela Aparelho.

### E assembly? E linguagem de máquina?

Foi investigado e medido antes de escrever uma linha:

- **Linguagem de máquina e assembly são a mesma coisa** (cada instrução de assembly é uma instrução do chip).
  Uma linguagem nova também não ajudaria: no fim ela vira as mesmas instruções.
- **Motor de hardware:** quem calcula o SHA é um circuito do chip. O tempo vai em escrever registradores e
  esperar o circuito; o compilador já gera essas mesmas instruções. Assembly não ganha nada aqui. O ganho
  veio de escrever **menos** registradores (+18 a +20%, acima).
- **Motor de software:** o compilador gerou **5.843 instruções** para o SHA duplo, cerca de **47 por
  rodada**. A conta do mínimo teórico no Xtensa LX7 dá ~46. As rotações já usam a forma mais curta do chip
  (`ssai` + `src`) e quase nada vai para a memória por falta de registrador. Assembly à mão ganharia no
  máximo 2 a 5% nesse motor, ou seja, **menos de 1% no total**.
- **Instruções vetoriais do S3 (PIE):** a soma vetorial `EE.VADDS` **satura** (trava no máximo) em vez de
  dar a volta. O SHA-256 precisa da soma que dá a volta, então esse caminho não serve para SHA-256.

O código original já usava os truques de algoritmo conhecidos (midstate, 3 rodadas pré-calculadas por
trabalho, desistir do hash na rodada 60 quando ele já não pode servir). O que sobrou de ganho estava no
hardware e na tela, e foi o que este firmware fez.

## A conta honesta

A chance de achar um bloco é `velocidade × segundos do ano ÷ (dificuldade × 2^32)`. Com 287 kH/s e a
dificuldade de outubro de 2026 (~140,6 T), dá **1 chance em ~67 bilhões por ano**. A tela Sorte faz essa conta
ao vivo.

A public-pool fixa a dificuldade das partes em 1. Com isso sai **uma parte aceita a cada ~4 horas**, o que é
normal e não muda a chance de achar um bloco.

## Para quem desenvolve

- Compila com PlatformIO: `pio run -e NerdminerV2`. Cada envio ao ramo `mac/nosso-firmware` compila sozinho
  no GitHub Actions e deixa os `.bin` como artefato.
- Opções de build no ambiente `NerdminerV2` do `platformio.ini`: `BRASIL`, `NOSSA_TELA`, `PAINEL_WEB`,
  `AJUSTE_SEM_SENHA`, `SCREEN_FEES_ENABLE`, `SCREEN_WORKERS_ENABLE`.
- `/api/afinidade?modo=0|1|2` troca o núcleo de cada motor (0 livre, 1 hardware no núcleo 0, 2 hardware no
  núcleo 1) e reinicia. Medido em 24/10: 287 / 290 / 282 kH/s, diferenças dentro do ruído (±2%). O padrão
  continua 0 (livre).
- A página do painel fica em `web/painel.html`; `python3 web/gerar_header.py` a embute em `src/painel_html.h`.
- O QR code é gerado por um codificador próprio e pequeno (`src/qrMini.cpp`: modo byte, correção L,
  versões 1 a 4), conferido com o leitor de QR do macOS.

## Como foi feito

Desenvolvido por Heinz Bidel com a ajuda do Claude (Anthropic): análise do código original, autotestes no
próprio aparelho, compilação no GitHub Actions, gravação pelo Chrome e cada mudança medida antes e depois.

## Licença e créditos

MIT, como o original. Créditos ao [BitMaker](https://github.com/BitMaker-hub/NerdMiner_v2) e a todos os
colaboradores do NerdMiner v2; o README original está em [README-original.md](README-original.md).
