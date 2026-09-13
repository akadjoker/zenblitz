#ifndef ENGINE_PARTICLEEMITTER_H
#define ENGINE_PARTICLEEMITTER_H

#include "engine/Model.h"
#include "engine/DynamicMesh.h"
#include <ct/vector.hpp>

namespace engine
{
    class ParticleEmitter : public Model
    {
    public:
        explicit ParticleEmitter(int maxParticles = 512);
        ParticleEmitter(const ParticleEmitter &other);

        Entity *clone() override { return new ParticleEmitter(*this); }

        void setRate(float rate) { mRate = rate < 0.0f ? 0.0f : rate; }
        void setVector(const Vector &vector) { mVector = vector; }
        void setSpread(const Vector &spread) { mSpread = spread; }
        void setGravity(const Vector &gravity) { mGravity = gravity; }
        void setLife(float minimum, float maximum);
        void setSpeed(float minimum, float maximum);
        void setSize(float start, float end);
        void setColor(const Vector &start, const Vector &end);
        void setAlpha(float start, float end);
        void setAtlas(int columns, int rows, int firstFrame = 0, int frameCount = 0);
        void emit(int count);
        void clearParticles();
        int particleCount() const { return mAlive; }
        int maxParticles() const { return (int)mParticles.size(); }

        void animate(float elapsed) override;
        bool render(const RenderContext &rc) override;
        void uploadQueue(gpu::Device &dev, int type) override;
        void freeGpu(gpu::Device &dev) override { mMesh.freeGpu(dev); }

    private:
        struct Particle
        {
            Vector position;
            Vector velocity;
            float age = 0.0f, life = 0.0f;
            bool active = false;
        };

        float random();
        void spawn();
        void rebuildMesh(const RenderContext &rc);

        ct::Vector<Particle> mParticles;
        ct::Vector<int> mOrder;
        DynamicMesh mMesh;
        int mAlive = 0, mCursor = 0;
        unsigned mRandom = 0x7f4a7c15u;
        float mRate = 0.0f, mRateCarry = 0.0f;
        Vector mVector{0, 1, 0}, mSpread;
        Vector mGravity;
        float mMinLife = 30.0f, mMaxLife = 60.0f;
        float mMinSpeed = 1.0f, mMaxSpeed = 1.0f;
        float mStartSize = 0.2f, mEndSize = 0.2f;
        Vector mStartColor{1, 1, 1}, mEndColor{1, 1, 1};
        float mStartAlpha = 1.0f, mEndAlpha = 0.0f;
        int mColumns = 1, mRows = 1, mFirstFrame = 0, mFrameCount = 1;
    };
}

#endif
