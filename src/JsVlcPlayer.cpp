#include "JsVlcPlayer.h"

#include <chrono>
#include <cmath>
#include <string.h>
#include <thread>

#include "NodeTools.h"
#include "JsVlcInput.h"
#include "JsVlcAudio.h"
#include "JsVlcVideo.h"
#include "JsVlcSubtitles.h"
#include "JsVlcPlaylist.h"

#if V8_MAJOR_VERSION > 4 || \
    ( V8_MAJOR_VERSION == 4 && V8_MINOR_VERSION > 4 ) || \
    ( V8_MAJOR_VERSION == 4 && V8_MINOR_VERSION == 4 && V8_BUILD_NUMBER >= 26 )

#define USE_ARRAY_BUFFER 1

#endif

#undef min
#undef max

const char* JsVlcPlayer::callbackNames[] =
{
    "FrameSetup",
    "FrameReady",
    "FrameCleanup",

    "MediaChanged",
    "NothingSpecial",
    "Opening",
    "Buffering",
    "Playing",
    "Paused",
    "Stopped",
    "Forward",
    "Backward",
    "BeginReached",
    "EndReached",
    "EncounteredError",

    "TimeChanged",
    "PositionChanged",
    "SeekableChanged",
    "PausableChanged",
    "LengthChanged",

    "LogMessage"
};

v8::Persistent<v8::Function> JsVlcPlayer::_jsConstructor;
std::set<JsVlcPlayer*> JsVlcPlayer::_instances;

///////////////////////////////////////////////////////////////////////////////
struct JsVlcPlayer::AsyncData
{
    virtual void process( JsVlcPlayer* ) = 0;
};

///////////////////////////////////////////////////////////////////////////////
struct JsVlcPlayer::CallbackData : public JsVlcPlayer::AsyncData
{
    CallbackData( JsVlcPlayer::Callbacks_e callback ) :
        callback( callback ) {}

    void process( JsVlcPlayer* );

    const JsVlcPlayer::Callbacks_e callback;
};

void JsVlcPlayer::CallbackData::process( JsVlcPlayer* jsPlayer )
{
    jsPlayer->callCallback( callback );
}

///////////////////////////////////////////////////////////////////////////////
struct JsVlcPlayer::LibvlcEvent : public JsVlcPlayer::AsyncData
{
    LibvlcEvent( const libvlc_event_t& libvlcEvent ) :
        libvlcEvent( libvlcEvent ) {}

    void process( JsVlcPlayer* );

    const libvlc_event_t libvlcEvent;
};

void JsVlcPlayer::LibvlcEvent::process( JsVlcPlayer* jsPlayer )
{
    jsPlayer->handleLibvlcEvent( libvlcEvent );
}

///////////////////////////////////////////////////////////////////////////////
struct JsVlcPlayer::LibvlcLogEvent : public JsVlcPlayer::AsyncData
{
    LibvlcLogEvent( int level, const std::string& message, const std::string& format ) :
        level( level ), message( message ), format( format ) {}

    void process( JsVlcPlayer* );

    const int level;
    std::string message;
    std::string format;
};

void JsVlcPlayer::LibvlcLogEvent::process( JsVlcPlayer* jsPlayer )
{
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    v8::HandleScope scope( isolate );

    v8::Local<v8::Integer> jsLevel = v8::Integer::New( isolate, level );
    v8::Local<v8::String> jsMessage = v8::String::NewFromUtf8( isolate, message.c_str(), v8::String::kNormalString );
    v8::Local<v8::String> jsFormat = v8::String::NewFromUtf8( isolate, format.c_str(), v8::String::kNormalString );

    jsPlayer->callCallback( CB_LogMessage, { jsLevel, jsMessage, jsFormat } );
}

///////////////////////////////////////////////////////////////////////////////
#define SET_CALLBACK_PROPERTY( objTemplate, name, callback )                                                       \
    objTemplate->SetAccessor( String::NewFromUtf8( Isolate::GetCurrent(), name, v8::String::kInternalizedString ), \
        [] ( v8::Local<v8::String> property,                                                                       \
             const v8::PropertyCallbackInfo<v8::Value>& info )                                                     \
        {                                                                                                          \
            JsVlcPlayer::getJsCallback( property, info, callback );                                                \
        },                                                                                                         \
        [] ( v8::Local<v8::String> property,                                                                       \
             v8::Local<v8::Value> value,                                                                           \
             const v8::PropertyCallbackInfo<void>& info )                                                          \
        {                                                                                                          \
            JsVlcPlayer::setJsCallback( property, value, info, callback );                                         \
        } )

