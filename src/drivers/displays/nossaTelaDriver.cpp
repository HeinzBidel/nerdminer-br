// Nosso firmware: interface propria para a LilyGO T-Display S3 (320 x 170 visiveis).
// Telas desenhadas por codigo, sem imagens prontas:
// Minerando, Sorte, Pool, Rede, Taxas, Relogio e Aparelho.
// A fonte NotoSans_Bold embutida so tem ASCII + á í ó ú minusculos: rotulos em maiusculas sem acento.

#include "displayDriver.h"

#ifdef NOSSA_TELA

#include <TFT_eSPI.h>
#include <WiFi.h>
#include <math.h>
#include "media/images_320_170.h"
#include "media/myFonts.h"
#include "version.h"
#include "monitor.h"
#include "mining.h"
#include "utils.h"
#include "OpenFontRender.h"
#include "rotation.h"

#define WIDTH 320
#define HEIGHT 170
#define NUM_TELAS 7

// Cores (RGB565)
#define COR_FUNDO    0x0841  // quase preto azulado
#define COR_CARTAO   0x18E3  // cinza escuro dos cartoes
#define COR_LINHA    0x3186  // bordas e trilhos
#define COR_LARANJA  0xF483  // laranja do Bitcoin (#F7931A)
#define COR_TEXTO    0xFFFF
#define COR_SUAVE    0x9CD3  // rotulos
#define COR_VERDE    0x072E
#define COR_AMARELO  0xFE60
#define COR_VERMELHO 0xFA8A

extern monitor_data mMonitor;
extern double best_diff;

OpenFontRender texto;   // NotoSans_Bold: rotulos e valores
OpenFontRender numeros; // DigitalNumbers: so digitos, ponto e dois-pontos
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite background = TFT_eSprite(&tft);

/********************* AJUDANTES *********************/

static void escreve(const String &s, int x, int y, int tam, uint16_t cor, uint16_t fundo)
{
  texto.setFontSize(tam);
  texto.drawString(s.c_str(), x, y, cor, fundo);
}

static void escreveDir(const String &s, int x, int y, int tam, uint16_t cor, uint16_t fundo)
{
  texto.setFontSize(tam);
  texto.rdrawString(s.c_str(), x, y, cor, fundo);
}

static void escreveCentro(const String &s, int x, int y, int tam, uint16_t cor, uint16_t fundo)
{
  texto.setFontSize(tam);
  texto.cdrawString(s.c_str(), x, y, cor, fundo);
}

// Numero grande em portugues: 55 bi, 1,2 mi, 830 mil
static String numeroGrande(double v)
{
  const char *sufixo = "";
  if (v >= 1e12)      { v /= 1e12; sufixo = " tri"; }
  else if (v >= 1e9)  { v /= 1e9;  sufixo = " bi"; }
  else if (v >= 1e6)  { v /= 1e6;  sufixo = " mi"; }
  else if (v >= 1e3)  { v /= 1e3;  sufixo = " mil"; }
  char buf[24];
  if (v < 10.0)
    snprintf(buf, sizeof(buf), "%.1f%s", v, sufixo);
  else
    snprintf(buf, sizeof(buf), "%.0f%s", v, sufixo);
  for (char *p = buf; *p; ++p)
    if (*p == '.') *p = ',';
  return String(buf);
}

// "R$595000" -> "R$ 595.000"
static String precoComPontos(const String &preco)
{
  String digitos, prefixo;
  for (unsigned int i = 0; i < preco.length(); ++i)
  {
    char c = preco[i];
    if (c >= '0' && c <= '9') digitos += c;
    else if (digitos.length() == 0) prefixo += c;
  }
  String saida;
  int n = digitos.length();
  for (int i = 0; i < n; ++i)
  {
    saida += digitos[i];
    int resto = n - 1 - i;
    if (resto > 0 && resto % 3 == 0) saida += '.';
  }
  return prefixo + " " + saida;
}

