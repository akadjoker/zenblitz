#include "engine/TerrainRep.h"
#include "engine/Model.h"
#include "engine/Collision.h"
#include <algorithm>
#include <cmath>

namespace engine
{
    namespace
    {
        // terrain.cpp's Plane::distToPoint via this engine's own Plane -
        // buildBlockInfo() needs a signed distance from a point to the
        // triangle plane through 3 corners, same as blitz::Plane(a,b,c)
        // already gives via its own n/d fields.
        float distToPlane(const Vector &a, const Vector &b, const Vector &c, const Vector &p)
        {
            return Plane(a, b, c).distance(p);
        }
    }

    unsigned short TerrainRep::heightAt(int x, int z) const
    {
        if (x < 0) x = 0; else if (x > mCellSize) x = mCellSize;
        if (z < 0) z = 0; else if (z > mCellSize) z = mCellSize;
        return mHeights[(size_t)z * (size_t)(mCellSize + 1) + (size_t)x];
    }

    float TerrainRep::heightNorm(int x, int z) const { return heightAt(x, z) / 65535.0f; }

    TerrainRep::TerrainRep(int cellShift)
        : mCellSize(1 << cellShift), mCellShift(cellShift), mCellMask((1 << cellShift) - 1)
    {
        mHeights.resize((size_t)(mCellSize + 1) * (size_t)(mCellSize + 1), 0);
        // blockSize must be 2^n+1 and evenly divide the heightmap
        // resolution (terrain.cpp's own TerrainNode constructor
        // constraint: "if hmapSize % (blockSize-1) != 0, blockSize = 17").
        // cellSize is itself always 2^cellShift here, so 17 only fails to
        // divide it when cellSize < 16 (a terrain smaller than 17x17) -
        // fall back to a block size that evenly covers the whole terrain
        // in one block for those tiny/test-only sizes.
        mBlockSize = (mCellSize % (17 - 1) == 0) ? 17 : (mCellSize + 1);
        calcMaxLevel();
        createBlockTree();
    }

    TerrainRep::TerrainRep(const TerrainRep &other)
        : mHeights(other.mHeights), mCellSize(other.mCellSize), mCellShift(other.mCellShift),
          mCellMask(other.mCellMask), mBlockSize(other.mBlockSize), mMaxLevel(other.mMaxLevel),
          mLodThreshold(other.mLodThreshold), mDetail(other.mDetail), mMorph(other.mMorph),
          mShading(other.mShading), mBlockTree(other.mBlockTree), mBlockTreeValid(other.mBlockTreeValid)
    {
    }

    void TerrainRep::clear()
    {
        for (size_t k = 0; k < mHeights.size(); ++k) mHeights[k] = 0;
        createBlockTree();
    }

    void TerrainRep::setShading(bool shading) { mShading = shading; }

    void TerrainRep::setDetail(int detail, bool morph)
    {
        mMorph = morph;
        if (detail < 2) detail = 2;
        if (detail > 60000) detail = 60000;
        mDetail = detail;
        // Blitz3D's TerrainDetail is a triangle-count target (ROAM's own
        // budget); Horde3D's meshQuality is a threshold with no such
        // budget concept - block count roughly doubles per quadtree level
        // and each block contributes ~2*blockSize^2 triangles, so a
        // higher requested triangle count should allow deeper subdivision
        // (a lower threshold). Calibrated, not derived: 2000 (the
        // original docs' own "typical value") maps to meshQuality 50
        // (Horde3D's own default), scaled by sqrt(detail) since block
        // count grows quadratically with subdivision depth.
        const float meshQuality = 50.0f * std::sqrt((float)mDetail / 2000.0f);
        mLodThreshold = 1.0f / std::max(meshQuality, 1.0f);
    }

    int TerrainRep::getSize() const { return mCellSize; }

    float TerrainRep::getHeight(int x, int z) const
    {
        // Clamp, don't wrap: x/z == mCellSize is the inclusive far edge
        // (the duplicated last row/col, see mHeights' comment in the
        // header), and Terrain::getHeight's own bounds check explicitly
        // allows it through. A `& mCellMask` wrap would alias that edge
        // back onto row/col 0.
        if (x < 0) x = 0; else if (x > mCellSize) x = mCellSize;
        if (z < 0) z = 0; else if (z > mCellSize) z = mCellSize;
        return heightNorm(x, z);
    }

