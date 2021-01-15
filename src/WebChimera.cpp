#include <node.h>
#include <v8.h>

#include "JsVlcPlayer.h"
#include "NodeTools.h"

#include <fstream>
void log1(const std::string& text)
{
  std::ofstream log_file("log_file.txt", std::ios_base::out | std::ios_base::app);
  log_file << text << std::endl;
}

// Initialize this addon to be context-aware.
extern "C" NODE_MODULE_EXPORT void
NODE_MODULE_INITIALIZER(
  v8::Local<v8::Object> exports,
  v8::Local<v8::Value> module,
  v8::Local<v8::Context> context
)
{
    using namespace v8;

    log1("Init.1");
    thisModule.Reset( Isolate::GetCurrent(), Local<Object>::Cast( module ) );
    log1("Init.2");

    JsVlcPlayer::initJsApi( exports );
    log1("Init.3");
}
