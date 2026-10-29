#include <sp2/engine.h>
#include <sp2/window.h>
#include <sp2/random.h>
#include <sp2/logging.h>
#include <sp2/graphics/gui/scene.h>
#include <sp2/graphics/gui/theme.h>
#include <sp2/graphics/gui/loader.h>
#include <sp2/graphics/scene/graphicslayer.h>
#include <sp2/graphics/scene/basicnoderenderpass.h>
#include <sp2/graphics/scene/collisionrenderpass.h>
#include <sp2/graphics/textureManager.h>
#include <sp2/graphics/meshdata.h>
#include <sp2/graphics/meshbuilder.h>
#include <sp2/collision/2d/box.h>
#include <sp2/collision/2d/chains.h>
#include <sp2/scene/scene.h>
#include <sp2/scene/node.h>
#include <sp2/scene/camera.h>
#include <sp2/io/cameraCapture.h>
#include <sp2/io/http/request.h>
#include <sp2/io/keybinding.h>
#include <sp2/stringutil/base64.h>

#include "photoProcess.h"
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif//__GNUC__
#include "FastNoiseLite.h"
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif//__GNUC__


sp::io::Keybinding key_move_x{"MOVE_X", {"-left", "right"}};
sp::io::Keybinding key_move_y{"MOVE_Y", {"-down", "up"}};
sp::io::Keybinding key_dig{"DIG", {"space"}};

sp::P<sp::Window> window;

class PhotoDrawingTexture : public sp::OpenGLTexture
{
public:
    PhotoDrawingTexture(sp::string filename) : sp::OpenGLTexture(sp::Texture::Texture::Type::Dynamic, "Camera")
    {
        smooth = true;

        sp::Image img;
        img.loadFromFile(filename);
        if (img.getSize().x > 0)
        {
            auto result = processDrawing(img, false);
            if (result.succes) {
                auto mesh_size = sp::Vector2f{0.5f,0.5f};
                
                sp::MeshData::Vertices vertices;
                sp::MeshData::Indices indices{0,1,2,2,1,3};
                vertices.reserve(4);

                sp::Vector2f uv0{float(result.rect[0].x)/float(img.getSize().x),float(result.rect[0].y)/float(img.getSize().y)};
                sp::Vector2f uv1{float(result.rect[1].x)/float(img.getSize().x),float(result.rect[1].y)/float(img.getSize().y)};
                sp::Vector2f uv2{float(result.rect[2].x)/float(img.getSize().x),float(result.rect[2].y)/float(img.getSize().y)};
                sp::Vector2f uv3{float(result.rect[3].x)/float(img.getSize().x),float(result.rect[3].y)/float(img.getSize().y)};
                LOG(Debug, result.rect[0]);
                LOG(Debug, result.rect[1]);
                LOG(Debug, result.rect[2]);
                LOG(Debug, result.rect[3]);
                
                vertices.emplace_back(sp::Vector3f(-mesh_size.x, -mesh_size.y, 0.0f), sp::Vector3f(0.0f,0.0f,1.0f), uv0);
                vertices.emplace_back(sp::Vector3f( mesh_size.x, -mesh_size.y, 0.0f), sp::Vector3f(0.0f,0.0f,1.0f), uv1);
                vertices.emplace_back(sp::Vector3f(-mesh_size.x,  mesh_size.y, 0.0f), sp::Vector3f(0.0f,0.0f,1.0f), uv3);
                vertices.emplace_back(sp::Vector3f( mesh_size.x,  mesh_size.y, 0.0f), sp::Vector3f(0.0f,0.0f,1.0f), uv2);
                
                mesh = sp::MeshData::create(std::move(vertices), std::move(indices));
                setImage(std::move(img));
            }
        }
    }

    std::shared_ptr<sp::MeshData> mesh;
};

class CameraTexture : public sp::OpenGLTexture
{
public:
    sp::io::CameraCapture cc{0, {640, 480}};

    CameraTexture() : sp::OpenGLTexture(sp::Texture::Texture::Type::Dynamic, "Camera")
    {
    }
    
    void bind() override {
        sp::Image img = cc.getFrame();
        // if (cc.getState() == sp::io::CameraCapture::State::Closed)
            img.loadFromFile("20261017143044.png");
        if (img.getSize().x > 0)
        {
            processDrawing(img, true);
            setImage(std::move(img));
        }

        sp::OpenGLTexture::bind();
    }

