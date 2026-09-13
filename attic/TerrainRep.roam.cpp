#include "engine/TerrainRep.h"
#include "engine/Model.h"
#include "engine/Collision.h"
#include <cmath>

namespace engine
{
    static bool clipLineBox(const Line &line, const Box &box)
    {
        static const Vector normals[] = {Vector(1,0,0), Vector(0,0,1), Vector(0,-1,0), Vector(-1,0,0), Vector(0,0,-1), Vector(0,1,0)};
        Vector a = line.o, b = line.o + line.d;
        for (int k = 0; k < 6; ++k)
        {
            const Vector &n = normals[k];
            Vector p = box.corner(k);
            float da = n.dot(a - p), db = n.dot(b - p);
            if (da < 0)
            {
                if (db < 0) return false;
                a += (b - a) * (da / (da - db));
            }
            else if (db < 0) b += (a - b) * (db / (db - da));
        }
        return true;
    }

    void TerrainRep::Tri::unlink()
    {
        Tri *edges[] = {e0, e1, e2};
        for (int k = 0; k < 3; ++k)
        {
            Tri *edge = edges[k];
            if (!edge) continue;
            if (edge->e0 == this) edge->e0 = nullptr;
            else if (edge->e1 == this) edge->e1 = nullptr;
            else if (edge->e2 == this) edge->e2 = nullptr;
        }
    }

    TerrainRep::TerrainRep(int cellShift)
        : mCellSize(1 << cellShift), mCellShift(cellShift), mCellMask((1 << cellShift) - 1),
          mEndTriId((1 << cellShift) * (1 << cellShift) * 2), mTriPool(4096)
    {
        mCells.resize((size_t)mCellSize * mCellSize);
        mErrors.resize(mEndTriId);
        mVertices.reserve(2048);
        mLeaves.reserve(2048);
        mQueue.reserve(2048);
        clear();
    }

    TerrainRep::TerrainRep(const TerrainRep &other)
        : mCells(other.mCells), mErrors(other.mErrors), mErrorsValid(other.mErrorsValid),
          mCellSize(other.mCellSize), mCellShift(other.mCellShift), mCellMask(other.mCellMask),
          mEndTriId(other.mEndTriId), mDetail(other.mDetail), mMorph(other.mMorph), mShading(other.mShading), mTriPool(4096)
    {
        mVertices.reserve((size_t)mDetail + 32);
        mLeaves.reserve((size_t)mDetail + 32);
        mQueue.reserve((size_t)mDetail + 32);
    }

    void TerrainRep::clear()
    {
        for (size_t k = 0; k < mCells.size(); ++k) mCells[k].height = 0;
        for (size_t k = 0; k < mErrors.size(); ++k) mErrors[k] = Error();
        mErrorsValid = true;
    }

    void TerrainRep::setShading(bool shading) { mShading = shading; }

    void TerrainRep::setDetail(int detail, bool morph)
    {
        mMorph = morph;
        if (detail < 2) detail = 2;
        if (detail > 60000) detail = 60000;
        mDetail = detail;
        mVertices.reserve((size_t)detail + 32);
        mLeaves.reserve((size_t)detail + 32);
        mQueue.reserve((size_t)detail + 32);
    }

    int TerrainRep::getSize() const { return mCellSize; }

    float TerrainRep::getHeight(int x, int z) const
    {
        return mCells[((z & mCellMask) << mCellShift) | (x & mCellMask)].height / 255.0f;
    }

    TerrainRep::Vert TerrainRep::makeVert(int x, int z) const
    {
        Vert vertex;
        vertex.x = (short)x;
        vertex.z = (short)z;
        vertex.value = Vector((float)x, getHeight(x, z), (float)z);
        vertex.sourceY = vertex.value.y;
        return vertex;
    }

    void TerrainRep::setHeight(int x, int z, float height, bool realtime)
    {
        mCells[((z & mCellMask) << mCellShift) | (x & mCellMask)].height = (unsigned char)(height * 255.0f);
        if (!mErrorsValid) return;
        if (!realtime) { mErrorsValid = false; return; }
        Vert a = makeVert(0,0), b = makeVert(mCellSize,0), c = makeVert(mCellSize,mCellSize), d = makeVert(0,mCellSize);
        calcError(2, x, z, b, c, a);
        calcError(3, x, z, d, a, c);
    }

