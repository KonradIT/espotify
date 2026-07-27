// M5Stack Core Basic (320x240 ILI9341) — layout adapted from the Cheap Yellow Display.
// Buttons: A=prev (GPIO39), B=play/pause (GPIO38), C=next (GPIO37)

#include "spotifyDisplay.h"

#include <TFT_eSPI.h>
#include <Button2.h>
#include <JPEGDEC.h>

TFT_eSPI tft = TFT_eSPI();
JPEGDEC jpeg;

const char *ALBUM_ART = "/album.jpg";

int JPEGDraw(JPEGDRAW *pDraw)
{
  // Stop further decoding as image is running off bottom of screen
  if (pDraw->y >= tft.height())
    return 0;

  tft.pushImage(pDraw->x, pDraw->y, pDraw->iWidth, pDraw->iHeight, pDraw->pPixels);
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
  if (myfile)
    myfile.close();
}
int32_t myRead(JPEGFILE *handle, uint8_t *buffer, int32_t length)
{
  if (!myfile)
    return 0;
  return myfile.read(buffer, length);
}
int32_t mySeek(JPEGFILE *handle, int32_t position)
{
  if (!myfile)
    return 0;
  return myfile.seek(position);
}

// ---- Buttons: A=prev, B=play/pause, C=next ----
// GPIO 34-39 are input-only with no internal pull resistor; M5Stack's PCB
// provides the external pull-up, so plain INPUT (not INPUT_PULLUP) is correct.
#define M5_BTN_A_PIN 39
#define M5_BTN_B_PIN 38
#define M5_BTN_C_PIN 37

Button2 btnA(M5_BTN_A_PIN, INPUT);
Button2 btnB(M5_BTN_B_PIN, INPUT);
Button2 btnC(M5_BTN_C_PIN, INPUT);

static bool s_prevTriggered = false;
static bool s_playPauseTriggered = false;
static bool s_nextTriggered = false;

extern bool spotifyIsPlaying;

// ---- Layout (320 x 240, same as CYD) ----
#define PROGRESS_BAR_Y (150 + 5)
#define PROGRESS_BAR_H 20
#define TEXT_START_Y (150 + 30)
#define TEXT_LINE_H 18

// Button hint row along the bottom edge, above the physical A/B/C buttons.
// Fits in the few spare pixels below CYD's text block (last line ends ~232).
#define BTN_ROW_H 6
#define BTN_ROW_Y (240 - BTN_ROW_H)
#define BTN_ZONE_W (320 / 3)
#define BTN_ZONE_MARGIN 6

class M5StackDisplay : public SpotifyDisplay
{
public:
  void displaySetup(SpotifyArduino *spotifyObj)
  {
    spotify_display = spotifyObj;

    Serial.println("m5stack display setup");
    setWidth(320);
    setHeight(240);

    setImageHeight(150);
    setImageWidth(150);

    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    // M5Stack's ILI9341 panel needs colours inverted; re-apply explicitly since
    // the driver sends INVON only once during init and some panels need it twice.
    tft.invertDisplay(true);

    // setPressedHandler fires on press-down, not after release — important
    // because updateCurrentlyPlaying() blocks for several seconds and
    // setClickHandler would miss a press made during that window.
    btnA.setPressedHandler([](Button2 &) { s_prevTriggered = true; });
    btnB.setPressedHandler([](Button2 &) { s_playPauseTriggered = true; });
    btnC.setPressedHandler([](Button2 &) { s_nextTriggered = true; });
  }

  void showDefaultScreen()
  {
    tft.fillScreen(TFT_BLACK);
    drawButtonRow(false, false, false);
  }

