#include <Arduino.h>
#include <WiFi.h>
#include "mbedtls/md.h"
#include "HTTPClient.h"
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <list>
#include <mutex>
#include "mining.h"
#include "utils.h"
#include "monitor.h"
#include "version.h"
#include "drivers/storage/storage.h"
#include "drivers/devices/device.h"

extern uint32_t templates;
extern uint32_t hashes;
extern uint32_t Mhashes;
extern uint32_t totalKHashes;
extern uint32_t elapsedKHs;
extern uint64_t upTime;

extern uint32_t shares; // increase if blockhash has 32 bits of zeroes
extern uint32_t valids; // increased if blockhash <= targethalfshares

extern double best_diff; // track best diff

extern monitor_data mMonitor;

//from saved config
extern TSettings Settings; 
bool invertColors = false;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 3600, 60000);
unsigned int bitcoin_price=0;
float bitcoin_change_24h = 0.0f; // Nosso firmware
String current_block = "793261";
global_data gData;
pool_data pData;
String poolAPIUrl;


void setup_monitor(void){
    /******** TIME ZONE SETTING *****/

    timeClient.begin();
    
    // Adjust offset depending on your zone
    // GMT +2 in seconds (zona horaria de Europa Central)
    timeClient.setTimeOffset(3600 * Settings.Timezone);

    Serial.println("TimeClient setup done");
#ifdef SCREEN_WORKERS_ENABLE
    poolAPIUrl = getPoolAPIUrl();
    Serial.println("poolAPIUrl: " + poolAPIUrl);
#endif
}

unsigned long mGlobalUpdate =0;

void updateGlobalData(void){
    
    if((mGlobalUpdate == 0) || (millis() - mGlobalUpdate > UPDATE_Global_min * 60 * 1000)){
    
        if (WiFi.status() != WL_CONNECTED) return;
            
        //Make first API call to get global hash and current difficulty
        HTTPClient http;
        http.setTimeout(10000);
        try {
        http.begin(getGlobalHash);
        int httpCode = http.GET();

        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            
            StaticJsonDocument<1024> doc;
            deserializeJson(doc, payload);
            String temp = "";
            if (doc.containsKey("currentHashrate")) temp = String(doc["currentHashrate"].as<float>());
            if(temp.length()>18 + 3) //Exahashes more than 18 digits + 3 digits decimals
              gData.globalHash = temp.substring(0,temp.length()-18 - 3);
            if (doc.containsKey("currentDifficulty")) gData.netDifficulty = doc["currentDifficulty"].as<double>();
            if (doc.containsKey("currentDifficulty")) temp = String(doc["currentDifficulty"].as<float>());
            if(temp.length()>10 + 3){ //Terahash more than 10 digits + 3 digit decimals
              temp = temp.substring(0,temp.length()-10 - 3);
              gData.difficulty = temp.substring(0,temp.length()-2) + "." + temp.substring(temp.length()-2,temp.length()) + "T";
            }
            doc.clear();

            mGlobalUpdate = millis();
        }
        http.end();

      
        //Nosso firmware: estimativa do proximo ajuste de dificuldade
        http.begin(getDifficulty);
        httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            StaticJsonDocument<64> filter;
            filter["difficultyChange"] = true;
            StaticJsonDocument<128> doc;
            deserializeJson(doc, payload, DeserializationOption::Filter(filter));
            if (doc.containsKey("difficultyChange")) gData.difficultyChange = doc["difficultyChange"].as<double>();
        }
        http.end();

        //Make third API call to get fees
        http.begin(getFees);
        httpCode = http.GET();

        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            
            StaticJsonDocument<1024> doc;
            deserializeJson(doc, payload);
            String temp = "";
            if (doc.containsKey("halfHourFee")) gData.halfHourFee = doc["halfHourFee"].as<int>();
#ifdef SCREEN_FEES_ENABLE
            if (doc.containsKey("fastestFee"))  gData.fastestFee = doc["fastestFee"].as<int>();
            if (doc.containsKey("hourFee"))     gData.hourFee = doc["hourFee"].as<int>();
            if (doc.containsKey("economyFee"))  gData.economyFee = doc["economyFee"].as<int>();
            if (doc.containsKey("minimumFee"))  gData.minimumFee = doc["minimumFee"].as<int>();
#endif
            doc.clear();

            mGlobalUpdate = millis();
        }
        
        http.end();
        } catch(...) {
          Serial.println("Global data HTTP error caught");
          http.end();
        }
    }
}

