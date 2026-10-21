.pragma library
// The WekControl stubs of one QML engine, standing in for WekControl.cpp's
// process-wide controllerRegistry().
var controls = [];
var _next = 0;
function nextId() { _next += 1; return _next; }