    void upload()
    {
        sp::Image img = cc.getFrame();
        auto png_buffer = img.saveToBuffer();
        LOG(Debug, png_buffer.size());
#ifdef EMSCRIPTEN
        sp::io::http::Request req("daid.eu", 443);
#else
        sp::io::http::Request req("daid.eu", 80);
#endif
        auto post_data = sp::stringutil::base64::encode(sp::string(reinterpret_cast<char*>(png_buffer.data()), png_buffer.size()));
        auto res = req.post("/gamedraw/upload.php", post_data);
    }
};

class QuadTree
{
public:
    uint32_t get(int x, int y) {
        if (x < 0 || y < 0 || x >= SIZE || y >= SIZE) return DEFAULT_VALUE;

        unsigned int depth = MAX_DEPTH - 1;
        auto node = &data[0];
        while(true) {
            auto idx = 0;
            if (x & (1 << depth)) { idx |= 1; }
            if (y & (1 << depth)) { idx |= 2; }
            auto value = (*node)[idx];
            if (!(value & TAG_BRANCH)) return value;
            depth -= 1;
            node = &data[value&~TAG_BRANCH];
        }
    }

    void set(int x, int y, uint32_t new_value)
    {
        sp2assert(!(new_value & TAG_BRANCH), "TAG_BRANCH set on value set in quadtree...");
        if (x < 0 || y < 0 || x >= SIZE || y >= SIZE) return;
        setImpl(0, x, y, MAX_DEPTH - 1, new_value);
    }

    std::shared_ptr<sp::MeshData> createDebugMesh()
    {
        sp::MeshBuilder mb;
        std::function<void(float,float,float,unsigned)> f = [this, &mb, &f](float x, float y, float size, unsigned idx) {
            auto& node = data[idx];
            for(int n=0; n<4; n++) {
                auto nx = x;
                auto ny = y;
                if (n & 1) nx += size;
                if (n & 2) ny += size;
                if (node[n] & TAG_BRANCH) {
                    f(nx, ny, size / 2, node[n] & ~TAG_BRANCH);
                } else {
                    float f = float(node[n] & 0xFF) / 255.0;
                    mb.addQuad(
                        {nx+size*0.1f, ny+size*0.1f, 0.0f},
                        {nx+size*0.9f, ny+size*0.1f, 0.0f},
                        {nx+size*0.1f, ny+size*0.9f, 0.0f},
                        {nx+size*0.9f, ny+size*0.9f, 0.0f},
                        {0.0f, f, 1.0f-f}
                    );
                }
            }
        };
        f(0, 0, SIZE / 2, 0);
        return mb.create();
    }

    static constexpr unsigned int MAX_DEPTH = 6;
    static constexpr int SIZE = 1 << MAX_DEPTH;
private:
    bool setImpl(uint32_t node_idx, int x, int y, int depth, uint32_t new_value)
    {
        int idx = 0;
        if (x & (1 << depth)) idx |= 1;
        if (y & (1 << depth)) idx |= 2;
        if (depth == 0) {
            data[node_idx][idx] = new_value;
        } else {
            auto value = data[node_idx][idx];
            if (value & TAG_BRANCH) {
                if (setImpl(value&~TAG_BRANCH, x, y, depth-1, new_value)) {
                    freeNode(value&~TAG_BRANCH);
                    data[node_idx][idx] = new_value;
                }
            } else if (value != new_value) {
                auto new_node_idx = allocNode(value);
                data[node_idx][idx] = new_node_idx | TAG_BRANCH;
                setImpl(new_node_idx, x, y, depth - 1, new_value);
            }
        }
        return data[node_idx][0] == data[node_idx][1] && data[node_idx][0] == data[node_idx][2] && data[node_idx][0] == data[node_idx][3];
    }

    uint32_t allocNode(uint32_t value)
    {
        if (freeList != TAG_BRANCH) {
            auto res = freeList;
            freeList = data[res][0];
            data[res] = {value,value,value,value}; 
            return res;
        }
        auto res = data.size();
        data.push_back({value,value,value,value});
        return res;
    }

    void freeNode(uint32_t index)
    {
        data[index][0] = freeList;
        freeList = index;
    }

    static constexpr uint32_t TAG_BRANCH = 0x80000000;
    static constexpr uint32_t DEFAULT_VALUE = 0x00000000;
    using Node = std::array<uint32_t, 4>;
    std::vector<Node> data {{DEFAULT_VALUE,DEFAULT_VALUE,DEFAULT_VALUE,DEFAULT_VALUE}};
    uint32_t freeList = TAG_BRANCH;
};