    Vector TerrainRep::getNormal(int x, int z) const
    {
        Vector v(x,getHeight(x,z),z), a(x,getHeight(x,z-1),z-1), b(x+1,getHeight(x+1,z),z), c(x,getHeight(x,z+1),z+1), d(x-1,getHeight(x-1,z),z);
        return (Plane(v,b,a).n + Plane(v,c,b).n + Plane(v,d,c).n + Plane(v,a,d).n).normalized();
    }

    TerrainRep::Error TerrainRep::calcError(int id, const Vert &v0, const Vert &v1, const Vert &v2) const
    {
        Error error;
        float top = v0.value.y;
        if (v1.value.y > top) top = v1.value.y;
        if (v2.value.y > top) top = v2.value.y;
        error.bound = top >= 1.0f ? 255 : (unsigned char)std::ceil(top * 255.0f);
        if (id >= mEndTriId) return error;
        Vert mid = makeVert((v1.x + v2.x) / 2, (v1.z + v2.z) / 2);
        float delta = std::fabs(mid.value.y - (v1.value.y + v2.value.y) * .5f);
        error.error = delta >= 1.0f ? 255 : (unsigned char)std::ceil((delta - blitz::EPSILON) * 255.0f);
        Error left = calcError(id * 2, mid, v2, v0), right = calcError(id * 2 + 1, mid, v0, v1);
        if (left.error > error.error) error.error = left.error;
        if (right.error > error.error) error.error = right.error;
        if (left.bound > error.bound) error.bound = left.bound;
        if (right.bound > error.bound) error.bound = right.bound;
        return mErrors[id] = error;
    }

    TerrainRep::Error TerrainRep::calcError(int id, int x, int z, const Vert &v0, const Vert &v1, const Vert &v2) const
    {
        if (id >= mEndTriId) return Error();
        int dx = -(v1.z-v0.z), dz = v1.x-v0.x;
        if ((x-v0.x)*dx + (z-v0.z)*dz < 0) return mErrors[id];
        dx = -(v2.z-v1.z); dz = v2.x-v1.x;
        if ((x-v1.x)*dx + (z-v1.z)*dz < 0) return mErrors[id];
        dx = -(v0.z-v2.z); dz = v0.x-v2.x;
        if ((x-v2.x)*dx + (z-v2.z)*dz < 0) return mErrors[id];
        return calcError(id, v0, v1, v2);
    }

    void TerrainRep::validateErrors() const
    {
        if (mErrorsValid) return;
        Vert a = makeVert(0,0), b = makeVert(mCellSize,0), c = makeVert(mCellSize,mCellSize), d = makeVert(0,mCellSize);
        calcError(2,b,c,a);
        calcError(3,d,a,c);
        mErrorsValid = true;
    }

    void TerrainRep::insert(Tri *tri, const Frustum &frustum, const Vector &eye)
    {
        if (tri->id >= mEndTriId || !mErrors[tri->id].error)
        {
            if (tri->clip & 63)
                for (int k = 0; k < 6; ++k)
                    if ((tri->clip & (1 << k)) && frustum.getPlane(k).distance(mVertices[tri->v0].value) < 0 && frustum.getPlane(k).distance(mVertices[tri->v1].value) < 0 && frustum.getPlane(k).distance(mVertices[tri->v2].value) < 0)
                    { tri->unlink(); destroyTri(tri); return; }
            tri->clip |= 128;
            mLeaves.push_back(tri);
            return;
        }
        if (tri->clip & 63)
        {
            Vector a=mVertices[tri->v0].value,b=mVertices[tri->v1].value,c=mVertices[tri->v2].value;
            Vector aa=a,bb=b,cc=c; a.y=b.y=c.y=0; aa.y=bb.y=cc.y=mErrors[tri->id].bound/255.0f;
            for (int k=0;k<6;++k) if (tri->clip&(1<<k))
            {
                const Plane &p=frustum.getPlane(k);
                int n=(p.distance(a)>=0)+(p.distance(b)>=0)+(p.distance(c)>=0)+(p.distance(aa)>=0)+(p.distance(bb)>=0)+(p.distance(cc)>=0);
                if (!n) { tri->unlink(); destroyTri(tri); return; }
                if (n==6) tri->clip&=~(1<<k);
            }
        }
        float distance=eye.distance((mVertices[tri->v1].value+mVertices[tri->v2].value)*.5f);
        if (distance < blitz::EPSILON) distance=blitz::EPSILON;
        tri->projectedError=mErrors[tri->id].error/distance;
        if (tri->projectedError > blitz::EPSILON) mQueue.push(tri);
        else { tri->clip|=128; mLeaves.push_back(tri); }
    }