    void TerrainRep::setHeight(int x, int z, float height, bool realtime)
    {
        if (x < 0) x = 0; else if (x > mCellSize) x = mCellSize;
        if (z < 0) z = 0; else if (z > mCellSize) z = mCellSize;
        const int cx = x, cz = z;
        if (height < 0.0f) height = 0.0f; else if (height > 1.0f) height = 1.0f;
        mHeights[(size_t)cz * (size_t)(mCellSize + 1) + (size_t)cx] = (unsigned short)(height * 65535.0f + 0.5f);
        // Height edits at the terrain's own top/right edge also have to
        // update the duplicated extra row/col (see mHeights' own comment
        // in TerrainRep.h) so block sampling at that edge sees the change.
        if (cx == mCellSize - 1) mHeights[(size_t)cz * (size_t)(mCellSize + 1) + (size_t)mCellSize] =
            mHeights[(size_t)cz * (size_t)(mCellSize + 1) + (size_t)cx];
        if (cz == mCellSize - 1)
            for (int i = 0; i <= mCellSize; ++i)
                mHeights[(size_t)mCellSize * (size_t)(mCellSize + 1) + (size_t)i] =
                    mHeights[(size_t)(mCellSize - 1) * (size_t)(mCellSize + 1) + (size_t)i];
        // Same lazy-rebuild contract the ROAM code's mErrorsValid had:
        // LoadTerrain calls setHeight() once per heightmap texel with
        // realtime=false, so rebuilding the whole block quadtree here
        // (a full heightmap scan) would make bulk loading O(n^2). Mark
        // dirty and defer to validateBlockTree(), called by every reader
        // (render/collide/getHeight-adjacent) before it touches
        // mBlockTree.
        if (realtime) createBlockTree();
        else mBlockTreeValid = false;
    }

    void TerrainRep::calcMaxLevel()
    {
        // terrain.cpp's calcMaxLevel(): pow2 = hmapSize / (blockSize-1),
        // maxLevel = log2(pow2).
        const int pow2 = mCellSize / (mBlockSize - 1);
        mMaxLevel = 0;
        for (int i = 1; i < pow2; i *= 2) ++mMaxLevel;
    }

    void TerrainRep::buildBlockInfo(BlockInfo &block, float minU, float minV, float maxU, float maxV) const
    {
        // terrain.cpp's buildBlockInfo(): walk every heightmap texel
        // inside this block's footprint, tracking min/max height and the
        // worst perpendicular distance from the real sampled height to
        // the two triangles the block's own (blockSize x blockSize) grid
        // would approximate that texel with.
        const float pixelStep = 1.0f / (float)mCellSize;
        const float stepU = (maxU - minU) / (float)(mBlockSize - 1);
        const float stepV = (maxV - minV) / (float)(mBlockSize - 1);

        for (int v = 0; v < mBlockSize - 1; ++v)
        {
            for (int u = 0; u < mBlockSize - 1; ++u)
            {
                auto sampleAt = [&](float su, float sv) -> Vector
                {
                    const int px = (int)(su * mCellSize + 0.5f);
                    const int pz = (int)(sv * mCellSize + 0.5f);
                    return Vector(su, heightNorm(px, pz), sv);
                };

                const Vector corner0 = sampleAt(minU + u * stepU, minV + v * stepV);
                const Vector corner1 = sampleAt(minU + u * stepU, minV + (v + 1) * stepV);
                const Vector corner2 = sampleAt(minU + (u + 1) * stepU, minV + v * stepV);
                const Vector corner3 = sampleAt(minU + (u + 1) * stepU, minV + (v + 1) * stepV);

                float vv = 0.0f;
                while (vv <= stepV)
                {
                    float uu = 0.0f;
                    while (uu <= stepU)
                    {
                        // tri0 = (corner0,corner1,corner2), tri1 =
                        // (corner1,corner2,corner3) - same split
                        // terrain.cpp uses (uu<=vv picks the lower-left
                        // triangle of the block's two-triangle quad).
                        const bool useTri0 = uu <= vv;
                        const Vector point = sampleAt(minU + u * stepU + uu, minV + v * stepV + vv);

                        block.minHeight = std::min(block.minHeight, point.y);
                        block.maxHeight = std::max(block.maxHeight, point.y);
                        const float dist = useTri0 ? distToPlane(corner0, corner1, corner2, point)
                                                    : distToPlane(corner1, corner2, corner3, point);
                        block.geoError = std::max(block.geoError, std::fabs(dist));

                        uu += pixelStep;
                    }
                    vv += pixelStep;
                }
            }
        }
    }

