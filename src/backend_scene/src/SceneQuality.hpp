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
//
// The switches of the scene-quality patch.  Each one's default is "as the
// scene asks", which is what the renderer did before the patch, and a
// default is never written into the property's text: with nothing switched,
// the text, the struct's decisions and so the parsed scene are the ones of
// the tree without the patch.
// * volumetrics (main.xml Volumetrics): Off, no light gets the volumetric
//   chain (no per-light nodes, no half-resolution targets, no passes): the
//   state WEKDE_VOLUMETRICS=force-off already gives.
// * reflections (main.xml Reflections): Off, a 3D scene with camera paths
//   gets no reflected camera, so no reflection passes and no blur chain, and
//   the scene-sized _rt_Reflection and _rt_Reflection_blur are not
//   registered; a material that samples _rt_Reflection reads a 4x4 target
//   of that name, which stays transparent black.
// * flatMsaa (main.xml MsaaOffForFlatScenes): Off, a scene drawn with the
//   orthographic camera that holds no model and nothing drawn in perspective
//   renders without multisampling, whatever MsaaMode says, as Wallpaper
//   Engine does since 2.3; a 3D scene, and a 2D one with such an object, is
//   left alone.

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
    enum class Volumetrics : std::uint8_t
    {
        Off   = 0,
        Scene = 1,
    };
    enum class Reflections : std::uint8_t
    {
        Off   = 0,
        Scene = 1,
    };
    enum class FlatMsaa : std::uint8_t
    {
        AsSet = 0,
        Off   = 1,
    };
    Textures     textures { Textures::Full };
    std::int32_t targetWidth { 0 };
    std::int32_t targetHeight { 0 };
    Volumetrics  volumetrics { Volumetrics::Scene };
    Reflections  reflections { Reflections::Scene };
    FlatMsaa     flatMsaa { FlatMsaa::AsSet };
    // Effect feedback (fluid-targets patch, main.xml EffectFeedback), key
    // "feedback": off, the scene is parsed and drawn as before the patch.
    // On, an effect that samples one of its own targets before drawing into
    // it in the same frame keeps those targets from frame to frame
    // (EffectFeedback.hpp), and shaders that declare g_PointerPositionLast or
    // g_Frametime get them.  Named in the text only when on, like the
    // switches above.
    bool         feedback { false };
    // Fluid simulation (fluid-effect patch, main.xml FluidSimulation), key
    // "fluid": off, nothing of that patch runs.  On, effect feedback is on
    // with it (the text need not name both), and an effect that carries over
    // between frames is built with the parts of its file that Wallpaper
    // Engine's fluid simulation uses (EffectFluid.hpp) and reads the cursor
    // as that effect does.  Named in the text only when on.
    bool         fluid { false };

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
            else if (key == "volumetrics" && v >= 0 && v <= 1)
                q.volumetrics = static_cast<Volumetrics>(v);
            else if (key == "reflections" && v >= 0 && v <= 1)
                q.reflections = static_cast<Reflections>(v);
            else if (key == "flatMsaa" && v >= 0 && v <= 1)
                q.flatMsaa = static_cast<FlatMsaa>(v);
            else if (key == "feedback" && v >= 0 && v <= 1)
                q.feedback = v == 1;
            else if (key == "fluid" && v >= 0 && v <= 1)
                q.fluid = v == 1;
        }
        if (q.fluid) q.feedback = true;
        return q;
    }

    // A switch of the scene-quality patch is named only when it is off its
    // default, so the text of a scene nobody switched is the old one.
    std::string ToString() const {
        std::string text = "textures=" + std::to_string((int)textures) + ";targetWidth=" + std::to_string(targetWidth) +
                           ";targetHeight=" + std::to_string(targetHeight);
        if (volumetrics != Volumetrics::Scene) text += ";volumetrics=" + std::to_string((int)volumetrics);
        if (reflections != Reflections::Scene) text += ";reflections=" + std::to_string((int)reflections);
        if (flatMsaa != FlatMsaa::AsSet) text += ";flatMsaa=" + std::to_string((int)flatMsaa);
        if (feedback) text += ";feedback=1";
        if (fluid) text += ";fluid=1";
        return text;
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
