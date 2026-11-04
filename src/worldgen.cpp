#include "worldgen.h"
#include <sp2/logging.h>
#include <sp2/random.h>
#include <sp2/script/environment.h>
#include <sp2/stringutil/convert.h>
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
    return mapgen_env->call<float>("sample", sp::Vector2f(x, y)).value();
}

static void genStep(QuadTree& tree, int x, int y, int depth)
{
    auto value = sampleAt(x, y);
    auto step_size = 1 << depth;
    float optimize_range = step_size + 3.0f;
    if (depth == 0 ||
        (value < -optimize_range && sampleAt(x + step_size, y) < -optimize_range && sampleAt(x, y + step_size) < -optimize_range && sampleAt(x + step_size, y + step_size) < -optimize_range) ||
        (value > optimize_range && sampleAt(x + step_size, y) > optimize_range && sampleAt(x, y + step_size) > optimize_range && sampleAt(x + step_size, y + step_size) > optimize_range)
    ) {
        tree.setAtDepth(x, y, std::clamp(int((value+1.0f)*128), 0, 255), depth);
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
    NoiseObject(int seed_offset)
    {
        noise.SetSeed(1337 + seed_offset * 17);
        noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
    }

    bool setParam(const sp::string& key, const sp::string& value)
    {
        if (key == "frequency") {
            noise.SetFrequency(sp::stringutil::convert::toFloat(value));
            return true;
        }
        return false;
    }

protected:
    float sample(sp::Vector2f pos)
    {
        return noise.GetNoise(pos.x, pos.y);
    }

    void onRegisterScriptBindings(sp::script::BindingClass& script_binding_class) override
    {
        script_binding_class.bind("get", &NoiseObject::sample);
    }

    FastNoiseLite noise;
};
sp::PList<NoiseObject> noise_list;

sp::string generateWorld(QuadTree& tree, sp::string code)
{
    mapgen_env.destroy();
    while(!noise_list.empty()) for(auto n : noise_list) n.destroy();
    mapgen_env = new sp::script::Environment();
    mapgen_env->setGlobal("Noise", [](lua_State* lua) -> int {
        auto no = new NoiseObject(noise_list.size());
        noise_list.add(no);
        luaL_checktype(lua, 1, LUA_TTABLE);
        lua_pushnil(lua);
        while(lua_next(lua, 1)) {
            if (lua_isstring(lua, -2)) {
                auto key = lua_tostring(lua, -2);
                auto value = lua_tostring(lua, -1);
                no->setParam(key, value);
            }
            lua_pop(lua, 1);
        }
        sp::script::pushToLua(lua, no);
        return 1;
    });
    mapgen_env->setGlobal("sin", static_cast<float(*)(float)>(std::sin));
    mapgen_env->setGlobal("cos", static_cast<float(*)(float)>(std::cos));
    mapgen_env->setGlobal("random", sp::random);
    auto res = mapgen_env->run(code);
    if (res.isErr()) {
        return res.error();
    }
    auto res2 = mapgen_env->call<float>("sample", sp::Vector2f(0, 0));
    if (res2.isErr()) {
        return res2.error();
    }
    genStep(tree, 0, 0, QuadTree::MAX_DEPTH);

    return "";
}