    void TerrainRep::createBlockTree() const
    {
        // terrain.cpp's createBlockTree(): one BlockInfo per block across
        // every level 0..maxLevel, level i holding 4^i blocks in row-major
        // (y then x) order, flattened with each level's own offset.
        size_t size = 0;
        for (int i = 0; i <= mMaxLevel; ++i) size += (size_t)(1 << i) * (size_t)(1 << i);
        mBlockTree.resize(size);

        int index = 0;
        for (int i = 0; i <= mMaxLevel; ++i)
        {
            const int numBlocks = 1 << i;
            for (int y = 0; y < numBlocks; ++y)
                for (int x = 0; x < numBlocks; ++x)
                    buildBlockInfo(mBlockTree[(size_t)index++], (float)x / numBlocks, (float)y / numBlocks,
                                  (float)(x + 1) / numBlocks, (float)(y + 1) / numBlocks);
        }
        mBlockTreeValid = true;
    }

    const TerrainRep::BlockInfo &TerrainRep::blockAt(int level, float minU, float minV) const
    {
        int offset = 0;
        for (int i = 0; i < level; ++i) offset += (1 << i) * (1 << i);
        const int levelBlocks = 1 << level;
        const int index = offset + (int)(minV * levelBlocks) * levelBlocks + (int)(minU * levelBlocks);
        return mBlockTree[(size_t)index];
    }

