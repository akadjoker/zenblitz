/*
** LoaderGltf.cpp — see LoaderGltf.h. Walks the cgltf_data tree cgltf_parse
** builds instead of hand-parsing the format: cgltf already resolves
** accessors/bufferViews/sparse data/normalization, which is exactly the
** class of bookkeeping LoaderX's own xfile parser had to do by hand for
** .x. Not a port of anything in the original Blitz3D (glTF postdates it) -
** see [[project-gltf-fbx-extension]] memory note for why this exists.
*/
#include "engine/LoaderGltf.h"
#include "engine/FilePath.h"
#include "engine/MeshModel.h"
#include "engine/Animator.h"
#include "engine/Texture.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include <SDL2/SDL_rwops.h>
#include <ct/vector.hpp>
#include <ct/hashmap.hpp>
#include <cstring>
#include <cmath>

namespace engine
{
    namespace
    {
        // glTF animation times are float seconds; Animator/Animation take
        // integer "frames" with no fixed real-world fps (the original
        // .b3d/.x loaders pass a file's raw integer keyframe straight
        // through, whatever tick rate the exporting tool used - see
        // [[project-gltf-fbx-extension]]). 30 fps matches .b3d's de-facto
        // convention.
        constexpr float kFramesPerSecond = 30.0f;

        ct::Vector<Texture *> g_textures;
        gpu::Device *g_dev;
        // The cgltf_options this file's load() built, kept for the
        // duration of one parse so readMaterial's base64-image decode
        // (cgltf_load_buffer_base64) can reach the same allocator
        // callbacks cgltf_parse/cgltf_load_buffers used.
        const cgltf_options *g_options;

        // cgltf's file callbacks, routed through SDL_RWops - this engine
        // never calls fopen/std::ifstream directly (see the
        // cross-platform I/O project rule).
        cgltf_result gltfFileRead(const cgltf_memory_options *, const cgltf_file_options *,
                                  const char *path, cgltf_size *size, void **data)
        {
            SDL_RWops *rw = SDL_RWFromFile(path, "rb");
            if (!rw) return cgltf_result_file_not_found;
            const Sint64 sz = SDL_RWsize(rw);
            if (sz <= 0) { SDL_RWclose(rw); return cgltf_result_io_error; }
            void *buf = malloc((size_t)sz);
            if (!buf) { SDL_RWclose(rw); return cgltf_result_out_of_memory; }
            Sint64 got = 0;
            while (got < sz)
            {
                const size_t n = SDL_RWread(rw, (unsigned char *)buf + got, 1, (size_t)(sz - got));
                if (n == 0) break;
                got += (Sint64)n;
            }
            SDL_RWclose(rw);
            if (got != sz) { free(buf); return cgltf_result_io_error; }
            *size = (cgltf_size)sz;
            *data = buf;
            return cgltf_result_success;
        }
        void gltfFileRelease(const cgltf_memory_options *, const cgltf_file_options *, void *data)
        {
            free(data);
        }

        ct::String directoryOf(const ct::String &path)
        {
            const size_t slash = path.find_last_of("/\\");
            return slash == ct::String::npos ? ct::String() : path.substr(0, slash);
        }

        // FrameTransformMatrix's node TRS (or an explicit 4x4 matrix) in the
        // same row-major (rows i/j/k, translation v) layout LoaderX's own
        // readFrameTransform builds for .x FrameTransformMatrix.
        Transform nodeLocalTransform(const cgltf_node &node)
        {
            float m[16];
            cgltf_node_transform_local(&node, m);
            return Transform(blitz::Matrix(
                                  Vector(m[0], m[1], m[2]),
                                  Vector(m[4], m[5], m[6]),
                                  Vector(m[8], m[9], m[10])),
                             Vector(m[12], m[13], m[14]));
        }