// Cabecalho comum: titulo, indicadores e hora
static void cabecalho(const char *titulo, const String &hora)
{
  background.fillRect(0, 0, WIDTH, 22, COR_CARTAO);
  background.fillRect(0, 22, WIDTH, 2, COR_LARANJA);
  escreve(titulo, 8, 3, 15, COR_LARANJA, COR_CARTAO);

  escreveDir(hora, 312, 3, 15, COR_TEXTO, COR_CARTAO);

  // Pool em uso: P1 principal (verde), P2 reserva (amarelo)
  bool reserva = activePoolIndex() == 1;
  background.fillRoundRect(228, 4, 26, 15, 4, reserva ? COR_AMARELO : COR_VERDE);
  escreveCentro(reserva ? "P2" : "P1", 241, 5, 11, COR_FUNDO, reserva ? COR_AMARELO : COR_VERDE);

  // Estado em palavras, com a cor so de apoio
  const char *estado;
  uint16_t corEstado;
  if (WiFi.status() != WL_CONNECTED)            { estado = "SEM WI-FI";  corEstado = COR_VERMELHO; }
  else if (mMonitor.NerdStatus == NM_hashing)   { estado = "ATIVO";      corEstado = COR_VERDE; }
  else if (mMonitor.NerdStatus == NM_Connecting){ estado = "CONECTANDO"; corEstado = COR_AMARELO; }
  else                                          { estado = "AJUSTE";     corEstado = COR_AMARELO; }
  background.fillCircle(128, 11, 4, corEstado);
  escreve(estado, 136, 5, 11, COR_TEXTO, COR_CARTAO);
}

// Nosso firmware: momento de comemorar. Quando o melhor resultado sobe, uma faixa
// laranja aparece por 15 s no rodape de qualquer tela.
static double s_recorde_visto = -1.0;
static uint32_t s_recorde_ate = 0;

static void faixaRecorde()
{
  if (s_recorde_visto < 0.0)
    s_recorde_visto = best_diff; // partida: o recorde salvo nao conta como novo
  if (best_diff > s_recorde_visto)
  {
    s_recorde_visto = best_diff;
    s_recorde_ate = millis() + 15000;
  }
  if ((int32_t)(s_recorde_ate - millis()) <= 0)
    return;
  char buf[16] = {0};
  suffix_string(best_diff, buf, sizeof(buf), 0);
  background.fillRect(0, 150, WIDTH, 20, COR_LARANJA);
  escreveCentro(String("NOVO RECORDE: ") + buf, WIDTH / 2, 152, 13, COR_FUNDO, COR_LARANJA);
}

static void pontosDePagina(int atual);

static void finalizaTela(int pagina)
{
  pontosDePagina(pagina);
  faixaRecorde();
  background.pushSprite(0, 0);
}

// Pontos de pagina no rodape
static void pontosDePagina(int atual)
{
  int x0 = WIDTH / 2 - (NUM_TELAS - 1) * 6;
  for (int i = 0; i < NUM_TELAS; ++i)
  {
    if (i == atual)
      background.fillRoundRect(x0 + i * 12 - 5, 163, 10, 4, 2, COR_LARANJA);
    else
      background.fillCircle(x0 + i * 12, 165, 2, COR_LINHA);
  }
}

static void cartao(int x, int y, int w, int h, const char *rotulo, const String &valor, int tamValor)
{
  background.fillRoundRect(x, y, w, h, 6, COR_CARTAO);
  escreve(rotulo, x + 8, y + 6, 11, COR_SUAVE, COR_CARTAO);
  escreve(valor, x + 8, y + 24, tamValor, COR_TEXTO, COR_CARTAO);
}

/********************* DRIVER *********************/

void nossaTela_Init(void)
{
#ifdef PIN_ENABLE5V
  pinMode(PIN_ENABLE5V, OUTPUT);
  digitalWrite(PIN_ENABLE5V, HIGH);
#endif

  tft.init();
  tft.setRotation(ROTATION_90);
  tft.setSwapBytes(true);
  background.createSprite(WIDTH, HEIGHT);
  background.setSwapBytes(true);

  texto.setDrawer(background);
  numeros.setDrawer(background);
  if (texto.loadFont(NotoSans_Bold, sizeof(NotoSans_Bold)))
    Serial.println("Erro ao carregar a fonte NotoSans_Bold");
  if (numeros.loadFont(DigitalNumbers, sizeof(DigitalNumbers)))
    Serial.println("Erro ao carregar a fonte DigitalNumbers");
}

void nossaTela_AlternateScreenState(void)
{
  int screen_state = digitalRead(TFT_BL);
  digitalWrite(TFT_BL, !screen_state);
}

void nossaTela_AlternateRotation(void)
{
  tft.setRotation(flipRotation(tft.getRotation()));
}