void JsVlcPlayer::initJsApi( const v8::Handle<v8::Object>& exports )
{
    node::AtExit( [] ( void* ) { JsVlcPlayer::closeAll(); } );

    JsVlcInput::initJsApi();
    JsVlcAudio::initJsApi();
    JsVlcVideo::initJsApi();
    JsVlcSubtitles::initJsApi();
    JsVlcPlaylist::initJsApi();

    using namespace v8;

    Isolate* isolate = Isolate::GetCurrent();
    HandleScope scope( isolate );

    Local<FunctionTemplate> constructorTemplate = FunctionTemplate::New( isolate, jsCreate );
    constructorTemplate->SetClassName( String::NewFromUtf8( isolate, "VlcPlayer", v8::String::kInternalizedString ) );

    Local<ObjectTemplate> protoTemplate = constructorTemplate->PrototypeTemplate();
    Local<ObjectTemplate> instanceTemplate = constructorTemplate->InstanceTemplate();
    instanceTemplate->SetInternalFieldCount( 1 );

    protoTemplate->Set( String::NewFromUtf8( isolate, "RV32", v8::String::kInternalizedString ),
                        Integer::New( isolate, static_cast<int>( PixelFormat::RV32 ) ),
                        static_cast<v8::PropertyAttribute>( ReadOnly | DontDelete ) );
    protoTemplate->Set( String::NewFromUtf8( isolate, "I420", v8::String::kInternalizedString ),
                        Integer::New( isolate, static_cast<int>( PixelFormat::I420 ) ),
                        static_cast<v8::PropertyAttribute>( ReadOnly | DontDelete ) );

    protoTemplate->Set( String::NewFromUtf8( isolate, "NothingSpecial", v8::String::kInternalizedString ),
                        Integer::New( isolate, libvlc_NothingSpecial ),
                        static_cast<v8::PropertyAttribute>( ReadOnly | DontDelete ) );
    protoTemplate->Set( String::NewFromUtf8( isolate, "Opening", v8::String::kInternalizedString ),
                        Integer::New( isolate, libvlc_Opening ),
                        static_cast<v8::PropertyAttribute>( ReadOnly | DontDelete ) );
    protoTemplate->Set( String::NewFromUtf8( isolate, "Buffering", v8::String::kInternalizedString ),
                        Integer::New( isolate, libvlc_Buffering ),
                        static_cast<v8::PropertyAttribute>( ReadOnly | DontDelete ) );
    protoTemplate->Set( String::NewFromUtf8( isolate, "Playing", v8::String::kInternalizedString ),
                        Integer::New( isolate, libvlc_Playing ),
                        static_cast<v8::PropertyAttribute>( ReadOnly | DontDelete ) );
    protoTemplate->Set( String::NewFromUtf8( isolate, "Paused", v8::String::kInternalizedString ),
                        Integer::New( isolate, libvlc_Paused ),
                        static_cast<v8::PropertyAttribute>( ReadOnly | DontDelete ) );
    protoTemplate->Set( String::NewFromUtf8( isolate, "Stopped", v8::String::kInternalizedString ),
                        Integer::New( isolate, libvlc_Stopped ),
                        static_cast<v8::PropertyAttribute>( ReadOnly | DontDelete ) );
    protoTemplate->Set( String::NewFromUtf8( isolate, "Ended", v8::String::kInternalizedString ),
                        Integer::New( isolate, libvlc_Ended ),
                        static_cast<v8::PropertyAttribute>( ReadOnly | DontDelete ) );
    protoTemplate->Set( String::NewFromUtf8( isolate, "Error", v8::String::kInternalizedString ),
                        Integer::New( isolate, libvlc_Error ),
                        static_cast<v8::PropertyAttribute>( ReadOnly | DontDelete ) );

    Local<String> vlcVersion = String::NewFromUtf8( isolate, libvlc_get_version(), v8::String::kNormalString );
    Local<String> vlcChangeset = String::NewFromUtf8( isolate, libvlc_get_changeset(), v8::String::kNormalString );

    protoTemplate->Set( String::NewFromUtf8( isolate, "vlcVersion", v8::String::kInternalizedString ),
                        vlcVersion,
                        static_cast<v8::PropertyAttribute>( ReadOnly | DontDelete ) );
    protoTemplate->Set( String::NewFromUtf8( isolate, "vlcChangeset", v8::String::kInternalizedString ),
                        vlcChangeset,
                        static_cast<v8::PropertyAttribute>( ReadOnly | DontDelete ) );

    SET_CALLBACK_PROPERTY( instanceTemplate, "onFrameSetup", CB_FrameSetup );
    SET_CALLBACK_PROPERTY( instanceTemplate, "onFrameReady", CB_FrameReady );
    SET_CALLBACK_PROPERTY( instanceTemplate, "onFrameCleanup", CB_FrameCleanup );

    SET_CALLBACK_PROPERTY( instanceTemplate, "onMediaChanged", CB_MediaPlayerMediaChanged );
    SET_CALLBACK_PROPERTY( instanceTemplate, "onNothingSpecial", CB_MediaPlayerNothingSpecial );
    SET_CALLBACK_PROPERTY( instanceTemplate, "onOpening", CB_MediaPlayerOpening );
    SET_CALLBACK_PROPERTY( instanceTemplate, "onBuffering", CB_MediaPlayerBuffering );
    SET_CALLBACK_PROPERTY( instanceTemplate, "onPlaying", CB_MediaPlayerPlaying );
    SET_CALLBACK_PROPERTY( instanceTemplate, "onPaused", CB_MediaPlayerPaused );
    SET_CALLBACK_PROPERTY( instanceTemplate, "onForward", CB_MediaPlayerForward );
    SET_CALLBACK_PROPERTY( instanceTemplate, "onBackward", CB_MediaPlayerBackward );
    SET_CALLBACK_PROPERTY( instanceTemplate, "onEncounteredError", CB_MediaPlayerEncounteredError );
    SET_CALLBACK_PROPERTY( instanceTemplate, "onBeginReached", CB_MediaPlayerBeginReached );
    SET_CALLBACK_PROPERTY( instanceTemplate, "onEndReached", CB_MediaPlayerEndReached );
    SET_CALLBACK_PROPERTY( instanceTemplate, "onStopped", CB_MediaPlayerStopped );

    SET_CALLBACK_PROPERTY( instanceTemplate, "onTimeChanged", CB_MediaPlayerTimeChanged );
    SET_CALLBACK_PROPERTY( instanceTemplate, "onPositionChanged", CB_MediaPlayerPositionChanged );
    SET_CALLBACK_PROPERTY( instanceTemplate, "onSeekableChanged", CB_MediaPlayerSeekableChanged );
    SET_CALLBACK_PROPERTY( instanceTemplate, "onPausableChanged", CB_MediaPlayerPausableChanged );
    SET_CALLBACK_PROPERTY( instanceTemplate, "onLengthChanged", CB_MediaPlayerLengthChanged );

    SET_CALLBACK_PROPERTY( instanceTemplate, "onLogMessage", CB_LogMessage );

    SET_RO_PROPERTY( instanceTemplate, "playing", &JsVlcPlayer::playing );
    SET_RO_PROPERTY( instanceTemplate, "playingReverse", &JsVlcPlayer::playingReverse );
    SET_RO_PROPERTY( instanceTemplate, "length", &JsVlcPlayer::length );
    SET_RO_PROPERTY( instanceTemplate, "frames", &JsVlcPlayer::frames );
    SET_RO_PROPERTY( instanceTemplate, "state", &JsVlcPlayer::state );

    SET_RO_PROPERTY( instanceTemplate, "input", &JsVlcPlayer::input );
    SET_RO_PROPERTY( instanceTemplate, "audio", &JsVlcPlayer::audio );
    SET_RO_PROPERTY( instanceTemplate, "video", &JsVlcPlayer::video );
    SET_RO_PROPERTY( instanceTemplate, "subtitles", &JsVlcPlayer::subtitles );
    SET_RO_PROPERTY( instanceTemplate, "playlist", &JsVlcPlayer::playlist );

    SET_RO_PROPERTY( instanceTemplate, "videoFrame", &JsVlcPlayer::getVideoFrame );
    SET_RO_PROPERTY( instanceTemplate, "events", &JsVlcPlayer::getEventEmitter );

    SET_RW_PROPERTY( instanceTemplate, "pixelFormat", &JsVlcPlayer::pixelFormat, &JsVlcPlayer::setPixelFormat );
    SET_RW_PROPERTY( instanceTemplate, "position", &JsVlcPlayer::position, &JsVlcPlayer::setPosition );
    SET_RW_PROPERTY( instanceTemplate, "time", &JsVlcPlayer::time, &JsVlcPlayer::setTime );
    SET_RW_PROPERTY( instanceTemplate, "frame", &JsVlcPlayer::frame, &JsVlcPlayer::setFrame );
    SET_RW_PROPERTY( instanceTemplate, "volume", &JsVlcPlayer::volume, &JsVlcPlayer::setVolume );
    SET_RW_PROPERTY( instanceTemplate, "mute", &JsVlcPlayer::muted, &JsVlcPlayer::setMuted );

    NODE_SET_PROTOTYPE_METHOD( constructorTemplate, "load", jsLoad );
    SET_METHOD( constructorTemplate, "play", &JsVlcPlayer::play );
    SET_METHOD( constructorTemplate, "playReverse", &JsVlcPlayer::playReverse );
    SET_METHOD( constructorTemplate, "pause", &JsVlcPlayer::pause );
    SET_METHOD( constructorTemplate, "togglePause", &JsVlcPlayer::togglePause );
    SET_METHOD( constructorTemplate, "stop",  &JsVlcPlayer::stop );
    SET_METHOD( constructorTemplate, "toggleMute", &JsVlcPlayer::toggleMute );
    SET_METHOD( constructorTemplate, "previousFrame", &JsVlcPlayer::previousFrame );
    SET_METHOD( constructorTemplate, "nextFrame", &JsVlcPlayer::nextFrame );

    SET_METHOD( constructorTemplate, "close", &JsVlcPlayer::close );

    Local<Function> constructor = constructorTemplate->GetFunction();
    _jsConstructor.Reset( isolate, constructor );

    exports->Set( String::NewFromUtf8( isolate, "VlcPlayer", v8::String::kInternalizedString ), constructor );
    exports->Set( String::NewFromUtf8( isolate, "createPlayer", v8::String::kInternalizedString ), constructor );

    exports->ForceSet( String::NewFromUtf8( isolate, "vlcVersion", v8::String::kInternalizedString ),
                       vlcVersion,
                       static_cast<v8::PropertyAttribute>( ReadOnly | DontDelete ) );
    exports->ForceSet( String::NewFromUtf8( isolate, "vlcChangeset", v8::String::kInternalizedString ),
                       vlcChangeset,
                       static_cast<v8::PropertyAttribute>( ReadOnly | DontDelete ) );
}