        // A glTF image is a file path (uri), a base64-encoded "data:" uri,
        // or (the common case for .glb, whose whole point is packing
        // everything into one file) bytes living in a buffer_view. All
        // three decode to the same in-memory buffer Texture::loadFromMemory
        // takes - only the plain-file-uri case can skip decoding and use
        // the cheaper Texture::load(path) instead.
        Texture *loadImageTexture(const cgltf_image *img)
        {
            if (!img) return nullptr;

            if (img->buffer_view)
            {
                const cgltf_buffer_view &view = *img->buffer_view;
                const unsigned char *base = view.data
                    ? (const unsigned char *)view.data
                    : (view.buffer ? (const unsigned char *)view.buffer->data : nullptr);
                if (base)
                    return Texture::loadFromMemory(img->name ? img->name : "", base + (view.data ? 0 : view.offset),
                                                   (unsigned)view.size, 0);
                return nullptr;
            }

            if (img->uri && std::strncmp(img->uri, "data:", 5) == 0)
            {
                const char *comma = std::strchr(img->uri, ',');
                if (comma && comma - img->uri >= 7 && std::strncmp(comma - 7, ";base64", 7) == 0)
                {
                    void *decoded = nullptr;
                    // The size cgltf_load_buffer_base64 wants is an upper
                    // bound on the decoded byte count, not a length it
                    // trusts blindly - 3 decoded bytes per 4 base64 chars
                    // covers it with room to spare for padding.
                    const cgltf_size approxSize = (cgltf_size)std::strlen(comma + 1) / 4 * 3 + 3;
                    if (g_options && cgltf_load_buffer_base64(g_options, approxSize, comma + 1, &decoded) ==
                                         cgltf_result_success)
                    {
                        Texture *tex = Texture::loadFromMemory(img->name ? img->name : "",
                                                               (const unsigned char *)decoded,
                                                               (unsigned)approxSize, 0);
                        free(decoded);
                        return tex;
                    }
                }
                return nullptr;
            }

            if (img->uri && img->uri[0])
                return Texture::load(img->uri, 0);

            return nullptr;
        }

        Brush readMaterial(const cgltf_material *mat)
        {
            Brush brush;
            if (!mat) return brush;

            if (mat->has_pbr_metallic_roughness)
            {
                const cgltf_pbr_metallic_roughness &pbr = mat->pbr_metallic_roughness;
                brush.setColor(Vector(pbr.base_color_factor[0], pbr.base_color_factor[1],
                                      pbr.base_color_factor[2]));
                brush.setAlpha(pbr.base_color_factor[3]);

                if (const cgltf_texture *tex = pbr.base_color_texture.texture)
                {
                    Texture *loaded = loadImageTexture(tex->image);
                    if (loaded)
                    {
                        if (g_dev)
                            brush.setTexture(0, BrushTexture::fromTexture(*g_dev, loaded, 0));
                        brush.setColor(Vector(1, 1, 1));
                        // kept alive for the process lifetime, same
                        // contract as LoaderX's g_textures - see
                        // LoaderX.cpp's declaration comment for why
                        // releasing here would free a GPU texture a
                        // still-live mesh just bound.
                        g_textures.push_back(loaded);
                    }
                }
            }
            else
                brush.setColor(Vector(1, 1, 1));

            if (mat->alpha_mode == cgltf_alpha_mode_blend) brush.setBlend(BlendAlpha);
            if (mat->double_sided) brush.setFX(brush.getFX() | FxDoubleSided);

            return brush;
        }

        // Reads one accessor fully into a temporary float buffer via
        // cgltf's own unpack (handles sparse accessors and normalized
        // integer component types, e.g. UNSIGNED_BYTE/SHORT joint
        // weights, without this loader re-deriving cgltf's denormalization
        // table - see cgltf/VERSION's caller-contract note).
        void unpackFloats(const cgltf_accessor *acc, ct::Vector<float> &out, int components)
        {
            out.resize(0);
            if (!acc) return;
            out.resize((size_t)acc->count * (size_t)components);
            cgltf_accessor_unpack_floats(acc, out.data(), out.size());
        }

