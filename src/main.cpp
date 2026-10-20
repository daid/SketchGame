#include <sp2/engine.h>
#include <sp2/window.h>
#include <sp2/random.h>
#include <sp2/logging.h>
#include <sp2/io/resourceProvider.h>
#include <sp2/graphics/gui/scene.h>
#include <sp2/graphics/gui/theme.h>
#include <sp2/graphics/gui/loader.h>
#include <sp2/graphics/scene/graphicslayer.h>
#include <sp2/graphics/scene/basicnoderenderpass.h>
#include <sp2/graphics/scene/collisionrenderpass.h>
#include <sp2/graphics/textureManager.h>
#include <sp2/graphics/meshdata.h>
#include <sp2/graphics/image/draw.h>
#include <sp2/graphics/image/blur.h>
#include <sp2/graphics/image/convert.h>
#include <sp2/graphics/image/threshold.h>
#include <sp2/graphics/image/resize.h>
#include <sp2/collision/2d/box.h>
#include <sp2/collision/2d/chains.h>
#include <sp2/scene/scene.h>
#include <sp2/scene/node.h>
#include <sp2/scene/camera.h>
#include <sp2/io/keybinding.h>
#include <sp2/io/cameraCapture.h>
#include <sp2/io/http/request.h>
#include <sp2/tween.h>
#include <sp2/stringutil/base64.h>
#include <array>
#include <optional>
#include <numeric>
#include <bitset>

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

class MarchingSquaresNode : public sp::Node
{
public:
    MarchingSquaresNode(sp::P<sp::Node> parent) : sp::Node(parent)
    {
        data.fill(0);

        for(int y=0;y<WIDTH;y++) {
            for(int x=0;x<WIDTH;x++) {
                data[x+y*WIDTH] = sp::irandom(0, 255);
            }
        }
        for(int x=0;x<WIDTH;x++) {
            data[x] = 255;
            data[x+(HEIGHT-1)*WIDTH] = 255;
        }
        for(int y=0;y<HEIGHT;y++) {
            data[0+y*WIDTH] = 255;
            data[(WIDTH-1)+y*WIDTH] = 255;
        }

        for(int n=0; n<5; n++)
            data[(3+n*2)+(HEIGHT/2)*WIDTH] = 255 - n * 10;
        updateMesh();
    }

    void updateMesh()
    {
        auto m = [](uint8_t a, uint8_t b) -> float {
            auto d = a - b;
            sp2assert(a > 127, "?");
            sp2assert(d > 0, "?");
            return float(a - 127) / float(d+1);
        };
        std::vector<std::pair<sp::Vector2f, sp::Vector2f>> lines;
        for(int y=0;y<WIDTH-1;y++) {
            for(int x=0;x<WIDTH-1;x++) {
                int flags = 0;
                auto a = data[(x+0)+(y+0)*WIDTH];
                auto b = data[(x+1)+(y+0)*WIDTH];
                auto c = data[(x+0)+(y+1)*WIDTH];
                auto d = data[(x+1)+(y+1)*WIDTH];
                if (a > 127) flags |= 0x01;
                if (b > 127) flags |= 0x02;
                if (c > 127) flags |= 0x04;
                if (d > 127) flags |= 0x08;
                switch(flags) {
                case 0: break;
                case 1: lines.push_back({{x+0.0f,y+m(a,c)},{x+m(a,b),y+0.0f}}); break;
                case 2: lines.push_back({{x+1.0f-m(b,a),y+0.0f},{x+1.0f,y+m(b,d)}}); break;
                case 3: lines.push_back({{x+0.0f,y+m(a,c)},{x+1.0f,y+m(b,d)}}); break;
                case 4: lines.push_back({{x+m(c,d),y+1.0f},{x+0.0f,y+1.0f-m(c,a)}}); break;
                case 5: lines.push_back({{x+m(c,d),y+1.0f},{x+m(a,b),y+0.0f}}); break;
                case 6:
                    lines.push_back({{x+1.0f-m(b,a),y+0.0f},{x+0.0f,y+1.0f-m(c,a)}});
                    lines.push_back({{x+m(c,d),y+1.0f},{x+1.0f,y+m(b,d)}});
                    break;
                case 7: lines.push_back({{x+m(c,d),y+1.0f},{x+1.0f,y+m(b,d)}}); break;
                case 8: lines.push_back({{x+1.0f,y+1.0f-m(d,b)},{x+1.0f-m(d,c),y+1.0f}}); break;
                case 9:
                    lines.push_back({{x+0.0f,y+m(a,c)},{x+1.0f-m(d,c),y+1.0f}});
                    lines.push_back({{x+1.0f,y+1.0f-m(d,b)},{x+m(a,b),y+0.0f}});
                    break;
                case 10: lines.push_back({{x+1.0f-m(b,a),y+0.0f},{x+1.0f-m(d,c),y+1.0f}}); break;
                case 11: lines.push_back({{x+0.0f,y+m(a,c)},{x+1.0f-m(d,c),y+1.0f}}); break;
                case 12: lines.push_back({{x+1.0f,y+1.0f-m(d,b)},{x+0.0f,y+1.0f-m(c,a)}}); break;
                case 13: lines.push_back({{x+1.0f,y+1.0f-m(d,b)},{x+m(a,b),y+0.0f}}); break;
                case 14: lines.push_back({{x+1.0f-m(b,a),y+0.0f},{x+0.0f,y+1.0f-m(c,a)}}); break;
                case 15: break;
                default: LOG(Debug, "??", flags);
                }
            }
        }

        sp::collision::Chains2D shape;
        for(auto& line : lines)
            shape.chains.push_back({line.first, line.second});
        setCollisionShape(shape);
    }

    static constexpr int WIDTH = 16;
    static constexpr int HEIGHT = 16;
    std::array<uint8_t, WIDTH*HEIGHT> data;
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
    auto scene = new sp::Scene("TEST");
    auto camera = new sp::Camera(scene->getRoot());
    camera->setOrtographic({5.0,5.0});
    scene->setDefaultCamera(camera);

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
    (new MarchingSquaresNode(scene->getRoot()))->setPosition({-8,-8});

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
