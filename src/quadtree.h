#pragma once

#include <sp2/assert.h>
#include <sp2/graphics/meshbuilder.h>
#include <stdint.h>
#include <vector>
#include <functional>


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
        sp2assert(!(new_value & TAG_BRANCH), "TAG_BRANCH set on value set in quadtree");
        if (x < 0 || y < 0 || x >= SIZE || y >= SIZE) return;
        setImpl(0, x, y, MAX_DEPTH - 1, new_value);
    }

    void setAtDepth(int x, int y, uint32_t new_value, int target_depth)
    {
        sp2assert(!(new_value & TAG_BRANCH), "TAG_BRANCH set on value set in quadtree");
        sp2assert((x & ((1 << target_depth) - 1)) == 0, "x not aligned to depth shift");
        sp2assert((y & ((1 << target_depth) - 1)) == 0, "y not aligned to depth shift");
        sp2assert(x >= 0 && x < SIZE, "x out of range");
        sp2assert(y >= 0 && y < SIZE, "y out of range");
        sp2assert(target_depth >= 0 && target_depth < int(MAX_DEPTH), "depth out of range");
        setAtDepthImpl(0, x, y, MAX_DEPTH - 1, new_value, target_depth);
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

    static constexpr unsigned int MAX_DEPTH = 8;
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

    bool setAtDepthImpl(uint32_t node_idx, int x, int y, int depth, uint32_t new_value, int target_depth)
    {
        int idx = 0;
        if (x & (1 << depth)) idx |= 1;
        if (y & (1 << depth)) idx |= 2;
        if (depth == target_depth) {
            if (data[node_idx][idx] & TAG_BRANCH) {
                recursiveFree(data[node_idx][idx]&~TAG_BRANCH);
            }
            data[node_idx][idx] = new_value;
        } else {
            auto value = data[node_idx][idx];
            if (value & TAG_BRANCH) {
                if (setAtDepthImpl(value&~TAG_BRANCH, x, y, depth-1, new_value, target_depth)) {
                    freeNode(value&~TAG_BRANCH);
                    data[node_idx][idx] = new_value;
                }
            } else if (value != new_value) {
                auto new_node_idx = allocNode(value);
                data[node_idx][idx] = new_node_idx | TAG_BRANCH;
                setAtDepthImpl(new_node_idx, x, y, depth - 1, new_value, target_depth);
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

    void recursiveFree(uint32_t index)
    {
        for(int n=0; n<4; n++)
            if (data[index][n] & TAG_BRANCH)
                recursiveFree(data[index][n]&~TAG_BRANCH);
        freeNode(index);
    }

    static constexpr uint32_t TAG_BRANCH = 0x80000000;
    static constexpr uint32_t DEFAULT_VALUE = 0x00000000;
    using Node = std::array<uint32_t, 4>;
    std::vector<Node> data {{DEFAULT_VALUE,DEFAULT_VALUE,DEFAULT_VALUE,DEFAULT_VALUE}};
    uint32_t freeList = TAG_BRANCH;
};
