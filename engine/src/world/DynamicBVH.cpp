 
#include "engine/DynamicBVH.h"
#include "engine/Log.h"

namespace engine
{

bool DynamicBVH::Volume::intersectsConvex(const Plane *planes, int planeCount, const Vector *points,
                                          int pointCount) const
{
  const Vector halfExtents = (max - min) * 0.5f;
  const Vector ofs = min + halfExtents;

  for (int i = 0; i < planeCount; ++i)
  {
    const Plane &p = planes[i];
    Vector point(p.n.x >= 0.0f ? halfExtents.x : -halfExtents.x,
                     p.n.y >= 0.0f ? halfExtents.y : -halfExtents.y,
                     p.n.z >= 0.0f ? halfExtents.z : -halfExtents.z);
    point = point + ofs;
    if (p.distance(point) < 0.0f)
      return false;
  }

  int badPointCountsPositive[3] = {0, 0, 0};
  int badPointCountsNegative[3] = {0, 0, 0};

  for (int k = 0; k < 3; ++k)
  {
    for (int i = 0; i < pointCount; ++i)
    {
      const float value = bvh_detail::component(points[i], k);
      const float upper = bvh_detail::component(ofs, k) + bvh_detail::component(halfExtents, k);
      const float lower = bvh_detail::component(ofs, k) - bvh_detail::component(halfExtents, k);
      if (value > upper)
        ++badPointCountsPositive[k];
      if (value < lower)
        ++badPointCountsNegative[k];
    }

    if (badPointCountsNegative[k] == pointCount)
      return false;
    if (badPointCountsPositive[k] == pointCount)
      return false;
  }

  return true;
}

void DynamicBVH::deleteNode(Node *node) { mNodeAllocator.destroy(node); }

void DynamicBVH::recurseDeleteNode(Node *node)
{
  if (!node->isLeaf())
  {
    recurseDeleteNode(node->children[0]);
    recurseDeleteNode(node->children[1]);
  }
  if (node == mRoot)
    mRoot = nullptr;
  deleteNode(node);
}

DynamicBVH::Node *DynamicBVH::createNode(Node *parent, void *data)
{
  Node *node = mNodeAllocator.create();
  node->parent = parent;
  node->data = data;
  return node;
}

DynamicBVH::Node *DynamicBVH::createNodeWithVolume(Node *parent, const Volume &volume, void *data)
{
  Node *node = createNode(parent, data);
  node->volume = volume;
  return node;
}

void DynamicBVH::insertLeaf(Node *root, Node *leaf)
{
  if (!mRoot)
  {
    mRoot = leaf;
    leaf->parent = nullptr;
    return;
  }

  if (!root->isLeaf())
  {
    do
    {
      root = root->children[leaf->volume.selectByProximity(root->children[0]->volume, root->children[1]->volume,
                                                            mTiebreaker)];
    } while (!root->isLeaf());
  }

  Node *prev = root->parent;
  Node *node = createNodeWithVolume(prev, leaf->volume.merge(root->volume), nullptr);
  if (prev)
  {
    prev->children[root->indexInParent()] = node;
    node->children[0] = root;
    root->parent = node;
    node->children[1] = leaf;
    leaf->parent = node;
    do
    {
      if (!prev->volume.contains(node->volume))
        prev->volume = prev->children[0]->volume.merge(prev->children[1]->volume);
      else
        break;
      node = prev;
    } while (nullptr != (prev = node->parent));
  }
  else
  {
    node->children[0] = root;
    root->parent = node;
    node->children[1] = leaf;
    leaf->parent = node;
    mRoot = node;
  }
}

DynamicBVH::Node *DynamicBVH::removeLeaf(Node *leaf)
{
  if (leaf == mRoot)
  {
    mRoot = nullptr;
    return nullptr;
  }

  Node *parent = leaf->parent;
  Node *prev = parent->parent;
  Node *sibling = parent->children[1 - leaf->indexInParent()];
  if (prev)
  {
    prev->children[parent->indexInParent()] = sibling;
    sibling->parent = prev;
    deleteNode(parent);
    while (prev)
    {
      const Volume pb = prev->volume;
      prev->volume = prev->children[0]->volume.merge(prev->children[1]->volume);
      if (pb.notEqualTo(prev->volume))
        prev = prev->parent;
      else
        break;
    }
    return prev ? prev : mRoot;
  }

  mRoot = sibling;
  sibling->parent = nullptr;
  deleteNode(parent);
  return mRoot;
}

void DynamicBVH::fetchLeaves(Node *root, ct::Vector<Node *> &outLeaves, int depth)
{
  if (root->isInternal() && depth)
  {
    fetchLeaves(root->children[0], outLeaves, depth - 1);
    fetchLeaves(root->children[1], outLeaves, depth - 1);
    deleteNode(root);
  }
  else
  {
    outLeaves.push_back(root);
  }
}

int DynamicBVH::split(Node **leaves, int count, const Vector &origin, const Vector &axis)
{
  int begin = 0;
  int end = count;
  for (;;)
  {
    while (begin != end && leaves[begin]->isLeftOfAxis(origin, axis))
      ++begin;

    if (begin == end)
      break;

    while (begin != end && !leaves[end - 1]->isLeftOfAxis(origin, axis))
      --end;

    if (begin == end)
      break;

    --end;
    Node *temp = leaves[begin];
    leaves[begin] = leaves[end];
    leaves[end] = temp;
    ++begin;
  }

  return begin;
}

DynamicBVH::Volume DynamicBVH::bounds(Node **leaves, int count)
{
  Volume volume = leaves[0]->volume;
  for (int i = 1; i < count; ++i)
    volume = volume.merge(leaves[i]->volume);
  return volume;
}

void DynamicBVH::bottomUp(Node **leaves, int count)
{
  while (count > 1)
  {
    float minsize = std::numeric_limits<float>::infinity();
    int minidx[2] = {-1, -1};
    for (int i = 0; i < count; ++i)
    {
      for (int j = i + 1; j < count; ++j)
      {
        const float sz = leaves[i]->volume.merge(leaves[j]->volume).size();
        if (sz < minsize)
        {
          minsize = sz;
          minidx[0] = i;
          minidx[1] = j;
        }
      }
    }
    Node *n[] = {leaves[minidx[0]], leaves[minidx[1]]};
    Node *p = createNodeWithVolume(nullptr, n[0]->volume.merge(n[1]->volume), nullptr);
    p->children[0] = n[0];
    p->children[1] = n[1];
    n[0]->parent = p;
    n[1]->parent = p;
    leaves[minidx[0]] = p;
    leaves[minidx[1]] = leaves[count - 1];
    --count;
  }
}

DynamicBVH::Node *DynamicBVH::topDown(Node **leaves, int count, int bottomUpThreshold)
{
  static const Vector axis[] = {Vector(1, 0, 0), Vector(0, 1, 0), Vector(0, 0, 1)};

  if (bottomUpThreshold <= 1)
  {
    Log::error("DynamicBVH: bottomUpThreshold invalido");
    return nullptr;
  }

  if (count > 1)
  {
    if (count > bottomUpThreshold)
    {
      const Volume vol = bounds(leaves, count);
      const Vector org = vol.center();
      int partition;
      int bestaxis = -1;
      int bestmidp = count;
      int splitcount[3][2] = {{0, 0}, {0, 0}, {0, 0}};

      for (int i = 0; i < count; ++i)
      {
        const Vector x = leaves[i]->volume.center() - org;
        for (int j = 0; j < 3; ++j)
          ++splitcount[j][vecDot(x, axis[j]) > 0.0f ? 1 : 0];
      }

      for (int i = 0; i < 3; ++i)
      {
        if (splitcount[i][0] > 0 && splitcount[i][1] > 0)
        {
          const int midp = static_cast<int>(std::fabs(static_cast<float>(splitcount[i][0] - splitcount[i][1])));
          if (midp < bestmidp)
          {
            bestaxis = i;
            bestmidp = midp;
          }
        }
      }

      if (bestaxis >= 0)
      {
        partition = split(leaves, count, org, axis[bestaxis]);
        if (partition == 0 || partition == count)
        {
          Log::error("DynamicBVH: particao invalida em topDown");
          return nullptr;
        }
      }
      else
      {
        partition = count / 2 + 1;
      }

      Node *node = createNodeWithVolume(nullptr, vol, nullptr);
      node->children[0] = topDown(&leaves[0], partition, bottomUpThreshold);
      node->children[1] = topDown(&leaves[partition], count - partition, bottomUpThreshold);
      node->children[0]->parent = node;
      node->children[1]->parent = node;
      return node;
    }

    bottomUp(leaves, count);
    return leaves[0];
  }

  return leaves[0];
}

DynamicBVH::Node *DynamicBVH::nodeSort(Node *node, Node *&outRoot)
{
  Node *p = node->parent;
  if (!node->isInternal())
  {
    Log::error("DynamicBVH: nodeSort chamado sobre folha");
    return nullptr;
  }
  if (p > node)
  {
    const int i = node->indexInParent();
    const int j = 1 - i;
    Node *s = p->children[j];
    Node *q = p->parent;
    if (node != p->children[i])
    {
      Log::error("DynamicBVH: nodeSort inconsistencia de indice");
      return nullptr;
    }
    if (q)
      q->children[p->indexInParent()] = node;
    else
      outRoot = node;
    s->parent = node;
    p->parent = node;
    node->parent = q;
    p->children[0] = node->children[0];
    p->children[1] = node->children[1];
    node->children[0]->parent = p;
    node->children[1]->parent = p;
    node->children[i] = p;
    node->children[j] = s;
    const Volume tmp = p->volume;
    p->volume = node->volume;
    node->volume = tmp;
    return p;
  }
  return node;
}

void DynamicBVH::clear()
{
  if (mRoot)
    recurseDeleteNode(mRoot);
  mLookahead = -1;
  mOpath = 0;
}

void DynamicBVH::optimizeBottomUp()
{
  if (!mRoot)
    return;
  ct::Vector<Node *> leaves;
  fetchLeaves(mRoot, leaves);
  bottomUp(&leaves[0], static_cast<int>(leaves.size()));
  mRoot = leaves[0];
}

void DynamicBVH::optimizeTopDown(int bottomUpThreshold)
{
  if (!mRoot)
    return;
  ct::Vector<Node *> leaves;
  fetchLeaves(mRoot, leaves);
  mRoot = topDown(&leaves[0], static_cast<int>(leaves.size()), bottomUpThreshold);
}

void DynamicBVH::optimizeIncremental(int passes)
{
  if (passes < 0)
    passes = mTotalLeaves;
  if (passes <= 0)
    return;

  do
  {
    if (!mRoot)
      break;
    Node *node = mRoot;
    unsigned bit = 0;
    while (node->isInternal())
    {
      node = nodeSort(node, mRoot)->children[(mOpath >> bit) & 1];
      bit = (bit + 1) & (sizeof(unsigned) * 8 - 1);
    }
    update(node);
    ++mOpath;
  } while (--passes);
}

DynamicBVH::ID DynamicBVH::insert(const Box &box, void *userdata)
{
  Volume volume;
  volume.min = box.a;
  volume.max = box.b;

  Node *leaf = createNodeWithVolume(nullptr, volume, userdata);
  insertLeaf(mRoot, leaf);
  ++mTotalLeaves;

  ID id;
  id.node = leaf;
  return id;
}

void DynamicBVH::update(Node *leaf, int lookahead)
{
  Node *root = removeLeaf(leaf);
  if (root)
  {
    if (lookahead >= 0)
    {
      for (int i = 0; i < lookahead && root->parent; ++i)
        root = root->parent;
    }
    else
    {
      root = mRoot;
    }
  }
  insertLeaf(root, leaf);
}

bool DynamicBVH::update(const ID &id, const Box &box)
{
  if (!id.valid())
  {
    Log::error("DynamicBVH: update com ID invalido");
    return false;
  }
  Node *leaf = id.node;

  Volume volume;
  volume.min = box.a;
  volume.max = box.b;

  if (leaf->volume.min.x == volume.min.x && leaf->volume.min.y == volume.min.y &&
      leaf->volume.min.z == volume.min.z && leaf->volume.max.x == volume.max.x &&
      leaf->volume.max.y == volume.max.y && leaf->volume.max.z == volume.max.z)
    return false;

  Node *base = removeLeaf(leaf);
  if (base)
  {
    if (mLookahead >= 0)
    {
      for (int i = 0; i < mLookahead && base->parent; ++i)
        base = base->parent;
    }
    else
    {
      base = mRoot;
    }
  }
  leaf->volume = volume;
  insertLeaf(base, leaf);
  return true;
}

void DynamicBVH::remove(const ID &id)
{
  if (!id.valid())
  {
    Log::error("DynamicBVH: remove com ID invalido");
    return;
  }
  Node *leaf = id.node;
  removeLeaf(leaf);
  deleteNode(leaf);
  --mTotalLeaves;
}

void DynamicBVH::extractLeaves(Node *node, ct::Vector<ID> &outElements)
{
  if (node->isInternal())
  {
    extractLeaves(node->children[0], outElements);
    extractLeaves(node->children[1], outElements);
  }
  else
  {
    ID id;
    id.node = node;
    outElements.push_back(id);
  }
}

void DynamicBVH::getElements(ct::Vector<ID> &outElements)
{
  if (mRoot)
    extractLeaves(mRoot, outElements);
}

int DynamicBVH::maxDepth() const
{
  if (!mRoot)
    return 0;
  int depth = 1;
  int maxDepthOut = 0;
  mRoot->maxDepth(depth, maxDepthOut);
  return maxDepthOut;
}

bool DynamicBVH::rayAabb(const Vector &rayFrom, const Vector &rayInvDirection, const unsigned int raySign[3],
                         const Vector bounds[2], float &outTmin, float lambdaMin, float lambdaMax)
{
  float tmax, tymin, tymax, tzmin, tzmax;
  outTmin = (bounds[raySign[0]].x - rayFrom.x) * rayInvDirection.x;
  tmax = (bounds[1 - raySign[0]].x - rayFrom.x) * rayInvDirection.x;
  tymin = (bounds[raySign[1]].y - rayFrom.y) * rayInvDirection.y;
  tymax = (bounds[1 - raySign[1]].y - rayFrom.y) * rayInvDirection.y;

  if (outTmin > tymax || tymin > tmax)
    return false;

  if (tymin > outTmin)
    outTmin = tymin;
  if (tymax < tmax)
    tmax = tymax;

  tzmin = (bounds[raySign[2]].z - rayFrom.z) * rayInvDirection.z;
  tzmax = (bounds[1 - raySign[2]].z - rayFrom.z) * rayInvDirection.z;

  if (outTmin > tzmax || tzmin > tmax)
    return false;
  if (tzmin > outTmin)
    outTmin = tzmin;
  if (tzmax < tmax)
    tmax = tzmax;

  return outTmin < lambdaMax && tmax > lambdaMin;
}

DynamicBVH::~DynamicBVH() { clear(); }


} // namespace engine
