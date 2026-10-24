// Nosso firmware: interface propria para a LilyGO T-Display S3 (320 x 170 visiveis).
// Visual de cartoes (mockup do Heinz de 24/10/2026), desenhado por codigo, sem imagens prontas.
// Telas: Minerando, Sorte, Pool, Rede, Taxas, Relogio, Aparelho e Ajustes (QR do painel web).
// A fonte NotoSans_Bold embutida so tem ASCII + á í ó ú minusculos: rotulos em maiusculas sem acento.

#include "displayDriver.h"

#ifdef NOSSA_TELA

#include <TFT_eSPI.h>
#include <WiFi.h>
#include <math.h>
#include "media/myFonts.h"
#include "version.h"
#include "monitor.h"
#include "mining.h"
#include "utils.h"
#include "wManager.h"
#include "qrMini.h"
#include "drivers/storage/storage.h"
#include "OpenFontRender.h"
#include "rotation.h"

#define WIDTH 320
#define HEIGHT 170
#define NUM_TELAS 8

// Cores do mockup (RGB565)
#define COR_FUNDO    0x0884  // #0A1220
#define COR_CARTAO   0x08C5  // #0F1B2D
#define COR_BORDA    0x2189  // #22324A
#define COR_TEXTO    0xFFFF
#define COR_SUAVE    0x9D99  // #9FB0C8
#define COR_LARANJA  0xF483  // #F7931A
#define COR_OURO     0xFE09  // #FFC14D
#define COR_VERDE    0x262B  // #22C55E
#define COR_VERDE_ESCURO 0x0A43
#define COR_AMARELO  0xFE60
#define COR_VERMELHO 0xEA28  // #EF4444
#define COR_AZUL     0x3C1E  // #3B82F6
#define COR_AZUL2    0x653F  // #60A5FA
#define COR_ROXO     0x8AFE  // #8B5CF6

extern monitor_data mMonitor;
extern double best_diff;
extern uint64_t upTime;
extern TSettings Settings;

OpenFontRender texto;   // NotoSans_Bold: rotulos e valores
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite background = TFT_eSprite(&tft);

/********************* AJUDANTES *********************/

static void escreve(const String &s, int x, int y, int tam, uint16_t cor, uint16_t fundo = COR_CARTAO)
{
  texto.setFontSize(tam);
  texto.drawString(s.c_str(), x, y, cor, fundo);
}

static void escreveDir(const String &s, int x, int y, int tam, uint16_t cor, uint16_t fundo = COR_CARTAO)
{
  texto.setFontSize(tam);
  texto.rdrawString(s.c_str(), x, y, cor, fundo);
}

static void escreveCentro(const String &s, int x, int y, int tam, uint16_t cor, uint16_t fundo = COR_CARTAO)
{
  texto.setFontSize(tam);
  texto.cdrawString(s.c_str(), x, y, cor, fundo);
}

static int largura(const String &s, int tam)
{
  texto.setFontSize(tam);
  return (int)texto.getTextWidth("%s", s.c_str());
}