void JsVlcPlayer::jsCreate( const v8::FunctionCallbackInfo<v8::Value>& args )
{
    using namespace v8;

    Isolate* isolate = Isolate::GetCurrent();
    HandleScope scope( isolate );

    Local<Object> thisObject = args.Holder();
    if( args.IsConstructCall() ) {
        Local<Array> options;
        if( args.Length() == 1 && args[0]->IsArray() ) {
            options = Local<Array>::Cast( args[0] );
        }

        JsVlcPlayer* jsPlayer = new JsVlcPlayer( thisObject, options );
        args.GetReturnValue().Set( jsPlayer->handle() );
    } else {
        Local<Value> argv[] = { args[0] };
        Local<Function> constructor =
            Local<Function>::New( isolate, _jsConstructor );
        args.GetReturnValue().Set( constructor->NewInstance( sizeof( argv ) / sizeof( argv[0] ), argv ) );
    }
}

void JsVlcPlayer::closeAll()
{
    for( JsVlcPlayer* p : _instances ) {
        p->close();
    }
}

JsVlcPlayer::JsVlcPlayer( v8::Local<v8::Object>& thisObject, const v8::Local<v8::Array>& vlcOpts ) :
    _libvlc( nullptr ),
    _cppInput( nullptr ),
    _cppAudio( nullptr ),
    _cppVideo( nullptr ),
    _cppSubtitles( nullptr ),
    _cppPlaylist( nullptr ),
    _isPlaying( false ),
    _reversePlayback( false ),
    _currentTime( 0 ),
    _performSeek( false ),
    _seekedFrameLoadedSanityChecks( MaxSanityChecks ),
    _lastTimeFrameReady( InvalidTime ),
    _lastTimeGlobalFrameReady( InvalidTime ),
    _loadVideoState( ELoadVideoState::UNLOADED ),
    _bufferingValue( 0.0f )
{
    Wrap( thisObject );

    _instances.insert( this );

    uv_loop_t* loop = uv_default_loop();

    uv_async_init( loop, &_async,
        [] ( uv_async_t* handle ) {
            if( handle->data )
                reinterpret_cast<JsVlcPlayer*>( handle->data )->handleAsync();
        }
    );
    _async.data = this;

    uv_timer_init( loop, &_errorTimer );
    _errorTimer.data = this;

    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    _jsEventEmitter.Reset( isolate,
        v8::Local<v8::Function>::Cast(
            Require( "events" )->Get(
                v8::String::NewFromUtf8( isolate,
                                         "EventEmitter",
                                         v8::String::kInternalizedString ) ) )->NewInstance() );

    initLibvlc( vlcOpts );

    _player.set_playback_mode( vlc::mode_normal );

    if( _libvlc && _player.open( _libvlc ) ) {
        _player.register_callback( this );
        VlcVideoOutput::open( &_player.basic_player() );
    } else {
        assert( false );
    }

    _jsInput = JsVlcInput::create( *this );
    _jsAudio = JsVlcAudio::create( *this );
    _jsVideo = JsVlcVideo::create( *this );
    _jsSubtitles = JsVlcSubtitles::create( *this );
    _jsPlaylist = JsVlcPlaylist::create( *this );
}

