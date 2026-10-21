.pragma library
// The CloneGroup stubs of one QML engine, standing in for CloneGroup.cpp's
// process-wide table.
var all = [];
var _nextId = 0;
var _nextFrame = 0;
// How many CloneGroups this engine has made (tests: none with ShareRenderer off).
var made = 0;
function nextId() { _nextId += 1; made += 1; return _nextId; }
function nextFrame() { _nextFrame += 1; return _nextFrame; }
