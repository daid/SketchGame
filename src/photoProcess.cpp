#include "photoProcess.h"

#include <sp2/graphics/image/resize.h>
#include <sp2/graphics/image/convert.h>
#include <sp2/graphics/image/threshold.h>
#include <sp2/graphics/image/draw.h>

static float findEdge(const sp::Image8& img, sp::Ray2i ray)
{
    if (ray.start.x < 0 || ray.start.y < 0 || ray.end.x < 0 || ray.end.y < 0) return 0.0f;
    if (ray.start.x >= img.getSize().x || ray.start.y >= img.getSize().y || ray.end.x >= img.getSize().x || ray.end.y >= img.getSize().y) return 0.0f;
    auto delta = ray.end-ray.start;
    auto stepcount = std::max(std::abs(delta.x), std::abs(delta.y));
    if (stepcount < 1) return 0.0f;
    auto min_value = std::numeric_limits<uint8_t>::max();
    auto max_value = std::numeric_limits<uint8_t>::min();
    for(int step=0; step<=stepcount; step++) {
        auto p = ray.start+delta*step/stepcount;
        auto value = img.getPtr()[p.x+p.y*img.getSize().x];
        min_value = std::min(min_value, value);
        max_value = std::max(max_value, value);
    }
    auto threshold = (min_value + max_value) / 2;
    for(int step=0; step<=stepcount; step++) {
        auto p = ray.start+delta*step/stepcount;
        auto value = img.getPtr()[p.x+p.y*img.getSize().x];
        if (value > threshold) return float(step) / float(stepcount);
    }
    return 1.0f;
}

std::vector<ImageRect> findRectangles(const sp::Image8& img)
{
    static const int DOWNSCALE = 4;
    static const int O = 2;
    //Convert to downscaled greyscale
    auto smallgrey = sp::image::resize(img, img.getSize() / DOWNSCALE);

    //Do edge detection on the downscaled image
    sp::Image8 edges(smallgrey.getSize());
    auto size = smallgrey.getSize();
    for(int y=O; y<size.y-O;y++) {
        for(int x=O; x<size.x-O;x++) {
            int value = 
                smallgrey.getPtr()[(x-O)+(y-O)*size.x] * -1 +
                smallgrey.getPtr()[(x+0)+(y-O)*size.x] * -1 +
                smallgrey.getPtr()[(x+O)+(y-O)*size.x] * -1 +
                smallgrey.getPtr()[(x-O)+(y+0)*size.x] * -1 +
                smallgrey.getPtr()[(x+0)+(y+0)*size.x] * 8 +
                smallgrey.getPtr()[(x+O)+(y+0)*size.x] * -1 + 
                smallgrey.getPtr()[(x-O)+(y+O)*size.x] * -1 +
                smallgrey.getPtr()[(x+0)+(y+O)*size.x] * -1 +
                smallgrey.getPtr()[(x+O)+(y+O)*size.x] * -1;
            edges.getPtr()[x+y*size.x] = std::clamp(-value, 0, 255);
        }
    }

    // On the downscaled edge detect img, search for rectangle shapes
    edges = sp::image::threshold(edges, 200);
    std::vector<ImageRect> rects;
    auto pixels = edges.getPtr();
    size = edges.getSize();
    for(auto start_point : sp::Rect2i(0, 0, size.x, size.y)) {
        if (pixels[start_point.x+start_point.y*size.x] == 0xFF) {
            std::vector<std::pair<sp::Vector2i, double>> point_score_list;
            std::vector<sp::Vector2i> todo_list;
            todo_list.push_back(start_point);
            while(todo_list.size() > 0)
            {
                auto position = todo_list.back();
                todo_list.pop_back();

                if (pixels[position.x + position.y * size.x] != 0xFF)
                    continue;
                pixels[position.x + position.y * size.x] = 0xFE;

                point_score_list.push_back({position, 0});

                if (position.x > 0)
                    todo_list.emplace_back(position.x - 1, position.y);
                if (position.y > 0)
                    todo_list.emplace_back(position.x, position.y - 1);
                if (position.x < size.x - 1)
                    todo_list.emplace_back(position.x + 1, position.y);
                if (position.y < size.y - 1)
                    todo_list.emplace_back(position.x, position.y + 1);
            }
            if (point_score_list.size() < 50)
                continue;

            sp::Vector2d center_point;
            for(auto [p, s] : point_score_list)
                center_point += sp::Vector2d(p);
            center_point /= double(point_score_list.size());
            auto f = [&](sp::Vector2d avoid) {
                double high_score = 0;
                sp::Vector2i result;
                for(auto& [p, s] : point_score_list) {
                    s += (sp::Vector2d(p) - avoid).length();
                    if (s >= high_score) {
                        high_score = s;
                        result = p;
                    }
                }
                return result;
            };
            auto p0 = f(center_point);
            auto p1 = f(sp::Vector2d(p0));
            auto p2 = f(sp::Vector2d(p1));
            auto p3 = f(sp::Vector2d(p2));
            auto side_length1 = (p2 - p0).length();
            auto side_length2 = (p1 - p3).length();
            auto side_length3 = (p3 - p0).length();
            auto side_length4 = (p1 - p2).length();
            if (side_length1 < 5 || side_length2 < 5 || side_length3 < 5 || side_length4 < 5)
                continue;
            if (std::abs(side_length1 - side_length2) > (side_length1 + side_length2) * 0.2)
                continue;
            if (std::abs(side_length3 - side_length4) > (side_length3 + side_length4) * 0.2)
                continue;
            if (std::abs((side_length1 + side_length2) - (side_length3 + side_length4)) > (side_length1 + side_length2 + side_length3 + side_length4) * 0.5)
                continue;
            ImageRect v{p0, p1, p2, p3};
            auto center = sp::Vector2i(center_point);
            std::sort(v.begin(), v.end(), [center](const sp::Vector2i& a, const sp::Vector2i& b) {
                return (b - center).angle() < (a - center).angle();
            });
            rects.push_back(v);
        }
    }
    
    // Scale the rects back up to the origonal image
    for(auto& rect : rects) for(auto& p : rect) p *= DOWNSCALE;

    //Corner correct with higher resolution image.
    for(auto& rect : rects) {
        for(size_t n=0; n<rect.size(); n++) {
            auto p0 = rect[n];
            auto p1 = rect[(n+1)%rect.size()];
            auto p2 = rect[(n+3)%rect.size()];
            auto d0 = (p1 - p0);
            auto d1 = (p2 - p0);
            
            auto p3 = p0 + d0 / 25 + d1 / 50;
            auto p4 = p0 + d0 / 25 - d1 / 15;
            auto edge = findEdge(img, sp::Ray2i(p3, p4));
            auto d2 = sp::Vector2i(sp::Vector2f(p4 - p3) * edge);

            auto p5 = p0 + d1 / 25 + d0 / 50;
            auto p6 = p0 + d1 / 25 - d0 / 15;
            edge = findEdge(img, sp::Ray2i(p5, p6));
            auto d3 = sp::Vector2i(sp::Vector2f(p6 - p5) * edge);

            rect[n] = p0 + d2 + d1 / 50 + d3 + d0 / 50;
        }
    }
    return rects;
}

