#include <Preferences.h>

#define NVS_NAMESPACE "spotify"
#define NVS_KEY_REFRESH "refreshToken"
#define NVS_KEY_CLIENT_ID "clientId"
#define NVS_KEY_CLIENT_SECRET "clientSecret"

void saveConfigFile(char *refreshToken, char *clientId, char *clientSecret);

static bool copyConfigToBuffers(char *refreshToken, char *clientId, char *clientSecret,
    const String &rt, const String &cid, const String &csec) {
  strncpy(refreshToken, rt.c_str(), 399);
  refreshToken[399] = '\0';
  strncpy(clientId, cid.c_str(), 49);
  clientId[49] = '\0';
  strncpy(clientSecret, csec.c_str(), 49);
  clientSecret[49] = '\0';
  return true;
}

bool fetchConfigFile(char *refreshToken, char *clientId, char *clientSecret) {
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, true)) {  // read-only
    Serial.println("NVS open failed (read)");
    return false;
  }

  String rt = prefs.getString(NVS_KEY_REFRESH, "");
  String cid = prefs.getString(NVS_KEY_CLIENT_ID, "");
  String csec = prefs.getString(NVS_KEY_CLIENT_SECRET, "");
  prefs.end();

  if (!cid.isEmpty() && !csec.isEmpty()) {
    Serial.println("reading config from NVS");
    return copyConfigToBuffers(refreshToken, clientId, clientSecret, rt, cid, csec);
  }

  Serial.println("Config missing client ID or Secret (or not yet saved)");
  return false;
}

void saveConfigFile(char *refreshToken, char *clientId, char *clientSecret) {
  Serial.println(F("Saving config to NVS"));
  Preferences prefs;
  if (!prefs.begin(NVS_NAMESPACE, false)) {  // read-write
    Serial.println("NVS open failed (write)");
    return;
  }

  prefs.putString(NVS_KEY_REFRESH, refreshToken);
  prefs.putString(NVS_KEY_CLIENT_ID, clientId);
  prefs.putString(NVS_KEY_CLIENT_SECRET, clientSecret);
  prefs.end();
  Serial.println(F("Config saved."));
}
