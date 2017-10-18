#include "JsVlcInput.h"

#include "NodeTools.h"
#include "JsVlcPlayer.h"
#include "JsVlcDeinterlace.h"

v8::Persistent<v8::Function> JsVlcInput::_jsConstructor;

void JsVlcInput::initJsApi()
{
    JsVlcDeinterlace::initJsApi();

    using namespace v8;

    Isolate* isolate = Isolate::GetCurrent();
    HandleScope scope( isolate );

    Local<FunctionTemplate> constructorTemplate = FunctionTemplate::New( isolate, jsCreate );
    constructorTemplate->SetClassName(
        String::NewFromUtf8( isolate, "VlcInput", v8::String::kInternalizedString ) );

    Local<ObjectTemplate> protoTemplate = constructorTemplate->PrototypeTemplate();
    Local<ObjectTemplate> instanceTemplate = constructorTemplate->InstanceTemplate();
    instanceTemplate->SetInternalFieldCount( 1 );

    SET_RO_PROPERTY( instanceTemplate, "length", &JsVlcInput::length );
    SET_RO_PROPERTY( instanceTemplate, "fps", &JsVlcInput::fps );
    SET_RO_PROPERTY( instanceTemplate, "state", &JsVlcInput::state );
    SET_RO_PROPERTY( instanceTemplate, "hasVout", &JsVlcInput::hasVout );

    SET_RW_PROPERTY( instanceTemplate, "position",
                     &JsVlcInput::position,
                     &JsVlcInput::setPosition );
    SET_RW_PROPERTY( instanceTemplate, "time",
                     &JsVlcInput::time,
                     &JsVlcInput::setTime );
    SET_RW_PROPERTY( instanceTemplate, "rate",
                     &JsVlcInput::rate,
                     &JsVlcInput::setRate );
    SET_RW_PROPERTY( instanceTemplate, "rateReverse",
                     &JsVlcInput::rateReverse,
                     &JsVlcInput::setRateReverse );

    Local<Function> constructor = constructorTemplate->GetFunction();
    _jsConstructor.Reset( isolate, constructor );
}

v8::UniquePersistent<v8::Object> JsVlcInput::create( JsVlcPlayer& player )
{
    using namespace v8;

    Isolate* isolate = Isolate::GetCurrent();
    HandleScope scope( isolate );

    Local<Function> constructor =
        Local<Function>::New( isolate, _jsConstructor );

    Local<Value> argv[] = { player.handle() };

    return { isolate, constructor->NewInstance( sizeof( argv ) / sizeof( argv[0] ), argv ) };
}

void JsVlcInput::jsCreate( const v8::FunctionCallbackInfo<v8::Value>& args )
{
    using namespace v8;

    Isolate* isolate = Isolate::GetCurrent();
    HandleScope scope( isolate );

    Local<Object> thisObject = args.Holder();
    if( args.IsConstructCall() && thisObject->InternalFieldCount() > 0 ) {
        JsVlcPlayer* jsPlayer =
            ObjectWrap::Unwrap<JsVlcPlayer>( Handle<Object>::Cast( args[0] ) );
        if( jsPlayer ) {
            JsVlcInput* jsPlaylist = new JsVlcInput( thisObject, jsPlayer );
            args.GetReturnValue().Set( thisObject );
        }
    } else {
        Local<Function> constructor =
            Local<Function>::New( isolate, _jsConstructor );
        Local<Value> argv[] = { args[0] };
        args.GetReturnValue().Set(
            constructor->NewInstance( sizeof( argv ) / sizeof( argv[0] ), argv ) );
    }
}

JsVlcInput::JsVlcInput( v8::Local<v8::Object>& thisObject, JsVlcPlayer* jsPlayer ) :
    _jsPlayer( jsPlayer ),
    _rateReverse( 1.0 )
{
    Wrap( thisObject );

    _jsPlayer->setInput( *this );
}

double JsVlcInput::length()
{
    return _jsPlayer->length();
}

double JsVlcInput::fps()
{
    return _jsPlayer->player().playback().get_fps();
}

unsigned JsVlcInput::state()
{
    return _jsPlayer->state();
}

bool JsVlcInput::hasVout()
{
    return _jsPlayer->player().video().has_vout();
}

double JsVlcInput::position()
{
    return _jsPlayer->position();
}

void JsVlcInput::setPosition( double position )
{
    _jsPlayer->setPosition( position );
}

double JsVlcInput::time()
{
    return _jsPlayer->time();
}

void JsVlcInput::setTime( double time )
{
    return _jsPlayer->setTime( time );
}

double JsVlcInput::rate()
{
    return _jsPlayer->player().playback().get_rate();
}

void JsVlcInput::setRate( double rate )
{
    _jsPlayer->player().playback().set_rate( static_cast<float>( rate ) );
}

double JsVlcInput::rateReverse()
{
    return _rateReverse;
}

void JsVlcInput::setRateReverse( double rateReverse )
{
    _rateReverse = rateReverse;
}
