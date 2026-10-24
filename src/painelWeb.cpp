#include <Arduino.h>
#include "painelWeb.h"

#ifdef PAINEL_WEB
#include <WiFi.h>
#include <WebServer.h>
#include "monitor.h"
#include "painel_html.h"

static WebServer s_servidor(80);
static bool s_iniciado = false;

static void paginaInicial()
{
  s_servidor.send_P(200, "text/html; charset=utf-8", PAINEL_HTML);
}

static void apiEstado()
{
  static char json[PAINEL_JSON_MAX];
  painelMarcaVisita();
  painelCopiaJson(json, sizeof(json));
  s_servidor.sendHeader("Cache-Control", "no-store");
  s_servidor.send(200, "application/json", json);
}

void painelWebProcessa(void)
{
  if (!s_iniciado)
  {
    if (WiFi.status() != WL_CONNECTED)
      return;
    s_servidor.on("/", paginaInicial);
    s_servidor.on("/api/estado", apiEstado);
    s_servidor.begin();
    s_iniciado = true;
    Serial.println("[PAINEL] http://" + WiFi.localIP().toString() + "/");
    return;
  }
  s_servidor.handleClient();
}

#else

void painelWebProcessa(void)
{
}

#endif // PAINEL_WEB