void nossaTela_LoadingScreen(void)
{
  background.fillSprite(COR_FUNDO);
  background.fillCircle(62, 80, 38, COR_LARANJA);
  escreveCentro("B", 62, 54, 46, COR_TEXTO, COR_LARANJA);
  escreve("NERD MINER", 116, 46, 30, COR_TEXTO, COR_FUNDO);
  escreve("firmware brasileiro", 118, 84, 15, COR_LARANJA, COR_FUNDO);
  escreve(CURRENT_VERSION, 118, 108, 13, COR_SUAVE, COR_FUNDO);
  escreveCentro("conectando ao Wi-Fi...", WIDTH / 2, 142, 13, COR_SUAVE, COR_FUNDO);
  background.pushSprite(0, 0);
}

// A tela de ajuste fica com a imagem original: o QR code dela leva ao Wi-Fi NerdMinerAP
void nossaTela_SetupScreen(void)
{
  tft.pushImage(0, 0, setupModeWidth, setupModeHeight, setupModeScreen);
}

// Tela 1: velocidade, grafico dos ultimos 10 minutos e contadores
void nossaTela_Minerando(unsigned long mElapsed)
{
  mining_data data = getMiningData(mElapsed);
  Serial.printf(">>> Completed %s share(s), %s Khashes, avg. hashrate %s KH/s\n",
                data.completedShares.c_str(), data.totalKHashes.c_str(), data.currentHashRate.c_str());

  background.fillSprite(COR_FUNDO);
  cabecalho("MINERANDO", data.currentTime);

  // Velocidade
  escreve("VELOCIDADE", 10, 30, 11, COR_SUAVE, COR_FUNDO);
  numeros.setFontSize(46);
  numeros.rdrawString(data.currentHashRate.c_str(), 128, 44, COR_TEXTO, COR_FUNDO);
  escreve("kH/s", 132, 66, 14, COR_LARANJA, COR_FUNDO);

  // Grafico dos ultimos 10 minutos
  const int gx = 176, gy = 30, gw = 136, gh = 58;
  background.fillRoundRect(gx, gy, gw, gh, 6, COR_CARTAO);
  escreve("10 MIN", gx + 6, gy + 4, 10, COR_SUAVE, COR_CARTAO);
  float hist[HASHRATE_HISTORY];
  uint8_t n = hashRateHistory(hist, HASHRATE_HISTORY);
  if (n >= 2)
  {
    float maxv = 1.0f;
    for (uint8_t i = 0; i < n; ++i)
      if (hist[i] > maxv) maxv = hist[i];
    const int px = gx + 6, py = gy + 18, pw = gw - 12, ph = gh - 24;
    int xa = px, ya = py + ph;
    for (uint8_t i = 0; i < n; ++i)
    {
      int x = px + (pw * i) / (HASHRATE_HISTORY - 1);
      int y = py + ph - (int)(ph * hist[i] / maxv);
      background.drawFastVLine(x, y, py + ph - y + 1, COR_LINHA);
      if (i > 0)
        background.drawLine(xa, ya, x, y, COR_LARANJA);
      xa = x;
      ya = y;
    }
  }
  else
    escreveCentro("medindo...", gx + gw / 2, gy + 26, 12, COR_SUAVE, COR_CARTAO);

  // Contadores
  cartao(8, 96, 96, 60, "MELHOR DIF", data.bestDiff, 18);
  cartao(112, 96, 96, 60, "PARTES 32b", data.completedShares, 18);
  cartao(216, 96, 96, 60, "TEMPO", data.timeMining, 13);

  finalizaTela(0);
}

