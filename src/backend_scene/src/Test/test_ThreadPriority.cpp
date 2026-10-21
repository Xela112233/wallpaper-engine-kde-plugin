// ProcessPriority (we-manager patch): a scene's renderer threads follow its
// RenderThreadGroup.  SceneObject's constructor starts the loopers
// (MainHandler::init) before QML sets processPriority, so a value set after
// the threads started must still reach them; and each scene has its own.
#include <doctest.h>

#include <chrono>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include "Looper/Looper.hpp"
#include "Timer/ThreadTimer.hpp"
#include "Utils/ThreadPriority.hpp"

using wallpaper::RenderThreadGroup;
using wallpaper::looper::Looper;

namespace
{
// Field 19 of /proc/self/task/<tid>/stat: the thread's nice value.
int NiceOf(pid_t tid) {
    std::ifstream in("/proc/self/task/" + std::to_string(tid) + "/stat");
    std::string   line;
    std::getline(in, line);
    const auto close = line.rfind(')');
    if (close == std::string::npos) return -100;
    std::istringstream rest(line.substr(close + 2));
    std::string        field;
    for (int i = 3; rest >> field; ++i)
        if (i == 19) return std::stoi(field);
    return -100;
}
std::string NameOf(pid_t tid) {
    std::ifstream in("/proc/self/task/" + std::to_string(tid) + "/comm");
    std::string   name;
    std::getline(in, name);
    return name;
}
// The threads join when they first run; wait until `n` have.
bool WaitForThreads(const RenderThreadGroup& group, size_t n) {
    for (int i = 0; i < 400; ++i) {
        if (group.Threads().size() >= n) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}
bool AllAt(const RenderThreadGroup& group, int nice) {
    for (const pid_t tid : group.Threads())
        if (NiceOf(tid) != nice) return false;
    return ! group.Threads().empty();
}
} // namespace

TEST_SUITE("ProcessPriority thread groups") {
    TEST_CASE("a value set after the loopers started reaches them") {
        auto group = std::make_shared<RenderThreadGroup>();
        auto main  = std::make_shared<Looper>();
        auto rend  = std::make_shared<Looper>();
        main->setName("main");
        rend->setName("render");
        main->setThreadGroup(group);
        rend->setThreadGroup(group);
        main->start();
        rend->start();
        REQUIRE(WaitForThreads(*group, 2));
        // What QML does once SceneObject's constructor has returned.
        CHECK(group->Set(wallpaper::RenderNiceForLevel(2)));
        CHECK(AllAt(*group, 10));
        bool named = true;
        for (const pid_t tid : group->Threads()) {
            const auto name = NameOf(tid);
            named = named && (name == "wek:main" || name == "wek:render");
        }
        CHECK(named);
        main->stop();
        rend->stop();
        CHECK(group->Threads().empty());
    }

    TEST_CASE("a thread that starts later takes the group's value") {
        auto group = std::make_shared<RenderThreadGroup>();
        CHECK(group->Set(5));
        auto looper = std::make_shared<Looper>();
        looper->setName("main");
        looper->setThreadGroup(group);
        looper->start();
        REQUIRE(WaitForThreads(*group, 1));
        CHECK(AllAt(*group, 5));
        looper->stop();
    }

    TEST_CASE("two scenes keep their own values") {
        auto low    = std::make_shared<RenderThreadGroup>();
        auto normal = std::make_shared<RenderThreadGroup>();
        auto a      = std::make_shared<Looper>();
        auto b      = std::make_shared<Looper>();
        a->setName("render");
        b->setName("render");
        a->setThreadGroup(low);
        b->setThreadGroup(normal);
        a->start();
        b->start();
        REQUIRE(WaitForThreads(*low, 1));
        REQUIRE(WaitForThreads(*normal, 1));
        CHECK(low->Set(10));
        CHECK(AllAt(*low, 10));
        CHECK(AllAt(*normal, 0));
        a->stop();
        b->stop();
    }

    TEST_CASE("the frame timer thread follows the group too") {
        auto group = std::make_shared<RenderThreadGroup>();
        wallpaper::ThreadTimer timer([](wallpaper::ThreadTimer::Clock::time_point now) {
            return now + std::chrono::milliseconds(20);
        });
        timer.SetThreadGroup(group);
        timer.Start();
        REQUIRE(WaitForThreads(*group, 1));
        CHECK(group->Set(10));
        CHECK(AllAt(*group, 10));
        CHECK(NameOf(group->Threads().front()) == "wek:timer");
        timer.Stop();
        CHECK(group->Threads().empty());
    }
}