void JsVlcPlayer::initLibvlc( const v8::Local<v8::Array>& vlcOpts )
{
    using namespace v8;

    if( _libvlc ) {
        assert( false );
        libvlc_release( _libvlc );
        _libvlc = nullptr;
    }

    if( vlcOpts.IsEmpty() || vlcOpts->Length() == 0 ) {
        _libvlc = libvlc_new( 0, nullptr );
    } else {
        std::deque<std::string> opts;
        std::vector<const char*> libvlcOpts;

        for( unsigned i = 0;
             i < std::min<unsigned>( vlcOpts->Length(), std::numeric_limits<short>::max() );
             ++i )
        {
            String::Utf8Value opt( vlcOpts->Get( i )->ToString() );
            if( opt.length() ) {
                auto it = opts.emplace( opts.end(), *opt );
                libvlcOpts.push_back( it->c_str() );
            }
        }

        _libvlc = libvlc_new( static_cast<int>( libvlcOpts.size() ), libvlcOpts.data() );
    }

    if( _libvlc ) {
        libvlc_log_set(_libvlc, JsVlcPlayer::log_event_wrapper, this);
    }
}

JsVlcPlayer::~JsVlcPlayer()
{
    close();

    _instances.erase( this );
}

void JsVlcPlayer::close()
{
    _player.unregister_callback( this );
    VlcVideoOutput::close();

    _player.close();

    _async.data = nullptr;
    uv_close( reinterpret_cast<uv_handle_t*>( &_async ), 0 );

    _errorTimer.data = nullptr;
    uv_timer_stop( &_errorTimer );

    if( _libvlc ) {
        libvlc_release( _libvlc );
        _libvlc = nullptr;
    }
}

void JsVlcPlayer::media_player_event( const libvlc_event_t* e )
{
    _asyncDataGuard.lock();
    _asyncData.emplace_back( new LibvlcEvent( *e ) );
    _asyncDataGuard.unlock();
    uv_async_send( &_async );
}

void JsVlcPlayer::log_event_wrapper( void *data, int level, const libvlc_log_t *ctx, const char *fmt, va_list args )
{
    ((JsVlcPlayer *)data)->log_event(level, ctx, fmt, args);
}

#ifndef _MSC_VER
inline int _vscprintf( const char* format, va_list argptr )
{
    return vsnprintf( nullptr, 0, format, argptr );
}
#endif

void JsVlcPlayer::log_event( int level, const libvlc_log_t *ctx, const char *fmt, va_list args )
{
#if defined(_DEBUG)
    va_list argsCopy;
    va_copy( argsCopy, args );
    int messageSize = _vscprintf( fmt, argsCopy );
    va_end( argsCopy );

    // If the format string is bad, there is nothing we'll ever be able to do.
    if( messageSize <= 0 )
        return;

    std::string message( messageSize + 1, '\0' );
    // vsnprintf is a bit of a mess in Microsoft-land, older versions do not guarantee termination.
    vsnprintf( &message[0], message.size(), fmt, args );
    while( '\0' == message[message.size() - 1] )
        message.resize( message.size() - 1 );

    _asyncDataGuard.lock();
    _asyncData.emplace_back( new LibvlcLogEvent( level, message, fmt ) );
    _asyncDataGuard.unlock();

    uv_async_send( &_async );
#endif
}

void JsVlcPlayer::handleAsync()
{
    while( !_asyncData.empty() ) {
        std::deque<std::unique_ptr<AsyncData> > tmpData;
        _asyncDataGuard.lock();
        _asyncData.swap( tmpData );
        _asyncDataGuard.unlock();
        for( const auto& i: tmpData ) {
            i->process( this );

            //events queue could be very long...
            if( VlcVideoOutput::isFrameReady() ) {
                onFrameReady();
            }
        }
    }
}

