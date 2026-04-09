// LILYGO T-Embed S3 CC1101 — 170x320 ST7789, rotary encoder (rotate L = prev, R = next)
// Official pins from Xinyuan-LilyGO/T-Embed-CC1101 README (GPIO 27/28 are flash/PSRAM on ESP32-S3!)
// Display: CS=41, DC=16, RST=40, BL=21, MOSI=9, SCLK=11. Encoder: A=4, B=5, KEY=0. PWR_EN=15.

#include "spotifyDisplay.h"

#include <TFT_eSPI.h>
#include <RotaryEncoder.h>
#include <JPEGDEC.h>
#include <Adafruit_NeoPixel.h>

TFT_eSPI _lcd = TFT_eSPI();
JPEGDEC jpeg;

const char *ALBUM_ART = "/album.jpg";

// Rotary encoder: official T-Embed CC1101 uses GPIO 4 (A) and 5 (B) — do NOT use 27/28 (flash/PSRAM)
#define TEMBED_ENCODER_A 4
#define TEMBED_ENCODER_B 5
#define TEMBED_BTN_PLAYPAUSE 0  // encoder key (LILYGO ENCODER_KEY)
#define TEMBED_BTN_SEEK       6  // secondary button: hold to scrub with encoder
#define TEMBED_LED_PIN       14  // WS2812 RGB LED ring around encoder
#define TEMBED_LED_COUNT      8  // number of LEDs in ring (adjust if different)

RotaryEncoder encoder(TEMBED_ENCODER_A, TEMBED_ENCODER_B, RotaryEncoder::LatchMode::FOUR3);

extern bool spotifyIsPlaying;
extern long songStartMillis;
extern long songDuration;

static bool s_prevTriggered = false;
static bool s_nextTriggered = false;
static bool s_playPauseTriggered = false;
static uint32_t s_lastBtnMs = 0;
static bool s_lastBtnState = true;  // HIGH = not pressed (pull-up)

// Seek (scrub) mode: hold TEMBED_BTN_SEEK, then encoder changes position
static bool s_seekWasHeld = false;
static long s_scrubPositionMs = 0;
static const long SEEK_STEP_MS = 10000;  // 10 seconds per encoder tick

// WS2812 LED ring: green = playing, blue = paused
static Adafruit_NeoPixel s_ledRing(TEMBED_LED_COUNT, TEMBED_LED_PIN, NEO_GRB + NEO_KHZ800);
static bool s_lastLedPlaying = false;
static bool s_ledInitialized = false;

// Accent color: sampled during decode (RGB565 → accumulated R,G,B)
static uint32_t s_accR = 0, s_accG = 0, s_accB = 0;
static uint32_t s_accCount = 0;

static inline void sampleAccentPixel(uint16_t rgb565)
{
  uint8_t r = (rgb565 >> 11) & 0x1F;
  uint8_t g = (rgb565 >> 5) & 0x3F;
  uint8_t b = rgb565 & 0x1F;
  s_accR += (r * 255) / 31;
  s_accG += (g * 255) / 63;
  s_accB += (b * 255) / 31;
  s_accCount++;
}

int JPEGDraw(JPEGDRAW *pDraw)
{
  if (pDraw->y >= _lcd.height())
    return 0;
  // Sample center pixel for accent (one per block)
  int cx = pDraw->iWidth / 2, cy = pDraw->iHeight / 2;
  if (pDraw->iWidth > 0 && pDraw->iHeight > 0)
    sampleAccentPixel(pDraw->pPixels[cy * pDraw->iWidth + cx]);
  _lcd.pushImage(pDraw->x, pDraw->y, pDraw->iWidth, pDraw->iHeight, pDraw->pPixels);
  return 1;
}

fs::File myfile;

void *myOpen(const char *filename, int32_t *size)
{
  myfile = SPIFFS.open(filename);
  *size = myfile.size();
  return &myfile;
}
void myClose(void *handle)
{
  if (myfile) myfile.close();
}
int32_t myRead(JPEGFILE *handle, uint8_t *buffer, int32_t length)
{
  if (!myfile) return 0;
  return myfile.read(buffer, length);
}
int32_t mySeek(JPEGFILE *handle, int32_t position)
{
  if (!myfile) return 0;
  return myfile.seek(position);
}

