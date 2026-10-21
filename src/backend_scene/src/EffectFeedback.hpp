#pragma once
// Effect feedback targets (we-manager fluid-targets patch, main.xml
// EffectFeedback, SceneQuality::feedback).  No Qt, no Vulkan: the parser and
// the tests build it on its own.
//
// A Wallpaper Engine effect file lists passes in order.  A pass samples the
// targets its "bind" list names and draws into its "target"; in between,
// {"command": "copy"} copies one target onto another and {"command": "swap"}
// exchanges two of them.  The targets are the effect's own "fbos".  An effect
// that carries a picture over from one frame to the next (a cursor trail, a
// fluid) samples a target before anything has drawn into it in this frame:
// what it reads then is what the frame before left there.  That read is what
// this file calls a cross-frame read, and an effect with one is a feedback
// effect.
//
// Without the patch (and with the setting off) the renderer gives every
// declared target a reusable image, which any later pass of the frame with a
// target of the same size may take over, runs a swap as a copy from source to
// target, never runs a command placed after the last pass, and lets a pass
// that samples its own target read a half-size copy made just before.  A
// feedback effect works there only by accident.
//
// Analyse() reads one effect's steps and says what a feedback effect needs:
// * which targets must keep their image from frame to frame (persistent):
//   every target with a cross-frame read, and every target tied to one by a
//   swap or as its twin;
// * a second image (twin) for a target that one pass both samples and draws
//   into: the pass samples the image that holds the target's contents and
//   draws into the other one, which then is the target;
// * every sample and every draw named by the image it means at that point of
//   the frame, with the swaps taken into account, so a swap costs nothing;
// * how the images change places when the frame is done (moves), so the next
//   frame finds each target's newest contents under the name the effect file
//   uses.  A pair swapped an odd number of times a frame exchanges its two
//   images; an even number leaves them.
// An effect without a cross-frame read gets feedback == false, and the caller
// leaves it exactly as it was.

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wallpaper::feedback
{

// One declared target ("fbos" entry).  Two targets can be swapped only when
// their shape (size divisor and format, as the caller counts them) is equal.
struct Fbo {
    std::string   name;
    std::uint32_t shape { 0 };
};

// One entry of the effect's pass list, in file order.
struct Step {
    enum class Kind : std::uint8_t
    {
        Pass,
        Copy,
        Swap,
    };
    Kind kind { Kind::Pass };
    // Pass: the names its "bind" list samples, in that order.
    // Copy and Swap: the command's "source".
    std::vector<std::string> reads;
    // Pass: its "target" (empty: the layer's chain).  Copy and Swap: the
    // command's "target".
    std::string target;
};

// An image: a declared target's own, or its twin.
struct Image {
    std::string fbo;
    bool        twin { false };

    bool operator==(const Image&) const = default;
    bool operator<(const Image& o) const { return fbo != o.fbo ? fbo < o.fbo : twin < o.twin; }
};

struct Plan {
    // True when some declared target is sampled or copied from before
    // anything of this frame was drawn into it.
    bool feedback { false };

    // Per step.  A name that is not a declared target (the chain input,
    // "previous") stays as written, with image == false.
    struct Ref {
        bool        image { false };
        Image       img;
        std::string name; // the authored name
    };
    struct Resolved {
        std::vector<Ref> reads;
        Ref              target;
        // A swap of two declared targets is done by naming, and a copy of an
        // image onto itself is nothing: neither becomes a pass.
        bool dropped { false };
        // A swap that cannot be done by naming (an undeclared name, or two
        // targets of different shape) stays the copy from source to target
        // it was.
        bool copies { false };
    };
    std::vector<Resolved> steps;

    std::set<Image> persistent; // images that keep their contents between frames
    std::set<Image> twins;      // second images the plan asks for (twin == true)

    // After the last pass of a frame: `first` takes the image `second` holds.
    // All moves happen at once.  Only persistent images move.
    std::vector<std::pair<Image, Image>> moves;
};

inline Plan Analyse(const std::vector<Fbo>& fbos, const std::vector<Step>& steps) {
    Plan plan;
    plan.steps.resize(steps.size());

    std::map<std::string, std::uint32_t> shape;
    for (const auto& f : fbos) shape.emplace(f.name, f.shape);
    auto declared = [&](const std::string& n) {
        return shape.count(n) != 0;
    };

    // Which image each declared name means right now, the images drawn into
    // so far in this frame, and for an image with a twin, its partner.
    std::map<std::string, Image> now;
    for (const auto& f : fbos) now[f.name] = Image { f.name, false };
    const std::map<std::string, Image> start = now;
    std::set<Image>                    written, crossRead;

    // Images that exchange contents belong together: swap partners and twins.
    std::map<Image, Image> group;
    auto find = [&](Image i) {
        while (group.count(i) != 0 && ! (group.at(i) == i)) i = group.at(i);
        return i;
    };
    auto join = [&](const Image& a, const Image& b) {
        const Image ra = find(a), rb = find(b);
        group[a]  = ra;
        group[b]  = rb;
        group[rb] = ra;
    };

    auto refOf = [&](const std::string& n) {
        Plan::Ref r;
        r.name = n;
        if (declared(n)) {
            r.image = true;
            r.img   = now.at(n);
        }
        return r;
    };
    auto read = [&](const Plan::Ref& r) {
        if (r.image && written.count(r.img) == 0) crossRead.insert(r.img);
    };

    for (std::size_t i = 0; i < steps.size(); i++) {
        const Step& s   = steps[i];
        auto&       out = plan.steps[i];
        for (const auto& n : s.reads) out.reads.push_back(refOf(n));
        out.target = refOf(s.target);

        if (s.kind == Step::Kind::Swap) {
            const bool byName = out.reads.size() == 1 && out.reads[0].image && out.target.image &&
                                shape.at(s.reads[0]) == shape.at(s.target);
            if (byName) {
                if (s.reads[0] != s.target) {
                    std::swap(now.at(s.reads[0]), now.at(s.target));
                    join(out.reads[0].img, out.target.img);
                }
                out.dropped = true;
                continue;
            }
            out.copies = true;
        }

        for (const auto& r : out.reads) read(r);

        if (s.kind != Step::Kind::Pass) {
            // A copy: source onto target.
            if (out.target.image) {
                if (out.reads.size() == 1 && out.reads[0].image && out.reads[0].img == out.target.img)
                    out.dropped = true;
                else
                    written.insert(out.target.img);
            }
            continue;
        }

        if (! out.target.image) continue;
        const bool samplesOwnTarget =
            std::any_of(out.reads.begin(), out.reads.end(), [&](const Plan::Ref& r) {
                return r.image && r.img == out.target.img;
            });
        if (samplesOwnTarget) {
            // Draw into the other image of the pair; it is the target from here on.
            const Image other { out.target.img.fbo, ! out.target.img.twin };
            plan.twins.insert(Image { other.fbo, true });
            join(out.target.img, other);
            out.target.img   = other;
            now.at(s.target) = other;
        }
        written.insert(out.target.img);
    }

    // A cross-frame read carries something only when the effect also draws
    // into that image, or into one that changes places with it; a target
    // that is sampled and never drawn is just empty, with or without this.
    std::set<Image> roots;
    for (const auto& c : crossRead) {
        const Image root = find(c);
        if (std::any_of(written.begin(), written.end(), [&](const Image& w) {
                return find(w) == root;
            }))
            roots.insert(root);
    }
    plan.feedback = ! roots.empty();
    if (! plan.feedback) return plan;

    // Persistent: every image of a group one of whose images is read across
    // frames.
    std::set<Image> all;
    for (const auto& f : fbos) all.insert(Image { f.name, false });
    for (const auto& t : plan.twins) all.insert(t);
    for (const auto& img : all)
        if (roots.count(find(img)) != 0) plan.persistent.insert(img);

    // The next frame starts with the authored names again, so the image a
    // name started on takes the image the name ended on.  Images no name
    // started on (twins) hold nothing a frame reads before it draws, and take
    // what is left over.
    std::set<Image> takers, givers;
    for (const auto& [name, first] : start) {
        if (plan.persistent.count(first) == 0) continue;
        const Image& last = now.at(name);
        takers.insert(first);
        givers.insert(last);
        if (! (first == last)) plan.moves.emplace_back(first, last);
    }
    std::vector<Image> freeTakers, freeGivers;
    for (const auto& img : plan.persistent) {
        if (takers.count(img) == 0) freeTakers.push_back(img);
        if (givers.count(img) == 0) freeGivers.push_back(img);
    }
    // Pair what is left within each group, so only images of one shape meet.
    for (const auto& t : freeTakers) {
        auto g = std::find_if(freeGivers.begin(), freeGivers.end(), [&](const Image& c) {
            return find(c) == find(t);
        });
        if (g == freeGivers.end()) continue;
        if (! (t == *g)) plan.moves.emplace_back(t, *g);
        freeGivers.erase(g);
    }
    return plan;
}

// Whether an "fbos" entry's format asks for floating point.  Wallpaper Engine
// names formats by their channels and bits ("rgba8888"); the float ones end
// in the bit count and an f ("rgba16161616f", "rg1616f", "r32f"), which is
// all this looks for.  The renderer's render targets are RGBA8 or RGBA16F,
// so every float format becomes RGBA16F (a half float, also where the name
// asks for 32 bits), on every target of a feedback effect that is declared
// so, kept or not: a field that goes negative (a velocity) does not survive
// RGBA8, in the passes between two kept targets either.  In an effect that
// carries nothing over, and with the setting off, the format is not read.
inline bool IsFloatFormat(std::string_view format) {
    if (format.size() < 2) return false;
    const char last = format.back(), before = format[format.size() - 2];
    return (last == 'f' || last == 'F') && before >= '0' && before <= '9';
}

} // namespace wallpaper::feedback