ImageRect biggestRect(const std::vector<ImageRect>& rects)
{
    ImageRect result;
    int biggest_area = 0;
    for(const auto& rect : rects) {
        auto area = 0;
        for(int n=0; n<4; n++)
            area += (rect[(n+3)%4].x + rect[n].x) * (rect[(n+3)%4].y - rect[n].y);
        area = std::abs(area / 2.0);
        if (area > biggest_area) {
            biggest_area = area;
            result = rect;
        }
    }
    return result;
}

uint8_t getAverage(const sp::Image8& img, sp::Vector2i pos, int size)
{
    unsigned int total = 0;
    for(int y=-size; y<=size;y++) {
        for(int x=-size; x<=size;x++) {
            total += img.getPtr()[(pos.x+x)+(pos.y+y)*img.getSize().x];
        }
    }
    return total / (size*2+1) / (size*2+1);
}

class PixelsInRect
{
public:
    PixelsInRect(std::array<sp::Vector2i, 4> rect) : rect(rect) {
        ymin = std::min(std::min(rect[0].y, rect[1].y), std::min(rect[2].y, rect[3].y));
        ymax = std::max(std::max(rect[0].y, rect[1].y), std::max(rect[2].y, rect[3].y));
        xmin = std::min(std::min(rect[0].x, rect[1].x), std::min(rect[2].x, rect[3].x));
        xmax = std::max(std::max(rect[0].x, rect[1].x), std::max(rect[2].x, rect[3].x));
    }

    struct Iterator {
        PixelsInRect& pir;
        sp::Vector2i pos;
        int xs, xe;