// Tela 2: a conta honesta da loteria
void nossaTela_Sorte(unsigned long mElapsed)
{
  mining_data data = getMiningData(mElapsed);

  background.fillSprite(COR_FUNDO);
  cabecalho("SORTE", data.currentTime);

  double khs = lastHashRateKHs();
  double dif = networkDifficulty();

  if (khs <= 0.0 || dif <= 0.0)
  {
    escreveCentro("aguardando dados da rede...", WIDTH / 2, 74, 15, COR_SUAVE, COR_FUNDO);
    finalizaTela(1);
    return;
  }

  // Hashes esperados por bloco = dificuldade x 2^32
  double hashesPorBloco = dif * 4294967296.0;
  double hashesPorAno = khs * 1000.0 * 31557600.0;
  double anosPorBloco = hashesPorBloco / hashesPorAno;

  escreve("CHANCE DE ACHAR UM BLOCO POR ANO", 10, 30, 11, COR_SUAVE, COR_FUNDO);
  escreve("1 em " + numeroGrande(anosPorBloco), 10, 46, 30, COR_LARANJA, COR_FUNDO);
  escreve("1 bloco a cada ~" + numeroGrande(anosPorBloco) + " anos", 10, 84, 13, COR_TEXTO, COR_FUNDO);

  // Melhor resultado contra o alvo da rede, em escala logaritmica
  char difRede[16] = {0};
  suffix_string(dif, difRede, sizeof(difRede), 0);
  escreve("SEU MELHOR x ALVO DA REDE", 10, 108, 11, COR_SUAVE, COR_FUNDO);
  double frac = 0.0;
  if (best_diff > 1.0)
    frac = log10(best_diff) / log10(dif);
  if (frac < 0.0) frac = 0.0;
  if (frac > 1.0) frac = 1.0;
  background.fillRoundRect(10, 124, 300, 12, 6, COR_CARTAO);
  int w = (int)(300 * frac);
  if (w >= 12)
    background.fillRoundRect(10, 124, w, 12, 6, COR_LARANJA);
  escreve(data.bestDiff, 10, 140, 13, COR_TEXTO, COR_FUNDO);
  escreveDir(difRede, 310, 140, 13, COR_TEXTO, COR_FUNDO);

  finalizaTela(1);
}

// Tela 3: preco e numeros da rede
void nossaTela_Rede(unsigned long mElapsed)
{
  coin_data data = getCoinData(mElapsed);

  background.fillSprite(COR_FUNDO);
  cabecalho("REDE BITCOIN", data.currentTime);

  escreve("VALOR DO BTC", 10, 30, 11, COR_SUAVE, COR_FUNDO);
  escreve(precoComPontos(data.btcPrice), 10, 44, 26, COR_LARANJA, COR_FUNDO);

  cartao(8, 80, 96, 50, "BLOCO", data.blockHeight, 16);
  cartao(112, 80, 96, 50, "TAXA 30 MIN", data.halfHourFee, 13);
  String hashRede = data.globalHashRate.length() ? data.globalHashRate + " EH/s" : String("--");
  cartao(216, 80, 96, 50, "HASH DA REDE", hashRede, 13);

  // Halving
  String faltam = data.remainingBlocks;
  faltam.replace(" BLOCKS", "");
  escreve("HALVING: faltam " + faltam + " blocos", 10, 136, 11, COR_SUAVE, COR_FUNDO);
  background.fillRoundRect(10, 151, 300, 6, 3, COR_CARTAO);
  int w = (int)(300 * data.progressPercent / 100.0f);
  if (w >= 6)
    background.fillRoundRect(10, 151, w, 6, 3, COR_LARANJA);

  finalizaTela(3);
}

// Tela 4: relogio grande
void nossaTela_Relogio(unsigned long mElapsed)
{
  clock_data data = getClockData(mElapsed);

  background.fillSprite(COR_FUNDO);
  cabecalho("RELOGIO", data.currentTime);

  numeros.setFontSize(72);
  numeros.cdrawString(data.currentTime.c_str(), WIDTH / 2, 34, COR_TEXTO, COR_FUNDO);
  escreveCentro(data.currentDate, WIDTH / 2, 110, 15, COR_SUAVE, COR_FUNDO);

  escreve(precoComPontos(data.btcPrice), 10, 136, 15, COR_LARANJA, COR_FUNDO);
  escreveDir(data.currentHashRate + " kH/s", 310, 136, 15, COR_TEXTO, COR_FUNDO);

  finalizaTela(5);
}


// Tela 3: a pool e o seu endereco
void nossaTela_Pool(unsigned long mElapsed)
{
  mining_data data = getMiningData(mElapsed);
  pool_data pool = getPoolData();

  background.fillSprite(COR_FUNDO);
  cabecalho("POOL", data.currentTime);

  bool reserva = activePoolIndex() == 1;
  escreve(reserva ? "POOL RESERVA" : "POOL PRINCIPAL", 10, 30, 11, COR_SUAVE, COR_FUNDO);
  escreve(activePoolAddress() + ":" + String(activePoolPort()), 10, 44, 18, COR_LARANJA, COR_FUNDO);

  cartao(8, 72, 96, 50, "MINERADORES", String(pool.workersCount), 18);
  cartao(112, 72, 96, 50, "HASH SOMADO", pool.workersHash.length() ? pool.workersHash + "H/s" : String("--"), 15);
  cartao(216, 72, 96, 50, "MELHOR NA POOL", pool.bestDifficulty.length() ? pool.bestDifficulty : String("--"), 15);

  escreve("MODELOS " + data.templates + "   PARTES " + data.completedShares + "   BLOCOS " + data.valids,
          10, 132, 12, COR_TEXTO, COR_FUNDO);

  finalizaTela(2);
}