class MarchingSquaresTexture : public sp::OpenGLTexture
{
public:
    MarchingSquaresTexture() : sp::OpenGLTexture(sp::Texture::Type::Dynamic, "") {}

    void update(sp::Image&& img) { setImage(std::move(img)); }
};

class MarchingSquaresNode : public sp::Node
{
public:
    MarchingSquaresNode(sp::P<sp::Node> parent) : sp::Node(parent)
    {
        FastNoiseLite noise;
        noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
        noise.SetFrequency(0.1);

        for(int y=0;y<tree.SIZE;y++) {
            for(int x=0;x<tree.SIZE;x++) {
                int value = noise.GetNoise(float(x), float(y)) * 1000;
                value -= (y - tree.SIZE*3/4) * 100;
                value = std::clamp(value, 0, 255);
                tree.set(x, y, value);
            }
        }

        noise.SetSeed(13370);
        for(int y=0;y<tree.SIZE;y++) {
            for(int x=0;x<tree.SIZE;x++) {
                auto value = tree.get(x, y);
                if (uint8_t(value) > 128 && noise.GetNoise(float(x), float(y)) > 0) {
                    tree.set(x, y, value | 0x100);
                }
            }
        }

        updateMesh();
    }

    void updateMesh()
    {
        for(auto p : sp::Rect2i({0,0}, {CHUNK_SIDE_COUNT,CHUNK_SIDE_COUNT})) {
            updateMesh(p);
        }

        // render_data.type = sp::RenderData::Type::Normal;
        // render_data.shader = sp::Shader::get("internal:normal_as_color.shader");
        // render_data.mesh = tree.createDebugMesh();
    }

