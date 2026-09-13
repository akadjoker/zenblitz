 
#ifndef ENGINE_DYNAMIC_BVH_H
#define ENGINE_DYNAMIC_BVH_H

#include "engine/Geom.h"

#include <ct/pool.hpp>
#include <ct/vector.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>

namespace engine
{
  using blitz::Vector;
  using blitz::Box;
  using blitz::Plane;

  namespace bvh_detail
  {
    inline Vector vecMin(const Vector &a, const Vector &b)
    {
      return Vector(a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y, a.z < b.z ? a.z : b.z);
    }
    inline Vector vecMax(const Vector &a, const Vector &b)
    {
      return Vector(a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y, a.z > b.z ? a.z : b.z);
    }
    inline float vecDot(const Vector &a, const Vector &b) { return a.dot(b); }
    /* Kinetix's Vec3 indexed by component; blitz::Vector has operator[] */
    inline float component(const Vector &v, int i) { return v[i]; }
  }
  using bvh_detail::vecMin;
  using bvh_detail::vecMax;
  using bvh_detail::vecDot;

  class DynamicBVH
  {
    struct Node;

  public:
    struct ID
    {
      Node *node = nullptr;
      bool valid() const { return node != nullptr; }
    };

  private:
    struct Volume
    {
      Vector min, max;

      Vector center() const { return (min + max) * 0.5f; }
      Vector length() const { return max - min; }

      bool contains(const Volume &other) const
      {
        return min.x <= other.min.x && min.y <= other.min.y && min.z <= other.min.z && max.x >= other.max.x &&
               max.y >= other.max.y && max.z >= other.max.z;
      }

      Volume merge(const Volume &other) const
      {
        Volume r;
        r.min = vecMin(min, other.min);
        r.max = vecMax(max, other.max);
        return r;
      }

      float size() const
      {
        const Vector edges = length();
        return edges.x * edges.y * edges.z + edges.x + edges.y + edges.z;
      }

      bool notEqualTo(const Volume &other) const
      {
        return min.x != other.min.x || min.y != other.min.y || min.z != other.min.z || max.x != other.max.x ||
               max.y != other.max.y || max.z != other.max.z;
      }

      float proximityTo(const Volume &other) const
      {
        const Vector d = (min + max) - (other.min + other.max);
        return std::fabs(d.x) + std::fabs(d.y) + std::fabs(d.z);
      }

      int selectByProximity(const Volume &a, const Volume &b, std::uint32_t &tiebreaker) const
      {
        const float proxA = proximityTo(a);
        const float proxB = proximityTo(b);
        if (proxA == proxB)
        {
          tiebreaker *= 1664525u;
          return (tiebreaker % 48271u) & 1;
        }
        return proxA < proxB ? 0 : 1;
      }

      bool intersects(const Volume &other) const
      {
        return min.x <= other.max.x && max.x >= other.min.x && min.y <= other.max.y && max.y >= other.min.y &&
               min.z <= other.max.z && max.z >= other.min.z;
      }

      bool intersectsConvex(const Plane *planes, int planeCount, const Vector *points,
                            int pointCount) const;
    };

    struct Node
    {
      Volume volume;
      Node *parent = nullptr;
      union
      {
        Node *children[2];
        void *data;
      };

      bool isLeaf() const { return children[1] == nullptr; }
      bool isInternal() const { return !isLeaf(); }

      int indexInParent() const { return parent->children[1] == this ? 1 : 0; }
      void maxDepth(int depth, int &outMaxDepth) const
      {
        if (isInternal())
        {
          children[0]->maxDepth(depth + 1, outMaxDepth);
          children[1]->maxDepth(depth + 1, outMaxDepth);
        }
        else
        {
          outMaxDepth = outMaxDepth > depth ? outMaxDepth : depth;
        }
      }

      int countLeaves() const
      {
        return isInternal() ? children[0]->countLeaves() + children[1]->countLeaves() : 1;
      }

      bool isLeftOfAxis(const Vector &origin, const Vector &axis) const
      {
        return vecDot(axis, volume.center() - origin) <= 0.0f;
      }

