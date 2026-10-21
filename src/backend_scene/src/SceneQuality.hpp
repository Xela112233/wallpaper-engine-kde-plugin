#pragma once
// Scene quality switches (we-manager texture-quality patch): what the screen's
// settings ask of a scene, read while it is parsed.  One struct, set through
// one property (PROPERTY_SCENE_QUALITY, "key=value;key=value"), held by
// SceneWallpaper and handed to WPSceneParser, which gives the texture parser
// its copy: every quality decision of a load reads that copy.  A later switch
// (volumetric lighting, reflections) is a field and a key here, not a new
// property.  No Qt, no Vulkan: tests build it on its own.
//
// textures (main.xml TextureQuality):
// * Full: every texture as authored.
// * Performance: one leading mip level left out (half the size per axis, a
//   quarter of the memory), as Wallpaper Engine's "High performance" texture
//   resolution brings a 4K image down to about 1080p.
// * Automatic: leading levels left out while the next level's image still
//   covers the output (targetWidth x targetHeight: the item's size times its
//   device pixel ratio times RenderScale), so nothing shown up to full screen
//   drops below the screen's own pixel density; with no target, none.
// A texture keeps at least one level, so one without mipmaps (Wallpaper
// Engine exempts those) is never smaller; sprite sheets and video textures
// are exempt.  Only the uploaded image shrinks: the texture's header, and
// with it g_TextureNResolution and mipmap_larger, stays as authored.

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

namespace wallpaper
{

struct SceneQuality {
    enum class Textures : std::uint8_t
    {
        Full        = 0,
        Performance = 1,
        Automatic   = 2,
    };
    Textures     textures { Textures::Full };
    std::int32_t targetWidth { 0 };
    std::int32_t targetHeight { 0 };

    bool operator==(const SceneQuality&) const = default;

    // "textures=2;targetWidth=1920;targetHeight=1080".  An unknown key is
    // left alone (a newer setting page, an older renderer), a bad value keeps
    // that field's default.
    static SceneQuality FromString(std::string_view text) {
        SceneQuality q;
        while (! text.empty()) {
            const auto end  = text.find(';');
            auto       pair = text.substr(0, end);
            text            = end == std::string_view::npos ? std::string_view {} : text.substr(end + 1);
            const auto eq   = pair.find('=');
            if (eq == std::string_view::npos) continue;
            const auto key = pair.substr(0, eq);
            long long  v   = 0;
            if (! ParseInt(pair.substr(eq + 1), v)) continue;
            if (key == "textures" && v >= 0 && v <= 2)
                q.textures = static_cast<Textures>(v);
            else if (key == "targetWidth" && v >= 0 && v <= 65535)
                q.targetWidth = (std::int32_t)v;
            else if (key == "targetHeight" && v >= 0 && v <= 65535)
                q.targetHeight = (std::int32_t)v;
        }
        return q;
    }

    std::string ToString() const {
        return "textures=" + std::to_string((int)textures) + ";targetWidth=" + std::to_string(targetWidth) +
               ";targetHeight=" + std::to_string(targetHeight);
    }

    static const char* TexturesName(Textures t) {
        switch (t) {
        case Textures::Performance: return "performance";
        case Textures::Automatic: return "automatic";
        default: return "full";
        }
    }

private:
    static bool ParseInt(std::string_view s, long long& out) {
        while (! s.empty() && s.front() == ' ') s.remove_prefix(1);
        while (! s.empty() && s.back() == ' ') s.remove_suffix(1);
        if (s.empty() || s.size() > 9) return false;
        long long v = 0;
        for (char c : s) {
            if (c < '0' || c > '9') return false;
            v = v * 10 + (c - '0');
        }
        out = v;
        return true;
    }
};

// How many leading mip levels of one slot to leave out.  mipCount: the
// slot's levels; w0 x h0: its first level; mapW x mapH: the image inside it
// (the header's map size; 0, or larger than the level, means the whole
// level); exempt: a sprite sheet or a video texture.
inline std::int32_t LeadingMipsToDrop(const SceneQuality& q, std::int32_t mipCount, std::int32_t w0,
                                      std::int32_t h0, std::int32_t mapW, std::int32_t mapH, bool exempt) {
    if (exempt || mipCount <= 1 || w0 <= 0 || h0 <= 0) return 0;
    std::int32_t want = 0;
    switch (q.textures) {
    case SceneQuality::Textures::Full: return 0;
    case SceneQuality::Textures::Performance: want = 1; break;
    case SceneQuality::Textures::Automatic: {
        if (q.targetWidth <= 0 || q.targetHeight <= 0) return 0;
        const std::int64_t imgW = (mapW > 0 && mapW <= w0) ? mapW : w0;
        const std::int64_t imgH = (mapH > 0 && mapH <= h0) ? mapH : h0;
        while (want + 1 < mipCount && (imgW >> (want + 1)) >= q.targetWidth &&
               (imgH >> (want + 1)) >= q.targetHeight)
            want++;
        break;
    }
    }
    return std::clamp<std::int32_t>(want, 0, mipCount - 1);
}

} // namespace wallpaper