        void unpackIndices(const cgltf_accessor *acc, ct::Vector<unsigned> &out)
        {
            out.resize(0);
            if (!acc) return;
            out.resize(acc->count);
            cgltf_accessor_unpack_indices(acc, out.data(), sizeof(unsigned), out.size());
        }

        void parsePrimitive(const cgltf_primitive &prim, MeshModel *mesh)
        {
            if (prim.type != cgltf_primitive_type_triangles) return;

            const cgltf_accessor *posAcc = nullptr, *normAcc = nullptr, *uvAcc = nullptr;
            const cgltf_accessor *jointsAcc = nullptr, *weightsAcc = nullptr;
            for (cgltf_size i = 0; i < prim.attributes_count; ++i)
            {
                const cgltf_attribute &attr = prim.attributes[i];
                switch (attr.type)
                {
                case cgltf_attribute_type_position: posAcc = attr.data; break;
                case cgltf_attribute_type_normal: normAcc = attr.data; break;
                case cgltf_attribute_type_texcoord: if (!uvAcc) uvAcc = attr.data; break;
                case cgltf_attribute_type_joints: if (!jointsAcc) jointsAcc = attr.data; break;
                case cgltf_attribute_type_weights: if (!weightsAcc) weightsAcc = attr.data; break;
                default: break;
                }
            }
            if (!posAcc || posAcc->count == 0) return;

            // A malformed/hand-edited file can declare an attribute
            // accessor with fewer elements than POSITION; every per-vertex
            // read below indexes these arrays by the position vertex
            // index, so treat a short accessor as absent rather than
            // reading past its end.
            if (normAcc && normAcc->count < posAcc->count) normAcc = nullptr;
            if (uvAcc && uvAcc->count < posAcc->count) uvAcc = nullptr;
            if (jointsAcc && jointsAcc->count < posAcc->count) jointsAcc = nullptr;
            if (weightsAcc && weightsAcc->count < posAcc->count) weightsAcc = nullptr;

            ct::Vector<float> positions, normals, uvs, joints, weights;
            unpackFloats(posAcc, positions, 3);
            unpackFloats(normAcc, normals, 3);
            unpackFloats(uvAcc, uvs, 2);
            unpackFloats(jointsAcc, joints, 4);
            unpackFloats(weightsAcc, weights, 4);

            ct::Vector<unsigned> indices;
            unpackIndices(prim.indices, indices);

            const Brush brush = readMaterial(prim.material);

            MeshLoader::beginMesh();
            const int firstVertex = MeshLoader::numVertices();
            const int vertexCount = (int)posAcc->count;
            const bool haveNormals = !normals.empty();
            const bool haveSkin = !joints.empty() && !weights.empty();

            for (int i = 0; i < vertexCount; ++i)
            {
                Surface::Vertex v;
                v.coords = Vector(positions[i * 3 + 0], positions[i * 3 + 1], positions[i * 3 + 2]);
                v.normal = haveNormals
                               ? Vector(normals[i * 3 + 0], normals[i * 3 + 1], normals[i * 3 + 2])
                               : Vector(0, 1, 0);
                v.color = ~0u;
                if (!uvs.empty())
                {
                    v.texCoords[0][0] = v.texCoords[1][0] = uvs[i * 2 + 0];
                    v.texCoords[0][1] = v.texCoords[1][1] = uvs[i * 2 + 1];
                }
                if (haveSkin)
                {
                    for (int slot = 0; slot < kMaxSurfaceBones; ++slot)
                    {
                        v.boneBones[slot] = (unsigned char)joints[i * 4 + slot];
                        v.boneWeights[slot] = weights[i * 4 + slot];
                    }
                }
                MeshLoader::addVertex(v);
            }

            const int triCount = indices.empty() ? vertexCount / 3 : (int)indices.size() / 3;
            for (int t = 0; t < triCount; ++t)
            {
                int tri[3];
                if (indices.empty())
                {
                    tri[0] = firstVertex + t * 3 + 0;
                    tri[1] = firstVertex + t * 3 + 1;
                    tri[2] = firstVertex + t * 3 + 2;
                }
                else
                {
                    tri[0] = firstVertex + (int)indices[t * 3 + 0];
                    tri[1] = firstVertex + (int)indices[t * 3 + 1];
                    tri[2] = firstVertex + (int)indices[t * 3 + 2];
                }
                MeshLoader::addTriangle(tri, 0, brush);
            }

            MeshLoader::endMesh(mesh);
            if (!haveNormals) mesh->updateNormals();
        }

