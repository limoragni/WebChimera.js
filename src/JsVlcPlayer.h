#pragma once

#include <memory>
#include <deque>
#include <set>

#include <v8.h>
#include <node.h>
#include <node_object_wrap.h>
#include <uv.h>

#include <libvlc_wrapper/vlc_player.h>
#include <libvlc_wrapper/vlc_vmem.h>

#include "VlcVideoOutput.h"

class JsVlcInput;
class JsVlcAudio;
class JsVlcVideo;
class JsVlcSubtitles;
class JsVlcPlaylist;

class JsVlcPlayer :
    public node::ObjectWrap,
    private VlcVideoOutput,
    private vlc::media_player_events_callback
{
    enum Callbacks_e {
        CB_FrameSetup = 0,
        CB_FrameReady,
        CB_FrameCleanup,

        CB_MediaPlayerMediaChanged,
        CB_MediaPlayerNothingSpecial,
        CB_MediaPlayerOpening,
        CB_MediaPlayerBuffering,
        CB_MediaPlayerPlaying,
        CB_MediaPlayerPaused,
        CB_MediaPlayerStopped,
        CB_MediaPlayerForward,
        CB_MediaPlayerBackward,
        CB_MediaPlayerBeginReached,
        CB_MediaPlayerEndReached,
        CB_MediaPlayerEncounteredError,

        CB_MediaPlayerTimeChanged,
        CB_MediaPlayerPositionChanged,
        CB_MediaPlayerSeekableChanged,
        CB_MediaPlayerPausableChanged,
        CB_MediaPlayerLengthChanged,

        CB_LogMessage,

        CB_Max,
    };

    static const char* callbackNames[CB_Max];

public:
    static void initJsApi( const v8::Handle<v8::Object>& exports );

    static void jsLoad( const v8::FunctionCallbackInfo<v8::Value>& args );

    static void getJsCallback( v8::Local<v8::String> property,
                               const v8::PropertyCallbackInfo<v8::Value>& info,
                               Callbacks_e callback );
    static void setJsCallback( v8::Local<v8::String> property,
                               v8::Local<v8::Value> value,
                               const v8::PropertyCallbackInfo<void>& info,
                               Callbacks_e callback );

    bool playing();
    bool playingReverse();
    double length();
    double fps();
    double frames();
    unsigned state();

    v8::Local<v8::Value> getVideoFrame();
    v8::Local<v8::Object> getEventEmitter();

    unsigned pixelFormat();
    void setPixelFormat( unsigned );

    double position();
    void setPosition( double );

    double time();
    void setTime( double );

    double frame();
    void setFrame( double );

    void previousFrame();
    void nextFrame();

    unsigned volume();
    void setVolume( unsigned );

    bool muted();
    void setMuted( bool );

    void load( const std::string& mrl, bool startPlaying, bool startPlayingReverse, unsigned atTime, double withFps );
    void play();
    void playReverse();
    void pause();
    void togglePause();
    void stop();
    void toggleMute();

    v8::Local<v8::Object> input();
    v8::Local<v8::Object> audio();
    v8::Local<v8::Object> video();
    v8::Local<v8::Object> subtitles();
    v8::Local<v8::Object> playlist();

    void setInput( JsVlcInput& input );
    void setAudio( JsVlcAudio& audio );
    void setVideo( JsVlcVideo& video );
    void setSubtitles( JsVlcSubtitles& subtitles );
    void setPlaylist( JsVlcPlaylist& playlist );

    vlc::player& player()
        { return _player; }

    void close();

private:
    static void jsCreate( const v8::FunctionCallbackInfo<v8::Value>& args );
    JsVlcPlayer( v8::Local<v8::Object>& thisObject, const v8::Local<v8::Array>& vlcOpts );
    ~JsVlcPlayer();

    struct AsyncData;
    struct CallbackData;
    struct LibvlcEvent;
    struct LibvlcLogEvent;

    static void closeAll();
    void initLibvlc( const v8::Local<v8::Array>& vlcOpts );

    void handleAsync();

    //could come from worker thread
    void media_player_event( const libvlc_event_t* );

    static void log_event_wrapper( void *, int, const libvlc_log_t *, const char *, va_list );
    void log_event( int, const libvlc_log_t *, const char *, va_list );

    void handleLibvlcEvent( const libvlc_event_t& );

    void currentItemEndReached();

    void callCallback( Callbacks_e callback,
                       std::initializer_list<v8::Local<v8::Value> > list = std::initializer_list<v8::Local<v8::Value> >() );

    void doCallCallback();

    void updateCurrentTime();
    void setCurrentTime( libvlc_time_t time );

    double rateReverse();
    void setRateReverse( double rateReverse );

    double decimalFrame();

protected:
    void* onFrameSetup( const RV32VideoFrame& ) override;
    void* onFrameSetup( const I420VideoFrame& ) override;
    void onFrameReady() override;
    void onFrameCleanup() override;

private:
    enum class ELoadVideoState
    {
        UNLOADED,
        LOADED,
        GETTING
    };

    static v8::Persistent<v8::Function> _jsConstructor;
    static std::set<JsVlcPlayer*> _instances;

    // Sanity checks are used because LibVLC sometimes sends a previous frame, not the right one that we want.
    static const unsigned MaxSanityChecks = 5;
    static const libvlc_time_t InvalidTime = ~0;

    libvlc_instance_t* _libvlc;
    vlc::player _player;

    uv_async_t _async;
    std::mutex _asyncDataGuard;
    std::deque<std::unique_ptr<AsyncData> > _asyncData;

    v8::UniquePersistent<v8::Value> _jsFrameBuffer;

    v8::UniquePersistent<v8::Function> _jsCallbacks[CB_Max];
    v8::UniquePersistent<v8::Object> _jsEventEmitter;

    v8::UniquePersistent<v8::Object> _jsInput;
    v8::UniquePersistent<v8::Object> _jsAudio;
    v8::UniquePersistent<v8::Object> _jsVideo;
    v8::UniquePersistent<v8::Object> _jsSubtitles;
    v8::UniquePersistent<v8::Object> _jsPlaylist;

    JsVlcInput* _cppInput;
    JsVlcAudio* _cppAudio;
    JsVlcVideo* _cppVideo;
    JsVlcSubtitles* _cppSubtitles;
    JsVlcPlaylist* _cppPlaylist;

    uv_timer_t _errorTimer;

    bool _startPlaying;
    bool _startPlayingReverse;
    bool _isPlaying;
    bool _reversePlayback;

    // Accumulated loading time used to apply it when first frame is ready and video is playing.
    libvlc_time_t _loadingTime;
    // Estimated playback time (used to get the frame number) to be as much frame-accurate as possible.
    libvlc_time_t _currentTime;
    // Flag used to seek a frame accurately when video is not playing, applying some sanity checks.
    bool _performSeek;
    // In frame-ready callback we avoid sending frames when video is paused. However, to be more
    // frame-accurate, we allow the callback to send the received frame n-times, since it could be different,
    // just to make sure the latest one sent is the proper one.
    unsigned _seekedFrameLoadedSanityChecks;

    // VLC playback time has a low refresh rate, for that reason we estimate the time by ourselves.
    // This value is used to compare the previous VLC time, and start estimating when it's updated.
    libvlc_time_t _lastPlaybackTimeFrameReady;
    // Timestamp used to know how much time has passed from the previous update. That difference is
    // added to the estimated current time taking into account the playback rate.
    libvlc_time_t _lastGlobalTimeFrameReady;

    ELoadVideoState _loadVideoState;
    float _bufferingValue;

    // Perform conversions from time to frame using this FPS value. Useful when we don't want to use the
    // internal FPS value, average frame rate, and we prefer using another one, e.g. raw frame rate.
    float _withFps;
};