    void updateMesh(sp::Vector2i chunk_pos)
    {
        auto m = [](int a, int b) -> float {
            a -= 128;
            b -= 128;
            auto d = a - b;
            sp2assert(a > 0, "?");
            sp2assert(d > 0, "?");
            auto res = float(a) / float(d+1);
            sp2assert(res > 0.0f, "?");
            sp2assert(res < 1.0f, "?");
            return res;
        };
        sp::collision::Chains2D shape;
        for(auto p : sp::Rect2i({0,0}, {CHUNK_SIZE, CHUNK_SIZE})) {
            int flags = 0;
            auto a = uint8_t(tree.get(p.x+0+chunk_pos.x*CHUNK_SIZE, p.y+0+chunk_pos.y*CHUNK_SIZE));
            auto b = uint8_t(tree.get(p.x+1+chunk_pos.x*CHUNK_SIZE, p.y+0+chunk_pos.y*CHUNK_SIZE));
            auto c = uint8_t(tree.get(p.x+0+chunk_pos.x*CHUNK_SIZE, p.y+1+chunk_pos.y*CHUNK_SIZE));
            auto d = uint8_t(tree.get(p.x+1+chunk_pos.x*CHUNK_SIZE, p.y+1+chunk_pos.y*CHUNK_SIZE));
            if (a > 128) flags |= 0x01;
            if (b > 128) flags |= 0x02;
            if (c > 128) flags |= 0x04;
            if (d > 128) flags |= 0x08;
            switch(flags) {
            case 0: break;
            case 1: shape.addLine(sp::Vector2f{p.x+0.0f,p.y+m(a,c)},sp::Vector2f{p.x+m(a,b),p.y+0.0f}); break;
            case 2: shape.addLine(sp::Vector2f{p.x+1.0f-m(b,a),p.y+0.0f},sp::Vector2f{p.x+1.0f,p.y+m(b,d)}); break;
            case 3: shape.addLine(sp::Vector2f{p.x+0.0f,p.y+m(a,c)},sp::Vector2f{p.x+1.0f,p.y+m(b,d)}); break;
            case 4: shape.addLine(sp::Vector2f{p.x+m(c,d),p.y+1.0f},sp::Vector2f{p.x+0.0f,p.y+1.0f-m(c,a)}); break;
            case 5: shape.addLine(sp::Vector2f{p.x+m(c,d),p.y+1.0f},sp::Vector2f{p.x+m(a,b),p.y+0.0f}); break;
            case 6:
                if (b*c >= a*d) {
                    shape.addLine(sp::Vector2f{p.x+1.0f-m(b,a),p.y+0.0f},sp::Vector2f{p.x+0.0f,p.y+1.0f-m(c,a)});
                    shape.addLine(sp::Vector2f{p.x+m(c,d),p.y+1.0f},sp::Vector2f{p.x+1.0f,p.y+m(b,d)});
                } else {
                    shape.addLine(sp::Vector2f{p.x+1.0f-m(b,a),p.y+0.0f},sp::Vector2f{p.x+1.0f,p.y+m(b,d)});
                    shape.addLine(sp::Vector2f{p.x+m(c,d),p.y+1.0f},sp::Vector2f{p.x+0.0f,p.y+1.0f-m(c,a)});
                }
                break;
            case 7: shape.addLine(sp::Vector2f{p.x+m(c,d),p.y+1.0f},sp::Vector2f{p.x+1.0f,p.y+m(b,d)}); break;
            case 8: shape.addLine(sp::Vector2f{p.x+1.0f,p.y+1.0f-m(d,b)},sp::Vector2f{p.x+1.0f-m(d,c),p.y+1.0f}); break;
            case 9:
                if (a*d >= b*c) {
                    shape.addLine(sp::Vector2f{p.x+0.0f,p.y+m(a,c)},sp::Vector2f{p.x+1.0f-m(d,c),p.y+1.0f});
                    shape.addLine(sp::Vector2f{p.x+1.0f,p.y+1.0f-m(d,b)},sp::Vector2f{p.x+m(a,b),p.y+0.0f});
                } else {
                    shape.addLine(sp::Vector2f{p.x+0.0f,p.y+m(a,c)},sp::Vector2f{p.x+m(a,b),p.y+0.0f});
                    shape.addLine(sp::Vector2f{p.x+1.0f,p.y+1.0f-m(d,b)},sp::Vector2f{p.x+1.0f-m(d,c),p.y+1.0f});
                }
                break;
            case 10: shape.addLine(sp::Vector2f{p.x+1.0f-m(b,a),p.y+0.0f},sp::Vector2f{p.x+1.0f-m(d,c),p.y+1.0f}); break;
            case 11: shape.addLine(sp::Vector2f{p.x+0.0f,p.y+m(a,c)},sp::Vector2f{p.x+1.0f-m(d,c),p.y+1.0f}); break;
            case 12: shape.addLine(sp::Vector2f{p.x+1.0f,p.y+1.0f-m(d,b)},sp::Vector2f{p.x+0.0f,p.y+1.0f-m(c,a)}); break;
            case 13: shape.addLine(sp::Vector2f{p.x+1.0f,p.y+1.0f-m(d,b)},sp::Vector2f{p.x+m(a,b),p.y+0.0f}); break;
            case 14: shape.addLine(sp::Vector2f{p.x+1.0f-m(b,a),p.y+0.0f},sp::Vector2f{p.x+0.0f,p.y+1.0f-m(c,a)}); break;
            case 15: break;
            default: LOG(Debug, "??", flags);
            }
        }

        auto& ci = chunk_info[chunk_pos.x + chunk_pos.y * CHUNK_SIDE_COUNT];

        if (!ci.texture) ci.texture = new MarchingSquaresTexture();
        sp::Image img{{CHUNK_SIZE+1, CHUNK_SIZE+1}};
        for(auto p : sp::Rect2i({0,0}, {CHUNK_SIZE+1, CHUNK_SIZE+1})) {
            img.getPtr()[p.x+p.y*(CHUNK_SIZE+1)] = tree.get(p.x+chunk_pos.x*CHUNK_SIZE, p.y+chunk_pos.y*CHUNK_SIZE) | 0xFF000000;
        }

        ci.texture->update(std::move(img));
        if (!ci.node) {
            sp::MeshData::Vertices vertices;
            sp::MeshData::Indices indices{0,1,2, 2,1,3};
            static const float u = float(CHUNK_SIZE) / float(CHUNK_SIZE+1);
            static const float v = float(1) / float(CHUNK_SIZE+1);
            vertices.emplace_back(sp::Vector3f{0,0,0}, sp::Vector2f{0,v});
            vertices.emplace_back(sp::Vector3f{CHUNK_SIZE,0,0}, sp::Vector2f{u,v});
            vertices.emplace_back(sp::Vector3f{0,CHUNK_SIZE,0}, sp::Vector2f{0,1});
            vertices.emplace_back(sp::Vector3f{CHUNK_SIZE,CHUNK_SIZE,0}, sp::Vector2f{u,1});
            ci.node = new Node(getParent());
            ci.node->setPosition(sp::Vector2d(chunk_pos*CHUNK_SIZE));
            ci.node->render_data.type = sp::RenderData::Type::Normal;
            ci.node->render_data.shader = sp::Shader::get("marchingsquares.shader");
            ci.node->render_data.mesh = sp::MeshData::create(std::move(vertices), std::move(indices));
            ci.node->render_data.texture = ci.texture;
            ci.node->render_data.shader->bind();
            ci.node->render_data.shader->setUniform("tile_texture", sp::texture_manager.get("tiles.png"), 1);
        }

        ci.node->setCollisionShape(shape);
    }

