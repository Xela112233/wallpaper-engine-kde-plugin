#pragma once
// Fluid simulation (we-manager fluid-effect patch, main.xml FluidSimulation,
// SceneQuality::fluid).  No Qt, no Vulkan: the parser and the tests build it
// on its own.
//
// Wallpaper Engine's fluid simulation is an effect file like any other
// (effects/fluidsimulation/effect.json): targets under "fbos", a pass list
// that solves the fluid on them, two swaps at its end.  It carries its
// velocity, pressure and dye over between frames, which the fluid-targets
// patch (EffectFeedback.hpp) gives it.  This file reads the parts of an
// effect file that the renderer never read and that the fluid effect uses:
//
// * "conditions" on a pass and on an entry of its "bind" list: the pass is
//   drawn, the texture bound, only when the pass's shader combos meet them.
//   The form is a list of alternatives (or one alternative on its own); an
//   alternative is an object of combo names, each with a number the combo
//   must equal or with {"op": "eq"|"ge"|"gt"|"le"|"lt", "value": n}; all of an
//   alternative's combos must hold, and one alternative is enough.  A combo
//   the pass does not have holds for nothing.
// * "fit" on a target: the target's larger side is that many pixels, whatever
//   the layer's size (the simulation's grid is 256 on a 4K layer too); the
//   other side follows the layer's proportions.
// * "clear" on a target: the colour ("r g b a") the target starts with.
// * "functions": named actions a script runs with
//   IEffect.executeMaterialFunction(name).  The one action read is "clear",
//   which puts the targets it lists back to their start colour.
//
// With the setting off none of this is read, and an effect is parsed and
// built as before.  On, it is read for every effect of the scene and used for
// those that carry a picture over between frames.
//
// What is known of Wallpaper Engine's effect file, and from where, is in
// NOTES.md D48 (unit 7b): the field names and their meaning are facts taken
// from an open reimplementation that reads the shipped file, not from the
// file.  The code here is written from those facts, not from that project's.

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wallpaper::fluid
{

// What an effect file asks of one combo.  Comparing the pass's value of the
// combo with `value` has three outcomes; `accepts` says which of them meet
// the condition.  An operator this file does not know accepts none.
struct Condition {
    enum Outcome : std::uint8_t
    {
        Below = 1,
        Same  = 2,
        Above = 4,
    };
    std::string  combo; // upper case, as combos are
    std::int64_t value { 0 };
    std::uint8_t accepts { Same };

    bool Met(std::int64_t have) const {
        const std::uint8_t outcome = have < value ? Below : have > value ? Above : Same;
        return (accepts & outcome) != 0;
    }

    // The file's name for a comparison, as the outcomes it accepts.
    static std::uint8_t Accepted(std::string_view op) {
        if (op == "eq") return Same;
        if (op == "lt") return Below;
        if (op == "gt") return Above;
        if (op == "le") return Below | Same;
        if (op == "ge") return Above | Same;
        return 0;
    }
};

// A list of alternatives, each a list of conditions.  No alternative at all:
// nothing is asked.
using Conditions = std::vector<std::vector<Condition>>;
using Combos     = std::map<std::string, std::int64_t>;

// Whether a pass with these combos meets the conditions: it does when it
// meets every condition of at least one alternative.  A condition on a combo
// the pass does not have is not met.
inline bool ConditionsMet(const Conditions& alternatives, const Combos& combos) {
    if (alternatives.empty()) return true;
    for (const auto& alternative : alternatives) {
        bool every = true;
        for (const Condition& c : alternative) {
            const auto have = combos.find(c.combo);
            if (have == combos.end() || ! c.Met(have->second)) {
                every = false;
                break;
            }
        }
        if (every) return true;
    }
    return false;
}

inline Conditions ReadConditions(const nlohmann::json& owner) {
    Conditions out;
    if (! owner.is_object() || ! owner.contains("conditions")) return out;
    const auto& j           = owner.at("conditions");
    const auto  alternative = [&out](const nlohmann::json& a) {
        if (! a.is_object()) return;
        std::vector<Condition> all;
        for (const auto& [key, value] : a.items()) {
            Condition c;
            c.combo = key;
            for (char& ch : c.combo) ch = (char)std::toupper((unsigned char)ch);
            if (value.is_number()) {
                c.value = value.get<std::int64_t>();
            } else if (value.is_object() && value.contains("value") && value.at("value").is_number()) {
                c.value = value.at("value").get<std::int64_t>();
                if (value.contains("op"))
                    c.accepts = value.at("op").is_string()
                                    ? Condition::Accepted(value.at("op").get<std::string>())
                                    : std::uint8_t { 0 };
            } else {
                c.accepts = 0;
            }
            all.push_back(std::move(c));
        }
        // An alternative that asks nothing would be met by every pass.
        if (! all.empty()) out.push_back(std::move(all));
    };
    if (j.is_array())
        for (const auto& a : j) alternative(a);
    else
        alternative(j);
    return out;
}

// The colour a "clear" names: up to four numbers with blanks between them,
// red, green, blue and alpha.  What the text does not give is 0; reading
// stops at the first thing that is not a number, and a number that is not
// finite counts as 0.
inline std::array<float, 4> ReadColour(const std::string& text) {
    std::array<float, 4> colour { 0.0f, 0.0f, 0.0f, 0.0f };
    std::istringstream   in(text);
    for (float& channel : colour) {
        float v = 0.0f;
        if (! (in >> v)) break;
        channel = std::isfinite(v) ? v : 0.0f;
    }
    return colour;
}

// The largest "fit" taken from a file.  A grid of that many pixels is far
// beyond what a simulation is given (Wallpaper Engine's fluid asks for 256);
// the bound keeps a mistaken number from asking for gigabytes.
inline constexpr std::int32_t kLargestFit = 4096;

// The size of a target with "fit", for a layer of width x height (fit > 0).
// The rule: the target has the layer's proportions, and the longer of its two
// sides is `fit` pixels long, however large the layer is.  So the longer side
// is set, and the shorter one is `fit` times the layer's shorter side over its
// longer side, to the nearest pixel and never less than one.  A target's size
// divisor changes nothing here, since it leaves the proportions as they are.
// A layer without a size gives a square.
inline std::pair<std::int32_t, std::int32_t> FitSize(std::int32_t width, std::int32_t height,
                                                     std::int32_t fit) {
    const std::int64_t longer = std::clamp<std::int64_t>(fit, 1, kLargestFit);
    if (width <= 0 || height <= 0) return { (std::int32_t)longer, (std::int32_t)longer };
    const bool         landscape  = width >= height;
    const std::int64_t layerLong  = landscape ? width : height;
    const std::int64_t layerShort = landscape ? height : width;
    const std::int64_t shorter    = std::max<std::int64_t>(1, (longer * layerShort + layerLong / 2) / layerLong);
    return landscape ? std::pair<std::int32_t, std::int32_t> { (std::int32_t)longer, (std::int32_t)shorter }
                     : std::pair<std::int32_t, std::int32_t> { (std::int32_t)shorter, (std::int32_t)longer };
}

struct Fbo {
    std::string          name;
    std::int32_t         fit { 0 };
    bool                 has_clear { false };
    std::array<float, 4> clear { 0.0f, 0.0f, 0.0f, 0.0f };
};

// One entry of the effect file's "passes", commands too, in file order.
struct Pass {
    bool                    command { false };
    Conditions              conditions;
    std::vector<Conditions> binds; // one per entry of "bind", in order
};

struct Function {
    std::string              name;
    std::vector<std::string> fbos; // of action "clear"
};

struct Extras {
    std::vector<Fbo>      fbos;
    std::vector<Pass>     passes;
    std::vector<Function> functions;
    // Entries of "functions" that were not taken, each with the reason, for
    // the journal: one whose shape is not the one known, and one with an
    // action other than "clear".
    std::vector<std::string> functionsNotRead;

    const Fbo* FindFbo(std::string_view name) const {
        for (const auto& f : fbos)
            if (f.name == name) return &f;
        return nullptr;
    }
    // Whether any pass or bind has conditions.
    bool Conditional() const {
        return std::any_of(passes.begin(), passes.end(), [](const Pass& p) {
            return ! p.conditions.empty() ||
                   std::any_of(p.binds.begin(), p.binds.end(), [](const Conditions& c) {
                       return ! c.empty();
                   });
        });
    }
    // The entries of "passes" that are material passes, in order.
    std::vector<const Pass*> MaterialPasses() const {
        std::vector<const Pass*> out;
        for (const auto& p : passes)
            if (! p.command) out.push_back(&p);
        return out;
    }
};

inline Extras Read(const nlohmann::json& effect) {
    Extras out;
    if (! effect.is_object()) return out;
    if (effect.contains("fbos") && effect.at("fbos").is_array()) {
        for (const auto& j : effect.at("fbos")) {
            Fbo f;
            if (! j.is_object() || ! j.contains("name") || ! j.at("name").is_string()) {
                out.fbos.push_back(std::move(f));
                continue;
            }
            f.name = j.at("name").get<std::string>();
            if (j.contains("fit") && j.at("fit").is_number())
                f.fit = (std::int32_t)std::clamp<double>(j.at("fit").get<double>(), 0.0, (double)kLargestFit);
            if (j.contains("clear") && j.at("clear").is_string()) {
                f.has_clear = true;
                f.clear     = ReadColour(j.at("clear").get<std::string>());
            }
            out.fbos.push_back(std::move(f));
        }
    }
    if (effect.contains("passes") && effect.at("passes").is_array()) {
        for (const auto& j : effect.at("passes")) {
            Pass p;
            p.command = ! (j.is_object() && j.contains("material"));
            if (! p.command) {
                p.conditions = ReadConditions(j);
                if (j.contains("bind") && j.at("bind").is_array())
                    for (const auto& b : j.at("bind")) p.binds.push_back(ReadConditions(b));
            }
            out.passes.push_back(std::move(p));
        }
    }
    if (effect.contains("functions") && effect.at("functions").is_object()) {
        for (const auto& [name, j] : effect.at("functions").items()) {
            // What a function's entry looks like is known second-hand only,
            // so every part is asked for its type before it is read.
            if (! j.is_object() || ! j.contains("action") || ! j.at("action").is_string()) {
                out.functionsNotRead.push_back(name + " (no action named)");
                continue;
            }
            const std::string action = j.at("action").get<std::string>();
            if (action != "clear") {
                out.functionsNotRead.push_back(name + " (action '" + action + "')");
                continue;
            }
            Function f;
            f.name = name;
            if (j.contains("fbos") && j.at("fbos").is_array())
                for (const auto& n : j.at("fbos"))
                    if (n.is_string()) f.fbos.push_back(n.get<std::string>());
            out.functions.push_back(std::move(f));
        }
    }
    return out;
}

// The scene's per-pass overrides of an effect ("passes" in scene.json's
// effect entry).  The renderer takes entry i for the effect's i-th material
// pass, and takes none at all when there are more entries than material
// passes.  An effect file whose pass list holds commands can come with one
// entry per line of that list, commands included (the fluid effect has 18
// material passes and two swaps, twenty lines); the entries then go by the
// line they stand at.  For each material pass, the entry to take; empty when
// the list is not one entry per line of a pass list with commands, which
// leaves the renderer's own rule in place.
inline std::vector<std::int32_t> SceneEntryOfPass(const Extras& extras, std::size_t sceneEntries) {
    std::vector<std::int32_t> out;
    std::size_t               materials = 0;
    for (const auto& p : extras.passes) materials += p.command ? 0u : 1u;
    if (sceneEntries != extras.passes.size() || sceneEntries == materials) return out;
    for (std::size_t i = 0; i < extras.passes.size(); i++)
        if (! extras.passes[i].command) out.push_back((std::int32_t)i);
    return out;
}

// What stays of a pass list once the conditions are applied: for each
// material pass whether it is drawn, and for each of its binds whether it is
// bound.  combosOf(p) gives material pass p's combos.
struct Kept {
    std::vector<bool>              pass;
    std::vector<std::vector<bool>> bind;
    bool                           all { true };
};

template<typename CombosOf>
Kept Apply(const Extras& extras, std::size_t materialPasses, CombosOf&& combosOf) {
    Kept       kept;
    const auto passes = extras.MaterialPasses();
    kept.pass.assign(materialPasses, true);
    kept.bind.resize(materialPasses);
    for (std::size_t p = 0; p < materialPasses && p < passes.size(); p++) {
        const Pass& x           = *passes[p];
        const bool  conditional = ! x.conditions.empty() ||
                                  std::any_of(x.binds.begin(), x.binds.end(), [](const Conditions& c) {
                                      return ! c.empty();
                                  });
        if (! conditional) continue;
        const Combos combos = combosOf(p);
        kept.pass[p]        = ConditionsMet(x.conditions, combos);
        for (const auto& b : x.binds) kept.bind[p].push_back(ConditionsMet(b, combos));
        kept.all = kept.all && kept.pass[p] &&
                   std::all_of(kept.bind[p].begin(), kept.bind[p].end(), [](bool b) {
                       return b;
                   });
    }
    return kept;
}

} // namespace wallpaper::fluid