unsigned long mHeightUpdate = 0;

String getBlockHeight(void){
    
    if((mHeightUpdate == 0) || (millis() - mHeightUpdate > UPDATE_Height_min * 60 * 1000)){
    
        if (WiFi.status() != WL_CONNECTED) return current_block;
            
        HTTPClient http;
        http.setTimeout(10000);
        try {
        http.begin(getHeightAPI);
        int httpCode = http.GET();

        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            payload.trim();

            current_block = payload;

            mHeightUpdate = millis();
        }        
        http.end();
        } catch(...) {
          Serial.println("Height HTTP error caught");
          http.end();
        }
    }
  
  return current_block;
}

unsigned long mBTCUpdate = 0;

String getBTCprice(void){
    
    if((mBTCUpdate == 0) || (millis() - mBTCUpdate > UPDATE_BTC_min * 60 * 1000)){
    
        if (WiFi.status() != WL_CONNECTED) {
            static char price_buffer[16];
            snprintf(price_buffer, sizeof(price_buffer), PRICE_FORMAT, bitcoin_price);
            return String(price_buffer);
        }
        
        HTTPClient http;
        http.setTimeout(10000);
        bool priceUpdated = false;

        try {
        http.begin(getBTCAPI);
        int httpCode = http.GET();

        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();

            StaticJsonDocument<1024> doc;
            deserializeJson(doc, payload);
          
            if (doc.containsKey("bitcoin") && doc["bitcoin"].containsKey(PRICE_KEY)) {
                bitcoin_price = doc["bitcoin"][PRICE_KEY];
                if (doc["bitcoin"].containsKey(PRICE_KEY_24H))
                    bitcoin_change_24h = doc["bitcoin"][PRICE_KEY_24H].as<float>();
            }

            doc.clear();

            mBTCUpdate = millis();
        }
        
        http.end();
        } catch(...) {
          Serial.println("BTC price HTTP error caught");
          http.end();
        }
    }  
  
  static char price_buffer[16];
  snprintf(price_buffer, sizeof(price_buffer), PRICE_FORMAT, bitcoin_price);
  return String(price_buffer);
}

unsigned long mTriggerUpdate = 0;
unsigned long initialMillis = millis();
unsigned long initialTime = 0;
unsigned long mPoolUpdate = 0;

void getTime(unsigned long* currentHours, unsigned long* currentMinutes, unsigned long* currentSeconds){
  
  //Check if need an NTP call to check current time
  if((mTriggerUpdate == 0) || (millis() - mTriggerUpdate > UPDATE_PERIOD_h * 60 * 60 * 1000)){ //60 sec. * 60 min * 1000ms
    if(WiFi.status() == WL_CONNECTED) {
        if(timeClient.update()) mTriggerUpdate = millis(); //NTP call to get current time
        initialTime = timeClient.getEpochTime(); // Guarda la hora inicial (en segundos desde 1970)
        Serial.print("TimeClient NTPupdateTime ");
    }
  }

  unsigned long elapsedTime = (millis() - mTriggerUpdate) / 1000; // Tiempo transcurrido en segundos
  unsigned long currentTime = initialTime + elapsedTime; // La hora actual

  // convierte la hora actual en horas, minutos y segundos
  *currentHours = currentTime % 86400 / 3600;
  *currentMinutes = currentTime % 3600 / 60;
  *currentSeconds = currentTime % 60;
}

String getDate(){
  
  unsigned long elapsedTime = (millis() - mTriggerUpdate) / 1000; // Tiempo transcurrido en segundos
  unsigned long currentTime = initialTime + elapsedTime; // La hora actual

  // Convierte la hora actual (epoch time) en una estructura tm
  struct tm *tm = localtime((time_t *)&currentTime);

  int year = tm->tm_year + 1900; // tm_year es el número de años desde 1900
  int month = tm->tm_mon + 1;    // tm_mon es el mes del año desde 0 (enero) hasta 11 (diciembre)
  int day = tm->tm_mday;         // tm_mday es el día del mes

  char currentDate[20];
  sprintf(currentDate, "%02d/%02d/%04d", tm->tm_mday, tm->tm_mon + 1, tm->tm_year + 1900);

  return String(currentDate);
}