      Node()
      {
        children[0] = nullptr;
        children[1] = nullptr;
      }
    };

    ct::Pool<Node> mNodeAllocator;
    Node *mRoot = nullptr;
    int mLookahead = -1;
    int mTotalLeaves = 0;
    std::uint32_t mOpath = 0;
    std::uint32_t mIndex = 0;
    std::uint32_t mTiebreaker = 134775813u;

    static constexpr int kAllocaStackSize = 128;

    void deleteNode(Node *node);
    void recurseDeleteNode(Node *node);
    Node *createNode(Node *parent, void *data);
    Node *createNodeWithVolume(Node *parent, const Volume &volume, void *data);
    void insertLeaf(Node *root, Node *leaf);
    Node *removeLeaf(Node *leaf);
    void fetchLeaves(Node *root, ct::Vector<Node *> &outLeaves, int depth = -1);
    static int split(Node **leaves, int count, const Vector &origin, const Vector &axis);
    static Volume bounds(Node **leaves, int count);
    void bottomUp(Node **leaves, int count);
    Node *topDown(Node **leaves, int count, int bottomUpThreshold);
    Node *nodeSort(Node *node, Node *&outRoot);
    void update(Node *leaf, int lookahead = -1);
    void extractLeaves(Node *node, ct::Vector<ID> &outElements);
    static bool rayAabb(const Vector &rayFrom, const Vector &rayInvDirection,
                        const unsigned int raySign[3], const Vector bounds[2], float &outTmin,
                        float lambdaMin, float lambdaMax);

  public:
    DynamicBVH() = default;
    ~DynamicBVH();

    void clear();
    bool isEmpty() const { return mRoot == nullptr; }
    void optimizeBottomUp();
    void optimizeTopDown(int bottomUpThreshold = 128);
    void optimizeIncremental(int passes);
    bool rootBounds(Vector &outMin, Vector &outMax) const
    {
      if (!mRoot)
        return false;
      outMin = mRoot->volume.min;
      outMax = mRoot->volume.max;
      return true;
    }
    ID insert(const Box &box, void *userdata);
    bool update(const ID &id, const Box &box);
    void remove(const ID &id);
    void getElements(ct::Vector<ID> &outElements);

    int leafCount() const { return mTotalLeaves; }
    int maxDepth() const;

    template <typename QueryResult>
    void aabbQuery(const Box &box, QueryResult &result) const;
    template <typename QueryResult>
    void convexQuery(const Plane *planes, int planeCount, const Vector *points, int pointCount,
                     QueryResult &result) const;
    template <typename QueryResult>
    void rayQuery(const Vector &from, const Vector &to, QueryResult &result) const;

    void setIndex(std::uint32_t index) { mIndex = index; }
    std::uint32_t index() const { return mIndex; }
  };

  template <typename QueryResult>
  void DynamicBVH::aabbQuery(const Box &box, QueryResult &result) const
  {
    if (!mRoot)
      return;

    Volume volume;
    volume.min = box.a;
    volume.max = box.b;

    const Node **allocaStack = static_cast<const Node **>(alloca(kAllocaStackSize * sizeof(const Node *)));
    const Node **stack = allocaStack;
    stack[0] = mRoot;
    std::int32_t depth = 1;
    std::int32_t threshold = kAllocaStackSize - 2;

    ct::Vector<const Node *> auxStack;

    do
    {
      --depth;
      const Node *n = stack[depth];
      if (n->volume.intersects(volume))
      {
        if (n->isInternal())
        {
          if (depth > threshold)
          {
            if (auxStack.empty())
            {
              auxStack.resize(kAllocaStackSize * 2);
              std::memcpy(auxStack.data(), allocaStack, kAllocaStackSize * sizeof(const Node *));
              allocaStack = nullptr;
            }
            else
            {
              auxStack.resize(auxStack.size() * 2);
            }
            stack = auxStack.data();
            threshold = static_cast<std::int32_t>(auxStack.size()) - 2;
          }
          stack[depth++] = n->children[0];
          stack[depth++] = n->children[1];
        }
        else if (result(n->data))
        {
          return;
        }
      }
    } while (depth > 0);
  }