void* JsVlcPlayer::onFrameSetup( const RV32VideoFrame& videoFrame )
{
    using namespace v8;

    if( 0 == videoFrame.width() || 0 == videoFrame.height() || 0 == videoFrame.size() ) {
        assert( false );
        return nullptr;
    }

    Isolate* isolate = Isolate::GetCurrent();
    HandleScope scope( isolate );

    Local<Object> global = isolate->GetCurrentContext()->Global();

    Local<Value> abv =
        global->Get(
            String::NewFromUtf8( isolate,
                                 "Uint8Array",
                                 v8::String::kInternalizedString ) );
    Local<Value> argv[] =
        { Integer::NewFromUnsigned( isolate, videoFrame.size() ) };
    Local<Uint8Array> jsArray =
        Handle<Uint8Array>::Cast( Handle<Function>::Cast( abv )->NewInstance( 1, argv ) );

    Local<Integer> jsWidth = Integer::New( isolate, videoFrame.width() );
    Local<Integer> jsHeight = Integer::New( isolate, videoFrame.height() );
    Local<Integer> jsPixelFormat = Integer::New( isolate, static_cast<int>( PixelFormat::RV32 ) );

    jsArray->ForceSet( String::NewFromUtf8( isolate, "width", v8::String::kInternalizedString ), jsWidth,
                       static_cast<v8::PropertyAttribute>( ReadOnly | DontDelete ) );
    jsArray->ForceSet( String::NewFromUtf8( isolate, "height", v8::String::kInternalizedString ), jsHeight,
                       static_cast<v8::PropertyAttribute>( ReadOnly | DontDelete ) );
    jsArray->ForceSet( String::NewFromUtf8( isolate, "pixelFormat", v8::String::kInternalizedString ), jsPixelFormat,
                       static_cast<v8::PropertyAttribute>( ReadOnly | DontDelete ) );

    _jsFrameBuffer.Reset( isolate, jsArray );

    callCallback( CB_FrameSetup, { jsWidth, jsHeight, jsPixelFormat, jsArray } );

#ifdef USE_ARRAY_BUFFER
    return jsArray->Buffer()->GetContents().Data();
#else
    return jsArray->GetIndexedPropertiesExternalArrayData();
#endif
}

void* JsVlcPlayer::onFrameSetup( const I420VideoFrame& videoFrame )
{
    using namespace v8;

    if( 0 == videoFrame.width() || 0 == videoFrame.height() ||
        0 == videoFrame.uPlaneOffset() || 0 == videoFrame.vPlaneOffset() ||
        0 == videoFrame.size() )
    {
        assert( false );
        return nullptr;
    }

    Isolate* isolate = Isolate::GetCurrent();
    HandleScope scope( isolate );

    Local<Object> global = isolate->GetCurrentContext()->Global();

    Local<Value> abv =
        global->Get(
            String::NewFromUtf8( isolate,
                                 "Uint8Array",
                                 v8::String::kInternalizedString ) );
    Local<Value> argv[] =
        { Integer::NewFromUnsigned( isolate, videoFrame.size() ) };
    Local<Uint8Array> jsArray =
        Handle<Uint8Array>::Cast( Handle<Function>::Cast( abv )->NewInstance( 1, argv ) );

    Local<Integer> jsWidth = Integer::New( isolate, videoFrame.width() );
    Local<Integer> jsHeight = Integer::New( isolate, videoFrame.height() );
    Local<Integer> jsPixelFormat = Integer::New( isolate, static_cast<int>( PixelFormat::I420 ) );

    jsArray->ForceSet( String::NewFromUtf8( isolate, "width", v8::String::kInternalizedString ),
                       jsWidth,
                       static_cast<v8::PropertyAttribute>( ReadOnly | DontDelete ) );
    jsArray->ForceSet( String::NewFromUtf8( isolate, "height", v8::String::kInternalizedString ),
                       jsHeight,
                       static_cast<v8::PropertyAttribute>( ReadOnly | DontDelete ) );
    jsArray->ForceSet( String::NewFromUtf8( isolate, "pixelFormat", v8::String::kInternalizedString ),
                       jsPixelFormat,
                       static_cast<v8::PropertyAttribute>( ReadOnly | DontDelete ) );
    jsArray->ForceSet( String::NewFromUtf8( isolate, "uOffset", v8::String::kInternalizedString ),
                       Integer::New( isolate, videoFrame.uPlaneOffset() ),
                       static_cast<v8::PropertyAttribute>( ReadOnly | DontDelete ) );
    jsArray->ForceSet( String::NewFromUtf8( isolate, "vOffset", v8::String::kInternalizedString ),
                       Integer::New( isolate, videoFrame.vPlaneOffset() ),
                       static_cast<v8::PropertyAttribute>( ReadOnly | DontDelete ) );

    _jsFrameBuffer.Reset( isolate, jsArray );

    callCallback( CB_FrameSetup, { jsWidth, jsHeight, jsPixelFormat, jsArray } );

#ifdef USE_ARRAY_BUFFER
    return jsArray->Buffer()->GetContents().Data();
#else
    return jsArray->GetIndexedPropertiesExternalArrayData();
#endif
}

void JsVlcPlayer::onFrameReady()
{
    vlc::player& p = player();
    vlc::playback& playback = p.playback();
    const libvlc_time_t playbackTime = playback.get_time();

    switch( _loadVideoState ) {
        case ELoadVideoState::LOADED:
            updateCurrentTime();

            if( _isPlaying ) {
                _seekedFrameLoadedSanityChecks = MaxSanityChecks;
                _performSeek = false;
                doCallCallback();

                // Stop reverse playback when gets the beginning.
                if( _reversePlayback && playbackTime == 0 ) {
                    callCallback( CB_MediaPlayerBeginReached );

                    _isPlaying = false;
                    _reversePlayback = false;
                }
            }
            else if( _performSeek ) {
              if( playbackTime == _currentTime ) {
                  doCallCallback();

                  if( 0u == --_seekedFrameLoadedSanityChecks )
                      _performSeek = false;
              }
              else if( playbackTime != _currentTime ) {
                  // It means that there have been another seek.
                  _seekedFrameLoadedSanityChecks = MaxSanityChecks;
              }
            }
            break;
        case ELoadVideoState::GETTING:
            if( libvlc_Paused == p.get_state() ) {
                if( playbackTime == _currentTime ) {
                    doCallCallback();
                    _loadVideoState = ELoadVideoState::LOADED;
                }
                else
                    playback.set_time( _currentTime );
            }
            else {
                p.pause();
            }
            break;
    }
}

