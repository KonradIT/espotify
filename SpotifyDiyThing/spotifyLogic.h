SpotifyDisplay *sp_Display;

SpotifyArduino spotify(client, NULL, NULL);

bool albumArtChanged = false;

long songStartMillis;
long songDuration;

char lastTrackUri[200];
char lastTrackContextUri[200];

// You might want to make this much smaller, so it will update responsively

unsigned long delayBetweenRequests = 5000; // Time between requests (5 seconds)
unsigned long requestDueTime;              // time when request due

unsigned long delayBetweenProgressUpdates = 500; // Time between requests (0.5 seconds)
unsigned long progressDueTime;                   // time when request due

bool spotifyIsPlaying = false;  // last known play state (for play/pause toggle from T-Embed button etc.)

static CurrentlyPlaying s_lastCurrentlyPlaying;
static char s_lastTrackName[200];
static char s_lastArtistName[150];
static char s_lastAlbumName[150];

static void copyCurrentlyPlayingForRedraw(CurrentlyPlaying &dest, const CurrentlyPlaying &src)
{
  dest = src;
  if (src.trackName)
  {
    strncpy(s_lastTrackName, src.trackName, sizeof(s_lastTrackName) - 1);
    s_lastTrackName[sizeof(s_lastTrackName) - 1] = '\0';
    dest.trackName = s_lastTrackName;
  }
  if (src.albumName)
  {
    strncpy(s_lastAlbumName, src.albumName, sizeof(s_lastAlbumName) - 1);
    s_lastAlbumName[sizeof(s_lastAlbumName) - 1] = '\0';
    dest.albumName = s_lastAlbumName;
  }
  if (src.numArtists > 0 && src.artists[0].artistName)
  {
    strncpy(s_lastArtistName, src.artists[0].artistName, sizeof(s_lastArtistName) - 1);
    s_lastArtistName[sizeof(s_lastArtistName) - 1] = '\0';
    dest.artists[0].artistName = s_lastArtistName;
  }
}

void spotifySetup(SpotifyDisplay *theDisplay, const char *clientId, const char *clientSecret)
{
  sp_Display = theDisplay;
  client.setCACert(spotify_server_cert);
  spotify.lateInit(clientId, clientSecret);

  lastTrackUri[0] = '\0';
  lastTrackContextUri[0] = '\0';
}

bool isSameTrack(const char *trackUri)
{

  return strcmp(lastTrackUri, trackUri) == 0;
}

void setTrackUri(const char *trackUri)
{
  strcpy(lastTrackUri, trackUri);
}

void setTrackContextUri(const char *trackContext)
{
  if (trackContext == NULL)
  {
    lastTrackContextUri[0] = '\0';
  }
  else
  {
    strcpy(lastTrackContextUri, trackContext);
  }
}

void spotifyRefreshToken(const char *refreshToken)
{
  spotify.setRefreshToken(refreshToken);

  // If you want to enable some extra debugging
  // uncomment the "#define SPOTIFY_DEBUG" in SpotifyArduino.h

  Serial.println("Refreshing Access Tokens");
  if (!spotify.refreshAccessToken())
  {
    Serial.println("Failed to get access tokens");
  }
}

void handleCurrentlyPlaying(CurrentlyPlaying currentlyPlaying)
{
  if (currentlyPlaying.trackUri != NULL)
  {
    copyCurrentlyPlayingForRedraw(s_lastCurrentlyPlaying, currentlyPlaying);
    if (!isSameTrack(currentlyPlaying.trackUri))
    {
      setTrackUri(currentlyPlaying.trackUri);
      setTrackContextUri(currentlyPlaying.contextUri);

      // We have a new Song, need to update the text
      sp_Display->printCurrentlyPlayingToScreen(currentlyPlaying);
    }

    albumArtChanged = sp_Display->processImageInfo(currentlyPlaying);

    sp_Display->displayTrackProgress(currentlyPlaying.progressMs, currentlyPlaying.durationMs);

    spotifyIsPlaying = currentlyPlaying.isPlaying;
    if (currentlyPlaying.isPlaying)
    {
      // If we know at what millis the song started at, we can make a good guess
      // at updating the progress bar more often than checking the API
      songStartMillis = millis() - currentlyPlaying.progressMs;
      songDuration = currentlyPlaying.durationMs;
    }
    else
    {
      // Song doesn't seem to be playing, do not update the progress
      songStartMillis = 0;
    }
  }
}

void updateProgressBar()
{
  if (songStartMillis != 0 && millis() > progressDueTime)
  {
    long songProgress = millis() - songStartMillis;
    if (songProgress > songDuration)
    {
      songProgress = songDuration;
    }
    sp_Display->displayTrackProgress(songProgress, songDuration);
    progressDueTime = millis() + delayBetweenProgressUpdates;
  }
}

void updateCurrentlyPlaying(boolean forceUpdate)
{
  if (forceUpdate || millis() > requestDueTime)
  {
    if (forceUpdate)
    {
      Serial.println("forcing an update");
    }
    // Serial.print("Free Heap: ");
    // Serial.println(ESP.getFreeHeap());

    Serial.println("getting currently playing song:");
    // Check if music is playing currently on the account.
    int status = spotify.getCurrentlyPlaying(handleCurrentlyPlaying, SPOTIFY_MARKET);
    if (status == 200)
    {
      Serial.println("Successfully got currently playing");
      if (albumArtChanged || forceUpdate)
      {
        sp_Display->clearImage();
        int displayImageResult = sp_Display->displayImage();

        if (displayImageResult)
        {
          albumArtChanged = false;
          // Redraw text on top of new accent background (drawImageFromFile fills the text area)
          if (s_lastCurrentlyPlaying.trackUri != NULL)
            sp_Display->printCurrentlyPlayingToScreen(s_lastCurrentlyPlaying);
        }
        else
        {
          Serial.print("failed to display image: ");
          Serial.println(displayImageResult);
        }
      }
    }
    else if (status == 204)
    {
      songStartMillis = 0;
      Serial.println("Doesn't seem to be anything playing");
    }
    else
    {
      Serial.print("Error: ");
      Serial.println(status);
    }

    requestDueTime = millis() + delayBetweenRequests;
  }
}