    void TerrainRep::renderBlock(float minU, float minV, float maxU, float maxV, int level, float scale,
                                 const Vector &localEye, const Frustum &frustum)
    {
        const float halfU = (minU + maxU) * 0.5f;
        const float halfV = (minV + maxV) * 0.5f;
        const BlockInfo &block = blockAt(level, minU, minV);

        // Block AABB in the same local (model-space, cell-unit) frame the
        // Frustum passed in was already built against - see render()'s
        // Frustum(worldFrustum, -renderTform) below, same "cull in local
        // space" convention the ROAM code this replaces already used.
        // Skirt-extended downward the same way terrain.cpp's
        // drawTerrainBlock builds its culling box (bBMin.y = minHeight -
        // skirtHeight).
        const float skirtHeight = mMorph ? 0.02f : 0.0f;
        Box localBox(Vector(minU * mCellSize, block.minHeight - skirtHeight, minV * mCellSize),
                    Vector(maxU * mCellSize, block.maxHeight, maxV * mCellSize));
        if (!frustum.cull(localBox)) return;

        const float dist = std::max((localEye - Vector((minU + maxU) * 0.5f * mCellSize,
                                                        (block.minHeight + block.maxHeight) * 0.5f,
                                                        (minV + maxV) * 0.5f * mCellSize))
                                        .length(),
                                    0.00001f);
        const float p = block.geoError / dist;

        if (p < mLodThreshold || level == mMaxLevel)
        {
            // Emit this block's (blockSize+2)^2 vertex grid (the +2 is
            // the one-texel skirt ring on every side) and its triangle
            // strip-equivalent-as-list indices, same layout terrain.cpp's
            // createVertices()/createIndices() build once and
            // drawTerrainBlock() re-samples the height stream of per
            // block - done here as a plain triangle list straight into
            // the shared DynamicMesh, since GpuGeometry has no separate
            // "static positions + dynamic height stream" split to mirror
            // Horde3D's own two-vertex-buffer trick.
            const int size = mBlockSize + 2;
            const float invScale = 1.0f / (float)(mBlockSize - 1);
            const int firstVertex = mMesh.vertexCount();

            for (int v = 0; v < size; ++v)
            {
                float t = (v - 1) * invScale;
                if (v == 0) t = 0.0f; else if (v == size - 1) t = 1.0f;

                for (int u = 0; u < size; ++u)
                {
                    float s = (u - 1) * invScale;
                    if (u == 0) s = 0.0f; else if (u == size - 1) s = 1.0f;

                    const float worldU = s * scale + minU;
                    const float worldV = t * scale + minV;
                    const int px = (int)(worldU * mCellSize + 0.5f);
                    const int pz = (int)(worldV * mCellSize + 0.5f);
                    float y = heightNorm(px, pz);

                    // Skirt: pull the outer ring down so adjacent blocks
                    // at a different LOD level never show a visible
                    // crack, smaller near the camera the same way
                    // terrain.cpp scales it by dist.
                    if (v == 0 || v == size - 1 || u == 0 || u == size - 1)
                        y = std::max(y - skirtHeight * dist, 0.0f);

                    DynamicMesh::Vertex vert;
                    vert.coords = Vector(worldU * mCellSize, y, worldV * mCellSize);
                    vert.normal = mShading
                        ? (Plane(Vector(0, heightNorm(px, pz - 1), -1) + vert.coords,
                                Vector(1, heightNorm(px + 1, pz), 0) + vert.coords,
                                Vector(0, heightNorm(px, pz + 1), 1) + vert.coords)
                              .n)
                        : Vector(0, 1, 0);
                    vert.texCoords[0][0] = vert.texCoords[1][0] = vert.coords.x;
                    vert.texCoords[0][1] = vert.texCoords[1][1] = (float)mCellSize - vert.coords.z;
                    mMesh.addVertex(vert);
                }
            }

            for (int v = 0; v < size - 1; ++v)
                for (int u = 0; u < size - 1; ++u)
                {
                    // Same {a,c,b}/{b,c,d} winding bb_cmds_world.cpp's
                    // makeGrid() (CreatePlane/CreateHeightMap, confirmed
                    // visible) uses for an identical a=(x,z) b=(x+1,z)
                    // c=(x,z+1) d=(x+1,z+1) grid cell - this engine's
                    // front-facing winding is not the CCW-from-above the
                    // naive {i00,i01,i11} order this replaced gave.
                    const int i00 = firstVertex + v * size + u;
                    const int i10 = firstVertex + v * size + u + 1;
                    const int i01 = firstVertex + (v + 1) * size + u;
                    const int i11 = firstVertex + (v + 1) * size + u + 1;
                    mMesh.addTriangle((unsigned short)i00, (unsigned short)i01, (unsigned short)i10);
                    mMesh.addTriangle((unsigned short)i10, (unsigned short)i01, (unsigned short)i11);
                }
        }
        else
        {
            const float childScale = scale * 0.5f;
            float us[4] = {minU, halfU, minU, halfU};
            float vs0[4] = {minV, minV, halfV, halfV};
            float ue[4] = {halfU, maxU, halfU, maxU};
            float ve[4] = {halfV, halfV, maxV, maxV};
            int order[4] = {0, 1, 2, 3};

            // Nearest-first traversal (terrain.cpp's own camera-relative
            // swap) - front-to-back order for better early z-rejection,
            // not needed for correctness but kept for parity with the
            // ported algorithm.
            if (localEye.x > halfU * mCellSize) { std::swap(order[0], order[1]); std::swap(order[2], order[3]); }
            if (localEye.z > halfV * mCellSize) { std::swap(order[0], order[2]); std::swap(order[1], order[3]); }

            for (int i = 0; i < 4; ++i)
            {
                const int b = order[i];
                renderBlock(us[b], vs0[b], ue[b], ve[b], level + 1, childScale, localEye, frustum);
            }
        }
    }

    void TerrainRep::render(Model *model, const RenderContext &context)
    {
        validateBlockTree();
        const Transform worldTform = model->getRenderTform();
        Frustum frustum(context.getWorldFrustum(), -worldTform);
        const Vector eye = frustum.getVertex(Frustum::VertEye);

        mMesh.begin(0, 0);
        renderBlock(0.0f, 0.0f, 1.0f, 1.0f, 0, 1.0f, eye, frustum);
        if (mMesh.triangleCount()) model->enqueue(&mMesh, model->getRenderBrush());
    }

