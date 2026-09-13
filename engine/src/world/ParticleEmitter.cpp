#include "engine/ParticleEmitter.h"
#include <ct/sort.hpp>

namespace engine
{
    namespace
    {
        static float clampf(float value, float low, float high)
        {
            return value < low ? low : value > high ? high : value;
        }

        static unsigned color(float r, float g, float b, float a)
        {
            const unsigned rr = (unsigned)(clampf(r, 0.0f, 1.0f) * 255.0f + 0.5f);
            const unsigned gg = (unsigned)(clampf(g, 0.0f, 1.0f) * 255.0f + 0.5f);
            const unsigned bb = (unsigned)(clampf(b, 0.0f, 1.0f) * 255.0f + 0.5f);
            const unsigned aa = (unsigned)(clampf(a, 0.0f, 1.0f) * 255.0f + 0.5f);
            return (aa << 24) | (rr << 16) | (gg << 8) | bb;
        }
    }

    ParticleEmitter::ParticleEmitter(int maxParticles)
    {
        if (maxParticles < 1) maxParticles = 1;
        if (maxParticles > 4096) maxParticles = 4096;
        mParticles.resize((size_t)maxParticles);
        mOrder.reserve((size_t)maxParticles);
        setRenderSpace(RenderSpaceWorld);
        setBlend(BlendAlpha);
        setFX(FxFullbright | FxVertexColor | FxDoubleSided | FxNoFog);
    }

    ParticleEmitter::ParticleEmitter(const ParticleEmitter &other)
        : Model(other), mRandom(other.mRandom), mRate(other.mRate), mVector(other.mVector),
          mSpread(other.mSpread), mGravity(other.mGravity), mMinLife(other.mMinLife),
          mMaxLife(other.mMaxLife), mMinSpeed(other.mMinSpeed), mMaxSpeed(other.mMaxSpeed),
          mStartSize(other.mStartSize), mEndSize(other.mEndSize), mStartColor(other.mStartColor),
          mEndColor(other.mEndColor), mStartAlpha(other.mStartAlpha), mEndAlpha(other.mEndAlpha),
          mColumns(other.mColumns), mRows(other.mRows), mFirstFrame(other.mFirstFrame),
          mFrameCount(other.mFrameCount)
    {
        mParticles.resize(other.mParticles.size());
        mOrder.reserve(other.mParticles.size());
        setRenderSpace(RenderSpaceWorld);
    }

    void ParticleEmitter::setLife(float minimum, float maximum)
    {
        if (minimum < 1.0f) minimum = 1.0f;
        if (maximum < minimum) maximum = minimum;
        mMinLife = minimum;
        mMaxLife = maximum;
    }

    void ParticleEmitter::setSpeed(float minimum, float maximum)
    {
        if (minimum < 0.0f) minimum = 0.0f;
        if (maximum < minimum) maximum = minimum;
        mMinSpeed = minimum;
        mMaxSpeed = maximum;
    }

    void ParticleEmitter::setSize(float start, float end)
    {
        mStartSize = start < 0.0f ? 0.0f : start;
        mEndSize = end < 0.0f ? 0.0f : end;
    }

    void ParticleEmitter::setColor(const Vector &start, const Vector &end)
    {
        mStartColor = start;
        mEndColor = end;
    }

    void ParticleEmitter::setAlpha(float start, float end)
    {
        mStartAlpha = clampf(start, 0.0f, 1.0f);
        mEndAlpha = clampf(end, 0.0f, 1.0f);
    }

    void ParticleEmitter::setAtlas(int columns, int rows, int firstFrame, int frameCount)
    {
        if (columns < 1) columns = 1;
        if (rows < 1) rows = 1;
        const int total = columns * rows;
        if (firstFrame < 0) firstFrame = 0;
        if (firstFrame >= total) firstFrame = total - 1;
        if (frameCount < 1 || frameCount > total - firstFrame) frameCount = total - firstFrame;
        mColumns = columns;
        mRows = rows;
        mFirstFrame = firstFrame;
        mFrameCount = frameCount;
    }

    float ParticleEmitter::random()
    {
        mRandom = mRandom * 1664525u + 1013904223u;
        return (float)(mRandom >> 8) * (1.0f / 16777216.0f);
    }

    void ParticleEmitter::spawn()
    {
        const int capacity = (int)mParticles.size();
        if (!capacity || mAlive >= capacity) return;
        for (int step = 0; step < capacity; ++step)
        {
            const int index = (mCursor + step) % capacity;
            Particle &particle = mParticles[index];
            if (particle.active) continue;
            mCursor = (index + 1) % capacity;
            particle.active = true;
            particle.age = 0.0f;
            particle.life = mMinLife + (mMaxLife - mMinLife) * random();
            const Vector local(mVector.x + (random() - 0.5f) * mSpread.x,
                               mVector.y + (random() - 0.5f) * mSpread.y,
                               mVector.z + (random() - 0.5f) * mSpread.z);
            const float speed = mMinSpeed + (mMaxSpeed - mMinSpeed) * random();
            particle.position = getWorldPosition();
            particle.velocity = getWorldTform().m * local * speed;
            ++mAlive;
            return;
        }
    }