String getTime(void){
  unsigned long currentHours, currentMinutes, currentSeconds;
  getTime(&currentHours, &currentMinutes, &currentSeconds);

  char LocalHour[10];
  sprintf(LocalHour, "%02d:%02d", currentHours, currentMinutes);
  
  String mystring(LocalHour);
  return LocalHour;
}

enum EHashRateScale
{
  HashRateScale_99KH,
  HashRateScale_999KH,
  HashRateScale_9MH
};

static EHashRateScale s_hashrate_scale = HashRateScale_99KH;
static uint32_t s_skip_first = 3;
static double s_top_hashrate = 0.0;

static std::list<double> s_hashrate_avg_list;

// Nosso firmware: ultima media e historico para o grafico da interface
static double s_last_hashrate = 0.0;
static float s_hist[HASHRATE_HISTORY];
static uint8_t s_hist_count = 0;
static uint8_t s_hist_head = 0;
static uint32_t s_hist_last_ms = 0;

double lastHashRateKHs(void)
{
  return s_last_hashrate;
}

// Copia o historico do mais antigo para o mais novo; devolve quantas amostras
uint8_t hashRateHistory(float *out, uint8_t max)
{
  uint8_t n = s_hist_count < max ? s_hist_count : max;
  uint8_t first = (s_hist_head + HASHRATE_HISTORY - n) % HASHRATE_HISTORY;
  for (uint8_t i = 0; i < n; ++i)
    out[i] = s_hist[(first + i) % HASHRATE_HISTORY];
  return n;
}

double networkDifficulty(void)
{
  updateGlobalData();
  return gData.netDifficulty;
}

double networkDifficultyChange(void)
{
  return gData.difficultyChange;
}

float bitcoinChange24h(void)
{
  return bitcoin_change_24h;
}

static void pushHashRateHistory(double khs)
{
  uint32_t now = millis();
  if (s_hist_count > 0 && now - s_hist_last_ms < HASHRATE_HISTORY_MS)
    return;
  s_hist_last_ms = now;
  s_hist[s_hist_head] = (float)khs;
  s_hist_head = (s_hist_head + 1) % HASHRATE_HISTORY;
  if (s_hist_count < HASHRATE_HISTORY)
    s_hist_count++;
}
static double s_hashrate_summ = 0.0;
static uint8_t s_hashrate_recalc = 0;

String getCurrentHashRate(unsigned long mElapsed)
{
  double hashrate = (double)elapsedKHs * 1000.0 / (double)mElapsed;

  s_hashrate_summ += hashrate;
  s_hashrate_avg_list.push_back(hashrate);
  if (s_hashrate_avg_list.size() > 10)
  {
    s_hashrate_summ -= s_hashrate_avg_list.front();
    s_hashrate_avg_list.pop_front();
  }

  ++s_hashrate_recalc;
  if (s_hashrate_recalc == 0)
  {
    s_hashrate_summ = 0.0;
    for (auto itt = s_hashrate_avg_list.begin(); itt != s_hashrate_avg_list.end(); ++itt)
      s_hashrate_summ += *itt;
  }

  double avg_hashrate = s_hashrate_summ / (double)s_hashrate_avg_list.size();
  if (avg_hashrate < 0.0)
    avg_hashrate = 0.0;

  if (s_skip_first > 0)
  {
    s_skip_first--;
  } else
  {
    s_last_hashrate = avg_hashrate;
    pushHashRateHistory(avg_hashrate);
    if (avg_hashrate > s_top_hashrate)
    {
      s_top_hashrate = avg_hashrate;
      if (avg_hashrate > 999.9)
        s_hashrate_scale = HashRateScale_9MH;
      else if (avg_hashrate > 99.9)
        s_hashrate_scale = HashRateScale_999KH;
    }
  }

  switch (s_hashrate_scale)
  {
    case HashRateScale_99KH:
      return String(avg_hashrate, 2);
    case HashRateScale_999KH:
      return String(avg_hashrate, 1);
    default:
      return String((int)avg_hashrate );
  }
}

