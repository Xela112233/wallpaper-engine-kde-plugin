#pragma once
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

// Effect feedback (we-manager fluid-targets patch, EffectFeedback.hpp): how
// the images of an effect's persistent targets change places between frames.
// Kept free of Vulkan so the doctests run it on plain structs.
namespace wallpaper::vulkan
{

// One effect's moves: `first` takes the image `second` holds, all at once (the
// moves of one effect are a permutation of its slots, so every slot that
// gives its image away also takes one).  A slot's generation goes up, which
// is how the passes holding copies of its handles notice (BoundImageStale).
// Slot needs `image` (movable) and `generation`.  `scratch` is reused between
// calls so a frame allocates nothing.
template<typename Slot, typename Image>
void MoveSlotImages(std::span<const std::pair<Slot*, Slot*>> moves, std::vector<Image>& scratch) {
    if (moves.size() == 2 && moves[0].first == moves[1].second &&
        moves[0].second == moves[1].first) {
        // The usual case: two images exchanged.
        std::swap(moves[0].first->image, moves[0].second->image);
        ++moves[0].first->generation;
        ++moves[0].second->generation;
        return;
    }
    scratch.clear();
    for (const auto& m : moves) scratch.push_back(std::move(m.second->image));
    for (std::size_t i = 0; i < moves.size(); i++) {
        moves[i].first->image = std::move(scratch[i]);
        ++moves[i].first->generation;
    }
    scratch.clear();
}

// Whether a set of moves can be carried out: every slot that takes an image
// gives its own to exactly one other, so no image is lost or held twice.
template<typename Slot>
bool MovesArePermutation(std::span<const std::pair<Slot*, Slot*>> moves) {
    for (const auto& m : moves) {
        std::size_t takes = 0, gives = 0, gives_m = 0;
        for (const auto& o : moves) {
            takes += o.first == m.first ? 1u : 0u;
            gives += o.second == m.first ? 1u : 0u;
            gives_m += o.second == m.second ? 1u : 0u;
        }
        if (m.first == nullptr || m.second == nullptr || takes != 1 || gives != 1 || gives_m != 1)
            return false;
    }
    return true;
}

} // namespace wallpaper::vulkan