// Layout: 320 x 170 landscape — larger cover on ESP32-S3 (medium image, scale half → ~150px)
#define IMG_SIZE     150
#define IMG_X        0
#define TEXT_X       158
#define PROG_BAR_Y   (170 - 14)
#define PROG_BAR_H   10
#define PROG_BAR_MARGIN 6
#define CONTENT_H    PROG_BAR_Y  // 156

class TEmbedDisplay : public SpotifyDisplay
{
  uint16_t accentBg = TFT_BLACK;
  uint16_t textColor = TFT_WHITE;

public:
  void displaySetup(SpotifyArduino *spotifyObj)
  {
    spotify_display = spotifyObj;

    setWidth(320);
    setHeight(170);
    setImageWidth(IMG_SIZE);
    setImageHeight(IMG_SIZE);

    // Power enable: keep board on (T-Embed CC1101 BOARD_PWR_EN = 15)
    pinMode(15, OUTPUT);
    digitalWrite(15, HIGH);

    // Backlight: official T-Embed CC1101 DISPLAY_BL = 21
    pinMode(21, OUTPUT);
    digitalWrite(21, HIGH);

    delay(50);

    // Manual hardware reset on GPIO 40 (DISPLAY_RST in LILYGO alt config); TFT_RST=-1 in build
    pinMode(40, OUTPUT);
    digitalWrite(40, LOW);
    delay(20);
    digitalWrite(40, HIGH);
    delay(150);

    _lcd.init();
    _lcd.setRotation(3);   // 3 = flipped 180° (270° from portrait)
    _lcd.fillScreen(TFT_BLACK);
    encoder.setPosition(0);

    pinMode(TEMBED_BTN_PLAYPAUSE, INPUT_PULLUP);
    pinMode(TEMBED_BTN_SEEK, INPUT_PULLUP);

    // LED ring init last: driving GPIO 14 can affect display inversion on this board,
    // so we re-apply correct inversion after NeoPixel begin()
    s_ledRing.begin();
    s_ledRing.setBrightness(TEMBED_LED_BRIGHTNESS);
    _lcd.invertDisplay(true);  // re-apply inversion after NeoPixel init on GPIO 14
    s_ledInitialized = true;
    s_lastLedPlaying = !spotifyIsPlaying;  // force first update in checkForInput
  }

  void showDefaultScreen()
  {
    _lcd.fillScreen(TFT_BLACK);
    _lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    _lcd.drawCentreString("IDLE.", screenCenterX, 75, 2);
  }

  void displayTrackProgress(long progress, long duration)
  {
    if (duration == 0) return;
    int barTotalW = screenWidth - (2 * PROG_BAR_MARGIN);
    int innerW = barTotalW - 2;
    int barW = map(progress, 0, duration, 0, innerW);
    _lcd.drawRect(PROG_BAR_MARGIN, PROG_BAR_Y, barTotalW, PROG_BAR_H, textColor);
    _lcd.fillRect(PROG_BAR_MARGIN + 1, PROG_BAR_Y + 1, barW, PROG_BAR_H - 2, textColor);
    _lcd.fillRect(PROG_BAR_MARGIN + 1 + barW, PROG_BAR_Y + 1, innerW - barW, PROG_BAR_H - 2, accentBg);
  }

  void printCurrentlyPlayingToScreen(CurrentlyPlaying currentlyPlaying)
  {
    int textW = screenWidth - TEXT_X;
    _lcd.fillRect(TEXT_X, 0, textW, CONTENT_H, accentBg);
    _lcd.setTextColor(textColor, accentBg);
    _lcd.setTextDatum(TL_DATUM);
    _lcd.drawString(currentlyPlaying.trackName, TEXT_X, 5, 2);
    _lcd.drawString(currentlyPlaying.artists[0].artistName, TEXT_X, 27, 2);
    _lcd.drawString(currentlyPlaying.albumName, TEXT_X, 49, 2);
  }