mining_data getMiningData(unsigned long mElapsed)
{
  mining_data data;

  char best_diff_string[16] = {0};
  suffix_string(best_diff, best_diff_string, 16, 0);

  char timeMining[15] = {0};
  uint64_t tm = upTime;
  int secs = tm % 60;
  tm /= 60;
  int mins = tm % 60;
  tm /= 60;
  int hours = tm % 24;
  int days = tm / 24;
  sprintf(timeMining, "%01d  %02d:%02d:%02d", days, hours, mins, secs);

  data.completedShares = shares;
  data.totalMHashes = Mhashes;
  data.totalKHashes = totalKHashes;
  data.currentHashRate = getCurrentHashRate(mElapsed);
  data.templates = templates;
  data.bestDiff = best_diff_string;
  data.timeMining = timeMining;
  data.valids = valids;
  data.temp = String(temperatureRead(), 0);
  data.currentTime = getTime();

  return data;
}

clock_data getClockData(unsigned long mElapsed)
{
  clock_data data;

  data.completedShares = shares;
  data.totalKHashes = totalKHashes;
  data.currentHashRate = getCurrentHashRate(mElapsed);
  data.btcPrice = getBTCprice();
  data.blockHeight = getBlockHeight();
  data.currentTime = getTime();
  data.currentDate = getDate();

  return data;
}

clock_data_t getClockData_t(unsigned long mElapsed)
{
  clock_data_t data;

  data.valids = valids;
  data.currentHashRate = getCurrentHashRate(mElapsed);
  getTime(&data.currentHours, &data.currentMinutes, &data.currentSeconds);

  return data;
}

coin_data getCoinData(unsigned long mElapsed)
{
  coin_data data;

  updateGlobalData(); // Update gData vars asking mempool APIs

  data.completedShares = shares;
  data.totalKHashes = totalKHashes;
  data.currentHashRate = getCurrentHashRate(mElapsed);
  data.btcPrice = getBTCprice();
  data.currentTime = getTime();
#ifdef SCREEN_FEES_ENABLE
  data.hourFee = String(gData.hourFee);
  data.fastestFee = String(gData.fastestFee);
  data.economyFee = String(gData.economyFee);
  data.minimumFee = String(gData.minimumFee);
#endif
  data.halfHourFee = String(gData.halfHourFee) + " sat/vB";
  data.netwrokDifficulty = gData.difficulty;
  data.globalHashRate = gData.globalHash;
  data.blockHeight = getBlockHeight();

  unsigned long currentBlock = data.blockHeight.toInt();
  unsigned long remainingBlocks = (((currentBlock / HALVING_BLOCKS) + 1) * HALVING_BLOCKS) - currentBlock;
  data.progressPercent = (HALVING_BLOCKS - remainingBlocks) * 100 / HALVING_BLOCKS;
  data.remainingBlocks = String(remainingBlocks) + " BLOCKS";

  return data;
}

String getPoolAPIUrl(void) {
    poolAPIUrl = String(getPublicPool);
    if (activePoolAddress() == "public-pool.io") {
        poolAPIUrl = "https://public-pool.io:40557/api/client/";
    } 
    else {
        if (activePoolAddress() == "pool.nerdminers.org") {
            poolAPIUrl = "https://pool.nerdminers.org/users/";
        }
        else {
            switch (activePoolPort()) {
                case 3333:
                    if (activePoolAddress() == "pool.sethforprivacy.com")
                        poolAPIUrl = "https://pool.sethforprivacy.com/api/client/";
                    if (activePoolAddress() == "pool.solomining.de")
                        poolAPIUrl = "https://pool.solomining.de/api/client/";
                    // Add more cases for other addresses with port 3333 if needed
                    break;
                case 2018:
                    // Local instance of public-pool.io on Umbrel or Start9
                    poolAPIUrl = "http://" + activePoolAddress() + ":2019/api/client/";
                    break;
                default:
                    poolAPIUrl = String(getPublicPool);
                    break;
            }
        }
    }
    return poolAPIUrl;
}

