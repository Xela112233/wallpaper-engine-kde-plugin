#pragma once
#include "VulkanPass.hpp"
#include <string>

#include "Vulkan/Device.hpp"
#include "Vulkan/TextureCache.hpp"
#include "Scene/Scene.h"

namespace wallpaper
{
namespace vulkan
{

class CopyPass : public VulkanPass {
public:
    struct Desc {
        std::string src;
        std::string dst;
        bool        flipY { false };
        // Effect feedback (fluid-targets patch): the copy is a command of a
        // feedback effect (sceneToRenderGraph sets it, and only with the
        // setting on).  Only such a copy can join two formats.
        bool feedback { false };

        ImageParameters vk_src;
        ImageParameters vk_dst;
        // The slots behind src/dst; the handle copies above are refreshed
        // from them by rebindImages().
        RtSlot* src_slot { nullptr };
        RtSlot* dst_slot { nullptr };
    };

    CopyPass(const Desc&);
    virtual ~CopyPass();

    void prepare(Scene&, const Device&, RenderingResources&) override;
    void execute(const Device&, RenderingResources&) override;
    void destory(const Device&, RenderingResources&) override;

    // Re-copies src/dst handles from their slots when a slot's generation
    // moved or its image appeared or vanished.
    void rebindImages();

    // Accessor so the pass-output cache safety analyzer can see CopyPass
    // writes — otherwise a COPY into an aliased pingpong RT looks like
    // "static bytes" to a cached upstream pass, leading to cumulative
    // feedback (see the Lucy cloud acceleration bug).
    const Desc& desc() const { return m_desc; }

private:
    Desc       m_desc;
    BoundImage m_src_bound, m_dst_bound;
};

// Effect feedback (fluid-targets patch): whether a copy has to convert,
// because it is a feedback effect's command and its two render targets differ
// in format.  vkCmdCopyImage copies bytes and wants formats of one texel
// size; a float target of a feedback effect copied onto an 8-bit one (or the
// other way round) is a blit instead.  For every other copy, which is every
// copy with the setting off, the formats are not looked at and the answer is
// no, as before the patch.
inline bool CopyConverts(bool feedback_copy, const RtSlot* src, const RtSlot* dst) {
    return feedback_copy && src != nullptr && dst != nullptr && src->key.format != dst->key.format;
}

// The image barriers before a copy wait for the last readers and writers of
// both images; the previous TOP_OF_PIPE source waited for nothing, and the
// destination's UNDEFINED -> TRANSFER_DST discard could overtake a sampler
// still reading it in the frame before.  The access mask names reads and
// writes, which TOP_OF_PIPE cannot carry.
inline VkPipelineStageFlags CopyBarrierSourceStages() {
    return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
           VK_PIPELINE_STAGE_TRANSFER_BIT;
}
inline VkAccessFlags CopyBarrierSourceAccess() {
    return VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
}

} // namespace vulkan
} // namespace wallpaper