void JsVlcPlayer::onFrameCleanup()
{
    callCallback( CB_FrameCleanup );
}

void JsVlcPlayer::handleLibvlcEvent( const libvlc_event_t& libvlcEvent )
{
    using namespace v8;

    Isolate* isolate = Isolate::GetCurrent();
    HandleScope scope( isolate );

    Callbacks_e callback = CB_Max;

    switch( libvlcEvent.type ) {
        case libvlc_MediaPlayerMediaChanged:
            callback = CB_MediaPlayerMediaChanged;
            break;
        case libvlc_MediaPlayerNothingSpecial:
            callback = CB_MediaPlayerNothingSpecial;
            break;
        case libvlc_MediaPlayerOpening:
            callback = CB_MediaPlayerOpening;
            break;
        case libvlc_MediaPlayerBuffering: {
            _bufferingValue = libvlcEvent.u.media_player_buffering.new_cache;
            callCallback( CB_MediaPlayerBuffering, { Number::New( isolate, _bufferingValue ) } );
            break;
        }
        case libvlc_MediaPlayerPlaying:
            callback = CB_MediaPlayerPlaying;
            break;
        case libvlc_MediaPlayerPaused:
            callback = CB_MediaPlayerPaused;
            break;
        case libvlc_MediaPlayerStopped:
            callback = CB_MediaPlayerStopped;
            break;
        case libvlc_MediaPlayerForward:
            callback = CB_MediaPlayerForward;
            break;
        case libvlc_MediaPlayerBackward:
            callback = CB_MediaPlayerBackward;
            break;
        case libvlc_MediaPlayerEndReached:
            callback = CB_MediaPlayerEndReached;
            uv_timer_stop( &_errorTimer );
            currentItemEndReached();
            break;
        case libvlc_MediaPlayerEncounteredError:
            callback = CB_MediaPlayerEncounteredError;
            //sometimes libvlc do some internal error handling
            //and sends EndReached after that,
            //so we have to wait it some time,
            //to not break playlist ligic.
            uv_timer_start( &_errorTimer,
                [] ( uv_timer_t* handle ) {
                    if( handle->data )
                        static_cast<JsVlcPlayer*>( handle->data )->currentItemEndReached();
                }, 1000, 0 );
            break;
        case libvlc_MediaPlayerTimeChanged: {
            const double new_time =
                static_cast<double>( libvlcEvent.u.media_player_time_changed.new_time );
            callCallback( CB_MediaPlayerTimeChanged,
                          { Number::New( isolate, static_cast<double>( new_time ) ) } );
            break;
        }
        case libvlc_MediaPlayerPositionChanged: {
            callCallback( CB_MediaPlayerPositionChanged,
                          { Number::New( isolate,
                                         libvlcEvent.u.media_player_position_changed.new_position ) } );
            break;
        }
        case libvlc_MediaPlayerSeekableChanged: {
            callCallback( CB_MediaPlayerSeekableChanged,
                          { Boolean::New( isolate,
                            libvlcEvent.u.media_player_seekable_changed.new_seekable != 0 ) } );
            break;
        }
        case libvlc_MediaPlayerPausableChanged: {
            callCallback( CB_MediaPlayerPausableChanged,
                          { Boolean::New( isolate,
                                          libvlcEvent.u.media_player_pausable_changed.new_pausable != 0 ) } );
            break;
        }
        case libvlc_MediaPlayerLengthChanged: {
            const double new_length =
                static_cast<double>( libvlcEvent.u.media_player_length_changed.new_length );
            callCallback( CB_MediaPlayerLengthChanged, { Number::New( isolate, new_length ) } );
            break;
        }
    }

    if( callback != CB_Max ) {
        callCallback( callback );
    }
}

void JsVlcPlayer::currentItemEndReached()
{
    if( vlc::mode_single != player().get_playback_mode() )
        player().next();
}

void JsVlcPlayer::callCallback( Callbacks_e callback,
                                std::initializer_list<v8::Local<v8::Value> > list )
{
    using namespace v8;

    Isolate* isolate = Isolate::GetCurrent();
    HandleScope scope( isolate );

    std::vector<v8::Local<v8::Value> > argList;
    argList.reserve( list.size() );
    argList.push_back(
        String::NewFromUtf8( isolate,
                             callbackNames[callback],
                             v8::String::kInternalizedString ) );
    if( list.size() > 0 )
        argList.insert( argList.end(), list );

    if( !_jsCallbacks[callback].IsEmpty() ) {
        Local<Function> callbackFunc =
            Local<Function>::New( isolate, _jsCallbacks[callback] );

        callbackFunc->Call( handle(), static_cast<int>( argList.size() - 1), argList.data() + 1 );
    }

    Local<Object> eventEmitter = getEventEmitter();
    Local<Function> emitFunction =
        v8::Local<v8::Function>::Cast(
            eventEmitter->Get(
                String::NewFromUtf8( isolate, "emit", v8::String::kInternalizedString ) ) );

    emitFunction->Call( eventEmitter, static_cast<int>( argList.size() ), argList.data() );
}

void JsVlcPlayer::doCallCallback() {
    using namespace v8;

    Isolate* isolate = Isolate::GetCurrent();
    HandleScope scope( isolate );

    assert( !_jsFrameBuffer.IsEmpty() ); //FIXME! maybe it worth add condition here
    callCallback( CB_FrameReady, {
      Local<Value>::New( isolate, _jsFrameBuffer ),
      Number::New( isolate, frame() ),
      Number::New( isolate, time() )
    } );
}