pool_data getPoolData(void){
    //pool_data pData;    
    if((mPoolUpdate == 0) || (millis() - mPoolUpdate > UPDATE_POOL_min * 60 * 1000)){      
        if (WiFi.status() != WL_CONNECTED) return pData;            
        //Make first API call to get global hash and current difficulty
        HTTPClient http;
        http.setTimeout(10000);        
        try {          
          String btcWallet = Settings.BtcWallet;
          // Serial.println(btcWallet);
          if (btcWallet.indexOf(".")>0) btcWallet = btcWallet.substring(0,btcWallet.indexOf("."));
#ifdef SCREEN_WORKERS_ENABLE
          poolAPIUrl = getPoolAPIUrl(); //Nosso firmware: segue a pool em uso (principal ou reserva)
          Serial.println("Pool API : " + poolAPIUrl+btcWallet);
          http.begin(poolAPIUrl+btcWallet);
#else
          http.begin(String(getPublicPool)+btcWallet);
#endif
          int httpCode = http.GET();
          if (httpCode == HTTP_CODE_OK) {
              String payload = http.getString();
              // Serial.println(payload);
              StaticJsonDocument<300> filter;
              filter["bestDifficulty"] = true;
              filter["workersCount"] = true;
              filter["workers"][0]["sessionId"] = true;
              filter["workers"][0]["hashRate"] = true;
              StaticJsonDocument<2048> doc;
              deserializeJson(doc, payload, DeserializationOption::Filter(filter));
              //Serial.println(serializeJsonPretty(doc, Serial));
              if (doc.containsKey("workersCount")) pData.workersCount = doc["workersCount"].as<int>();
              const JsonArray& workers = doc["workers"].as<JsonArray>();
              float totalhashs = 0;
              for (const JsonObject& worker : workers) {
                totalhashs += worker["hashRate"].as<double>();
                /* Serial.print(worker["sessionId"].as<String>()+": ");
                Serial.print(" - "+worker["hashRate"].as<String>()+": ");
                Serial.println(totalhashs); */
              }
              char totalhashs_s[16] = {0};
              suffix_string(totalhashs, totalhashs_s, 16, 0);
              pData.workersHash = String(totalhashs_s);

              double temp;
              if (doc.containsKey("bestDifficulty")) {
              temp = doc["bestDifficulty"].as<double>();            
              char best_diff_string[16] = {0};
              suffix_string(temp, best_diff_string, 16, 0);
              pData.bestDifficulty = String(best_diff_string);
              }
              doc.clear();
              mPoolUpdate = millis();
              Serial.println("\n####### Pool Data OK!");               
          } else {
              Serial.println("\n####### Pool Data HTTP Error!");    
              /* Serial.println(httpCode);
              String payload = http.getString();
              Serial.println(payload); */
              // mPoolUpdate = millis();
              pData.bestDifficulty = "P";
              pData.workersHash = "E";
              pData.workersCount = 0;
              http.end();
              return pData; 
          }
          http.end();
        } catch(...) {
          Serial.println("####### Pool Error!");          
          // mPoolUpdate = millis();
          pData.bestDifficulty = "P";
          pData.workersHash = "Error";
          pData.workersCount = 0;
          http.end();
          return pData;
        } 
    }
    return pData;
}

/********************* Nosso firmware: dados do painel web *********************/

static std::mutex s_painel_mutex;
static char s_painel_json[PAINEL_JSON_MAX] = "{}";
static uint32_t s_painel_visita = 0;

void painelMarcaVisita(void)
{
  s_painel_visita = millis();
}

size_t painelCopiaJson(char *destino, size_t max)
{
  std::lock_guard<std::mutex> lock(s_painel_mutex);
  strncpy(destino, s_painel_json, max - 1);
  destino[max - 1] = 0;
  return strlen(destino);
}

static String jsonTexto(const String &s)
{
  String r = "\"";
  for (unsigned int i = 0; i < s.length(); ++i)
  {
    char c = s[i];
    if (c == '"' || c == '\\') { r += '\\'; r += c; }
    else if ((uint8_t)c >= 0x20) r += c;
  }
  return r + "\"";
}