        MeshModel *parseNode(const cgltf_node *node, ct::HashMap<const cgltf_node *, MeshModel *> &byNode,
                            bool animOnly)
        {
            MeshModel *e = new MeshModel();
            if (node->name && node->name[0]) e->setName(node->name);
            e->setLocalTform(nodeLocalTransform(*node));
            byNode.put(node, e);

            if (node->mesh && !animOnly)
                for (cgltf_size p = 0; p < node->mesh->primitives_count; ++p)
                    parsePrimitive(node->mesh->primitives[p], e);

            for (cgltf_size c = 0; c < node->children_count; ++c)
            {
                MeshModel *sub = parseNode(node->children[c], byNode, animOnly);
                sub->setParent(e);
            }
            return e;
        }

        // AnimationSampler input/output pairs, per target node/path
        // (translation/rotation/scale), folded into the MeshModel's own
        // Animation the same way LoaderX's parseAnimKey does for a .x
        // AnimationKey block.
        void applyAnimation(const cgltf_animation &anim, ct::HashMap<const cgltf_node *, MeshModel *> &byNode,
                            int &outAnimLen)
        {
            for (cgltf_size c = 0; c < anim.channels_count; ++c)
            {
                const cgltf_animation_channel &channel = anim.channels[c];
                if (!channel.target_node || !channel.sampler) continue;
                MeshModel **found = byNode.find(channel.target_node);
                if (!found) continue;
                MeshModel *target = *found;

                const cgltf_animation_sampler &sampler = *channel.sampler;
                ct::Vector<float> times, values;
                unpackFloats(sampler.input, times, 1);
                const int components = channel.target_path == cgltf_animation_path_type_rotation ? 4
                                       : channel.target_path == cgltf_animation_path_type_weights ? 1
                                                                                                    : 3;
                unpackFloats(sampler.output, values, components);
                if (times.empty() || values.empty()) continue;

                // cubic-spline samplers store (in-tangent, value,
                // out-tangent) triplets per key; only the value is used
                // here, same simplification LoaderX/LoaderB3D already make
                // (neither carries tangent data into engine::Animation).
                const bool cubic = sampler.interpolation == cgltf_interpolation_type_cubic_spline;
                const int stride = cubic ? 3 : 1;

                Animation animation = target->getAnimation();
                for (cgltf_size k = 0; k < times.size() && k * (cgltf_size)stride < values.size() / (cgltf_size)components; ++k)
                {
                    const int frame = (int)std::lround(times[k] * kFramesPerSecond);
                    if (frame > outAnimLen) outAnimLen = frame;
                    const cgltf_size v = (cgltf_size)(k * stride + (cubic ? 1 : 0)) * (cgltf_size)components;

                    switch (channel.target_path)
                    {
                    case cgltf_animation_path_type_translation:
                        animation.setPositionKey(frame, Vector(values[v + 0], values[v + 1], values[v + 2]));
                        break;
                    case cgltf_animation_path_type_scale:
                        animation.setScaleKey(frame, Vector(values[v + 0], values[v + 1], values[v + 2]));
                        break;
                    case cgltf_animation_path_type_rotation:
                        // glTF quaternions are (x,y,z,w); engine::Quat is (w, xyz).
                        animation.setRotationKey(
                            frame, Quat(values[v + 3], Vector(values[v + 0], values[v + 1], values[v + 2])));
                        break;
                    default:
                        break;
                    }
                }
                target->setAnimation(animation);
            }
        }