    bool TerrainRep::collideBlock(const Line &line, float radius, Collision *current, const Transform &transform,
                                  float minU, float minV, float maxU, float maxV, int level,
                                  const Box &localBox) const
    {
        const BlockInfo &block = blockAt(level, minU, minV);
        Box box(Vector(minU * mCellSize, block.minHeight, minV * mCellSize),
               Vector(maxU * mCellSize, block.maxHeight, maxV * mCellSize));
        if (!box.overlaps(localBox)) return false;

        if (level == mMaxLevel)
        {
            // Leaf block: test its actual (blockSize-1)^2 grid of
            // triangles, same resolution renderBlock() would draw at max
            // level - the ROAM version this replaces tested down to
            // individual bintree leaves; a block is this scheme's
            // equivalent leaf granularity.
            const float invScale = 1.0f / (float)(mBlockSize - 1);
            auto sampleAt = [&](float su, float sv) -> Vector
            {
                const int px = (int)(su * mCellSize + 0.5f);
                const int pz = (int)(sv * mCellSize + 0.5f);
                return Vector(su * mCellSize, heightNorm(px, pz), sv * mCellSize);
            };

            // Two passes over the block's grid: every face first, edge and
            // corner cylinders only if no face caught the sweep.
            //
            // This grid is far denser than what the ROAM implementation
            // this replaced fed the same collision code. ROAM subdivided by
            // geometric error, so flat ground stayed a couple of huge
            // triangles and a sweep landed well inside a face. Here the
            // leaf is always the full heightmap resolution, so a sphere of
            // any useful radius spans several 1x1 cells and touches many
            // shared edges. That matters because Collision::update() keeps
            // a hit only when its t is strictly better than the one already
            // recorded: the first triangle to report a given t owns the
            // contact, and every later triangle reporting the same t is
            // dropped as a tie. With faces and edges interleaved, an edge
            // cylinder from a neighbouring cell routinely registers first
            // and the real face - same t, but the normal that actually
            // points out of the ground - loses the tie. The reported normal
            // then points sideways and the sweep resolves against a wall
            // that is not there, driving the entity through the terrain.
            //
            // Ordering the passes makes the face win the tie, and costs
            // nothing when the sweep genuinely hits an edge or a crease
            // between cells: pass two still runs, against the same
            // Collision, and still finds it.
            bool hit = false;
            for (int pass = 0; pass < 2 && !hit; ++pass)
            {
                const bool facesOnly = (pass == 0);
                for (int v = 0; v < mBlockSize - 1; ++v)
                {
                    for (int u = 0; u < mBlockSize - 1; ++u)
                    {
                        const float u0 = minU + (maxU - minU) * u * invScale;
                        const float u1 = minU + (maxU - minU) * (u + 1) * invScale;
                        const float v0 = minV + (maxV - minV) * v * invScale;
                        const float v1 = minV + (maxV - minV) * (v + 1) * invScale;
                        const Vector p00 = sampleAt(u0, v0), p10 = sampleAt(u1, v0);
                        const Vector p01 = sampleAt(u0, v1), p11 = sampleAt(u1, v1);
                        // Collision::triangleCollide backface-culls (rejects
                        // when the ray hits the triangle from behind its own
                        // plane normal) - winding has to give an upward-facing
                        // normal for a Y-up terrain, the same (v0, v2, v1)
                        // order the ROAM code this replaces used.
                        //
                        // Both triangles of the cell are tested
                        // unconditionally (not `else if`): the quad splits
                        // along p00-p11 and a sweep landing near that diagonal
                        // can touch both, so testing the second only when the
                        // first misses would discard the nearer of the two.
                        if (current->triangleCollide(line, radius, transform * p00, transform * p01,
                                                      transform * p11, facesOnly))
                            hit = true;
                        if (current->triangleCollide(line, radius, transform * p00, transform * p11,
                                                      transform * p10, facesOnly))
                            hit = true;
                    }
                }
            }
            return hit;
        }

        const float halfU = (minU + maxU) * 0.5f, halfV = (minV + maxV) * 0.5f;
        bool hit = false;
        hit |= collideBlock(line, radius, current, transform, minU, minV, halfU, halfV, level + 1, localBox);
        hit |= collideBlock(line, radius, current, transform, halfU, minV, maxU, halfV, level + 1, localBox);
        hit |= collideBlock(line, radius, current, transform, minU, halfV, halfU, maxV, level + 1, localBox);
        hit |= collideBlock(line, radius, current, transform, halfU, halfV, maxU, maxV, level + 1, localBox);
        return hit;
    }

    bool TerrainRep::collide(const Line &line, float radius, Collision *current, const Transform &transform) const
    {
        validateBlockTree();
        const Transform local = -transform;
        Box localBox(line);
        localBox.expand(radius > 0.0f ? radius : blitz::EPSILON);
        localBox = local * localBox;
        return collideBlock(line, radius, current, transform, 0.0f, 0.0f, 1.0f, 1.0f, 0, localBox);
    }
}