static uint16_t mistura(uint16_t a, uint16_t b, int t, int max)
{
  int r = ((a >> 11) * (max - t) + (b >> 11) * t) / max;
  int g = (((a >> 5) & 0x3F) * (max - t) + ((b >> 5) & 0x3F) * t) / max;
  int bl = ((a & 0x1F) * (max - t) + (b & 0x1F) * t) / max;
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

// Barra com trilho e preenchimento em degrade (o numero vai sempre ao lado: a cor nunca e o unico sinal)
static void barra(int x, int y, int w, int h, float frac, uint16_t c1, uint16_t c2)
{
  int r = h / 2;
  background.fillRoundRect(x, y, w, h, r, COR_BORDA);
  if (frac <= 0.0f)
    return;
  if (frac > 1.0f)
    frac = 1.0f;
  int fw = (int)(w * frac);
  if (fw < h)
    fw = h;
  for (int i = 0; i < fw; ++i)
  {
    int borda = i < r ? r - i : (i >= fw - r ? i - (fw - r) + 1 : 0);
    int topo = y, alt = h;
    if (borda > 0)
    {
      int corte = r - (int)sqrtf((float)(r * r - (r - borda) * (r - borda)));
      topo += corte;
      alt -= 2 * corte;
    }
    if (alt > 0)
      background.drawFastVLine(x + i, topo, alt, mistura(c1, c2, i, fw));
  }
}

// Ponto decimal vira virgula
static String virgula(const char *s)
{
  String r(s);
  r.replace(".", ",");
  return r;
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
  snprintf(buf, sizeof(buf), v < 10.0 ? "%.1f%s" : "%.0f%s", v, sufixo);
  return virgula(buf);
}

// Separador de milhar: "921384" -> "921.384"; mantem o prefixo ("R$")
static String comPontos(const String &s)
{
  String digitos, prefixo;
  for (unsigned int i = 0; i < s.length(); ++i)
  {
    char c = s[i];
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
  if (prefixo.length())
    return prefixo + " " + saida;
  return saida;
}

static String tempoLigado()
{
  uint64_t t = upTime;
  char buf[24];
  snprintf(buf, sizeof(buf), "%ud %u:%02u:%02u", (unsigned)(t / 86400), (unsigned)(t / 3600 % 24),
           (unsigned)(t / 60 % 60), (unsigned)(t % 60));
  return String(buf);
}

static String melhorDif()
{
  char buf[16] = {0};
  suffix_string(best_diff, buf, sizeof(buf), 0);
  return String(buf);
}

/********************* ICONES (14 x 14, desenhados) *********************/

enum Icone { IC_PICARETA, IC_DADO, IC_POOL, IC_GLOBO, IC_MOEDAS, IC_RELOGIO, IC_APARELHO, IC_ENGRENAGEM };

static void icone(Icone ic, int x, int y, uint16_t c)
{
  switch (ic)
  {
    case IC_PICARETA:
      background.drawLine(x + 2, y + 13, x + 10, y + 5, c);
      background.drawLine(x + 3, y + 13, x + 11, y + 5, c);
      background.drawLine(x + 3, y + 2, x + 13, y + 12, c);
      background.drawLine(x + 4, y + 2, x + 13, y + 11, c);
      background.drawLine(x + 3, y + 2, x + 7, y + 1, c);
      break;
    case IC_DADO:
      background.fillRoundRect(x, y, 14, 14, 3, c);
      background.fillCircle(x + 4, y + 4, 1, COR_CARTAO);
      background.fillCircle(x + 10, y + 4, 1, COR_CARTAO);
      background.fillCircle(x + 7, y + 7, 1, COR_CARTAO);
      background.fillCircle(x + 4, y + 10, 1, COR_CARTAO);
      background.fillCircle(x + 10, y + 10, 1, COR_CARTAO);
      break;
    case IC_POOL:
      background.drawLine(x + 7, y + 3, x + 3, y + 11, c);
      background.drawLine(x + 7, y + 3, x + 11, y + 11, c);
      background.drawCircle(x + 7, y + 3, 2, c);
      background.drawCircle(x + 3, y + 11, 2, c);
      background.drawCircle(x + 11, y + 11, 2, c);
      break;
    case IC_GLOBO:
      background.drawCircle(x + 7, y + 7, 6, c);
      background.drawEllipse(x + 7, y + 7, 3, 6, c);
      background.drawFastHLine(x + 1, y + 7, 13, c);
      break;
    case IC_MOEDAS:
      for (int k = 0; k < 3; ++k)
        background.drawEllipse(x + 7, y + 3 + k * 4, 6, 2, c);
      break;
    case IC_RELOGIO:
      background.drawCircle(x + 7, y + 7, 6, c);
      background.drawFastVLine(x + 7, y + 3, 5, c);
      background.drawLine(x + 7, y + 7, x + 10, y + 9, c);
      break;
    case IC_APARELHO:
      background.drawRoundRect(x + 1, y, 12, 14, 2, c);
      background.drawFastHLine(x + 4, y + 4, 6, c);
      background.drawFastHLine(x + 4, y + 7, 6, c);
      background.fillCircle(x + 7, y + 11, 1, c);
      break;
    case IC_ENGRENAGEM:
      background.fillCircle(x + 7, y + 7, 5, c);
      background.fillRect(x + 6, y, 3, 14, c);
      background.fillRect(x, y + 6, 14, 3, c);
      background.fillCircle(x + 7, y + 7, 2, COR_CARTAO);
      break;
  }
}

static void trofeu(int x, int y, uint16_t c, uint16_t fundo)
{
  background.drawCircle(x + 2, y + 3, 2, c);
  background.drawCircle(x + 10, y + 3, 2, c);
  background.fillRect(x + 2, y, 9, 5, c);
  background.fillTriangle(x + 2, y + 5, x + 10, y + 5, x + 6, y + 9, c);
  background.fillRect(x + 5, y + 9, 3, 2, c);
  background.fillRect(x + 3, y + 11, 7, 2, c);
  (void)fundo;
}

/********************* MOLDURA COMUM *********************/

static void estadoAtual(const char **txt, uint16_t *cor)
{
  if (WiFi.status() != WL_CONNECTED)             { *txt = "SEM WI-FI";  *cor = COR_VERMELHO; }
  else if (mMonitor.NerdStatus == NM_hashing)    { *txt = "ATIVO";      *cor = COR_VERDE; }
  else if (mMonitor.NerdStatus == NM_Connecting) { *txt = "CONECTANDO"; *cor = COR_AMARELO; }
  else                                           { *txt = "AJUSTE";     *cor = COR_AMARELO; }
}

static void selo(int xDireita, const char *txt, uint16_t cor)
{
  int lw = largura(txt, 10);
  int px = xDireita - lw - 22;
  background.drawRoundRect(px, 4, lw + 22, 17, 8, cor);
  background.fillCircle(px + 8, 12, 3, cor);
  escreve(txt, px + 14, 7, 10, COR_TEXTO);
}

static void fundoCartao()
{
  background.fillSprite(COR_FUNDO);
  background.fillRoundRect(0, 0, WIDTH, HEIGHT, 8, COR_CARTAO);
  background.drawRoundRect(0, 0, WIDTH, HEIGHT, 8, COR_BORDA);
}

// Cartao da tela: icone, titulo numerado, selo de estado e hora
static void moldura(Icone ic, int numero, const char *titulo, const String &hora)
{
  fundoCartao();
  icone(ic, 9, 6, COR_LARANJA);
  escreve(String(numero) + ". " + titulo, 30, 5, 14, COR_TEXTO);
  const char *est;
  uint16_t corEst;
  estadoAtual(&est, &corEst);
  selo(262, est, corEst);
  escreveDir(hora, 312, 6, 12, COR_TEXTO);
  background.drawFastHLine(1, 26, WIDTH - 2, COR_LARANJA);
}

static void pontosDePagina(int atual)
{
  int x0 = WIDTH / 2 - (NUM_TELAS - 1) * 6;
  for (int i = 0; i < NUM_TELAS; ++i)
  {
    if (i == atual)
      background.fillRoundRect(x0 + i * 12 - 4, 163, 9, 4, 2, COR_LARANJA);
    else
      background.fillCircle(x0 + i * 12, 165, 2, COR_BORDA);
  }
}

// Momento de comemorar: quando o melhor resultado sobe, faixa verde por 15 s
static double s_recorde_visto = -1.0;
static uint32_t s_recorde_ate = 0;

static bool recordeNovo()
{
  if (s_recorde_visto < 0.0)
    s_recorde_visto = best_diff; // partida: o recorde salvo nao conta como novo
  if (best_diff > s_recorde_visto)
  {
    s_recorde_visto = best_diff;
    s_recorde_ate = millis() + 15000;
  }
  return (int32_t)(s_recorde_ate - millis()) > 0;
}

// Na tela 1 a faixa fica sempre (apagada quando nao ha recorde novo); nas outras so aparece no recorde
static void faixaRecorde(int y, bool sempre)
{
  bool novo = recordeNovo();
  if (!novo && !sempre)
    return;
  uint16_t fundo = novo ? COR_VERDE : COR_FUNDO;
  background.fillRoundRect(10, y, 300, 16, 5, fundo);
  if (!novo)
    background.drawRoundRect(10, y, 300, 16, 5, COR_BORDA);
  trofeu(16, y + 2, novo ? COR_FUNDO : COR_OURO, fundo);
  escreve((novo ? "NOVO RECORDE: " : "RECORDE: ") + melhorDif(), 34, y + 3, 11, novo ? COR_FUNDO : COR_TEXTO, fundo);
  escreveDir(">", 302, y + 2, 12, novo ? COR_FUNDO : COR_SUAVE, fundo);
}

static void finalizaTela(int pagina)
{
  pontosDePagina(pagina);
  if (pagina != 0)
    faixaRecorde(144, false);
  background.pushSprite(0, 0);
}

// Cartao pequeno: rotulo em cima, valor embaixo
static void cartao(int x, int y, int w, int h, const char *rotulo, const String &valor, int tamValor)
{
  background.fillRoundRect(x, y, w, h, 5, COR_FUNDO);
  background.drawRoundRect(x, y, w, h, 5, COR_BORDA);
  escreve(rotulo, x + 7, y + 5, 9, COR_SUAVE, COR_FUNDO);
  escreve(valor, x + 7, y + 19, tamValor, COR_TEXTO, COR_FUNDO);
}

// QR com margem branca de 2 modulos, centrado num quadrado de `caixa` pixels
static bool desenhaQR(const String &conteudo, int x, int y, int caixa)
{
  static uint8_t mod[QRMINI_MAX_LADO * QRMINI_MAX_LADO];
  int lado = qrMiniGera(conteudo.c_str(), mod);
  if (lado == 0)
    return false;
  int esc = caixa / (lado + 4);
  if (esc < 1)
    esc = 1;
  int total = esc * (lado + 4);
  int ox = x + (caixa - total) / 2, oy = y + (caixa - total) / 2;
  background.fillRoundRect(ox, oy, total, total, 4, TFT_WHITE);
  for (int j = 0; j < lado; ++j)
    for (int i = 0; i < lado; ++i)
      if (mod[j * lado + i])
        background.fillRect(ox + (i + 2) * esc, oy + (j + 2) * esc, esc, esc, TFT_BLACK);
  return true;
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
  if (texto.loadFont(NotoSans_Bold, sizeof(NotoSans_Bold)))
    Serial.println("Erro ao carregar a fonte NotoSans_Bold");
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
  fundoCartao();
  background.fillCircle(66, 85, 40, COR_LARANJA);
  escreveCentro("B", 66, 59, 48, COR_TEXTO, COR_LARANJA);
  escreve("NERDMINER", 122, 44, 28, COR_TEXTO);
  escreve("v2 BR", 122, 78, 18, COR_VERDE);
  escreve(CURRENT_VERSION, 186, 82, 12, COR_SUAVE);
  escreve("conectando ao Wi-Fi...", 122, 114, 13, COR_SUAVE);
  background.pushSprite(0, 0);
}

// Ajuste inicial (rede propria aberta): QR para o celular entrar na rede
void nossaTela_SetupScreen(void)
{
  fundoCartao();
  icone(IC_ENGRENAGEM, 9, 6, COR_LARANJA);
  escreve("AJUSTES", 30, 5, 14, COR_TEXTO);
  selo(312, "AGUARDANDO", COR_AMARELO);
  background.drawFastHLine(1, 26, WIDTH - 2, COR_LARANJA);

  String rede = wifiAjusteNome();
  escreve("1. ENTRE NA REDE WI-FI", 12, 34, 10, COR_SUAVE);
  escreve(rede, 12, 48, 15, COR_LARANJA);
  escreve(String("senha ") + DEFAULT_WIFIPW, 12, 68, 12, COR_TEXTO);
  escreve("2. A pagina de ajuste abre", 12, 90, 11, COR_SUAVE);
  escreve("sozinha; se nao abrir, va em", 12, 104, 11, COR_SUAVE);
  escreve("192.168.4.1", 12, 120, 15, COR_TEXTO);
  escreve("3. Informe Wi-Fi, carteira e pool", 12, 144, 11, COR_SUAVE);

  desenhaQR(String("WIFI:T:WPA;S:") + rede + ";P:" + DEFAULT_WIFIPW + ";;", 196, 32, 118);
  background.pushSprite(0, 0);
}

// 1. Minerando
void nossaTela_Minerando(unsigned long mElapsed)
{
  mining_data data = getMiningData(mElapsed);
  Serial.printf(">>> Completed %s share(s), %s Khashes, avg. hashrate %s KH/s\n",
                data.completedShares.c_str(), data.totalKHashes.c_str(), data.currentHashRate.c_str());

  moldura(IC_PICARETA, 1, "MINERANDO", data.currentTime);

  escreve("VELOCIDADE", 12, 34, 10, COR_SUAVE);
  escreve(data.currentHashRate, 12, 48, 36, COR_TEXTO);
  escreve("kH/s", 16 + largura(data.currentHashRate, 36), 66, 14, COR_VERDE);

  // Grafico dos ultimos 10 minutos
  const int gx = 172, gy = 32, gw = 140, gh = 62;
  background.fillRoundRect(gx, gy, gw, gh, 5, COR_FUNDO);
  background.drawRoundRect(gx, gy, gw, gh, 5, COR_BORDA);
  escreve("últimos 10 min", gx + 6, gy + 4, 9, COR_SUAVE, COR_FUNDO);
  float hist[HASHRATE_HISTORY];
  uint8_t n = hashRateHistory(hist, HASHRATE_HISTORY);
  if (n >= 2)
  {
    float minv = hist[0], maxv = hist[0];
    for (uint8_t i = 1; i < n; ++i)
    {
      if (hist[i] > maxv) maxv = hist[i];
      if (hist[i] < minv) minv = hist[i];
    }
    if (maxv - minv < 2.0f) { maxv += 1.0f; minv -= 1.0f; }
    if (minv < 0.0f) minv = 0.0f;
    escreve(String((int)maxv), gx + 6, gy + 17, 8, COR_SUAVE, COR_FUNDO);
    escreve(String((int)minv), gx + 6, gy + gh - 12, 8, COR_SUAVE, COR_FUNDO);
    const int px = gx + 30, py = gy + 18, pw = gw - 36, ph = gh - 24;
    int xa = 0, ya = 0;
    for (uint8_t i = 0; i < n; ++i)
    {
      int x = px + (pw * i) / (HASHRATE_HISTORY - 1);
      int y = py + ph - (int)(ph * (hist[i] - minv) / (maxv - minv));
      background.drawFastVLine(x, y, py + ph - y + 1, COR_VERDE_ESCURO);
      if (i > 0)
      {
        background.drawLine(xa, ya, x, y, COR_VERDE);
        background.drawLine(xa, ya - 1, x, y - 1, COR_VERDE);
      }
      xa = x;
      ya = y;
    }
  }
  else
    escreveCentro("medindo...", gx + gw / 2, gy + 30, 11, COR_SUAVE, COR_FUNDO);

  cartao(10, 100, 96, 40, "MELHOR DIF", melhorDif(), 14);
  trofeu(90, 104, COR_OURO, COR_FUNDO);
  cartao(112, 100, 96, 40, "PARTES 32b", data.completedShares, 14);
  cartao(214, 100, 96, 40, "TEMPO", tempoLigado(), 12);

  faixaRecorde(144, true);
  pontosDePagina(0);
  background.pushSprite(0, 0);
}

// 2. Sorte: a conta honesta da loteria
void nossaTela_Sorte(unsigned long mElapsed)
{
  mining_data data = getMiningData(mElapsed);
  moldura(IC_DADO, 2, "SORTE", data.currentTime);

  double khs = lastHashRateKHs();
  double dif = networkDifficulty();
  if (khs <= 0.0 || dif <= 0.0)
  {
    escreveCentro("aguardando dados da rede...", WIDTH / 2, 74, 14, COR_SUAVE);
    finalizaTela(1);
    return;
  }

  // Hashes esperados por bloco = dificuldade x 2^32
  double anosPorBloco = (dif * 4294967296.0) / (khs * 1000.0 * 31557600.0);

  escreve("CHANCE DE ACHAR UM BLOCO POR ANO", 12, 34, 10, COR_SUAVE);
  escreve("1 em " + numeroGrande(anosPorBloco), 12, 48, 30, COR_LARANJA);
  escreve("1 bloco a cada ~" + numeroGrande(anosPorBloco) + " anos", 12, 84, 12, COR_TEXTO);

  char difRede[16] = {0};
  suffix_string(dif, difRede, sizeof(difRede), 0);
  escreve("SEU MELHOR x ALVO DA REDE (escala log)", 12, 104, 9, COR_SUAVE);
  double frac = best_diff > 1.0 ? log10(best_diff) / log10(dif) : 0.0;
  barra(12, 118, 296, 10, (float)frac, COR_LARANJA, COR_OURO);
  escreve(melhorDif(), 12, 132, 12, COR_TEXTO);
  escreveDir(difRede, 308, 132, 12, COR_TEXTO);

  finalizaTela(1);
}

// 3. Pool
void nossaTela_Pool(unsigned long mElapsed)
{
  mining_data data = getMiningData(mElapsed);
  pool_data pool = getPoolData();
  moldura(IC_POOL, 3, "POOL", data.currentTime);

  bool reserva = activePoolIndex() == 1;
  escreve(reserva ? "POOL RESERVA" : "POOL PRINCIPAL", 12, 34, 10, COR_SUAVE);
  escreve(activePoolAddress() + ":" + String(activePoolPort()), 12, 47, 17, COR_LARANJA);

  cartao(10, 72, 96, 40, "MINERADORES", String(pool.workersCount), 14);
  cartao(112, 72, 96, 40, "HASH SOMADO", pool.workersHash.length() ? pool.workersHash + "H/s" : String("--"), 13);
  cartao(214, 72, 96, 40, "MELHOR NA POOL", pool.bestDifficulty.length() ? pool.bestDifficulty : String("--"), 13);

  escreve("MODELOS " + data.templates + "   PARTES " + data.completedShares + "   BLOCOS " + data.valids,
          12, 120, 11, COR_TEXTO);

  int quedas = poolQuedasUltimaHora();
  uint16_t corCon = quedas == 0 ? COR_VERDE : (quedas < 3 ? COR_AMARELO : COR_VERMELHO);
  background.fillCircle(16, 142, 3, corCon);
  escreve(quedas == 0 ? String("CONEXAO ESTAVEL") : String("CONEXAO INSTAVEL: ") + quedas + " quedas em 1 h",
          24, 137, 10, COR_TEXTO);

  finalizaTela(2);
}

// 4. Rede
void nossaTela_Rede(unsigned long mElapsed)
{
  coin_data data = getCoinData(mElapsed);
  moldura(IC_GLOBO, 4, "REDE", data.currentTime);

  escreve("VALOR DO BTC", 12, 34, 10, COR_SUAVE);
  String preco = comPontos(data.btcPrice);
  escreve(preco, 12, 46, 24, COR_LARANJA);
  float var = bitcoinChange24h();
  int vx = 18 + largura(preco, 24);
  uint16_t corVar = var >= 0.0f ? COR_VERDE : COR_VERMELHO;
  if (var >= 0.0f)
    background.fillTriangle(vx, 61, vx + 8, 61, vx + 4, 55, corVar);
  else
    background.fillTriangle(vx, 55, vx + 8, 55, vx + 4, 61, corVar);
  char buf[32];
  snprintf(buf, sizeof(buf), "%+.2f%% (24h)", var);
  escreve(virgula(buf), vx + 11, 53, 10, corVar);

  cartao(10, 76, 96, 40, "BLOCO", comPontos(data.blockHeight), 14);
  cartao(112, 76, 96, 40, "TAXA 30 MIN", data.halfHourFee, 12);
  cartao(214, 76, 96, 40, "HASH DA REDE", data.globalHashRate.length() ? data.globalHashRate + " EH/s" : String("--"), 12);

  snprintf(buf, sizeof(buf), "%+.1f%%", networkDifficultyChange());
  escreve("DIFICULDADE " + data.netwrokDifficulty + "   ajuste " + virgula(buf) + " (estimativa)", 12, 122, 10, COR_TEXTO);

  String faltam = data.remainingBlocks;
  faltam.replace(" BLOCKS", "");
  escreve("HALVING: faltam " + comPontos(faltam), 12, 138, 9, COR_SUAVE);
  barra(180, 140, 128, 6, data.progressPercent / 100.0f, COR_AZUL, COR_AZUL2);

  finalizaTela(3);
}

// 5. Taxas: barras horizontais a partir do zero
void nossaTela_Taxas(unsigned long mElapsed)
{
  coin_data data = getCoinData(mElapsed);
  moldura(IC_MOEDAS, 5, "TAXAS", data.currentTime);
  escreve("SAT/VB PARA ENTRAR NUM BLOCO", 12, 34, 10, COR_SUAVE);

  const char *rotulos[5] = {"Próximo", "30 min", "1 hora", "Barata", "Mínima"};
  int valores[5] = {data.fastestFee.toInt(), data.halfHourFee.toInt(), data.hourFee.toInt(),
                    data.economyFee.toInt(), data.minimumFee.toInt()};
  uint16_t c1[5] = {COR_LARANJA, COR_AZUL, COR_AZUL, COR_AZUL, COR_ROXO};
  uint16_t c2[5] = {COR_OURO, COR_AZUL2, COR_AZUL2, COR_AZUL2, COR_ROXO};
  int maxv = 1;
  for (int i = 0; i < 5; ++i)
    if (valores[i] > maxv) maxv = valores[i];
  for (int i = 0; i < 5; ++i)
  {
    int y = 48 + i * 18;
    escreve(rotulos[i], 12, y, 12, COR_TEXTO);
    barra(84, y + 2, 196, 9, (float)valores[i] / maxv, c1[i], c2[i]);
    escreveDir(String(valores[i]), 308, y - 1, 13, COR_TEXTO);
  }
  background.drawCircle(17, 147, 5, COR_SUAVE);
  escreveCentro("i", 17, 142, 9, COR_SUAVE);
  escreve("Em ~30 min entra com " + String(valores[1]) + " sat/vB", 28, 141, 10, COR_SUAVE);

  finalizaTela(4);
}

// 6. Relogio
void nossaTela_Relogio(unsigned long mElapsed)
{
  clock_data data = getClockData(mElapsed);
  moldura(IC_RELOGIO, 6, "RELOGIO", data.currentTime);

  escreveCentro(data.currentTime, WIDTH / 2, 32, 52, COR_TEXTO);
  escreveCentro(data.currentDate, WIDTH / 2, 92, 15, COR_AZUL2);
  background.drawFastHLine(20, 116, 280, COR_BORDA);
  background.drawFastVLine(160, 122, 34, COR_BORDA);

  escreveCentro(comPontos(data.btcPrice), 80, 122, 15, COR_LARANJA);
  escreveCentro("VALOR DO BTC", 80, 142, 9, COR_AZUL2);
  escreveCentro(data.currentHashRate + " kH/s", 240, 122, 15, COR_TEXTO);
  escreveCentro("VELOCIDADE", 240, 142, 9, COR_AZUL2);

  finalizaTela(5);
}

// 7. Aparelho
void nossaTela_Aparelho(unsigned long mElapsed)
{
  mining_data data = getMiningData(mElapsed);
  moldura(IC_APARELHO, 7, "APARELHO", data.currentTime);

  float temp = temperatureRead();
  int rssi = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : -100;
  uint32_t livre = ESP.getFreeHeap(), total = ESP.getHeapSize();
  uint16_t corTemp = temp < 70.0f ? COR_VERDE : COR_VERMELHO;

  cartao(10, 32, 96, 46, "TEMPERATURA", String(temp, 0) + " C", 14);
  barra(17, 70, 82, 4, temp / 85.0f, corTemp, corTemp);
  cartao(112, 32, 96, 46, "SINAL WI-FI", rssi > -100 ? String(rssi) + " dBm" : String("--"), 14);
  barra(119, 70, 82, 4, (rssi + 100) / 70.0f, COR_VERDE, COR_VERDE);
  cartao(214, 32, 96, 46, "MEMORIA LIVRE", String(livre / 1024) + " kB", 14);
  barra(221, 70, 82, 4, total ? (float)livre / total : 0.0f, COR_VERDE, COR_VERDE);

  uint32_t khsOriginal = 0, khsRapido = 0;
  hwShaSelfTestKHs(khsOriginal, khsRapido);
  String sha = hwShaFastPath() ? String("rápido, ") + khsRapido + " kH/s (original " + khsOriginal + ")"
                               : String("original, ") + khsOriginal + " kH/s";
  escreve("SHA EM HARDWARE: " + sha, 12, 86, 10, COR_TEXTO);
  escreve("IP " + WiFi.localIP().toString() + "    Wi-Fi " + WiFi.SSID(), 12, 102, 10, COR_TEXTO);
  escreve(String("VERSAO ") + CURRENT_VERSION, 12, 118, 10, COR_SUAVE);
  escreve("LIGADO " + tempoLigado(), 150, 118, 10, COR_AZUL2);

  finalizaTela(6);
}

// 8. Ajustes: QR do painel web e configuracao em uso
void nossaTela_Ajustes(unsigned long mElapsed)
{
  mining_data data = getMiningData(mElapsed);
  moldura(IC_ENGRENAGEM, 8, "AJUSTES", data.currentTime);

  bool conectado = WiFi.status() == WL_CONNECTED;
  String url = "http://" + WiFi.localIP().toString() + "/";

  escreve("PAINEL NA REDE DE CASA", 12, 34, 10, COR_SUAVE);
  escreve(conectado ? url : String("sem Wi-Fi"), 12, 47, 13, COR_LARANJA);
  escreve("Aponte o celular para o QR", 12, 64, 10, COR_SUAVE);

  String carteira = String(Settings.BtcWallet);
  if (carteira.length() > 16)
    carteira = carteira.substring(0, 8) + "..." + carteira.substring(carteira.length() - 6);
  String reserva = Settings.PoolAddress2.length() ? Settings.PoolAddress2 + ":" + String(Settings.PoolPort2) : String("nenhuma");
  escreve("POOL  " + Settings.PoolAddress + ":" + String(Settings.PoolPort), 12, 84, 10, COR_TEXTO);
  escreve("RESERVA  " + reserva, 12, 99, 10, COR_TEXTO);
  escreve("CARTEIRA  " + carteira, 12, 114, 10, COR_TEXTO);
  escreve("Para mudar: ligue segurando", 12, 132, 10, COR_SUAVE);
  escreve("a tecla de cima", 12, 145, 10, COR_SUAVE);

  if (conectado)
    desenhaQR(url, 192, 32, 124);
  else
    escreveCentro("sem Wi-Fi", 254, 88, 12, COR_SUAVE);

  finalizaTela(7);
}

void nossaTela_AnimateCurrentScreen(unsigned long frame)
{
}

void nossaTela_DoLedStuff(unsigned long frame)
{
}

CyclicScreenFunction nossaTelaCyclicScreens[NUM_TELAS] = {nossaTela_Minerando, nossaTela_Sorte, nossaTela_Pool, nossaTela_Rede,
                                                          nossaTela_Taxas, nossaTela_Relogio, nossaTela_Aparelho, nossaTela_Ajustes};

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