        bool operator!=(const Iterator& other) { return pos.y != other.pos.y; }
        sp::Vector2i operator*() { return pos; }
        void operator++() {
            pos.x++;
            if (pos.x > xe) {
                pos.y += 1;
                updateXRange();
                pos.x = xs;
            }
        }
        void updateXRange() {
            xs = pir.xmax;
            xe = pir.xmin;
            for(int n=0; n<4; n++) {
                if ((pir.rect[n].y <= pos.y && pir.rect[(n+1)%4].y > pos.y) || (pir.rect[n].y > pos.y && pir.rect[(n+1)%4].y <= pos.y)) {
                    int x = pir.rect[n].x + (pir.rect[(n+1)%4].x-pir.rect[n].x) * (pos.y - pir.rect[n].y) / (pir.rect[(n+1)%4].y-pir.rect[n].y);
                    xs = std::min(xs, x);
                    xe = std::max(xe, x);
                }
            }
        }
    };

    Iterator begin() {
        Iterator res = {*this, {xmin, ymin}};
        res.updateXRange();
        res.pos.x = res.xs;
        return res;
    }
    Iterator end() {
        return {*this, {xmax, ymax+1}};
    }

    std::array<sp::Vector2i, 4> rect;
    int ymin, ymax, xmin, xmax;
};

DrawingProcessResult processDrawing(sp::Image& img, bool debug)
{
    auto grey = sp::image::convert<uint8_t>(img);
    auto rects = findRectangles(grey);

    if (debug) {
        for(const auto& rect : rects) {
            for(size_t n=1; n<rect.size(); n++) {
                sp::image::draw(img, sp::Ray2i(rect[n-1], rect[n]), 0xFF0000FF);
            }
            sp::image::draw(img, sp::Ray2i(rect[rect.size()-1], rect[0]), 0xFF0000FF);
        }
    }

    if (rects.size() == 0) return {false};
    auto rect = biggestRect(rects);

    auto diagonal0 = rect[2] - rect[0];
    auto diagonal1 = rect[3] - rect[1];

    ImageRect small_rect = {
        rect[0] + diagonal0 / 12,
        rect[1] + diagonal1 / 12,
        rect[2] - diagonal0 / 12,
        rect[3] - diagonal1 / 12,
    };

    uint8_t global_min = 255;
    uint8_t global_max = 0;
    for(auto p : PixelsInRect(small_rect)) {
        auto value = grey.getPtr()[p.x+p.y*grey.getSize().x];
        global_min = std::min(global_min, value);
        global_max = std::max(global_max, value);
    }
    std::array<uint8_t, 4> rect_value;
    for(int n=0; n<4; n++) rect_value[n] = getAverage(grey, small_rect[n], 4);

    auto range = global_max - global_min;
    global_min += range / 8;
    global_max -= range / 4;
    for(auto p : PixelsInRect(rect)) {
        auto d0 = (p - small_rect[0]);
        auto d1 = (small_rect[1] - small_rect[0]);
        auto d2 = (small_rect[3] - small_rect[0]);
        auto f0 = 255 * d1.dot(d0) / d1.dot(d1);
        auto f1 = 255 * d2.dot(d0) / d2.dot(d2);
        auto max0 = (rect_value[0] * (255-f0) + rect_value[1] * (f0)) / 255;
        auto max1 = (rect_value[3] * (255-f0) + rect_value[2] * (f0)) / 255;
        auto max = (max0 * (255-f1) + max1*f1) / 255;
        auto min = global_min;
        max -= range / 20;
        if (max == min) min--;
        auto clamp_min = 0;
        clamp_min = std::max(clamp_min, std::clamp(255-f0 * 15, 0, 255));
        clamp_min = std::max(clamp_min, std::clamp(255-f1 * 15, 0, 255));
        clamp_min = std::max(clamp_min, std::clamp(255-(255-f0) * 15, 0, 255));
        clamp_min = std::max(clamp_min, std::clamp(255-(255-f1) * 15, 0, 255));

        auto value = img.getPtr()[p.x+p.y*grey.getSize().x];
        auto r = std::clamp((int(value & 0xFF) - min) * 255 / (max - min), clamp_min, 255);
        auto g = std::clamp(((int(value >> 8) & 0xFF) - min) * 255 / (max - min), clamp_min, 255);
        auto b = std::clamp(((int(value >> 16) & 0xFF) - min) * 255 / (max - min), clamp_min, 255);
        value = 0xFF000000 | (r) | (g << 8) | (b << 16);
        img.getPtr()[p.x+p.y*grey.getSize().x] = value;
    }

    if (debug) {
        for(size_t n=1; n<small_rect.size(); n++) {
            sp::image::draw(img, sp::Ray2i(small_rect[n-1], small_rect[n]), 0xFFFF0000);
        }
        sp::image::draw(img, sp::Ray2i(small_rect[small_rect.size()-1], small_rect[0]), 0xFFFF0000);
    }
    return {true, small_rect};
}