  void checkForInput()
  {
    encoder.tick();
    int dir = (int)encoder.getDirection();
    bool seekHeld = (digitalRead(TEMBED_BTN_SEEK) == LOW);

    // When seek button is held: encoder scrubs position instead of changing track
    if (seekHeld && songStartMillis != 0 && songDuration > 0)
    {
      if (!s_seekWasHeld)
      {
        s_scrubPositionMs = (long)(millis() - songStartMillis);
        if (s_scrubPositionMs < 0) s_scrubPositionMs = 0;
        if (s_scrubPositionMs > songDuration) s_scrubPositionMs = songDuration;
        s_seekWasHeld = true;
      }
      if (dir != 0)
      {
        s_scrubPositionMs -= (long)dir * SEEK_STEP_MS;
        if (s_scrubPositionMs < 0) s_scrubPositionMs = 0;
        if (s_scrubPositionMs > songDuration) s_scrubPositionMs = songDuration;
        spotify_display->seek((int)s_scrubPositionMs);
        songStartMillis = millis() - s_scrubPositionMs;
        requestDueTime = 0;
        displayTrackProgress(s_scrubPositionMs, songDuration);
      }
    }
    else
    {
      s_seekWasHeld = false;
      // getDirection(): DIR_CW = 1 (next), DIR_CCW = -1 (prev)
      if (dir < 0)
        s_nextTriggered = true;
      else if (dir > 0)
        s_prevTriggered = true;
    }

    // Play/pause button (encoder key): active LOW, debounced
    bool btn = (digitalRead(TEMBED_BTN_PLAYPAUSE) == LOW);
    uint32_t now = millis();
    if (btn != s_lastBtnState)
    {
      if (now - s_lastBtnMs > 40)
      {
        s_lastBtnState = btn;
        s_lastBtnMs = now;
        if (btn)
          s_playPauseTriggered = true;
      }
    }

    if (s_prevTriggered)
    {
      s_prevTriggered = false;
      flashEncoderFeedback(true);
      Serial.println("ENCODER: previous track (rotate L)");
      int result = spotify_display->previousTrack();
      Serial.print("previousTrack HTTP: ");
      Serial.println(result);
      requestDueTime = 0;
    }
    if (s_nextTriggered)
    {
      s_nextTriggered = false;
      flashEncoderFeedback(false);
      Serial.println("ENCODER: next track (rotate R)");
      int result = spotify_display->nextTrack();
      Serial.print("nextTrack HTTP: ");
      Serial.println(result);
      requestDueTime = 0;
    }
    if (s_playPauseTriggered)
    {
      s_playPauseTriggered = false;
      int result;
      if (spotifyIsPlaying)
      {
        Serial.println("BTN: pause");
        result = spotify_display->pause();
      }
      else
      {
        Serial.println("BTN: play");
        result = spotify_display->play();
      }
      Serial.print("playback HTTP: ");
      Serial.println(result);
      spotifyIsPlaying = !spotifyIsPlaying;  // toggle until next API state
      requestDueTime = 0;
    }

    // LED ring: green when playing, blue when paused (update only when state changes)
    if (s_ledInitialized && s_lastLedPlaying != spotifyIsPlaying)
    {
      s_lastLedPlaying = spotifyIsPlaying;
      uint32_t c = spotifyIsPlaying ? s_ledRing.Color(0, 255, 0) : s_ledRing.Color(0, 0, 255);
      for (int i = 0; i < s_ledRing.numPixels(); i++)
        s_ledRing.setPixelColor(i, c);
      s_ledRing.show();
    }
  }

  void clearImage()
  {
    _lcd.fillRect(IMG_X, 0, IMG_SIZE + 8, CONTENT_H, accentBg);
  }

  boolean processImageInfo(CurrentlyPlaying currentlyPlaying)
  {
    // Prefer medium image for bigger cover on ESP32-S3; fall back to small
    int idx = (currentlyPlaying.numImages >= 2) ? (currentlyPlaying.numImages - 2) : (currentlyPlaying.numImages - 1);
    SpotifyImage img = currentlyPlaying.albumImages[idx];
    if (!albumDisplayed || !isSameAlbum(img.url))
    {
      albumDisplayed = false;
      setImageHeight(img.height);
      setImageWidth(img.width);
      setAlbumArtUrl(img.url);
      return true;
    }
    return false;
  }

  int displayImage()
  {
    int imageStatus = displayImageUsingFile(_albumArtUrl);
    if (imageStatus == 1)
      albumDisplayed = true;
    return imageStatus;
  }

  void markDisplayAsTagRead() {}
  void markDisplayAsTagWritten() {}

