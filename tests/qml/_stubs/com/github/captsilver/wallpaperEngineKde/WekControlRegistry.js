.pragma library
// The WekControl stubs of one QML engine, standing in for WekControl.cpp's
// process-wide controllerRegistry().
var controls = [];
// The controller ToggleKeyboard started keyboard mode on (WekControl.cpp's
// keyboardOwner()), or null.
var keyboardOwner = null;
var _next = 0;
function nextId() { _next += 1; return _next; }
