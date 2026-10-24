#ifndef _WMANAGER_H
#define _WMANAGER_H

#include <Arduino.h>

void init_WifiManager();
void wifiManagerProcess();
void reset_configuration();

String wifiAjusteNome(void);  // Nosso firmware: rede de ajuste (NerdMinerAP ou nome do gravador)

#endif // _WMANAGER_H
