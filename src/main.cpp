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
#include <sp2/stringutil/base64.h>

#include "photoProcess.h"


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
    int8_t sample(int x, int y) {
        if (x < 0 || y < 0 || x >= SIZE || y >= SIZE) return -127;

        uint32_t idx = 0;
        unsigned int depth = MAX_DEPTH;
        auto value = data[idx];
        
        return -127;
    }

private:
    unsigned int MAX_DEPTH = 10;
    int SIZE = 1 << MAX_DEPTH;
    static constexpr uint32_t TAG_BRANCH = 0x80000000;
    std::vector<uint32_t> data{0};
};

class MarchingSquaresNode : public sp::Node
{
public:
    MarchingSquaresNode(sp::P<sp::Node> parent) : sp::Node(parent)
    {
        data.fill(-128);

        for(int y=0;y<WIDTH;y++) {
            for(int x=0;x<WIDTH;x++) {
                data[x+y*WIDTH] = sp::irandom(-128, 127);
            }
        }
        for(int x=0;x<WIDTH;x++) {
            data[x] = 127;
            data[x+(HEIGHT-1)*WIDTH] = 127;
        }
        for(int y=0;y<HEIGHT;y++) {
            data[0+y*WIDTH] = 127;
            data[(WIDTH-1)+y*WIDTH] = 127;
        }

        // for(int n=0; n<5; n++)
        //     data[(3+n)+(HEIGHT/2+n)*WIDTH] = 127 - n * 10;
        updateMesh();
    }

    void updateMesh()
    {
        const float MESH_SCALE = 0.5f;
        auto m = [](int8_t a, int8_t b) -> float {
            auto d = a - b;
            sp2assert(a > 0, "?");
            sp2assert(d > 0, "?");
            auto res = float(a) / float(d+1);
            sp2assert(res > 0.0f, "?");
            sp2assert(res < 1.0f, "?");
            return res;
        };
        sp::collision::Chains2D shape;
        for(int y=0;y<WIDTH-1;y++) {
            for(int x=0;x<WIDTH-1;x++) {
                int flags = 0;
                auto a = data[(x+0)+(y+0)*WIDTH];
                auto b = data[(x+1)+(y+0)*WIDTH];
                auto c = data[(x+0)+(y+1)*WIDTH];
                auto d = data[(x+1)+(y+1)*WIDTH];
                if (a > 0) flags |= 0x01;
                if (b > 0) flags |= 0x02;
                if (c > 0) flags |= 0x04;
                if (d > 0) flags |= 0x08;
                switch(flags) {
                case 0: break;
                case 1: shape.addLine(sp::Vector2f{x+0.0f,y+m(a,c)}*MESH_SCALE,sp::Vector2f{x+m(a,b),y+0.0f}*MESH_SCALE); break;
                case 2: shape.addLine(sp::Vector2f{x+1.0f-m(b,a),y+0.0f}*MESH_SCALE,sp::Vector2f{x+1.0f,y+m(b,d)}*MESH_SCALE); break;
                case 3: shape.addLine(sp::Vector2f{x+0.0f,y+m(a,c)}*MESH_SCALE,sp::Vector2f{x+1.0f,y+m(b,d)}*MESH_SCALE); break;
                case 4: shape.addLine(sp::Vector2f{x+m(c,d),y+1.0f}*MESH_SCALE,sp::Vector2f{x+0.0f,y+1.0f-m(c,a)}*MESH_SCALE); break;
                case 5: shape.addLine(sp::Vector2f{x+m(c,d),y+1.0f}*MESH_SCALE,sp::Vector2f{x+m(a,b),y+0.0f}*MESH_SCALE); break;
                case 6:
                    if (b*c >= a*d) {
                        shape.addLine(sp::Vector2f{x+1.0f-m(b,a),y+0.0f}*MESH_SCALE,sp::Vector2f{x+0.0f,y+1.0f-m(c,a)}*MESH_SCALE);
                        shape.addLine(sp::Vector2f{x+m(c,d),y+1.0f}*MESH_SCALE,sp::Vector2f{x+1.0f,y+m(b,d)}*MESH_SCALE);
                    } else {
                        shape.addLine(sp::Vector2f{x+1.0f-m(b,a),y+0.0f}*MESH_SCALE,sp::Vector2f{x+1.0f,y+m(b,d)}*MESH_SCALE);
                        shape.addLine(sp::Vector2f{x+m(c,d),y+1.0f}*MESH_SCALE,sp::Vector2f{x+0.0f,y+1.0f-m(c,a)}*MESH_SCALE);
                    }
                    break;
                case 7: shape.addLine(sp::Vector2f{x+m(c,d),y+1.0f}*MESH_SCALE,sp::Vector2f{x+1.0f,y+m(b,d)}*MESH_SCALE); break;
                case 8: shape.addLine(sp::Vector2f{x+1.0f,y+1.0f-m(d,b)}*MESH_SCALE,sp::Vector2f{x+1.0f-m(d,c),y+1.0f}*MESH_SCALE); break;
                case 9:
                    if (a*d >= b*c) {
                        shape.addLine(sp::Vector2f{x+0.0f,y+m(a,c)}*MESH_SCALE,sp::Vector2f{x+1.0f-m(d,c),y+1.0f}*MESH_SCALE);
                        shape.addLine(sp::Vector2f{x+1.0f,y+1.0f-m(d,b)}*MESH_SCALE,sp::Vector2f{x+m(a,b),y+0.0f}*MESH_SCALE);
                    } else {
                        shape.addLine(sp::Vector2f{x+0.0f,y+m(a,c)}*MESH_SCALE,sp::Vector2f{x+m(a,b),y+0.0f}*MESH_SCALE);
                        shape.addLine(sp::Vector2f{x+1.0f,y+1.0f-m(d,b)}*MESH_SCALE,sp::Vector2f{x+1.0f-m(d,c),y+1.0f}*MESH_SCALE);
                    }
                    break;
                case 10: shape.addLine(sp::Vector2f{x+1.0f-m(b,a),y+0.0f}*MESH_SCALE,sp::Vector2f{x+1.0f-m(d,c),y+1.0f}*MESH_SCALE); break;
                case 11: shape.addLine(sp::Vector2f{x+0.0f,y+m(a,c)}*MESH_SCALE,sp::Vector2f{x+1.0f-m(d,c),y+1.0f}*MESH_SCALE); break;
                case 12: shape.addLine(sp::Vector2f{x+1.0f,y+1.0f-m(d,b)}*MESH_SCALE,sp::Vector2f{x+0.0f,y+1.0f-m(c,a)}*MESH_SCALE); break;
                case 13: shape.addLine(sp::Vector2f{x+1.0f,y+1.0f-m(d,b)}*MESH_SCALE,sp::Vector2f{x+m(a,b),y+0.0f}*MESH_SCALE); break;
                case 14: shape.addLine(sp::Vector2f{x+1.0f-m(b,a),y+0.0f}*MESH_SCALE,sp::Vector2f{x+0.0f,y+1.0f-m(c,a)}*MESH_SCALE); break;
                case 15: break;
                default: LOG(Debug, "??", flags);
                }
            }
        }

        sp::MeshBuilder mb;
        for(const auto& loop : shape.loops) {
            auto p0 = loop.back();
            for(auto p1 : loop) {
                auto v = p1 - p0;
                auto n = sp::Vector2f{v.y,-v.x}.normalized() * 0.05f;
                mb.addQuad(p0 + n, p1 + n, p0 - n, p1 - n);
                p0 = p1;
            }
        }

        render_data.type = sp::RenderData::Type::Normal;
        render_data.shader = sp::Shader::get("internal:color.shader");
        render_data.mesh = mb.create();

        setCollisionShape(shape);
    }

