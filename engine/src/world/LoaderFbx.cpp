/*
** LoaderFbx.cpp — see LoaderFbx.h. Walks the ofbx::IScene tree
** ofbx::load() builds instead of hand-parsing the binary/text FBX chunk
** format: ofbx already resolves objects/connections/curves, the class of
** bookkeeping LoaderX's own xfile parser had to do by hand for .x. Not a
** port of anything in the original Blitz3D (FBX has no original loader
** to port) - see [[project-gltf-fbx-extension]] memory note for why this
** exists.
**
** Skinning follows LoaderB3D's own convention exactly (Surface::Vertex's
** boneBones[]/boneWeights() via MeshLoader::addBone, bone index 0
** reserved for the mesh's own root transform, real bones at indices
** 1..N, mesh->setAnimator(new Animator(bones, animLen)) then
** mesh->createBones()) rather than reinventing a skeleton model - see
** LoaderB3D.cpp's readBone()/readObject() for the pattern this mirrors.
*/
#include "engine/LoaderFbx.h"
#include "engine/FilePath.h"
#include "engine/MeshModel.h"
#include "engine/Object.h"
#include "engine/Animator.h"
#include "engine/Texture.h"

#include "ofbx.h"

#include <SDL2/SDL_rwops.h>
#include <ct/vector.hpp>
#include <ct/hashmap.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace engine
{
    namespace
    {
        // See [[project-gltf-fbx-extension]]: 30 fps matches .b3d's
        // de-facto keyframe-tick convention, same choice LoaderGltf makes
        // for glTF's float-seconds animation times.
        constexpr float kFramesPerSecond = 30.0f;
        constexpr float kDegToRad = 0.0174532925199432957692369076848861f;

        ct::Vector<Texture *> g_textures;
        gpu::Device *g_dev;

        Vector toVector(const ofbx::Vec3 &v) { return Vector((float)v.x, (float)v.y, (float)v.z); }

        // ofbx::Matrix is the same D3D-style row-major 4x4 (translation in
        // the last row) LoaderX's own readFrameTransform already converts
        // to blitz::Transform for .x's FrameTransformMatrix - same layout,
        // same conversion.
        Transform toTransform(const ofbx::Matrix &m)
        {
            return Transform(blitz::Matrix(
                                  Vector((float)m.m[0], (float)m.m[1], (float)m.m[2]),
                                  Vector((float)m.m[4], (float)m.m[5], (float)m.m[6]),
                                  Vector((float)m.m[8], (float)m.m[9], (float)m.m[10])),
                             Vector((float)m.m[12], (float)m.m[13], (float)m.m[14]));
        }

        // Builds the same rotation ofbx's own (internal, not exported)
        // getRotationMatrix(euler, order) composes, as a Quat instead of a
        // D3D-style Matrix so it drops straight into Animation::
        // setRotationKey/Object::setLocalRotation - axis-angle quaternions
        // compose in the same right-to-left ("rightmost applied first")
        // order matrix products do for the same rotation convention.
        Quat axisQuat(const Vector &axis, float radians)
        {
            const float half = radians * 0.5f;
            return Quat(cosf(half), axis * sinf(half));
        }

        Quat eulerQuat(const Vector &degrees, ofbx::RotationOrder order)
        {
            const Quat qx = axisQuat(Vector(1, 0, 0), degrees.x * kDegToRad);
            const Quat qy = axisQuat(Vector(0, 1, 0), degrees.y * kDegToRad);
            const Quat qz = axisQuat(Vector(0, 0, 1), degrees.z * kDegToRad);
            switch (order)
            {
            case ofbx::RotationOrder::EULER_XYZ: return qz * qy * qx;
            case ofbx::RotationOrder::EULER_XZY: return qy * qz * qx;
            case ofbx::RotationOrder::EULER_YXZ: return qz * qx * qy;
            case ofbx::RotationOrder::EULER_YZX: return qx * qz * qy;
            case ofbx::RotationOrder::EULER_ZXY: return qy * qx * qz;
            case ofbx::RotationOrder::EULER_ZYX:
            default: return qx * qy * qz;
            }
        }

        ct::String directoryOf(const ct::String &path)
        {
            const size_t slash = path.find_last_of("/\\");
            return slash == ct::String::npos ? ct::String() : path.substr(0, slash);
        }

        Brush readMaterial(const ofbx::Material *mat)
        {
            Brush brush;
            if (!mat)
            {
                brush.setColor(Vector(1, 1, 1));
                return brush;
            }

            const ofbx::Color diffuse = mat->getDiffuseColor();
            brush.setColor(Vector(diffuse.r, diffuse.g, diffuse.b));

            if (const ofbx::Texture *tex = mat->getTexture(ofbx::Texture::DIFFUSE))
            {
                char filename[512];
                tex->getFileName().toString(filename);
                if (filename[0])
                {
                    Texture *loaded = Texture::load(filename, 0);
                    if (loaded)
                    {
                        if (g_dev) brush.setTexture(0, BrushTexture::fromTexture(*g_dev, loaded, 0));
                        brush.setColor(Vector(1, 1, 1));
                        // kept alive for the process lifetime, same
                        // contract as LoaderX/LoaderGltf's own g_textures.
                        g_textures.push_back(loaded);
                    }
                }
            }

            return brush;
        }

        // A polygon's last vertex index is stored as its bitwise NOT
        // (negative once cast to int) to mark the polygon boundary - see
        // ofbx/VERSION.
        int unwindIndex(int i) { return i < 0 ? -i - 1 : i; }

        struct WeightSlot
        {
            int bone = -1;
            float weight = 0.0f;
        };

        // FBX skin weights are not capped to 4 influences per vertex (see
        // ofbx/VERSION) - MeshLoader::addBone already keeps only the
        // strongest kMaxSurfaceBones by weight (insertion-sorted, lowest
        // dropped first), and MeshLoader::endMesh renormalizes whatever
        // survives that cut to sum to 1 - so every weight from the file
        // is simply handed to addBone and both concerns are already
        // handled downstream, the same as LoaderB3D's own readBone() does
        // for .b3d's skin chunk.
        void addSkinWeights(int vertex, const ct::Vector<WeightSlot> &slots)
        {
            for (size_t i = 0; i < slots.size(); ++i)
                if (slots[i].bone >= 0 && slots[i].weight > 0.0f)
                    MeshLoader::addBone(vertex, slots[i].weight, slots[i].bone);
        }

        void parseMesh(const ofbx::Mesh *fbxMesh, MeshModel *mesh,
                       const ct::HashMap<unsigned long long, int> &boneIndexById, bool collapse)
        {
            const ofbx::Geometry *geom = fbxMesh->getGeometry();
            if (!geom) return;

            const int vertexCount = geom->getVertexCount();
            const int indexCount = geom->getIndexCount();
            if (vertexCount <= 0 || indexCount <= 0) return;

            const ofbx::Vec3 *srcPositions = geom->getVertices();
            const ofbx::Vec3 *srcNormals = geom->getNormals();
            const ofbx::Vec2 *srcUvs = geom->getUVs(0);
            const int *srcIndices = geom->getFaceIndices();
            const int *srcMaterials = geom->getMaterials();
            const ofbx::Skin *skin = geom->getSkin();

            // Static meshes bake both their node's own scene placement
            // (getGlobalTransform - where this Mesh object sits in the
            // FBX's node hierarchy) and their geometric matrix (a
            // mesh-local pivot/offset separate from the node transform)
            // into vertices, same as Radion's own FbxImporter combines
            // nodeXform*geomXform. A LoadAnimMesh-style skinned mesh keeps
            // vertices in the shared bind space the skin palette (bones[])
            // expects instead - same static-vs-skinned split LoaderB3D's
            // own bones-vs-plain-mesh path makes. LoadMesh's HintCollapse
            // discards the skeleton entirely (a flat handle has nowhere to
            // keep it), so a skinned mesh loaded that way bakes its node
            // transform too - the closest a rest-pose-only handle can get
            // to "where the geometry sits" without a skinning palette.
            const bool skinned = skin && skin->getClusterCount() > 0 && !collapse;
            const Transform nodeXform = skinned ? Transform() : toTransform(fbxMesh->getGlobalTransform());
            const Transform geomXform = toTransform(fbxMesh->getGeometricMatrix());
            const Transform finalXform = nodeXform * geomXform;

            ct::Vector<ct::Vector<WeightSlot>> weightsPerVertex;
            if (skinned)
            {
                weightsPerVertex.resize((size_t)vertexCount);
                for (int ci = 0; ci < skin->getClusterCount(); ++ci)
                {
                    const ofbx::Cluster *cluster = skin->getCluster(ci);
                    if (!cluster || !cluster->getLink()) continue;
                    const int *boneIndex = boneIndexById.find((unsigned long long)cluster->getLink()->id);
                    if (!boneIndex) continue;
                    const int *idx = cluster->getIndices();
                    const double *w = cluster->getWeights();
                    const int count = cluster->getIndicesCount();
                    for (int i = 0; i < count; ++i)
                        if (idx[i] >= 0 && idx[i] < vertexCount)
                            weightsPerVertex[(size_t)idx[i]].push_back({*boneIndex, (float)w[i]});
                }
            }

            // Resolved once per material slot instead of once per
            // triangle - readMaterial() loads (and, via Texture::load,
            // opens/decodes from disk) the material's texture, so calling
            // it per-triangle would re-read and re-decode the same image
            // file for every triangle sharing that material.
            ct::Vector<Brush> materials;
            materials.resize((size_t)std::max(fbxMesh->getMaterialCount(), 1));
            for (int mi2 = 0; mi2 < (int)materials.size(); ++mi2)
                materials[(size_t)mi2] = readMaterial(mi2 < fbxMesh->getMaterialCount() ? fbxMesh->getMaterial(mi2) : nullptr);

            MeshLoader::beginMesh();
            const int firstVertex = MeshLoader::numVertices();

            for (int vi = 0; vi < vertexCount; ++vi)
            {
                Surface::Vertex v;
                Vector p = toVector(srcPositions[vi]);
                Vector n = srcNormals ? toVector(srcNormals[vi]) : Vector(0, 1, 0);
                if (!skinned)
                {
                    p = finalXform * p;
                    // Rotate only (no translation) - finalXform.m already
                    // folds the node's rotation/scale, matching how
                    // LoaderB3DS applies its own conversion matrix to
                    // normals elsewhere in this engine.
                    n = finalXform.m * n;
                }
                v.coords = p;
                v.normal = n;
                v.color = ~0u;
                if (srcUvs)
                {
                    const ofbx::Vec2 &uv = srcUvs[vi];
                    v.texCoords[0][0] = v.texCoords[1][0] = (float)uv.x;
                    // FBX's V is bottom-left origin; this engine's other
                    // loaders (LoaderB3D's .b3d exporter convention, and
                    // LoaderGltf's own top-left flip) already store V
                    // flipped, so match them rather than the file's raw V.
                    v.texCoords[0][1] = v.texCoords[1][1] = 1.0f - (float)uv.y;
                }
                MeshLoader::addVertex(v);
                if (skinned) addSkinWeights(firstVertex + vi, weightsPerVertex[(size_t)vi]);
            }

            for (int ii = 0; ii + 2 < indexCount; )
            {
                // Triangulated on load (ofbx::load is called with
                // LoadFlags::TRIANGULATE below), so every polygon here is
                // already exactly 3 corners.
                const int i0 = unwindIndex(srcIndices[ii + 0]);
                const int i1 = unwindIndex(srcIndices[ii + 1]);
                const int i2 = unwindIndex(srcIndices[ii + 2]);
                ii += 3;
                if (i0 < 0 || i0 >= vertexCount || i1 < 0 || i1 >= vertexCount || i2 < 0 || i2 >= vertexCount)
                    continue;

                const int triangleIndex = (ii - 3) / 3;
                int matIndex = srcMaterials ? srcMaterials[triangleIndex] : 0;
                if (matIndex < 0 || matIndex >= (int)materials.size()) matIndex = 0;
                const Brush &brush = materials[(size_t)matIndex];

                int tri[3] = {firstVertex + i0, firstVertex + i1, firstVertex + i2};
                MeshLoader::addTriangle(tri, matIndex, brush);
            }

            MeshLoader::endMesh(mesh);
            if (!srcNormals) mesh->updateNormals();
        }

        bool isSkeletonNode(const ofbx::Object *o)
        {
            return o && (o->getType() == ofbx::Object::Type::LIMB_NODE ||
                        o->getType() == ofbx::Object::Type::NULL_NODE);
        }

        // Every ancestor of a used bone has to be a bone too (its world
        // transform anchors the chain below it), same requirement
        // LoaderB3D's own bone chunks satisfy implicitly by nesting -
        // an FBX file has no such nesting guarantee, so this walks up
        // from every LIMB_NODE/skin cluster link and marks the whole
        // chain required.
        void collectRequiredBones(const ofbx::IScene &scene, ct::Vector<const ofbx::Object *> &out)
        {
            ct::HashMap<unsigned long long, bool> required;
            auto requireChain = [&](const ofbx::Object *leaf)
            {
                for (const ofbx::Object *o = leaf; isSkeletonNode(o); o = o->getParent())
                {
                    if (required.find((unsigned long long)o->id)) return;
                    required.put((unsigned long long)o->id, true);
                }
            };

            for (int i = 0; i < scene.getAllObjectCount(); ++i)
            {
                const ofbx::Object *o = scene.getAllObjects()[i];
                if (o && o->getType() == ofbx::Object::Type::LIMB_NODE) requireChain(o);
            }
            for (int mi = 0; mi < scene.getMeshCount(); ++mi)
            {
                const ofbx::Mesh *mesh = scene.getMesh(mi);
                const ofbx::Geometry *geom = mesh ? mesh->getGeometry() : nullptr;
                const ofbx::Skin *skin = geom ? geom->getSkin() : nullptr;
                if (!skin) continue;
                for (int ci = 0; ci < skin->getClusterCount(); ++ci)
                {
                    const ofbx::Cluster *cluster = skin->getCluster(ci);
                    if (cluster) requireChain(cluster->getLink());
                }
            }

            // Topological order (parent before child) so building each
            // bone's engine::Object can parent it to an already-built one.
            ct::HashMap<unsigned long long, bool> placed;
            while (out.size() < required.size())
            {
                bool progressed = false;
                for (int i = 0; i < scene.getAllObjectCount(); ++i)
                {
                    const ofbx::Object *o = scene.getAllObjects()[i];
                    if (!o || !required.find((unsigned long long)o->id) || placed.find((unsigned long long)o->id)) continue;
                    const ofbx::Object *parent = o->getParent();
                    if (isSkeletonNode(parent) && required.find((unsigned long long)parent->id) &&
                        !placed.find((unsigned long long)parent->id))
                        continue;
                    out.push_back(o);
                    placed.put((unsigned long long)o->id, true);
                    progressed = true;
                }
                if (!progressed) break; // cyclic/unresolvable - stop rather than loop forever
            }
        }

        void applyBoneAnimation(const ofbx::Object *bone, Object *node, const ofbx::AnimationLayer *layer,
                                int &animLen)
        {
            if (!layer) return;
            const ofbx::AnimationCurveNode *posNode = layer->getCurveNode(*bone, "Lcl Translation");
            const ofbx::AnimationCurveNode *rotNode = layer->getCurveNode(*bone, "Lcl Rotation");
            const ofbx::AnimationCurveNode *sclNode = layer->getCurveNode(*bone, "Lcl Scaling");
            if (!posNode && !rotNode && !sclNode) return;

            // Union of every curve's own key times across all three
            // properties and all three axes - re-sampling each
            // AnimationCurveNode at these times (its own
            // getNodeLocalTransform already linearly interpolates between
            // its real keys) reproduces the source curves exactly without
            // this loader re-deriving per-axis interpolation itself.
            ct::Vector<float> times;
            auto collectTimes = [&](const ofbx::AnimationCurveNode *curveNode)
            {
                if (!curveNode) return;
                for (int axis = 0; axis < 3; ++axis)
                {
                    const ofbx::AnimationCurve *curve = curveNode->getCurve(axis);
                    if (!curve) continue;
                    const ofbx::i64 *keyTimes = curve->getKeyTime();
                    for (int k = 0; k < curve->getKeyCount(); ++k)
                        times.push_back((float)ofbx::fbxTimeToSeconds(keyTimes[k]));
                }
            };
            collectTimes(posNode);
            collectTimes(rotNode);
            collectTimes(sclNode);
            if (times.empty()) return;

            // Sort and dedupe (nearly-equal float seconds from different
            // curves collapse to the same integer frame anyway once
            // scaled by kFramesPerSecond, so exact float equality is not
            // required here - just enough order to skip re-adding an
            // identical frame number).
            for (size_t i = 1; i < times.size(); ++i)
            {
                float key = times[i];
                size_t j = i;
                while (j > 0 && times[j - 1] > key) { times[j] = times[j - 1]; --j; }
                times[j] = key;
            }

            const ofbx::RotationOrder order = bone->getRotationOrder();
            Animation animation = node->getAnimation();
            int lastFrame = -1;
            for (size_t i = 0; i < times.size(); ++i)
            {
                const double t = (double)times[i];
                const int frame = (int)lroundf(times[i] * kFramesPerSecond);
                if (frame == lastFrame) continue; // two source times rounding to the same tick
                lastFrame = frame;
                if (frame > animLen) animLen = frame;

                if (posNode) animation.setPositionKey(frame, toVector(posNode->getNodeLocalTransform(t)));
                if (sclNode) animation.setScaleKey(frame, toVector(sclNode->getNodeLocalTransform(t)));
                if (rotNode) animation.setRotationKey(frame, eulerQuat(toVector(rotNode->getNodeLocalTransform(t)), order));
            }
            node->setAnimation(animation);
        }
    }

    MeshModel *LoaderFbx::load(const ct::String &f, const Transform &conv, int hint, gpu::Device *dev)
    {
        (void)conv;
        const ct::String path = resolveCaseInsensitive(f);

        SDL_RWops *rw = SDL_RWFromFile(path.c_str(), "rb");
        if (!rw) return nullptr;
        const Sint64 sz = SDL_RWsize(rw);
        if (sz <= 0) { SDL_RWclose(rw); return nullptr; }
        ct::Vector<unsigned char> fileBuf((size_t)sz, 0);
        Sint64 got = 0;
        while (got < sz)
        {
            const size_t n = SDL_RWread(rw, fileBuf.data() + got, 1, (size_t)(sz - got));
            if (n == 0) break;
            got += (Sint64)n;
        }
        SDL_RWclose(rw);
        if (got != sz) return nullptr;

        ofbx::IScene *scene =
            ofbx::load(fileBuf.data(), (int)fileBuf.size(), (ofbx::u64)ofbx::LoadFlags::TRIANGULATE);
        if (!scene) return nullptr;

        g_dev = dev;
        setTexturePath(directoryOf(path));

        MeshModel *root = new MeshModel();

        // Build the bone chain first (parented Object nodes, no geometry)
        // so parseMesh's cluster->getLink() lookups resolve to an
        // already-existing engine bone index - mirrors LoaderB3D's own
        // ordering (readBone() runs before the mesh chunk it skins).
        ct::Vector<const ofbx::Object *> requiredBones;
        collectRequiredBones(*scene, requiredBones);

        ct::HashMap<unsigned long long, int> boneIndexById; // FBX object id -> 1-based bones[] slot
        ct::Vector<Object *> bones;
        bones.push_back(nullptr); // slot 0 reserved for the mesh's own root, filled in below
        ct::HashMap<unsigned long long, Object *> nodeById;
        for (size_t i = 0; i < requiredBones.size(); ++i)
        {
            const ofbx::Object *fbxBone = requiredBones[i];
            Object *node = new Object();
            if (fbxBone->name[0]) node->setName(fbxBone->name);
            node->setLocalPosition(toVector(fbxBone->getLocalTranslation()));
            node->setLocalScale(toVector(fbxBone->getLocalScaling()));
            node->setLocalRotation(eulerQuat(toVector(fbxBone->getLocalRotation()), fbxBone->getRotationOrder()));

            const ofbx::Object *parent = fbxBone->getParent();
            Object **parentNode = isSkeletonNode(parent) ? nodeById.find((unsigned long long)parent->id) : nullptr;
            node->setParent(parentNode ? *parentNode : root);

            boneIndexById.put((unsigned long long)fbxBone->id, (int)bones.size());
            bones.push_back(node);
            nodeById.put((unsigned long long)fbxBone->id, node);
        }

        const bool hasSkin = bones.size() > 1;
        const bool collapse = (hint & MeshLoader::HintCollapse) != 0;
        const bool animOnly = (hint & MeshLoader::HintAnimOnly) != 0;

        if (!animOnly)
            for (int mi = 0; mi < scene->getMeshCount(); ++mi)
                parseMesh(scene->getMesh(mi), root, boneIndexById, collapse);

        int animLen = 0;
        if (!collapse && !animOnly)
        {
            // A file can hold more than one AnimationStack (Maya/Blender
            // commonly export an empty default take alongside the real
            // one) - stack 0 is not guaranteed to be the one carrying
            // usable curves, so pick the first stack/layer that actually
            // has a curve node for one of this skeleton's own bones
            // rather than assuming index 0.
            const ofbx::AnimationLayer *layer = nullptr;
            for (int si = 0; si < scene->getAnimationStackCount() && !layer; ++si)
            {
                const ofbx::AnimationStack *stack = scene->getAnimationStack(si);
                for (int li = 0; stack; ++li)
                {
                    const ofbx::AnimationLayer *candidateLayer = stack->getLayer(li);
                    if (!candidateLayer) break;
                    for (size_t i = 0; i < requiredBones.size() && !layer; ++i)
                        if (candidateLayer->getCurveNode(*requiredBones[i], "Lcl Translation") ||
                            candidateLayer->getCurveNode(*requiredBones[i], "Lcl Rotation") ||
                            candidateLayer->getCurveNode(*requiredBones[i], "Lcl Scaling"))
                            layer = candidateLayer;
                    if (layer) break;
                }
            }
            if (layer)
                for (size_t i = 0; i < requiredBones.size(); ++i)
                    applyBoneAnimation(requiredBones[i], bones[i + 1], layer, animLen);
        }

        scene->destroy();
        setTexturePath(ct::String());

        if (collapse)
        {
            // LoadMesh's flat-handle contract: parseMesh(..., collapse)
            // already baked every mesh's own node transform straight into
            // root's vertices above (see its nodeXform/finalXform), so
            // root already IS the one flat MeshModel LoadMesh promises -
            // the bone Object nodes built earlier are pure transform
            // carriers with no geometry of their own and are simply
            // unused now, same as LoaderX/LoaderGltf's collapseMesh
            // discards a Frame node once its vertices are folded in.
            while (root->children()) delete root->children();
            return root;
        }

        if (hasSkin)
        {
            bones[0] = root;
            root->setAnimator(new Animator(bones, animLen));
            root->createBones();
        }
        else if (!requiredBones.empty())
        {
            // Unskinned but still has a bone/node hierarchy (a rig with
            // no mesh weights, or pure Frame-transform animation like
            // .x's own non-skinned path) - each bone Object already
            // carries its own Animation from applyBoneAnimation (empty
            // Animation objects if animLen ended up 0, i.e. no curves
            // were found). Attach an Animator the same way LoaderX/
            // LoaderGltf unconditionally do for their own node trees, so
            // Animate/SetAnimTime work on this handle exactly like they
            // do for an equivalent .x/.gltf file - Animator(root, ...)
            // walks root's children itself (Animator::addObjs), so it
            // picks up the same bone Objects "bones" already holds.
            root->setAnimator(new Animator(root, animLen));
        }

        return root;
    }
}