// Tela 5: taxas da rede em barras horizontais a partir do zero
void nossaTela_Taxas(unsigned long mElapsed)
{
  coin_data data = getCoinData(mElapsed);

  background.fillSprite(COR_FUNDO);
  cabecalho("TAXAS", data.currentTime);
  escreve("SAT/VB PARA ENTRAR NUM BLOCO", 10, 30, 11, COR_SUAVE, COR_FUNDO);

  const char *rotulos[5] = {"PROXIMO", "30 MIN", "1 HORA", "BARATA", "MINIMA"};
  int valores[5] = {data.fastestFee.toInt(), data.halfHourFee.toInt(), data.hourFee.toInt(),
                    data.economyFee.toInt(), data.minimumFee.toInt()};
  int maxv = 1;
  for (int i = 0; i < 5; ++i)
    if (valores[i] > maxv) maxv = valores[i];
  for (int i = 0; i < 5; ++i)
  {
    int y = 46 + i * 22;
    escreve(rotulos[i], 10, y + 2, 12, COR_TEXTO, COR_FUNDO);
    background.fillRoundRect(84, y + 3, 186, 12, 4, COR_CARTAO);
    int w = (186 * valores[i]) / maxv;
    if (w >= 8)
      background.fillRoundRect(84, y + 3, w, 12, 4, i == 0 ? COR_LARANJA : COR_SUAVE);
    escreveDir(String(valores[i]), 312, y + 1, 14, COR_TEXTO, COR_FUNDO);
  }

  finalizaTela(4);
}

// Tela 7: o proprio aparelho
void nossaTela_Aparelho(unsigned long mElapsed)
{
  mining_data data = getMiningData(mElapsed);

  background.fillSprite(COR_FUNDO);
  cabecalho("APARELHO", data.currentTime);

  uint32_t khsOriginal = 0, khsRapido = 0;
  hwShaSelfTestKHs(khsOriginal, khsRapido);
  String sha = hwShaFastPath() ? String("rápido, ") + khsRapido + " kH/s" : String("original, ") + khsOriginal + " kH/s";

  int rssi = WiFi.RSSI();
  String sinal = WiFi.status() == WL_CONNECTED ? String(rssi) + " dBm" : String("--");

  cartao(8, 30, 96, 50, "TEMPERATURA", data.temp + " C", 18);
  cartao(112, 30, 96, 50, "SINAL WI-FI", sinal, 15);
  cartao(216, 30, 96, 50, "MEMORIA", String(ESP.getFreeHeap() / 1024) + " kB", 15);

  escreve("SHA EM HARDWARE: " + sha, 10, 90, 12, COR_TEXTO, COR_FUNDO);
  escreve("IP " + WiFi.localIP().toString() + "   Wi-Fi " + WiFi.SSID(), 10, 110, 12, COR_TEXTO, COR_FUNDO);
  escreve(String("VERSAO ") + CURRENT_VERSION + "   LIGADO " + data.timeMining, 10, 130, 12, COR_SUAVE, COR_FUNDO);

  finalizaTela(6);
}

void nossaTela_AnimateCurrentScreen(unsigned long frame)
{
}

void nossaTela_DoLedStuff(unsigned long frame)
{
}

CyclicScreenFunction nossaTelaCyclicScreens[NUM_TELAS] = {nossaTela_Minerando, nossaTela_Sorte, nossaTela_Pool, nossaTela_Rede,
                                                          nossaTela_Taxas, nossaTela_Relogio, nossaTela_Aparelho};

DisplayDriver nossaTelaDriver = {
    nossaTela_Init,
    nossaTela_AlternateScreenState,
    nossaTela_AlternateRotation,
    nossaTela_LoadingScreen,
    nossaTela_SetupScreen,
    nossaTelaCyclicScreens,
    nossaTela_AnimateCurrentScreen,
    nossaTela_DoLedStuff,
    SCREENS_ARRAY_SIZE(nossaTelaCyclicScreens),
    0,
    WIDTH,
    HEIGHT};

#endif