  template <typename QueryResult>
  void DynamicBVH::convexQuery(const Plane *planes, int planeCount, const Vector *points, int pointCount,
                               QueryResult &result) const
  {
    if (!mRoot)
      return;

    Volume volume;
    for (int i = 0; i < pointCount; ++i)
    {
      if (i == 0)
      {
        volume.min = points[0];
        volume.max = points[0];
      }
      else
      {
        volume.min = vecMin(volume.min, points[i]);
        volume.max = vecMax(volume.max, points[i]);
      }
    }

    const Node **allocaStack = static_cast<const Node **>(alloca(kAllocaStackSize * sizeof(const Node *)));
    const Node **stack = allocaStack;
    stack[0] = mRoot;
    std::int32_t depth = 1;
    std::int32_t threshold = kAllocaStackSize - 2;

    ct::Vector<const Node *> auxStack;

    do
    {
      --depth;
      const Node *n = stack[depth];
      if (n->volume.intersects(volume) && n->volume.intersectsConvex(planes, planeCount, points, pointCount))
      {
        if (n->isInternal())
        {
          if (depth > threshold)
          {
            if (auxStack.empty())
            {
              auxStack.resize(kAllocaStackSize * 2);
              std::memcpy(auxStack.data(), allocaStack, kAllocaStackSize * sizeof(const Node *));
              allocaStack = nullptr;
            }
            else
            {
              auxStack.resize(auxStack.size() * 2);
            }
            stack = auxStack.data();
            threshold = static_cast<std::int32_t>(auxStack.size()) - 2;
          }
          stack[depth++] = n->children[0];
          stack[depth++] = n->children[1];
        }
        else if (result(n->data))
        {
          return;
        }
      }
    } while (depth > 0);
  }

  template <typename QueryResult>
  void DynamicBVH::rayQuery(const Vector &from, const Vector &to, QueryResult &result) const
  {
    if (!mRoot)
      return;

    Vector rayDir = to - from;
    rayDir = rayDir.normalized();

    Vector invDir;
    invDir.x = rayDir.x == 0.0f ? 1e20f : 1.0f / rayDir.x;
    invDir.y = rayDir.y == 0.0f ? 1e20f : 1.0f / rayDir.y;
    invDir.z = rayDir.z == 0.0f ? 1e20f : 1.0f / rayDir.z;
    const unsigned int signs[3] = {invDir.x < 0.0f, invDir.y < 0.0f, invDir.z < 0.0f};

    const float lambdaMax = vecDot(rayDir, to - from);

    Vector bounds[2];

    const Node **allocaStack = static_cast<const Node **>(alloca(kAllocaStackSize * sizeof(const Node *)));
    const Node **stack = allocaStack;
    stack[0] = mRoot;
    std::int32_t depth = 1;
    std::int32_t threshold = kAllocaStackSize - 2;

    ct::Vector<const Node *> auxStack;

    do
    {
      --depth;
      const Node *node = stack[depth];
      bounds[0] = node->volume.min;
      bounds[1] = node->volume.max;
      float tmin = 1.0f;
      const float lambdaMin = 0.0f;
      const bool hit = rayAabb(from, invDir, signs, bounds, tmin, lambdaMin, lambdaMax);
      if (hit)
      {
        if (node->isInternal())
        {
          if (depth > threshold)
          {
            if (auxStack.empty())
            {
              auxStack.resize(kAllocaStackSize * 2);
              std::memcpy(auxStack.data(), allocaStack, kAllocaStackSize * sizeof(const Node *));
              allocaStack = nullptr;
            }
            else
            {
              auxStack.resize(auxStack.size() * 2);
            }
            stack = auxStack.data();
            threshold = static_cast<std::int32_t>(auxStack.size()) - 2;
          }
          stack[depth++] = node->children[0];
          stack[depth++] = node->children[1];
        }
        else if (result(node->data))
        {
          return;
        }
      }
    } while (depth > 0);
  }

} // namespace engine

#endif
