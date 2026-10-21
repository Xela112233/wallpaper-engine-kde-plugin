.pragma library
// The load guards of one QML engine (FileHelper stubs' markLoading,
// clearLoading, stoppedLoading), standing in for ~/.config/wekde/loading/ and
// src/LoadGuard.hpp.  A guard's `owner` is the stub instance that wrote it
// while that "process" runs; crash() ends the process: its guards stay, owned
// by nobody, and count for stoppedLoading as LoadGuard::stopped() counts a
// dead process's.
var guards = [];
var marks = 0;
var _next = 0;
function nextOwner() { _next += 1; return "filehelper-stub-" + _next; }
function reset() { guards = []; marks = 0; }
function mark(owner, workshopId, source, after) {
    guards = guards.filter(g => g.owner !== owner);
    guards.push({ owner: owner, workshopid: workshopId, source: source, time: Date.now(), app: "plasmashell",
                  pid: 4242, after: after || "" });
    marks += 1;
    return true;
}
function clear(owner) { guards = guards.filter(g => g.owner !== owner); }
function crash() { for (const g of guards) g.owner = null; }
function age(ms) { for (const g of guards) g.time -= ms; }
function stopped(source, withinMs) {
    let best = null;
    for (const g of guards) {
        const age = Date.now() - g.time;
        if (g.owner !== null || g.source !== source || age > withinMs || age < -withinMs) continue;
        if (g.after === "resume") continue;          // a reload after sleep (LoadGuard::stopped skips it too)
        if (best === null || g.time > best.time) best = g;
    }
    return best === null ? ({}) : ({ workshopid: best.workshopid, source: best.source, time: best.time,
                                     app: best.app, pid: best.pid, file: "/stub/loading/" + best.pid + ".json" });
}
function owned(owner) { return guards.filter(g => g.owner === owner); }