void JsVlcPlayer::updateCurrentTime() {
    using namespace std::chrono;

    milliseconds msSinceEpoch = duration_cast<milliseconds>( system_clock::now().time_since_epoch() );
    const libvlc_time_t currentTimeGlobal = static_cast<libvlc_time_t>( msSinceEpoch.count() );

    if( _isPlaying && !_reversePlayback ) {
        const libvlc_time_t playbackTime = player().playback().get_time();
        if( _lastTimeFrameReady == playbackTime ) {
            _currentTime += static_cast<libvlc_time_t>( static_cast<double>( currentTimeGlobal - _lastTimeGlobalFrameReady ) * _cppInput->rate() );

            const libvlc_time_t length = player().playback().get_length();
            _currentTime = std::min( _currentTime, length );
        }
        else {
            _lastTimeFrameReady = playbackTime;

            if( playbackTime > _currentTime ) {
                const libvlc_time_t length = player().playback().get_length();
                _currentTime = std::min( playbackTime, length );
            }
        }
    }

    _lastTimeGlobalFrameReady = currentTimeGlobal;
}

void JsVlcPlayer::setCurrentTime( libvlc_time_t time )
{
    const libvlc_time_t videoLength = player().playback().get_length();
    if( 0 != videoLength )
        _currentTime = std::max( 0l, std::min( time, videoLength ) );
    else
        _currentTime = std::max( 0l, time );

    _lastTimeFrameReady = InvalidTime;
    _lastTimeGlobalFrameReady = InvalidTime;
}

double JsVlcPlayer::rateReverse()
{
    return _cppInput->rateReverse();
}

void JsVlcPlayer::setRateReverse( double rateReverse )
{
    _cppInput->setRateReverse( rateReverse );
}

double JsVlcPlayer::decimalFrame() {
  return static_cast<double>( static_cast<float>( time() ) / ( 1000.0f / player().playback().get_fps() ) );
}

void JsVlcPlayer::jsLoad( const v8::FunctionCallbackInfo<v8::Value>& args )
{
    using namespace v8;

    JsVlcPlayer* jsPlayer = ObjectWrap::Unwrap<JsVlcPlayer>( args.Holder() );

    assert( args.Length() >= 1 );
    String::Utf8Value mrl( args[0]->ToString() );
    if( mrl.length() ) {
        bool startPlaying = false;
        if( args.Length() >= 2 ) {
            assert( args[1]->IsBoolean() );
            startPlaying = args[1]->ToBoolean()->Value();
        }

        unsigned atTime = 0;
        if( args.Length() >= 3 ) {
          assert(args[2]->IsUint32());
          atTime = args[2]->ToUint32()->Value();
        }

        jsPlayer->load( *mrl, startPlaying, atTime );
    }
}

void JsVlcPlayer::getJsCallback( v8::Local<v8::String> property,
                                 const v8::PropertyCallbackInfo<v8::Value>& info,
                                 Callbacks_e callback )
{
    using namespace v8;

    JsVlcPlayer* jsPlayer = ObjectWrap::Unwrap<JsVlcPlayer>( info.Holder() );

    if( jsPlayer->_jsCallbacks[callback].IsEmpty() )
        return;

    Isolate* isolate = Isolate::GetCurrent();
    HandleScope scope( isolate );

    Local<Function> callbackFunc =
        Local<Function>::New( isolate, jsPlayer->_jsCallbacks[callback] );

    info.GetReturnValue().Set( callbackFunc );
}

void JsVlcPlayer::setJsCallback( v8::Local<v8::String> property,
                                 v8::Local<v8::Value> value,
                                 const v8::PropertyCallbackInfo<void>& info,
                                 Callbacks_e callback )
{
    using namespace v8;

    Isolate* isolate = Isolate::GetCurrent();
    HandleScope scope( isolate );

    JsVlcPlayer* jsPlayer = ObjectWrap::Unwrap<JsVlcPlayer>( info.Holder() );

    Local<Function> callbackFunc = Local<Function>::Cast( value );
    if( !callbackFunc.IsEmpty() )
        jsPlayer->_jsCallbacks[callback].Reset( isolate, callbackFunc );
}

bool JsVlcPlayer::playing()
{
    return _isPlaying;
}

bool JsVlcPlayer::playingReverse()
{
    return _reversePlayback;
}

double JsVlcPlayer::length()
{
    return static_cast<double>( player().playback().get_length() );
}

double JsVlcPlayer::frames()
{
    vlc::playback& playback = player().playback();

    return std::ceil( static_cast<double>( static_cast<float>( playback.get_length() ) * playback.get_fps() / 1000.0f ) ) + 1;
}

unsigned JsVlcPlayer::state()
{
    return player().get_state();
}

v8::Local<v8::Value> JsVlcPlayer::getVideoFrame()
{
    return v8::Local<v8::Value>::New( v8::Isolate::GetCurrent(), _jsFrameBuffer );
}

v8::Local<v8::Object> JsVlcPlayer::getEventEmitter()
{
    return v8::Local<v8::Object>::New( v8::Isolate::GetCurrent(), _jsEventEmitter );
}

unsigned JsVlcPlayer::pixelFormat()
{
    return static_cast<unsigned>( VlcVideoOutput::pixelFormat() );
}

void JsVlcPlayer::setPixelFormat( unsigned format )
{
    switch( format ) {
        case static_cast<unsigned>( PixelFormat::RV32 ):
            VlcVideoOutput::setPixelFormat( PixelFormat::RV32 );
            break;
        case static_cast<unsigned>( PixelFormat::I420 ):
            VlcVideoOutput::setPixelFormat( PixelFormat::I420 );
            break;
    }
}

double JsVlcPlayer::position()
{
    assert( _currentTime >= 0 && _currentTime <= length() );

    return static_cast<double>( _currentTime ) / length();
}

void JsVlcPlayer::setPosition( double position )
{
    position = std::max( 0.0, std::min( position, 1.0 ) );

    _performSeek = true;
    setCurrentTime( static_cast<libvlc_time_t>( position * length() ) );
    player().playback().set_position( static_cast<float>( position ) );
}

