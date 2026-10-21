#pragma once
// The CPU priority of one scene's renderer threads (we-manager's
// ProcessPriority patch).  Linux nice values are per thread, so lowering the
// renderer's leaves plasmashell's own GUI thread, the panels and the desktop
// at theirs.
//
// Each scene (one SceneViewer, one MainHandler) has its own group: its
// "wek:main" and "wek:render" loopers and its "wek:timer" frame timer join
// it when they start, and Set() reaches the threads already running, so the
// first scene after a plasmashell start (whose threads start before QML sets
// processPriority) and two screens set apart each get their own value.
// Threads those threads start inherit the nice they have at that moment.
//
// Raising a nice value needs no privilege; lowering it again needs
// CAP_SYS_NICE or an RLIMIT_NICE above 0.  Without either, a thread that
// cannot go back keeps its value until the scene is loaded again.
#include <algorithm>
#include <cerrno>
#include <mutex>
#include <vector>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

namespace wallpaper
{

// 0 = normal, 5 = below normal, 10 = low.
inline int RenderNiceForLevel(int level) { return level >= 2 ? 10 : level == 1 ? 5 : 0; }

inline pid_t CurrentThreadId() { return static_cast<pid_t>(syscall(SYS_gettid)); }

class RenderThreadGroup {
public:
    // On a renderer thread right after it is named: records it and gives it
    // the group's nice value.
    void Join() {
        std::lock_guard<std::mutex> lk(m_mutex);
        const pid_t tid = CurrentThreadId();
        if (std::find(m_tids.begin(), m_tids.end(), tid) == m_tids.end()) m_tids.push_back(tid);
        apply(tid);
        Current() = this;
    }
    // On the same thread before it ends.
    void Leave() {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_tids.erase(std::remove(m_tids.begin(), m_tids.end(), CurrentThreadId()), m_tids.end());
        if (Current() == this) Current() = nullptr;
    }
    // Any thread: the nice value of the group's threads from now on, given
    // at once to those running.  False when one could not take it (going
    // back up without the privilege); that thread keeps its own.
    bool Set(int nice) {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_nice  = nice;
        bool ok = true;
        for (const pid_t tid : m_tids) ok = apply(tid) && ok;
        return ok;
    }
    int Nice() const {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_nice;
    }
    std::vector<pid_t> Threads() const {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_tids;
    }
    // The group the calling thread joined, or null.  The render thread
    // creates the Vulkan device, which reads it for the GPU queue priority.
    static RenderThreadGroup*& Current() {
        static thread_local RenderThreadGroup* group = nullptr;
        return group;
    }

private:
    // Under m_mutex.  PRIO_PROCESS with a thread id is that one thread.
    bool apply(pid_t tid) const {
        errno           = 0;
        const int now   = getpriority(PRIO_PROCESS, static_cast<id_t>(tid));
        if (errno == 0 && now == m_nice) return true;
        return setpriority(PRIO_PROCESS, static_cast<id_t>(tid), m_nice) == 0;
    }

    mutable std::mutex m_mutex;
    int                m_nice { 0 };
    std::vector<pid_t> m_tids;
};

} // namespace wallpaper