    void digAt(sp::Vector2d center, double radius)
    {
        auto cp = sp::Vector2i(std::floor(center.x), std::floor(center.y));
        auto extend = int(std::ceil(radius)) + 1;
        for(auto p : sp::Rect2i{cp - sp::Vector2i{extend, extend}, sp::Vector2i{extend*2, extend*2}}) {
            auto wp = sp::Vector2d(p);
            auto dist = (wp-center).length();
            auto signed_distance = dist - radius;
            if (signed_distance < 0) {
                auto adjust = int(signed_distance * 255);
                auto old_value = tree.get(p.x, p.y);
                auto value = (std::max(uint8_t(old_value) + adjust, 0) & 0xFF) | (old_value & 0xFF00);
                if (uint8_t(value) <= 128 && uint8_t(old_value) > 128) {
                    auto test = new sp::Node(getParent());
                    test->setPosition(sp::Vector2d(p));
                    test->render_data.type = sp::RenderData::Type::Normal;
                    test->render_data.shader = sp::Shader::get("internal:color.shader");
                    test->render_data.mesh = sp::MeshData::createQuad({0.2f,0.2f});
                    test->render_data.color = sp::HsvColor(sp::random(0, 360), 100, 100);
                    sp::collision::Box2D shape{0.2f,0.2f};
                    shape.restitution = 1.1;
                    test->setCollisionShape(shape);
                    test->setLinearVelocity({sp::random(-1,1),sp::random(-1,1)});
                }
                tree.set(p.x, p.y, value);
            }
        }
        updateMesh();
    }

private:
    static constexpr int CHUNK_SIZE = 8;
    static constexpr int CHUNK_SIDE_COUNT = QuadTree::SIZE/CHUNK_SIZE;
    struct ChunkInfo {
        MarchingSquaresTexture* texture = nullptr;
        sp::P<sp::Node> node;
    };
    std::array<ChunkInfo, CHUNK_SIDE_COUNT*CHUNK_SIDE_COUNT> chunk_info;

    QuadTree tree;
};

class PlayerPawn : public sp::Node
{
public:
    PlayerPawn(sp::P<sp::Node> parent) : sp::Node(parent)
    {
        render_data.type = sp::RenderData::Type::Normal;
        render_data.shader = sp::Shader::get("internal:color.shader");
        render_data.mesh = sp::MeshData::createQuad({0.5f,0.5f});
        render_data.color = sp::Color(0.2, 1.0, 0.2);

        sp::collision::Box2D shape{0.5f,0.5f};
        shape.fixed_rotation = true;
        setCollisionShape(shape);
    }

    void onFixedUpdate() override
    {
        auto velocity = getLinearVelocity2D();
        velocity.x = velocity.x * .5 + (key_move_x.getValue() * 10.0) * .5;
        if (key_move_y.getValue() != 0.0)
            velocity.y = velocity.y * .5 + (key_move_y.getValue() * 10.0) * .5;
        velocity.y -= 9.8 * sp::Engine::fixed_update_delta;
        setLinearVelocity(velocity);
    }
};

