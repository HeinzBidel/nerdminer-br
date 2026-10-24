// Nosso firmware: gerador de QR code pequeno e sem dependencias.
// Modo byte, correcao L, versoes 1 a 4 (ate 78 caracteres), mascara 0.
// Escrito a partir da norma ISO/IEC 18004 seguindo a estrutura do qrcodegen (Nayuki, MIT).
#ifndef QR_MINI_H
#define QR_MINI_H

#include <stdint.h>
#include <stdbool.h>

#define QRMINI_MAX_LADO 33  // versao 4

// Gera o QR de `texto`. `modulos` recebe lado x lado bytes (1 = escuro), linha a linha.
// Devolve o lado (21, 25, 29 ou 33) ou 0 se o texto nao couber.
int qrMiniGera(const char *texto, uint8_t modulos[QRMINI_MAX_LADO * QRMINI_MAX_LADO]);

#endif
