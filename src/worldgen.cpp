#include "worldgen.h"
#include <sp2/logging.h>
#include <sp2/script/environment.h>
#include <algorithm>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif//__GNUC__
#include "FastNoiseLite.h"
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif//__GNUC__

sp::P<sp::script::Environment> mapgen_env;
FastNoiseLite noise;

static float sampleAt(int x, int y)
{
    float value = noise.GetNoise(float(x), float(y)) * 5.0f;
    value -= (y - std::sin(x*0.01f) * 50.0f - QuadTree::SIZE*3/4.0f) * 0.1f;
    return value;
}

static void genStep(QuadTree& tree, int x, int y, int depth)
{
    auto value = sampleAt(x, y);
    auto step_size = 1 << depth;
    float optimize_range = step_size + 2.0f;
    if (depth == 0 ||
        (value < -optimize_range && sampleAt(x + step_size, y) < -optimize_range && sampleAt(x, y + step_size) < -optimize_range && sampleAt(x + step_size, y + step_size) < -optimize_range) ||
        (value > 1.0f+optimize_range && sampleAt(x + step_size, y) > 1.0f+optimize_range && sampleAt(x, y + step_size) > 1.0f+optimize_range && sampleAt(x + step_size, y + step_size) > 1.0f+optimize_range)
    ) {
        tree.setAtDepth(x, y, std::clamp(int(value*255), 0, 255), depth);
    } else {
        genStep(tree, x, y, depth-1);
        genStep(tree, x+(step_size>>1), y, depth-1);
        genStep(tree, x, y+(step_size>>1), depth-1);
        genStep(tree, x+(step_size>>1), y+(step_size>>1), depth-1);
    }
}

class NoiseObject : public sp::script::BindingObject
{
public:

protected:
    void onRegisterScriptBindings(sp::script::BindingClass& script_binding_class) override
    {
    }

    FastNoiseLite noise;
};

void generateWorld(QuadTree& tree)
{
    mapgen_env.destroy();
    mapgen_env = new sp::script::Environment();
    mapgen_env->setGlobal("Noise", [](lua_State* lua) -> int {
        auto no = new NoiseObject();
        luaL_checktype(lua, 1, LUA_TTABLE);
        sp::script::pushToLua(lua, no);
        return 1;
    });
    noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
    noise.SetFrequency(0.1);

    genStep(tree, 0, 0, QuadTree::MAX_DEPTH);

    // for(int y=0;y<tree.SIZE;y++) {
    //     for(int x=0;x<tree.SIZE;x++) {
    //         int value = noise.GetNoise(float(x), float(y)) * 1000;
    //         value -= (y - std::sin(x*0.01) * 50 - tree.SIZE*3/4) * 100;
    //         value = std::clamp(value, 0, 255);
    //         tree.setAtDepth(x, y, value, 0);
    //     }
    //     LOG(Debug, y);
    // }

    // noise.SetSeed(13370);
    // for(int y=0;y<tree.SIZE;y++) {
    //     for(int x=0;x<tree.SIZE;x++) {
    //         auto value = tree.get(x, y);
    //         if (uint8_t(value) > 128 && noise.GetNoise(float(x), float(y)) > 0) {
    //             tree.set(x, y, value | 0x100);
    //         }
    //     }
    //     LOG(Debug, y);
    // }
}