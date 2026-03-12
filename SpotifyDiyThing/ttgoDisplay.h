// TTGO T-Display (135x240, ST7789V) — 240x135 in landscape
// Buttons: GPIO 0 (prev), GPIO 35 (next)

#include "spotifyDisplay.h"

#include <TFT_eSPI.h>
#include <Button2.h>
#include <JPEGDEC.h>

TFT_eSPI _lcd = TFT_eSPI();
JPEGDEC jpeg;

const char *ALBUM_ART = "/album.jpg";

int JPEGDraw(JPEGDRAW *pDraw)
{
  if (pDraw->y >= _lcd.height())
    return 0;
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

// ---- Button pins ----
#define TTGO_BTN_PREV 0   // top button  → previous track
#define TTGO_BTN_NEXT 35  // bottom button → next track

Button2 btnPrev(TTGO_BTN_PREV);
Button2 btnNext(TTGO_BTN_NEXT);

static bool s_prevTriggered = false;
static bool s_nextTriggered = false;

// ---- Layout constants (240 x 135 landscape) ----
#define IMG_SIZE     64   // Spotify small image (64x64), no scaling needed
#define IMG_X        0
#define TEXT_X       (IMG_SIZE + 10)
#define PROG_BAR_Y   (135 - 14)
#define PROG_BAR_H   10
#define PROG_BAR_MARGIN 6   // left/right margin so bar isn't cropped at edge

class TTGODisplay : public SpotifyDisplay
{
public:
  void displaySetup(SpotifyArduino *spotifyObj)
  {
    spotify_display = spotifyObj;

    setWidth(240);
    setHeight(135);
    setImageWidth(IMG_SIZE);
    setImageHeight(IMG_SIZE);

    _lcd.init();
    _lcd.setRotation(1);
    _lcd.fillScreen(TFT_BLACK);

    // setPressedHandler fires on press-down, not after release — important
    // because updateCurrentlyPlaying() blocks for several seconds and
    // setClickHandler would miss a press made during that window.
    btnPrev.setPressedHandler([](Button2 &) { s_prevTriggered = true; });
    btnNext.setPressedHandler([](Button2 &) { s_nextTriggered = true; });

  }

  void showDefaultScreen()
  {
    _lcd.fillScreen(TFT_BLACK);
    _lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    _lcd.drawCentreString("IDLE.", screenCenterX, 55, 2);
  }

  void displayTrackProgress(long progress, long duration)
  {
    if (duration == 0) return;
    int barTotalW = screenWidth - (2 * PROG_BAR_MARGIN);
    int innerW = barTotalW - 2;  // inside 1px border each side
    int barW = map(progress, 0, duration, 0, innerW);
    _lcd.drawRect(PROG_BAR_MARGIN, PROG_BAR_Y, barTotalW, PROG_BAR_H, TFT_WHITE);
    _lcd.fillRect(PROG_BAR_MARGIN + 1, PROG_BAR_Y + 1, barW, PROG_BAR_H - 2, TFT_WHITE);
    _lcd.fillRect(PROG_BAR_MARGIN + 1 + barW, PROG_BAR_Y + 1, innerW - barW, PROG_BAR_H - 2, TFT_BLACK);
  }

  void printCurrentlyPlayingToScreen(CurrentlyPlaying currentlyPlaying)
  {
    int textW = screenWidth - TEXT_X;
    _lcd.fillRect(TEXT_X, 0, textW, PROG_BAR_Y, TFT_BLACK);
    _lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    _lcd.drawString(currentlyPlaying.trackName, TEXT_X, 5, 2);
    _lcd.drawString(currentlyPlaying.artists[0].artistName, TEXT_X, 27, 2);
    _lcd.drawString(currentlyPlaying.albumName, TEXT_X, 49, 2);
  }

  void checkForInput()
  {
    btnPrev.loop();
    btnNext.loop();

    if (s_prevTriggered)
    {
      s_prevTriggered = false;
      flashButtonFeedback(true);
      Serial.println("BTN: previous track");
      int result = spotify_display->previousTrack();
      Serial.print("previousTrack HTTP: ");
      Serial.println(result);
      requestDueTime = 0;
    }
    if (s_nextTriggered)
    {
      s_nextTriggered = false;
      flashButtonFeedback(false);
      Serial.println("BTN: next track");
      int result = spotify_display->nextTrack();
      Serial.print("nextTrack HTTP: ");
      Serial.println(result);
      requestDueTime = 0;
    }
  }

  void clearImage()
  {
    _lcd.fillRect(IMG_X, 0, IMG_SIZE + 5, PROG_BAR_Y, TFT_BLACK);
  }

  boolean processImageInfo(CurrentlyPlaying currentlyPlaying)
  {
    // Use the smallest image (64x64) — fits neatly on the 135px-tall screen
    SpotifyImage smallImage = currentlyPlaying.albumImages[currentlyPlaying.numImages - 1];
    if (!albumDisplayed || !isSameAlbum(smallImage.url))
    {
      albumDisplayed = false;
      setImageHeight(smallImage.height);
      setImageWidth(smallImage.width);
      setAlbumArtUrl(smallImage.url);
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

  // NFC is not wired on TTGO — stubs satisfy the interface
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
  // Briefly highlights the left or right edge so the user knows the press registered
  void flashButtonFeedback(bool isPrev)
  {
    int x = isPrev ? 0 : (screenWidth - 4);
    _lcd.fillRect(x, 0, 4, screenHeight, TFT_GREEN);
    delay(120);
    _lcd.fillRect(x, 0, 4, screenHeight, TFT_BLACK);
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
    unsigned long lTime = millis();
    jpeg.open(imageFileUri, myOpen, myClose, myRead, mySeek, JPEGDraw);
    jpeg.setPixelType(1);
    int imageY = (screenHeight - imageHeight) / 2;
    int decodeStatus = jpeg.decode(IMG_X, imageY, 0); // no scaling — small image is already 64x64
    jpeg.close();
    Serial.print("Image decode (ms): ");
    Serial.println(millis() - lTime);
    return decodeStatus;
  }
};