class Scene : public sp::Scene
{
public:
    Scene() : sp::Scene("MAIN")
    {
        auto camera = new sp::Camera(getRoot());
        camera->setOrtographic({15.0,15.0});
        setDefaultCamera(camera);

        // auto pdt = new PhotoDrawingTexture("2.png");
        // for(int cnt=0; cnt<10;cnt++) {
        //     auto n = new sp::Node(getRoot());
        //     n->render_data.type = sp::RenderData::Type::Subtractive;
        //     n->render_data.texture = pdt;
        //     n->render_data.shader = sp::Shader::get("internal:basic.shader");
        //     n->render_data.mesh = pdt->mesh;
        //     sp::collision::Box2D shape{0.5, 0.5};
        //     n->setCollisionShape(shape);
        //     n->setPosition({sp::random(-.1,.1),sp::random(-.1,.1)});
        //     n->setRotation(sp::random(0, 360));
        // }
        msn = new MarchingSquaresNode(getRoot());
        cursor = new sp::Node(getRoot());
        cursor->render_data.type = sp::RenderData::Type::Normal;
        cursor->render_data.shader = sp::Shader::get("internal:color.shader");
        cursor->render_data.mesh = sp::MeshData::createQuad({0.2f,0.2f});
        // for(int n=0; n<10; n++) {
        //     auto test = new sp::Node(getRoot());
        //     test->render_data.type = sp::RenderData::Type::Normal;
        //     test->render_data.shader = sp::Shader::get("internal:color.shader");
        //     test->render_data.mesh = sp::MeshData::createQuad({0.2f,0.2f});
        //     sp::collision::Box2D shape{0.2f,0.2f};
        //     shape.restitution = 1.1;
        //     test->setCollisionShape(shape);
        //     test->setLinearVelocity({1,1});
        // }
        player = new PlayerPawn(getRoot());
        player->setPosition({QuadTree::SIZE / 2, QuadTree::SIZE});
    }

    void onUpdate(float delta) override
    {
        getCamera()->setPosition(player->getPosition2D());

        if (key_dig.getDown()) {
            msn->digAt(player->getPosition2D(), 2.0);
        }
    }

    bool onPointerMove(sp::Ray3d ray, int id) override
    {
        auto p = sp::Vector2d(ray.start.x, ray.start.y);
        p -= msn->getPosition2D();
        auto pi = sp::Vector2i(std::round(p.x), std::round(p.y));
        cursor->setPosition(msn->getPosition2D() + sp::Vector2d(pi));
        return false;
    }

    bool onPointerDown(sp::io::Pointer::Button button, sp::Ray3d ray, int id) override
    {
        onPointerDrag(ray, id);
        return true;
    }

    void onPointerDrag(sp::Ray3d ray, int id) override {
        auto p = sp::Vector2d(ray.start.x, ray.start.y);
        msn->digAt(p, 0.2);
    }

    void onPointerUp(sp::Ray3d ray, int id) override {}

    sp::P<MarchingSquaresNode> msn;
    sp::P<sp::Node> cursor;
    sp::P<PlayerPawn> player;
};

int main(int argc, char** argv)
{
    sp::P<sp::Engine> engine = new sp::Engine();

    //Create resource providers, so we can load things.
    sp::io::ResourceProvider::createDefault();

    //Disable or enable smooth filtering by default, enabling it gives nice smooth looks, but disabling it gives a more pixel art look.
    sp::texture_manager.setDefaultSmoothFiltering(false);

    //Create a window to render on, and our engine.
    window = new sp::Window();
#if !defined(DEBUG) && !defined(EMSCRIPTEN)
    //window->setFullScreen(true);
#endif
    window->setClearColor(sp::Color(1, 1, 1));
    window->setClearColor(sp::Color(0, 0, 0));

    sp::gui::Theme::loadTheme("default", "gui/theme/basic.theme.txt");
    new sp::gui::Scene(sp::Vector2d(640, 480));

    sp::P<sp::SceneGraphicsLayer> scene_layer = new sp::SceneGraphicsLayer(1);
    scene_layer->addRenderPass(new sp::BasicNodeRenderPass());
#ifdef DEBUG
    scene_layer->addRenderPass(new sp::CollisionRenderPass());
#endif
    window->addLayer(scene_layer);

    //TODO: Create your own scene(s) here and populate them with nodes.
    auto scene = new Scene();

    // auto n = new sp::Node(scene->getRoot());
    // n->render_data.type = sp::RenderData::Type::Normal;
    // n->render_data.mesh = sp::MeshData::createQuad({2.0*640.0/480.0, 2});
    // n->render_data.shader = sp::Shader::get("internal:basic.shader");
    // auto ct = new CameraTexture();
    // n->render_data.texture = ct;

    // auto gui = sp::gui::Loader::load("gui/photoshoot.gui", "PHOTOSHOOT");
    // gui->getWidgetWithID("CAPTURE")->setEventCallback([ct, gui](sp::Variant) {
    //     ct->upload();
    //     gui->getWidgetWithID("CAPTURE")->hide();
    // });

    engine->run();

    return 0;
}