    void TerrainRep::split(Tri *tri, const Frustum &frustum, const Vector &eye)
    {
        if (tri->e2 && tri->e2->e2 != tri) split(tri->e2, frustum, eye);
        int index=(int)mVertices.size();
        Vert mid=makeVert((mVertices[tri->v1].x+mVertices[tri->v2].x)/2,(mVertices[tri->v1].z+mVertices[tri->v2].z)/2);
        mid.sourceY=(mVertices[tri->v1].value.y+mVertices[tri->v2].value.y)*.5f;
        mVertices.push_back(mid);
        Tri *left=createTri(tri->id*2,tri->clip,index,tri->v2,tri->v0,nullptr,nullptr,tri->e0);
        Tri *right=createTri(tri->id*2+1,tri->clip,index,tri->v0,tri->v1,nullptr,left,tri->e1);
        left->e0=right;
        if (left->e2) { if(left->e2->e0==tri) left->e2->e0=left; else if(left->e2->e1==tri) left->e2->e1=left; else left->e2->e2=left; }
        if (right->e2) { if(right->e2->e0==tri) right->e2->e0=right; else if(right->e2->e1==tri) right->e2->e1=right; else right->e2->e2=right; }
        if (Tri *back=tri->e2)
        {
            Tri *br=createTri(back->id*2,back->clip,index,back->v2,back->v0,nullptr,right,back->e0);
            Tri *bl=createTri(back->id*2+1,back->clip,index,back->v0,back->v1,left,br,back->e1);
            right->e0=br; left->e1=br->e0=bl;
            if(br->e2){if(br->e2->e0==back)br->e2->e0=br;else if(br->e2->e1==back)br->e2->e1=br;else br->e2->e2=br;}
            if(bl->e2){if(bl->e2->e0==back)bl->e2->e0=bl;else if(bl->e2->e1==back)bl->e2->e1=bl;else bl->e2->e2=bl;}
            back->id=0; insert(br, frustum, eye); insert(bl, frustum, eye);
        }
        tri->id=0; insert(left, frustum, eye); insert(right, frustum, eye);
    }

    void TerrainRep::clearWorking()
    {
        for(size_t k=0;k<mLeaves.size();++k) destroyTri(mLeaves[k]);
        mLeaves.clear();
        while(!mQueue.empty()){ destroyTri(mQueue.top()); mQueue.pop(); }
        mVertices.clear();
    }