  void displayTrackProgress(long progress, long duration)
  {
    float percentage = ((float)progress / (float)duration) * 100;
    int clampedPercentage = (int)percentage;
    int barXWidth = map(clampedPercentage, 0, 100, 0, screenWidth - 40);

    // Draw outer Rectangle, in theory we only need to do this once!
    tft.drawRect(19, PROGRESS_BAR_Y, screenWidth - 38, PROGRESS_BAR_H, TFT_WHITE);

    // Draw the white portion of the filled bar
    tft.fillRect(20, PROGRESS_BAR_Y + 1, barXWidth, PROGRESS_BAR_H - 2, TFT_WHITE);

    // Fill whats left black
    tft.fillRect(20 + barXWidth, PROGRESS_BAR_Y + 1, (screenWidth - 20) - (20 + barXWidth), PROGRESS_BAR_H - 2, TFT_BLACK);
  }

  void printCurrentlyPlayingToScreen(CurrentlyPlaying currentlyPlaying)
  {
    // Clear the text (stop above the button hint row, unlike CYD which has no such row)
    int textAreaHeight = BTN_ROW_Y - TEXT_START_Y;
    tft.fillRect(0, TEXT_START_Y, screenWidth, textAreaHeight, TFT_BLACK);

    tft.drawCentreString(currentlyPlaying.trackName, screenCenterX, TEXT_START_Y, 2);
    tft.drawCentreString(currentlyPlaying.artists[0].artistName, screenCenterX, TEXT_START_Y + TEXT_LINE_H, 2);
    tft.drawCentreString(currentlyPlaying.albumName, screenCenterX, TEXT_START_Y + (TEXT_LINE_H * 2), 2);
  }

  void checkForInput()
  {
    btnA.loop();
    btnB.loop();
    btnC.loop();

    if (s_prevTriggered)
    {
      s_prevTriggered = false;
      drawButtonRow(true, false, false);
      Serial.println("BTN A: previous track");
      int result = spotify_display->previousTrack();
      Serial.print("previousTrack HTTP: ");
      Serial.println(result);
      drawButtonRow(false, false, false);
      requestDueTime = 0;
    }
    if (s_playPauseTriggered)
    {
      s_playPauseTriggered = false;
      drawButtonRow(false, true, false);
      int result;
      if (spotifyIsPlaying)
      {
        Serial.println("BTN B: pause");
        result = spotify_display->pause();
      }
      else
      {
        Serial.println("BTN B: play");
        result = spotify_display->play();
      }
      Serial.print("playback HTTP: ");
      Serial.println(result);
      spotifyIsPlaying = !spotifyIsPlaying;
      drawButtonRow(false, false, false);
      requestDueTime = 0;
    }
    if (s_nextTriggered)
    {
      s_nextTriggered = false;
      drawButtonRow(false, false, true);
      Serial.println("BTN C: next track");
      int result = spotify_display->nextTrack();
      Serial.print("nextTrack HTTP: ");
      Serial.println(result);
      drawButtonRow(false, false, false);
      requestDueTime = 0;
    }
  }

  // Image Related
  void clearImage()
  {
    int imagePosition = screenCenterX - (imageWidth / 2);
    tft.fillRect(imagePosition, 0, imageWidth, imageHeight, TFT_BLACK);
  }

  boolean processImageInfo(CurrentlyPlaying currentlyPlaying)
  {
    SpotifyImage currentlyPlayingMedImage = currentlyPlaying.albumImages[currentlyPlaying.numImages - 2];
    if (!albumDisplayed || !isSameAlbum(currentlyPlayingMedImage.url))
    {
      // We have a differenent album than we currently have displayed
      albumDisplayed = false;
      setImageHeight(currentlyPlayingMedImage.height / 2); // medium image is 300, we are going to scale it to half
      setImageWidth(currentlyPlayingMedImage.width / 2);
      setAlbumArtUrl(currentlyPlayingMedImage.url);
      return true;
    }

    return false;
  }

  int displayImage()
  {
    int imageStatus = displayImageUsingFile(_albumArtUrl);
    Serial.print("imageStatus: ");
    Serial.println(imageStatus);
    if (imageStatus == 1)
    {
      albumDisplayed = true;
      return imageStatus;
    }

    return imageStatus;
  }

