// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/media/android/webmediaplayer_android.h"

#include <string>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/file_path.h"
#include "base/logging.h"
#include "base/utf_string_conversions.h"
#include "media/base/android/media_player_bridge.h"
#include "net/base/mime_util.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebMediaPlayerClient.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebView.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebCookieJar.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/platform/WebRect.h"
#include "webkit/media/android/webmediaplayer_proxy_android.h"
#include "webkit/media/webmediaplayer_util.h"
#include "webkit/media/webvideoframe_impl.h"

using WebKit::WebCanvas;
using WebKit::WebMediaPlayerClient;
using WebKit::WebMediaPlayer;
using WebKit::WebRect;
using WebKit::WebSize;
using WebKit::WebTimeRanges;
using WebKit::WebURL;
using WebKit::WebVideoFrame;
using media::MediaPlayerBridge;
using media::VideoFrame;
using webkit_media::WebVideoFrameImpl;

namespace webkit_media {

// Because we create the media player lazily on android, the duration of the
// media is initially unknown to us. This makes the user unable to perform
// seek. To solve this problem, we use a temporary duration of 100 seconds when
// the duration is unknown. And we scale the seek position later when duration
// is available.
// TODO(qinmin): create a thread and use android MediaMetadataRetriever
// class to extract the duration.
static const float kTemporaryDuration = 100.0f;

bool WebMediaPlayerAndroid::incognito_mode_ = false;

WebMediaPlayerAndroid::WebMediaPlayerAndroid(
    WebMediaPlayerClient* client,
    WebKit::WebCookieJar* cookie_jar)
    : client_(client),
      buffered_(1u),
      video_frame_(new WebVideoFrameImpl(VideoFrame::CreateEmptyFrame())),
      proxy_(new WebMediaPlayerProxyAndroid(base::MessageLoopProxy::current(),
                                            AsWeakPtr())),
      prepared_(false),
      duration_(0),
      pending_seek_(0),
      seeking_(false),
      playback_completed_(false),
      buffered_bytes_(0),
      cookie_jar_(cookie_jar),
      pending_play_event_(false),
      network_state_(WebMediaPlayer::NetworkStateEmpty),
      ready_state_(WebMediaPlayer::ReadyStateHaveNothing) {
  video_frame_.reset(new WebVideoFrameImpl(VideoFrame::CreateEmptyFrame()));
}

WebMediaPlayerAndroid::~WebMediaPlayerAndroid() {
  if (media_player_.get()) {
    media_player_->Stop();
  }
}

void WebMediaPlayerAndroid::InitIncognito(bool incognito_mode) {
  incognito_mode_ = incognito_mode;
}

void WebMediaPlayerAndroid::load(const WebURL& url) {
  url_ = url;

  UpdateNetworkState(WebMediaPlayer::NetworkStateLoading);
  UpdateReadyState(WebMediaPlayer::ReadyStateHaveNothing);

  // Calling InitializeMediaPlayer() will cause android mediaplayer to start
  // buffering and decoding the data. On mobile devices, this costs a lot of
  // data usage and could even introduce performance issues. So we don't
  // initialize the player unless it is a local file. We will start loading
  // the media only when play/seek/fullsceen button is clicked.
  if (url_.SchemeIs("file")) {
    InitializeMediaPlayer();
    return;
  }

  // TODO(qinmin): we need a method to calculate the duration of the media.
  // Android does not provide any function to do that.
  // Set the initial duration value to kTemporaryDuration so that user can
  // touch the seek bar to perform seek. We will scale the seek position later
  // when we got the actual duration.
  duration_ = kTemporaryDuration;

  // Pretend everything has been loaded so that webkit can
  // still call play() and seek().
  UpdateReadyState(WebMediaPlayer::ReadyStateHaveMetadata);
  UpdateReadyState(WebMediaPlayer::ReadyStateHaveEnoughData);
}

void WebMediaPlayerAndroid::cancelLoad() {
  NOTIMPLEMENTED();
}

void WebMediaPlayerAndroid::play() {
  if (media_player_.get()) {
    if (!prepared_)
      pending_play_event_ = true;
    else
      PlayInternal();
  } else {
    pending_play_event_ = true;
    InitializeMediaPlayer();
  }
}

void WebMediaPlayerAndroid::pause() {
  if (media_player_.get()) {
    if (!prepared_)
      pending_play_event_ = false;
    else
      PauseInternal();
  } else {
    // We don't need to load media if pause() is called.
    pending_play_event_ = false;
  }
}

void WebMediaPlayerAndroid::seek(float seconds) {
  // Record the time to seek when OnMediaPrepared() is called.
  pending_seek_ = seconds;

  // Reset |playback_completed_| so that we return the correct current time.
  playback_completed_ = false;

  if (media_player_.get()) {
    if (prepared_)
      SeekInternal(seconds);
  } else {
    InitializeMediaPlayer();
  }
}

bool WebMediaPlayerAndroid::supportsFullscreen() const {
  return true;
}

bool WebMediaPlayerAndroid::supportsSave() const {
  return false;
}

void WebMediaPlayerAndroid::setEndTime(float seconds) {
  // Deprecated.
  // TODO(qinmin): Remove this from WebKit::WebMediaPlayer as it is never used.
}

void WebMediaPlayerAndroid::setRate(float rate) {
  NOTIMPLEMENTED();
}

void WebMediaPlayerAndroid::setVolume(float volume) {
  if (media_player_.get())
    media_player_->SetVolume(volume, volume);
}

void WebMediaPlayerAndroid::setVisible(bool visible) {
  // Deprecated.
  // TODO(qinmin): Remove this from WebKit::WebMediaPlayer as it is never used.
}

bool WebMediaPlayerAndroid::totalBytesKnown() {
  NOTIMPLEMENTED();
  return false;
}

bool WebMediaPlayerAndroid::hasVideo() const {
  // TODO(qinmin): need a better method to determine whether the current media
  // content contains video. Android does not provide any function to do
  // this.
  // We don't know whether the current media content has video unless
  // the player is prepared. If the player is not prepared, we fall back
  // to the mime-type. There may be no mime-type on a redirect URL.
  // In that case, we conservatively assume it contains video so that
  // enterfullscreen call will not fail.
  if (!prepared_) {
    if (!url_.has_path())
      return false;
    std::string mime;
    if(!net::GetMimeTypeFromFile(FilePath(url_.path()), &mime))
      return true;
    return mime.find("audio/") == std::string::npos;
  }

  return !natural_size_.isEmpty();
}

bool WebMediaPlayerAndroid::hasAudio() const {
  // TODO(hclam): Query status of audio and return the actual value.
  return true;
}

bool WebMediaPlayerAndroid::paused() const {
  if (!prepared_)
    return !pending_play_event_;
  return !media_player_->IsPlaying();
}

bool WebMediaPlayerAndroid::seeking() const {
  return seeking_;
}

float WebMediaPlayerAndroid::duration() const {
  return duration_;
}

float WebMediaPlayerAndroid::currentTime() const {
  // If the player is pending for a seek, return the seek time.
  if (!prepared_ || seeking())
    return pending_seek_;

  // When playback is about to finish, android media player often stops
  // at a time which is smaller than the duration. This makes webkit never
  // know that the playback has finished. To solve this, we set the
  // current time to media duration when OnPlaybackComplete() get called.
  // And return the greater of the two values so that the current
  // time is most updated.
  if (playback_completed_)
    return duration();
  return static_cast<float>(media_player_->GetCurrentTime().InSecondsF());
}

int WebMediaPlayerAndroid::dataRate() const {
  // Deprecated.
  // TODO(qinmin): Remove this from WebKit::WebMediaPlayer as it is never used.
  return 0;
}

WebSize WebMediaPlayerAndroid::naturalSize() const {
  return natural_size_;
}

WebMediaPlayer::NetworkState WebMediaPlayerAndroid::networkState() const {
  return network_state_;
}

WebMediaPlayer::ReadyState WebMediaPlayerAndroid::readyState() const {
  return ready_state_;
}

const WebTimeRanges& WebMediaPlayerAndroid::buffered() {
  return buffered_;
}

float WebMediaPlayerAndroid::maxTimeSeekable() const {
  // TODO(hclam): If this stream is not seekable this should return 0.
  return duration();
}

unsigned long long WebMediaPlayerAndroid::bytesLoaded() const {
  return buffered_bytes_;
}

unsigned long long WebMediaPlayerAndroid::totalBytes() const {
  // Deprecated.
  // TODO(qinmin): Remove this from WebKit::WebMediaPlayer as it is never used.
  return 0;
}

void WebMediaPlayerAndroid::setSize(const WebSize& size) {
  texture_size_ = size;
}

void WebMediaPlayerAndroid::paint(WebKit::WebCanvas* canvas,
                                  const WebKit::WebRect& rect,
                                  uint8_t alpha) {
  NOTIMPLEMENTED();
}

bool WebMediaPlayerAndroid::hasSingleSecurityOrigin() const {
  return false;
}

WebMediaPlayer::MovieLoadType
    WebMediaPlayerAndroid::movieLoadType() const {
  // Deprecated.
  // TODO(qinmin): Remove this from WebKit::WebMediaPlayer as it is never used.
  return WebMediaPlayer::MovieLoadTypeUnknown;
}

float WebMediaPlayerAndroid::mediaTimeForTimeValue(float timeValue) const {
  return ConvertSecondsToTimestamp(timeValue).InSecondsF();
}

unsigned WebMediaPlayerAndroid::decodedFrameCount() const {
  NOTIMPLEMENTED();
  return 0;
}

unsigned WebMediaPlayerAndroid::droppedFrameCount() const {
  NOTIMPLEMENTED();
  return 0;
}

unsigned WebMediaPlayerAndroid::audioDecodedByteCount() const {
  NOTIMPLEMENTED();
  return 0;
}

unsigned WebMediaPlayerAndroid::videoDecodedByteCount() const {
  NOTIMPLEMENTED();
  return 0;
}

void WebMediaPlayerAndroid::OnMediaPrepared() {
  if (!media_player_.get())
    return;

  prepared_ = true;

  // Update the media duration first so that webkit will get the correct
  // duration when UpdateReadyState is called.
  float dur = duration_;
  duration_ = media_player_->GetDuration().InSecondsF();

  if (url_.SchemeIs("file"))
    UpdateNetworkState(WebMediaPlayer::NetworkStateLoaded);

  if (ready_state_ != WebMediaPlayer::ReadyStateHaveEnoughData) {
    UpdateReadyState(WebMediaPlayer::ReadyStateHaveMetadata);
    UpdateReadyState(WebMediaPlayer::ReadyStateHaveEnoughData);
  } else {
    // If the status is already set to ReadyStateHaveEnoughData, set it again
    // to make sure that Videolayerchromium will get created.
    UpdateReadyState(WebMediaPlayer::ReadyStateHaveEnoughData);
  }

  if (!url_.SchemeIs("file")) {
    // In we have skipped loading, the duration was preset to
    // kTemporaryDuration. We have to update webkit about the new duration.
    if (duration_ != dur) {
      // Scale the |pending_seek_| according to the new duration.
      pending_seek_ = pending_seek_ * duration_ / kTemporaryDuration;
      client_->durationChanged();
    }
  }

  // If media player was recovered from a saved state, consume all the pending
  // events.
  seek(pending_seek_);

  if (pending_play_event_)
    PlayInternal();

  pending_play_event_ = false;
}

void WebMediaPlayerAndroid::OnPlaybackComplete() {
  // Set the current time equal to duration to let webkit know that play back
  // is completed.
  playback_completed_ = true;
  client_->timeChanged();
}

void WebMediaPlayerAndroid::OnBufferingUpdate(int percentage) {
  buffered_[0].end = duration() * percentage / 100;
  // Implement a trick here to fake progress event, as WebKit checks
  // consecutive bytesLoaded() to see if any progress made.
  // See HTMLMediaElement::progressEventTimerFired.
  // TODO(qinmin): need a method to calculate the buffered bytes.
  buffered_bytes_++;
}

void WebMediaPlayerAndroid::OnSeekComplete() {
  seeking_ = false;

  UpdateReadyState(WebMediaPlayer::ReadyStateHaveEnoughData);

  client_->timeChanged();
}

void WebMediaPlayerAndroid::OnMediaError(int error_type) {
  switch (error_type) {
    case MediaPlayerBridge::MEDIA_ERROR_UNKNOWN:
      // When playing an bogus URL or bad file we fire a MEDIA_ERROR_UNKNOWN.
      // As WebKit uses FormatError to indicate an error for bogus URL or bad
      // file we default a MEDIA_ERROR_UNKNOWN to NetworkStateFormatError.
      UpdateNetworkState(WebMediaPlayer::NetworkStateFormatError);
      break;
    case MediaPlayerBridge::MEDIA_ERROR_SERVER_DIED:
      // TODO(zhenghao): Media server died. In this case, the application must
      // release the MediaPlayer object and instantiate a new one.
      UpdateNetworkState(WebMediaPlayer::NetworkStateDecodeError);
      break;
    case MediaPlayerBridge::MEDIA_ERROR_NOT_VALID_FOR_PROGRESSIVE_PLAYBACK:
      UpdateNetworkState(WebMediaPlayer::NetworkStateFormatError);
      break;
    case MediaPlayerBridge::MEDIA_ERROR_INVALID_CODE:
      break;
  }
  client_->repaint();
}

void WebMediaPlayerAndroid::OnMediaInfo(int info_type) {
  NOTIMPLEMENTED();
}

void WebMediaPlayerAndroid::OnVideoSizeChanged(int width, int height) {
  natural_size_.width = width;
  natural_size_.height = height;
}

void WebMediaPlayerAndroid::UpdateNetworkState(
    WebMediaPlayer::NetworkState state) {
  network_state_ = state;
  client_->networkStateChanged();
}

void WebMediaPlayerAndroid::UpdateReadyState(
    WebMediaPlayer::ReadyState state) {
  ready_state_ = state;
  client_->readyStateChanged();
}

void WebMediaPlayerAndroid::SetVideoSurface(jobject j_surface) {
  if (media_player_.get())
    media_player_->SetVideoSurface(j_surface);
}

void WebMediaPlayerAndroid::InitializeMediaPlayer() {
  CHECK(!media_player_.get());
  prepared_ = false;
  media_player_.reset(new MediaPlayerBridge());
  media_player_->SetStayAwakeWhilePlaying();

  std::string cookies;
  if (cookie_jar_ != NULL) {
    WebURL url(url_);
    cookies = UTF16ToUTF8(cookie_jar_->cookies(url, url));
  }
  media_player_->SetDataSource(url_.spec(), cookies, incognito_mode_);

  media_player_->Prepare(
      base::Bind(&WebMediaPlayerProxyAndroid::MediaInfoCallback, proxy_),
      base::Bind(&WebMediaPlayerProxyAndroid::MediaErrorCallback, proxy_),
      base::Bind(&WebMediaPlayerProxyAndroid::VideoSizeChangedCallback, proxy_),
      base::Bind(&WebMediaPlayerProxyAndroid::BufferingUpdateCallback, proxy_),
      base::Bind(&WebMediaPlayerProxyAndroid::MediaPreparedCallback, proxy_));
}

void WebMediaPlayerAndroid::PlayInternal() {
  CHECK(prepared_);

  if (paused())
    media_player_->Start(base::Bind(
        &WebMediaPlayerProxyAndroid::PlaybackCompleteCallback, proxy_));
}

void WebMediaPlayerAndroid::PauseInternal() {
  CHECK(prepared_);
  media_player_->Pause();
}

void WebMediaPlayerAndroid::SeekInternal(float seconds) {
  CHECK(prepared_);
  seeking_ = true;
  media_player_->SeekTo(ConvertSecondsToTimestamp(seconds), base::Bind(
      &WebMediaPlayerProxyAndroid::SeekCompleteCallback, proxy_));
}

WebVideoFrame* WebMediaPlayerAndroid::getCurrentFrame() {
  return video_frame_.get();
}

void WebMediaPlayerAndroid::putCurrentFrame(
    WebVideoFrame* web_video_frame) {
}

}  // namespace webkit_media