    void TerrainRep::render(Model *model, const RenderContext &context)
    {
        clearWorking(); validateErrors();
        Frustum frustum(context.getWorldFrustum(), -model->getRenderTform());
        Vector eye=frustum.getVertex(Frustum::VertEye);
        mVertices.push_back(makeVert(0,0)); mVertices.push_back(makeVert(mCellSize,0)); mVertices.push_back(makeVert(mCellSize,mCellSize)); mVertices.push_back(makeVert(0,mCellSize));
        Tri *a=createTri(2,63,1,2,0),*b=createTri(3,63,3,0,2); a->e2=b;b->e2=a; insert(a,frustum,eye);insert(b,frustum,eye);
        while(!mQueue.empty() && (int)(mLeaves.size()+mQueue.size())<mDetail){ Tri *tri=mQueue.top();mQueue.pop();if(tri->id)split(tri,frustum,eye);destroyTri(tri); }
        while(!mQueue.empty()){Tri *tri=mQueue.top();mQueue.pop();if(tri->id)mLeaves.push_back(tri);else destroyTri(tri);}
        if(mMorph && mVertices.size()>4){int count=((int)mLeaves.size())/4;if(count>(int)mVertices.size())count=(int)mVertices.size();for(int k=0;k<count;++k){Vert &v=mVertices[mVertices.size()-count+k];v.value.y+=(v.sourceY-v.value.y)*(float)k/(float)count;}}
        mMesh.begin((int)mVertices.size(),(int)mLeaves.size());
        for(size_t k=0;k<mVertices.size();++k){DynamicMesh::Vertex v;v.coords=mVertices[k].value;v.normal=mShading?getNormal(mVertices[k].x,mVertices[k].z):Vector(0,1,0);v.texCoords[0][0]=v.texCoords[1][0]=v.coords.x;v.texCoords[0][1]=v.texCoords[1][1]=mCellSize-v.coords.z;mMesh.addVertex(v);}
        for(size_t k=0;k<mLeaves.size();++k){Tri *tri=mLeaves[k];mMesh.addTriangle(tri->v0,tri->v2,tri->v1);destroyTri(tri);}mLeaves.clear();
        if(mMesh.triangleCount()) model->enqueue(&mMesh,model->getRenderBrush());
    }

    bool TerrainRep::collide(const Line &line, Collision *current, const Transform &transform, int id,
                             const Vert &v0, const Vert &v1, const Vert &v2, const Line &localLine) const
    {
        Box box(v0.value); box.update(v1.value); box.update(v2.value);
        if (id >= mEndTriId || !mErrors[id].error)
            return clipLineBox(localLine, box) && current->triangleCollide(line, 0, transform * v0.value, transform * v2.value, transform * v1.value);
        box.a.y = 0; box.b.y = mErrors[id].bound / 255.0f;
        if (!clipLineBox(localLine, box)) return false;
        Vert mid = makeVert((v1.x+v2.x)/2, (v1.z+v2.z)/2);
        bool left=collide(line,current,transform,id*2,mid,v2,v0,localLine);
        bool right=collide(line,current,transform,id*2+1,mid,v0,v1,localLine);
        return left || right;
    }

    bool TerrainRep::collide(const Line &line, float radius, Collision *current, const Transform &transform, int id,
                             const Vert &v0, const Vert &v1, const Vert &v2, const Box &localBox) const
    {
        Box box(v0.value); box.update(v1.value); box.update(v2.value);
        if (id >= mEndTriId || !mErrors[id].error)
            return box.overlaps(localBox) && current->triangleCollide(line, radius, transform*v0.value, transform*v2.value, transform*v1.value);
        box.a.y=0; box.b.y=mErrors[id].bound/255.0f;
        if (!box.overlaps(localBox)) return false;
        Vert mid=makeVert((v1.x+v2.x)/2,(v1.z+v2.z)/2);
        bool left=collide(line,radius,current,transform,id*2,mid,v2,v0,localBox);
        bool right=collide(line,radius,current,transform,id*2+1,mid,v0,v1,localBox);
        return left || right;
    }

    bool TerrainRep::collide(const Line &line, float radius, Collision *current, const Transform &transform) const
    {
        validateErrors();
        Vert a=makeVert(0,0),b=makeVert(mCellSize,0),c=makeVert(mCellSize,mCellSize),d=makeVert(0,mCellSize);
        if (!radius)
        {
            Line local=-transform*line;
            bool first=collide(line,current,transform,2,b,c,a,local);
            bool second=collide(line,current,transform,3,d,a,c,local);
            return first || second;
        }
        Box box(line); box.expand(radius); box=-transform*box;
        bool first=collide(line,radius,current,transform,2,b,c,a,box);
        bool second=collide(line,radius,current,transform,3,d,a,c,box);
        return first || second;
    }
}
