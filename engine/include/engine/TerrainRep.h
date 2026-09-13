#ifndef ENGINE_TERRAINREP_H
#define ENGINE_TERRAINREP_H

#include "engine/DynamicMesh.h"
#include "engine/Frustum.h"
#include "engine/Model.h"
#include <ct/vector.hpp>

namespace engine
{
    // Ported from Horde3D's terrain extension (Extensions/Terrain/Source/
    // terrain.cpp, Copyright 2006-2020 Nicolas Schulz/Volker Wiendl/Horde3D
    // team, Eclipse Public License v1.0) - a quadtree of fixed-topology
    // blockSize x blockSize blocks, each block's on/off decision driven by
    // its precomputed geometric error projected against camera distance,
    // instead of the original Blitz3D's own ROAM/bintree scheme this
    // replaces. See terrain.cpp's drawTerrainBlock()/buildBlockInfo() for
    // the algorithm this is a line-by-line port of.
    //
    // What Horde3D's extension does NOT have, ported here separately since
    // TerrainRep needs it: line/box collision (buildBlockInfo's min/max
    // height per block reused as the collision bounding volume, same
    // "descend the quadtree, box-reject, leaf-test" shape the old ROAM
    // collide() already used).
    class TerrainRep
    {
    public:
        explicit TerrainRep(int cellShift);
        TerrainRep(const TerrainRep &other);

        void clear();
        void setShading(bool shading);
        void setDetail(int detail, bool morph);
        void setHeight(int x, int z, float height, bool realtime);

        int getSize() const;
        float getHeight(int x, int z) const;
        void render(Model *model, const RenderContext &context);
        bool collide(const blitz::Line &line, float radius, Collision *current, const Transform &transform) const;

        void freeGpu(gpu::Device &dev) { mMesh.freeGpu(dev); }

    private:
        // terrain.h's BlockInfo, unchanged (minHeight/maxHeight/geoError).
        struct BlockInfo
        {
            float minHeight = 1.0f;
            float maxHeight = 0.0f;
            float geoError = 0.0f;
        };

        // 16-bit height (terrain.cpp's _heightData, R+G-channel packed in
        // Horde3D's own texture-backed version - here just a plain array,
        // since ModifyTerrain writes directly rather than through a
        // decoded heightmap texture). (mCellSize+1)^2 entries: one extra
        // row/col duplicating the last real one, same as
        // TerrainNode::updateHeightData's "fill in last rows" step, so
        // block-edge sampling never reads out of bounds.
        ct::Vector<unsigned short> mHeights;

        int mCellSize = 0, mCellShift = 0, mCellMask = 0;
        // blockSize must be 2^n+1 (terrain.cpp's own constraint, checked
        // in the original TerrainNode constructor); fixed at construction
        // from cellShift rather than user-settable like Horde3D's
        // BlockSizeI param, since Blitz3D's CreateTerrain/LoadTerrain have
        // no equivalent command to expose it through.
        int mBlockSize = 17;
        int mMaxLevel = 0;
        // 1/meshQuality in Horde3D; derived from Blitz3D's own
        // TerrainDetail detail_level (a triangle-count target, not a
        // threshold) - see setDetail()'s comment for the mapping.
        float mLodThreshold = 1.0f / 50.0f;
        int mDetail = 2000;
        bool mMorph = true, mShading = false;

        // terrain.cpp's _blockTree: flat array, one BlockInfo per block
        // across every quadtree level (level 0 = 1 block, level N = 4^N),
        // built once at construction/whenever the heightmap or block size
        // changes (createBlockTree()).
        mutable ct::Vector<BlockInfo> mBlockTree;
        // Same lazy-rebuild contract the ROAM code this replaces had for
        // its own error tree (mErrorsValid): LoadTerrain/an editor brush
        // calls setHeight() once per texel with realtime=false, and
        // rebuilding the whole quadtree (a full heightmap scan) after
        // every one of those would be the exact O(n) per-edit cost this
        // scheme exists to avoid. Marked dirty on any non-realtime edit,
        // rebuilt lazily the first time render()/collide()/getHeight()
        // actually needs current block bounds.
        mutable bool mBlockTreeValid = true;

        DynamicMesh mMesh;

        unsigned short heightAt(int x, int z) const;
        float heightNorm(int x, int z) const;
        void buildBlockInfo(BlockInfo &block, float minU, float minV, float maxU, float maxV) const;
        void createBlockTree() const;
        void calcMaxLevel();
        void validateBlockTree() const { if (!mBlockTreeValid) createBlockTree(); }
        const BlockInfo &blockAt(int level, float minU, float minV) const;

        // Gathers one quadtree traversal's visible blocks into mMesh
        // (terrain.cpp's drawTerrainBlock, but appending into one shared
        // DynamicMesh instead of issuing one draw call per block - see
        // TerrainRep.cpp's render() comment for why).
        void renderBlock(float minU, float minV, float maxU, float maxV, int level, float scale,
                         const Vector &localEye, const Frustum &frustum);

        bool collideBlock(const blitz::Line &line, float radius, Collision *current, const Transform &transform,
                          float minU, float minV, float maxU, float maxV, int level, const Box &localBox) const;
    };
}

#endif