  // NFC is not wired on M5Stack Core Basic — stubs satisfy the interface
  void markDisplayAsTagRead() {}
  void markDisplayAsTagWritten() {}

  void drawWifiManagerMessage(WiFiManager *myWiFiManager)
  {
    Serial.println("Entered Conf Mode");
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString("Entered Conf Mode:", screenCenterX, 5, 2);
    tft.drawString("Connect to the following WIFI AP:", 5, 28, 2);
    tft.setTextColor(TFT_BLUE, TFT_BLACK);
    tft.drawString(myWiFiManager->getConfigPortalSSID(), 20, 48, 2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Password:", 5, 64, 2);
    tft.setTextColor(TFT_BLUE, TFT_BLACK);
    tft.drawString("thing123", 20, 82, 2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);

    tft.drawString("If it doesn't AutoConnect, use this IP:", 5, 110, 2);
    tft.setTextColor(TFT_BLUE, TFT_BLACK);
    tft.drawString(WiFi.softAPIP().toString(), 20, 128, 2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
  }

  void drawRefreshTokenMessage()
  {
    Serial.println("Refresh Token Mode");
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString("Refresh Token Mode:", screenCenterX, 5, 2);
    tft.drawString("You need to authorize this device to use", 5, 28, 2);
    tft.drawString("your spotify account.", 5, 46, 2);

    tft.drawString("Visit the following address and follow", 5, 82, 2);
    tft.drawString("the instrucitons:", 5, 100, 2);
    tft.setTextColor(TFT_BLUE, TFT_BLACK);
    tft.drawString(WiFi.localIP().toString(), 10, 128, 2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
  }

private:
  // Draws the three button-hint zones along the bottom edge, above the physical
  // A/B/C buttons. The active zone flashes green for the duration of its API call.
  void drawButtonRow(bool aActive, bool bActive, bool cActive)
  {
    drawButtonZone(0, aActive);
    drawButtonZone(1, bActive);
    drawButtonZone(2, cActive);
  }

  void drawButtonZone(int zoneIndex, bool active)
  {
    int x0 = (zoneIndex * BTN_ZONE_W) + BTN_ZONE_MARGIN;
    int w = BTN_ZONE_W - (BTN_ZONE_MARGIN * 2);
    tft.fillRect(x0, BTN_ROW_Y, w, BTN_ROW_H, active ? TFT_GREEN : TFT_DARKGREY);
  }

  int displayImageUsingFile(char *albumArtUrl)
  {
    // In this example I reuse the same filename
    // over and over, maybe saving the art using
    // the album URI as the name would be better
    // as you could save having to download them each
    // time, but this seems to work fine.
    if (SPIFFS.exists(ALBUM_ART) == true)
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

    // Spotify uses a different cert for the Image server, so we need to swap to that for the call
    client.setCACert(spotify_image_server_cert);
    bool gotImage = spotify_display->getImage(albumArtUrl, &f);

    // WiFiClientSecure::connect() doesn't stop() a still-open session before
    // reusing it, so without this a button press right after an image download
    // can hit the API host mid-teardown and fail with "connection reset" (-80).
    client.stop();

    // Swapping back to the main spotify cert
    client.setCACert(spotify_server_cert);

    // Make sure to close the file!
    f.close();

    if (gotImage)
    {
      return drawImagefromFile(ALBUM_ART);
    }
    else
    {
      return -2;
    }
  }

  int drawImagefromFile(const char *imageFileUri)
  {
    unsigned long lTime = millis();
    lTime = millis();
    jpeg.open((const char *)imageFileUri, myOpen, myClose, myRead, mySeek, JPEGDraw);
    jpeg.setPixelType(1);
    int imagePosition = screenCenterX - (imageWidth / 2);
    // decode will return 1 on sucess and 0 on a failure
    int decodeStatus = jpeg.decode(imagePosition, 0, JPEG_SCALE_HALF);
    jpeg.close();
    Serial.print("Time taken to decode and display Image (ms): ");
    Serial.println(millis() - lTime);

    return decodeStatus;
  }
};