// Chamado pelo runMonitor a cada segundo. So busca dados na internet se alguem abriu o painel nos ultimos 2 min.
void painelAtualiza(void)
{
  if (s_painel_visita == 0 || millis() - s_painel_visita > 120000UL)
    return;

  updateGlobalData();
  getBTCprice();
  String bloco = getBlockHeight();
  getPoolData();

  const char *estado = "AJUSTE";
  if (WiFi.status() != WL_CONNECTED) estado = "SEM WI-FI";
  else if (mMonitor.NerdStatus == NM_hashing) estado = "ATIVO";
  else if (mMonitor.NerdStatus == NM_Connecting) estado = "CONECTANDO";

  char melhor[16] = {0};
  suffix_string(best_diff, melhor, sizeof(melhor), 0);

  float hist[HASHRATE_HISTORY];
  uint8_t n = hashRateHistory(hist, HASHRATE_HISTORY);
  String h = "[";
  for (uint8_t i = 0; i < n; ++i)
  {
    if (i) h += ",";
    h += String(hist[i], 1);
  }
  h += "]";

  uint32_t khsOriginal = 0, khsRapido = 0;
  hwShaSelfTestKHs(khsOriginal, khsRapido);

  unsigned long blocoN = bloco.toInt();
  unsigned long faltam = blocoN ? (((blocoN / HALVING_BLOCKS) + 1) * HALVING_BLOCKS) - blocoN : 0;

  String j;
  j.reserve(2048);
  j += "{\"v\":" + jsonTexto(CURRENT_VERSION);
  j += ",\"estado\":" + jsonTexto(estado);
  j += ",\"hora\":" + jsonTexto(getTime()) + ",\"data\":" + jsonTexto(getDate());
  j += ",\"khs\":" + String(lastHashRateKHs(), 1) + ",\"hist\":" + h;
  j += ",\"melhor\":" + jsonTexto(melhor) + ",\"melhorNum\":" + String(best_diff, 0);
  j += ",\"partes\":" + String(shares) + ",\"blocos\":" + String(valids) + ",\"modelos\":" + String(templates);
  j += ",\"ligado\":" + String((uint32_t)upTime);
  j += ",\"temp\":" + String(temperatureRead(), 0) + ",\"heap\":" + String(ESP.getFreeHeap()) + ",\"heapTotal\":" + String(ESP.getHeapSize());
  j += ",\"rssi\":" + String(WiFi.RSSI()) + ",\"ip\":" + jsonTexto(WiFi.localIP().toString()) + ",\"ssid\":" + jsonTexto(WiFi.SSID());
  j += ",\"shaRapido\":" + String(hwShaFastPath() ? "true" : "false") + ",\"khsOriginal\":" + String(khsOriginal) + ",\"khsRapido\":" + String(khsRapido);
  j += ",\"pool\":{\"end\":" + jsonTexto(activePoolAddress()) + ",\"porta\":" + String(activePoolPort());
  j += ",\"reserva\":" + String(activePoolIndex() == 1 ? "true" : "false") + ",\"mineradores\":" + String(pData.workersCount);
  j += ",\"hash\":" + jsonTexto(pData.workersHash) + ",\"melhor\":" + jsonTexto(pData.bestDifficulty) + ",\"quedas\":" + String(poolQuedasUltimaHora()) + "}";
  j += ",\"preco\":" + String(bitcoin_price) + ",\"moeda\":" + jsonTexto(String(PRICE_FORMAT).substring(0, String(PRICE_FORMAT).indexOf('%')));
  j += ",\"var24h\":" + String(bitcoin_change_24h, 2) + ",\"bloco\":" + String(blocoN);
  j += ",\"hashRede\":" + jsonTexto(gData.globalHash) + ",\"dificuldade\":" + jsonTexto(gData.difficulty);
  j += ",\"difNum\":" + String(gData.netDifficulty, 0) + ",\"ajuste\":" + String(gData.difficultyChange, 2);
#ifdef SCREEN_FEES_ENABLE
  j += ",\"taxas\":[" + String(gData.fastestFee) + "," + String(gData.halfHourFee) + "," + String(gData.hourFee) + "," + String(gData.economyFee) + "," + String(gData.minimumFee) + "]";
#else
  j += ",\"taxas\":[0," + String(gData.halfHourFee) + ",0,0,0]";
#endif
  j += ",\"halving\":{\"faltam\":" + String(faltam) + ",\"pct\":" + String(blocoN ? 100.0 * (HALVING_BLOCKS - faltam) / HALVING_BLOCKS : 0.0, 1) + "}";
  j += ",\"config\":{\"pool\":" + jsonTexto(Settings.PoolAddress) + ",\"porta\":" + String(Settings.PoolPort);
  j += ",\"pool2\":" + jsonTexto(Settings.PoolAddress2) + ",\"porta2\":" + String(Settings.PoolPort2);
  j += ",\"carteira\":" + jsonTexto(String(Settings.BtcWallet)) + ",\"fuso\":" + String(Settings.Timezone) + "}}";

  std::lock_guard<std::mutex> lock(s_painel_mutex);
  strncpy(s_painel_json, j.c_str(), PAINEL_JSON_MAX - 1);
  s_painel_json[PAINEL_JSON_MAX - 1] = 0;
}