  void drawWifiManagerMessage(WiFiManager *myWiFiManager)
  {
    Serial.println("Entered Conf Mode");
    _lcd.fillScreen(TFT_BLACK);
    _lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    _lcd.drawCentreString("WiFi Config Mode", screenCenterX, 5, 2);
    _lcd.setTextColor(TFT_CYAN, TFT_BLACK);
    _lcd.drawString(myWiFiManager->getConfigPortalSSID(), 5, 30, 2);
    _lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    _lcd.drawString("Pass: thing123", 5, 52, 2);
    _lcd.setTextColor(TFT_CYAN, TFT_BLACK);
    _lcd.drawString(WiFi.softAPIP().toString(), 5, 74, 2);
  }

  void drawRefreshTokenMessage()
  {
    Serial.println("Refresh Token Mode");
    _lcd.fillScreen(TFT_BLACK);
    _lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    _lcd.drawCentreString("Spotify Auth", screenCenterX, 5, 2);
    _lcd.drawString("Visit this address:", 5, 30, 2);
    _lcd.setTextColor(TFT_CYAN, TFT_BLACK);
    _lcd.drawString(WiFi.localIP().toString(), 5, 52, 2);
  }

private:
  void flashEncoderFeedback(bool isPrev)
  {
    int x = isPrev ? 0 : (screenWidth - 4);
    _lcd.fillRect(x, 0, 4, screenHeight, TFT_GREEN);
    delay(120);
    _lcd.fillRect(x, 0, 4, screenHeight, accentBg);
  }

  int displayImageUsingFile(char *albumArtUrl)
  {
    if (SPIFFS.exists(ALBUM_ART))
    {
      Serial.println("Removing existing image");
      SPIFFS.remove(ALBUM_ART);
    }

    fs::File f = SPIFFS.open(ALBUM_ART, "w+");
    if (!f)
    {
      Serial.println("file open failed");
      return -1;
    }

    client.setCACert(spotify_image_server_cert);
    bool gotImage = spotify_display->getImage(albumArtUrl, &f);
    client.setCACert(spotify_server_cert);
    f.close();

    if (gotImage)
      return drawImageFromFile(ALBUM_ART);

    return -2;
  }

  int drawImageFromFile(const char *imageFileUri)
  {
    s_accR = s_accG = s_accB = s_accCount = 0;

    unsigned long lTime = millis();
    jpeg.open(imageFileUri, myOpen, myClose, myRead, mySeek, JPEGDraw);
    jpeg.setPixelType(1);

    // Use half scale for medium/large images so cover is ~150px (ESP32-S3 headroom)
    int scaleOpt = (imageWidth > 140 || imageHeight > 140) ? JPEG_SCALE_HALF : 0;
    int decodedH = (scaleOpt == JPEG_SCALE_HALF) ? (imageHeight / 2) : imageHeight;
    int decodedW = (scaleOpt == JPEG_SCALE_HALF) ? (imageWidth / 2) : imageWidth;
    int imageY = (CONTENT_H - decodedH) / 2;

    int decodeStatus = jpeg.decode(IMG_X, imageY, scaleOpt);
    jpeg.close();

    // Compute accent from sampled pixels and choose text color for readability
    if (s_accCount > 0)
    {
      uint8_t ar = (uint8_t)(s_accR / s_accCount);
      uint8_t ag = (uint8_t)(s_accG / s_accCount);
      uint8_t ab = (uint8_t)(s_accB / s_accCount);
      accentBg = _lcd.color565(ar, ag, ab);
      float lum = 0.299f * ar + 0.587f * ag + 0.114f * ab;
      textColor = (lum < 128.0f) ? TFT_WHITE : TFT_BLACK;
    }

    // Fill non-image areas with accent so background is cohesive
    _lcd.fillRect(decodedW, 0, screenWidth - decodedW, CONTENT_H, accentBg);
    if (imageY > 0)
      _lcd.fillRect(0, 0, decodedW, imageY, accentBg);
    if (imageY + decodedH < CONTENT_H)
      _lcd.fillRect(0, imageY + decodedH, decodedW, CONTENT_H - (imageY + decodedH), accentBg);

    Serial.print("Image decode (ms): ");
    Serial.println(millis() - lTime);
    return decodeStatus;
  }
};