    static constexpr int WIDTH = 16;
    static constexpr int HEIGHT = 16;
    std::array<int8_t, WIDTH*HEIGHT> data;
};

class Scene : public sp::Scene
{
public:
    Scene() : sp::Scene("MAIN")
    {
        auto camera = new sp::Camera(getRoot());
        camera->setOrtographic({5.0,5.0});
        setDefaultCamera(camera);

        // auto pdt = new PhotoDrawingTexture("20261017143044.png");
        // for(int cnt=0; cnt<10;cnt++) {
        //     auto n = new sp::Node(scene->getRoot());
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
        msn->setPosition({-4,-4});
        cursor = new sp::Node(getRoot());
        cursor->render_data.type = sp::RenderData::Type::Normal;
        cursor->render_data.shader = sp::Shader::get("internal:color.shader");
        cursor->render_data.mesh = sp::MeshData::createQuad({0.2f,0.2f});
        for(int n=0; n<10; n++) {
            auto test = new sp::Node(getRoot());
            test->render_data.type = sp::RenderData::Type::Normal;
            test->render_data.shader = sp::Shader::get("internal:color.shader");
            test->render_data.mesh = sp::MeshData::createQuad({0.2f,0.2f});
            sp::collision::Box2D shape{0.2f,0.2f};
            shape.restitution = 1.0;
            test->setCollisionShape(shape);
            test->setLinearVelocity({1,1});
        }
    }

    bool onPointerMove(sp::Ray3d ray, int id) override
    {
        auto p = sp::Vector2d(ray.start.x, ray.start.y);
        p -= msn->getPosition2D();
        auto pi = sp::Vector2i(std::round(p.x*2), std::round(p.y*2));
        cursor->setPosition(msn->getPosition2D() + sp::Vector2d(pi)*0.5);
        return false;
    }

    bool onPointerDown(sp::io::Pointer::Button button, sp::Ray3d ray, int id) override
    {
        onPointerDrag(ray, id);
        return true;
    }

    void onPointerDrag(sp::Ray3d ray, int id) override {
        auto p = sp::Vector2d(ray.start.x, ray.start.y);
        p -= msn->getPosition2D();
        auto pi = sp::Vector2i(std::round(p.x*2), std::round(p.y*2));
        cursor->setPosition(msn->getPosition2D() + sp::Vector2d(pi)*0.5);
        if (pi.x >= 0 && pi.y >= 0 && pi.x < msn->WIDTH && pi.y < msn->HEIGHT) {
            msn->data[pi.x+pi.y*msn->WIDTH] -= 1;
            msn->updateMesh();
        }
    }

    void onPointerUp(sp::Ray3d ray, int id) override {}

    sp::P<MarchingSquaresNode> msn;
    sp::P<sp::Node> cursor;
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