    void ParticleEmitter::emit(int count)
    {
        if (count < 1) return;
        const int free = (int)mParticles.size() - mAlive;
        if (count > free) count = free;
        for (int index = 0; index < count; ++index) spawn();
    }

    void ParticleEmitter::clearParticles()
    {
        for (size_t index = 0; index < mParticles.size(); ++index) mParticles[index].active = false;
        mAlive = 0;
        mRateCarry = 0.0f;
    }

    void ParticleEmitter::animate(float elapsed)
    {
        Object::animate(elapsed);
        if (elapsed < 0.0f) return;
        for (size_t index = 0; index < mParticles.size(); ++index)
        {
            Particle &particle = mParticles[index];
            if (!particle.active) continue;
            particle.age += elapsed;
            if (particle.age >= particle.life)
            {
                particle.active = false;
                --mAlive;
                continue;
            }
            particle.velocity += mGravity * elapsed;
            particle.position += particle.velocity * elapsed;
        }

        if (!visible()) return;
        mRateCarry += mRate * elapsed;
        int count = (int)mRateCarry;
        if (count > 0)
        {
            mRateCarry -= (float)count;
            emit(count);
        }
    }

    void ParticleEmitter::rebuildMesh(const RenderContext &rc)
    {
        mOrder.clear();
        const Vector eye = rc.getCameraTform().v;
        for (size_t index = 0; index < mParticles.size(); ++index)
            if (mParticles[index].active) mOrder.push_back((int)index);
        ct::sort(mOrder.begin(), mOrder.end(), [this, &eye](int a, int b)
        {
            const Vector da = mParticles[a].position - eye;
            const Vector db = mParticles[b].position - eye;
            return da.dot(da) > db.dot(db);
        });

        mMesh.begin((int)mOrder.size() * 4, (int)mOrder.size() * 2);
        const Vector right = rc.getCameraTform().m.i;
        const Vector up = rc.getCameraTform().m.j;
        const float invColumns = 1.0f / (float)mColumns;
        const float invRows = 1.0f / (float)mRows;
        const float insetU = invColumns * 0.001f;
        const float insetV = invRows * 0.001f;

        for (size_t index = 0; index < mOrder.size(); ++index)
        {
            const Particle &particle = mParticles[mOrder[index]];
            const float t = clampf(particle.age / particle.life, 0.0f, 1.0f);
            const float size = (mStartSize + (mEndSize - mStartSize) * t) * 0.5f;
            const Vector tint = mStartColor + (mEndColor - mStartColor) * t;
            const unsigned tintColor = color(tint.x, tint.y, tint.z, mStartAlpha + (mEndAlpha - mStartAlpha) * t);
            int frame = mFirstFrame + (int)(t * (float)mFrameCount);
            if (frame >= mFirstFrame + mFrameCount) frame = mFirstFrame + mFrameCount - 1;
            const int column = frame % mColumns;
            const int row = frame / mColumns;
            const float u0 = (float)column * invColumns + insetU;
            const float v0 = (float)row * invRows + insetV;
            const float u1 = (float)(column + 1) * invColumns - insetU;
            const float v1 = (float)(row + 1) * invRows - insetV;
            const Vector positions[4] =
            {
                particle.position - right * size + up * size,
                particle.position + right * size + up * size,
                particle.position + right * size - up * size,
                particle.position - right * size - up * size,
            };
            const float uvs[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
            const int base = mMesh.vertexCount();
            for (int vertex = 0; vertex < 4; ++vertex)
            {
                DynamicMesh::Vertex value;
                value.coords = positions[vertex];
                value.normal = -rc.getCameraTform().m.k;
                value.color = tintColor;
                value.texCoords[0][0] = uvs[vertex][0];
                value.texCoords[0][1] = uvs[vertex][1];
                mMesh.addVertex(value);
            }
            mMesh.addTriangle((unsigned short)base, (unsigned short)(base + 1), (unsigned short)(base + 2));
            mMesh.addTriangle((unsigned short)base, (unsigned short)(base + 2), (unsigned short)(base + 3));
        }
    }

    bool ParticleEmitter::render(const RenderContext &rc)
    {
        if (!mAlive) return false;
        rebuildMesh(rc);
        enqueue(&mMesh, getRenderBrush());
        return true;
    }

    void ParticleEmitter::uploadQueue(gpu::Device &dev, int type)
    {
        ct::Vector<QueueEntry> &entries = queue(type);
        for (size_t index = 0; index < entries.size(); ++index)
            if (entries[index].dynamicMesh && entries[index].dynamicMesh->upload(dev))
                entries[index].geom = entries[index].dynamicMesh->geometry();
    }
}