double JsVlcPlayer::time()
{
    assert( _currentTime >= 0.0 && _currentTime <= length() );

    return static_cast<double>( _currentTime );
}

void JsVlcPlayer::setTime( double time )
{
    _performSeek = true;
    setCurrentTime( static_cast<libvlc_time_t>( time ) );
    player().playback().set_time( _currentTime );
}

double JsVlcPlayer::frame()
{
    const double iFrame = std::round( decimalFrame() );

    return std::min( iFrame, frames() );
}

void JsVlcPlayer::setFrame( double frame )
{
    frame = std::max( 0.0, std::min( frame, frames() ) );

    setTime( std::min( frame * static_cast<double>( 1000.0f / player().playback().get_fps() ), length() ) );
}

void JsVlcPlayer::previousFrame()
{
    pause();

    const double iFrame = decimalFrame();
    if( iFrame > 0.0 )
        setFrame( std::ceil( iFrame ) - 1 );
}

void JsVlcPlayer::nextFrame()
{
    pause();

    vlc::playback& playback = player().playback();
    const double frames = static_cast<double>( static_cast<float>( playback.get_length() ) / ( 1000.0f / playback.get_fps() ) );
    const double iFrame = decimalFrame();
    if( iFrame < frames - 1.0 )
        setFrame( std::floor( iFrame ) + 1 );
    else
        setTime( static_cast<double>( playback.get_length() ) );
}

unsigned JsVlcPlayer::volume()
{
    return player().audio().get_volume();
}

void JsVlcPlayer::setVolume( unsigned volume )
{
    player().audio().set_volume( volume );
}

bool JsVlcPlayer::muted()
{
    return player().audio().is_muted();
}

void JsVlcPlayer::setMuted( bool mute )
{
    player().audio().set_mute( mute );
}

void JsVlcPlayer::load( const std::string& mrl, bool startPlaying, unsigned atTime )
{
    stop();
    setCurrentTime( static_cast<libvlc_time_t>( atTime ) );
    setRateReverse( 1.0 );
    _reversePlayback = false;

    vlc::player& p = player();

    p.clear_items();
    const int idx = p.add_media( mrl.c_str() );
    if( idx >= 0 ) {
        _isPlaying = startPlaying;

        p.play( idx );
        if( startPlaying ) {
          _loadVideoState = ELoadVideoState::LOADED;
          p.playback().set_time( _currentTime );
        }
        else {
          _loadVideoState = ELoadVideoState::GETTING;
          p.pause();
        }
    }
    else {
        _isPlaying = false;
    }
}

void JsVlcPlayer::play()
{
    _isPlaying = true;
    _reversePlayback = false;

    player().play();
}

void JsVlcPlayer::playReverse()
{
    // Avoid creating more than one thread to do the same.
    if( _reversePlayback )
        return;

    _isPlaying = true;
    _reversePlayback = true;

    player().pause();

    std::thread reverseUpdateThread(
        [ this ]()
        {
            while( _isPlaying && _reversePlayback ) {
                vlc::playback& playback = player().playback();
                const double msPerFrame = static_cast<double>( 1000.0f / playback.get_fps() );
                const libvlc_time_t msToGoBack = static_cast<libvlc_time_t>( msPerFrame * rateReverse() );

                const libvlc_time_t playbackTime = playback.get_time();
                if( playbackTime > 0 ) {
                    setTime( static_cast<double>( _currentTime - msToGoBack ) );
                    std::this_thread::sleep_for( std::chrono::milliseconds( static_cast<libvlc_time_t>( msPerFrame ) ) );
                }
                else {
                    break;
                }
            }
        }
    );
    reverseUpdateThread.detach();
}

void JsVlcPlayer::pause()
{
    _isPlaying = false;
    _reversePlayback = false;

    player().pause();
}

void JsVlcPlayer::togglePause()
{
    _isPlaying = !_isPlaying;
    _reversePlayback = false;

    player().togglePause();
}

void JsVlcPlayer::stop()
{
    _loadVideoState = ELoadVideoState::UNLOADED;
    _isPlaying = false;
    _reversePlayback = false;
    setRateReverse( 1.0 );

    setCurrentTime( 0 );
    player().stop();
}

void JsVlcPlayer::toggleMute()
{
    player().audio().toggle_mute();
}

v8::Local<v8::Object> JsVlcPlayer::input()
{
    return v8::Local<v8::Object>::New( v8::Isolate::GetCurrent(), _jsInput );
}

v8::Local<v8::Object> JsVlcPlayer::audio()
{
    return v8::Local<v8::Object>::New( v8::Isolate::GetCurrent(), _jsAudio );
}

v8::Local<v8::Object> JsVlcPlayer::video()
{
    return v8::Local<v8::Object>::New( v8::Isolate::GetCurrent(), _jsVideo );
}

v8::Local<v8::Object> JsVlcPlayer::subtitles()
{
    return v8::Local<v8::Object>::New( v8::Isolate::GetCurrent(), _jsSubtitles );
}

v8::Local<v8::Object> JsVlcPlayer::playlist()
{
    return v8::Local<v8::Object>::New( v8::Isolate::GetCurrent(), _jsPlaylist );
}

void JsVlcPlayer::setInput( JsVlcInput& input )
{
    _cppInput = &input;
}

void JsVlcPlayer::setAudio( JsVlcAudio& audio )
{
    _cppAudio = &audio;
}

void JsVlcPlayer::setVideo( JsVlcVideo& video )
{
    _cppVideo = &video;
}

void JsVlcPlayer::setSubtitles( JsVlcSubtitles& subtitles )
{
    _cppSubtitles = &subtitles;
}

void JsVlcPlayer::setPlaylist( JsVlcPlaylist& playlist )
{
    _cppPlaylist = &playlist;
}
