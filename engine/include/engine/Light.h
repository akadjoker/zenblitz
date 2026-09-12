#ifndef ENGINE_LIGHT_H
#define ENGINE_LIGHT_H

#include "engine/Object.h"

namespace engine
{
    class Light : public Object
    {
    public:
        enum
        {
            LightDistant = 1, LightPoint = 2, LightSpot = 3
        };

        Light(int type) : mType(type) {}

        Light *getLight() override { return this; }
        Entity *clone() override { return new Light(*this); }

        void setRange(float r) { mRange = r; }
        void setColor(const Vector &v) { mColor = v; }
        void setConeAngles(float inner, float outer) { mInnerAngle = inner; mOuterAngle = outer; }

        bool beginRender(float tween) override
        {
            Object::beginRender(tween);
            mRenderPos = getRenderTform().v;
            mRenderDir = getRenderTform().m.k;
            return true;
        }

        int getType() const { return mType; }
        float getRange() const { return mRange; }
        const Vector &getColor() const { return mColor; }
        float getInnerAngle() const { return mInnerAngle; }
        float getOuterAngle() const { return mOuterAngle; }
        const Vector &getRenderPosition() const { return mRenderPos; }
        const Vector &getRenderDirection() const { return mRenderDir; }

    private:
        int mType;
        float mRange = 1000.0f;
        Vector mColor{1, 1, 1};
        float mInnerAngle = 15.0f, mOuterAngle = 30.0f;
        Vector mRenderPos, mRenderDir;
    };
}

#endif
