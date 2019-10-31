@echo off

set script_path=%~dp0
set VLC_PLUGIN_PATH=%script_path%bin\plugins

rem call build_node
npm test