        // bbLoadMesh's collapseMesh, same as LoaderX's own copy: LoadMesh
        // wants one flat MeshModel a script can ScaleMesh/FlipMesh/FitMesh
        // directly, not a node hierarchy only LoadAnimMesh callers walk.
        void collapseMesh(MeshModel *dest, Entity *e)
        {
            while (e->children()) collapseMesh(dest, e->children());
            if (Model *m = e->getModel())
                if (MeshModel *t = m->getMeshModel())
                {
                    t->transform(e->getWorldTform());
                    dest->add(*t);
                }
            delete e;
        }
    }

    MeshModel *LoaderGltf::load(const ct::String &f, const Transform &conv, int hint, gpu::Device *dev)
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

        cgltf_options options;
        std::memset(&options, 0, sizeof(options));
        options.file.read = gltfFileRead;
        options.file.release = gltfFileRelease;
        g_options = &options;

        cgltf_data *data = nullptr;
        if (cgltf_parse(&options, fileBuf.data(), fileBuf.size(), &data) != cgltf_result_success)
        {
            g_options = nullptr;
            return nullptr;
        }

        // cgltf_load_buffers' gltf_path is combined with a URI by
        // cgltf_combine_paths, which keeps everything up to (and
        // including) base's LAST slash - so it wants the .gltf file's own
        // full path here, not a pre-stripped directory (passing just the
        // directory drops its own last path component, since that becomes
        // the string this trims after the slash it finds).
        if (cgltf_load_buffers(&options, data, path.c_str()) != cgltf_result_success)
        {
            cgltf_free(data);
            g_options = nullptr;
            return nullptr;
        }

        g_dev = dev;
        setTexturePath(directoryOf(path));

        MeshModel *root = new MeshModel();
        ct::HashMap<const cgltf_node *, MeshModel *> byNode;

        const bool collapse = (hint & MeshLoader::HintCollapse) != 0;
        // Unlike LoadMesh, LoadAnimSeq only wants Animation keys out of a
        // file, not geometry - skip building any mesh/texture data the
        // same way LoaderX's parseTopLevel gates parseMesh on !animOnly,
        // so this hint actually avoids the vertex/texture work it exists
        // to avoid, instead of doing it and throwing it away.
        const bool animOnly = (hint & MeshLoader::HintAnimOnly) != 0;

        const cgltf_scene *scene = data->scene ? data->scene : (data->scenes_count ? &data->scenes[0] : nullptr);
        if (scene)
        {
            for (cgltf_size i = 0; i < scene->nodes_count; ++i)
            {
                MeshModel *sub = parseNode(scene->nodes[i], byNode, animOnly);
                sub->setParent(root);
            }
        }
        else
        {
            // No scene declared: fall back to every root node (no parent),
            // same leniency LoaderX shows a .x file with a bare top-level
            // Mesh and no enclosing Frame.
            for (cgltf_size i = 0; i < data->nodes_count; ++i)
                if (!data->nodes[i].parent)
                {
                    MeshModel *sub = parseNode(&data->nodes[i], byNode, animOnly);
                    sub->setParent(root);
                }
        }

        int animLen = 0;
        if (!collapse)
            for (cgltf_size i = 0; i < data->animations_count; ++i)
                applyAnimation(data->animations[i], byNode, animLen);

        cgltf_free(data);
        g_options = nullptr;
        setTexturePath(ct::String());

        if (!collapse) root->setAnimator(new Animator(root, animLen));
        else
            while (root->children()) collapseMesh(root, root->children());

        return root;
    }
}
