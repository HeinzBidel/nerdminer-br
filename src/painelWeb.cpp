#include <Arduino.h>
#include "painelWeb.h"

#ifdef PAINEL_WEB
#include <WiFi.h>
#include <WebServer.h>
#include "monitor.h"
#include "painel_html.h"
#include <Preferences.h>

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

// Troca o nucleo de cada motor e reinicia (medicao de desempenho pela rede de casa)
static void apiAfinidade()
{
  int modo = s_servidor.arg("modo").toInt();
  if (!s_servidor.hasArg("modo") || modo < 0 || modo > 2)
  {
    s_servidor.send(400, "text/plain", "use /api/afinidade?modo=0|1|2");
    return;
  }
  Preferences prefs;
  prefs.begin("nerdbr", false);
  prefs.putUChar("afin", (uint8_t)modo);
  prefs.end();
  s_servidor.send(200, "text/plain", "ok, reiniciando com afinidade " + String(modo));
  delay(500);
  ESP.restart();
}

void painelWebProcessa(void)
{
  if (!s_iniciado)
  {
    if (WiFi.status() != WL_CONNECTED)
      return;
    s_servidor.on("/", paginaInicial);
    s_servidor.on("/api/estado", apiEstado);
    s_servidor.on("/api/afinidade", apiAfinidade);
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
