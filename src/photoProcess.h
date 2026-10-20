#pragma once

#include <sp2/graphics/image.h>
#include <array>

using ImageRect = std::array<sp::Vector2i, 4>;

std::vector<ImageRect> findRectangles(const sp::Image8& img);
ImageRect biggestRect(const std::vector<ImageRect>& rects);
uint8_t getAverage(const sp::Image8& img, sp::Vector2i pos, int size);

struct DrawingProcessResult {
    bool succes;
    ImageRect rect;
};

DrawingProcessResult processDrawing(sp::Image& img, bool debug);