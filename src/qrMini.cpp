#include "qrMini.h"
#include <string.h>

// Correcao L, bloco unico nas versoes 1 a 4
static const int s_dados[5] = {0, 19, 34, 55, 80};   // codewords de dados
static const int s_ecc[5]   = {0, 7, 10, 15, 20};    // codewords de correcao
static const int s_alinha[5] = {0, 0, 18, 22, 26};   // centro do padrao de alinhamento (0 = nenhum)

static uint8_t s_mod[QRMINI_MAX_LADO][QRMINI_MAX_LADO];
static bool s_func[QRMINI_MAX_LADO][QRMINI_MAX_LADO];
static int s_lado;

static void funcao(int x, int y, bool escuro)
{
  s_mod[y][x] = escuro ? 1 : 0;
  s_func[y][x] = true;
}

static void localizador(int cx, int cy)
{
  for (int dy = -4; dy <= 4; ++dy)
    for (int dx = -4; dx <= 4; ++dx)
    {
      int x = cx + dx, y = cy + dy;
      if (x < 0 || y < 0 || x >= s_lado || y >= s_lado)
        continue;
      int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
      int d = adx > ady ? adx : ady;
      funcao(x, y, d != 2 && d != 4);
    }
}

static void alinhamento(int cx, int cy)
{
  for (int dy = -2; dy <= 2; ++dy)
    for (int dx = -2; dx <= 2; ++dx)
    {
      int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
      int d = adx > ady ? adx : ady;
      funcao(cx + dx, cy + dy, d != 1);
    }
}

static void formato(int mascara)
{
  int dado = (1 << 3) | mascara;  // nivel L = 01
  int resto = dado;
  for (int i = 0; i < 10; ++i)
    resto = (resto << 1) ^ ((resto >> 9) * 0x537);
  int bits = ((dado << 10) | resto) ^ 0x5412;

  for (int i = 0; i <= 5; ++i)
    funcao(8, i, (bits >> i) & 1);
  funcao(8, 7, (bits >> 6) & 1);
  funcao(8, 8, (bits >> 7) & 1);
  funcao(7, 8, (bits >> 8) & 1);
  for (int i = 9; i < 15; ++i)
    funcao(14 - i, 8, (bits >> i) & 1);

  for (int i = 0; i < 8; ++i)
    funcao(s_lado - 1 - i, 8, (bits >> i) & 1);
  for (int i = 8; i < 15; ++i)
    funcao(8, s_lado - 15 + i, (bits >> i) & 1);
  funcao(8, s_lado - 8, true);  // modulo escuro fixo
}

// Multiplicacao em GF(256) com o polinomio 0x11D
static uint8_t gfMul(uint8_t a, uint8_t b)
{
  int z = 0;
  for (int i = 7; i >= 0; --i)
  {
    z = (z << 1) ^ ((z >> 7) * 0x11D);
    z ^= ((b >> i) & 1) * a;
  }
  return (uint8_t)z;
}

static void reedSolomon(const uint8_t *dados, int nDados, uint8_t *ecc, int nEcc)
{
  uint8_t gerador[32];
  memset(gerador, 0, sizeof(gerador));
  gerador[nEcc - 1] = 1;
  uint8_t raiz = 1;
  for (int i = 0; i < nEcc; ++i)
  {
    for (int j = 0; j < nEcc; ++j)
    {
      gerador[j] = gfMul(gerador[j], raiz);
      if (j + 1 < nEcc)
        gerador[j] ^= gerador[j + 1];
    }
    raiz = gfMul(raiz, 0x02);
  }
  memset(ecc, 0, nEcc);
  for (int i = 0; i < nDados; ++i)
  {
    uint8_t fator = dados[i] ^ ecc[0];
    memmove(ecc, ecc + 1, nEcc - 1);
    ecc[nEcc - 1] = 0;
    for (int j = 0; j < nEcc; ++j)
      ecc[j] ^= gfMul(gerador[j], fator);
  }
}

int qrMiniGera(const char *texto, uint8_t modulos[QRMINI_MAX_LADO * QRMINI_MAX_LADO])
{
  int n = (int)strlen(texto);
  int versao = 0;
  for (int v = 1; v <= 4; ++v)
    if (4 + 8 + n * 8 <= s_dados[v] * 8)
    {
      versao = v;
      break;
    }
  if (versao == 0)
    return 0;

  // Fluxo de bits: modo byte (0100), contagem de 8 bits, dados, terminador, enchimento
  uint8_t cw[100];
  memset(cw, 0, sizeof(cw));
  int nBits = 0;
  int capacidade = s_dados[versao] * 8;
  #define POE_BITS(valor, quantos) \
    for (int _i = (quantos) - 1; _i >= 0; --_i, ++nBits) \
      if (((valor) >> _i) & 1) cw[nBits >> 3] |= (uint8_t)(0x80 >> (nBits & 7));
  POE_BITS(0x4, 4);
  POE_BITS(n, 8);
  for (int i = 0; i < n; ++i)
    POE_BITS((uint8_t)texto[i], 8);
  int term = capacidade - nBits < 4 ? capacidade - nBits : 4;
  POE_BITS(0, term);
  POE_BITS(0, (8 - nBits % 8) % 8);
  for (uint8_t pad = 0xEC; nBits < capacidade; pad ^= 0xEC ^ 0x11)
    POE_BITS(pad, 8);
  #undef POE_BITS

  int nDados = s_dados[versao], nEcc = s_ecc[versao];
  reedSolomon(cw, nDados, cw + nDados, nEcc);
  int total = nDados + nEcc;

  // Padroes fixos
  s_lado = 17 + 4 * versao;
  memset(s_mod, 0, sizeof(s_mod));
  memset(s_func, 0, sizeof(s_func));
  for (int i = 0; i < s_lado; ++i)
  {
    funcao(6, i, i % 2 == 0);
    funcao(i, 6, i % 2 == 0);
  }
  localizador(3, 3);
  localizador(s_lado - 4, 3);
  localizador(3, s_lado - 4);
  if (s_alinha[versao])
    alinhamento(s_alinha[versao], s_alinha[versao]);
  formato(0);  // reserva a area; o valor final vem depois da mascara

  // Dados em ziguezague, de baixo para cima, pulando a coluna 6
  int i = 0;
  for (int direita = s_lado - 1; direita >= 1; direita -= 2)
  {
    if (direita == 6)
      direita = 5;
    for (int vert = 0; vert < s_lado; ++vert)
      for (int j = 0; j < 2; ++j)
      {
        int x = direita - j;
        bool subindo = ((direita + 1) & 2) == 0;
        int y = subindo ? s_lado - 1 - vert : vert;
        if (!s_func[y][x] && i < total * 8)
        {
          s_mod[y][x] = (cw[i >> 3] >> (7 - (i & 7))) & 1;
          ++i;
        }
      }
  }

  // Mascara 0: inverte onde (x + y) e par
  for (int y = 0; y < s_lado; ++y)
    for (int x = 0; x < s_lado; ++x)
      if (!s_func[y][x] && (x + y) % 2 == 0)
        s_mod[y][x] ^= 1;
  formato(0);

  for (int y = 0; y < s_lado; ++y)
    for (int x = 0; x < s_lado; ++x)
      modulos[y * s_lado + x] = s_mod[y][x];
  return s_lado;
}
