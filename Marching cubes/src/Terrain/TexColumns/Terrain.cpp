#include "Terrain.h"

void Terrain::Initialize(ID3D12Device* device, float worldSize, int maxLOD, XMFLOAT3 terrainOffset)
{
    mWorldSize = worldSize;
    mMaxLOD = maxLOD;
    mHeightScale = 500;
    mTerrainOffset = terrainOffset;
    tileIndex = 0;
    BuildTree();

}


void Terrain::Update(const XMFLOAT3& cameraPos, BoundingFrustum& frustum)
{
    mVisibleTiles.clear();
    mRoot->UpdateVisibility(frustum, cameraPos, mVisibleTiles, mHeightScale, (int)mWorldSize);
}

bool QuadTreeNode::ShouldSplit(const XMFLOAT3& cameraPos, float heightscale, int mapsize) const
{
    auto camPos = cameraPos;
    camPos.y = 0;
    XMVECTOR camPosVec = XMLoadFloat3(&camPos);
    float lodneeddist = (mapsize / 2.0f - depth * mapsize / 16);
    BoundingSphere sphere;
    sphere.Center = cameraPos;
    sphere.Radius = lodneeddist;
    if (sphere.Intersects(boundingBox))
    {
        return true;
    }
    return false;
}

// 4. Обновление видимости в квадродереве
void QuadTreeNode::UpdateVisibility(BoundingFrustum& frustum, const XMFLOAT3& cameraPos, std::vector<Tile*>& visibleTiles, float heightscale, int mapsize)
{
    if (frustum.Contains(boundingBox) == DISJOINT)
    {
        return; // Узел полностью не виден
    }

    // Если узел является "листом" (нет дочерних узлов) или не нужно его разбивать
    if (!children[0] || !ShouldSplit(cameraPos, heightscale, mapsize))
    {
        // Рендерим текущий узел (тайл)
        if (tile)
        {
            visibleTiles.push_back(tile);
        }
    }
    else // Если нужно разбивать
    {
        // Рекурсивно обновляем видимость дочерних узлов
        for (int i = 0; i < 4; i++)
        {
            if (children[i])
            {
                children[i]->UpdateVisibility(frustum, cameraPos, visibleTiles, heightscale, mapsize);
            }
        }
    }
}

std::vector<std::shared_ptr<Tile>>& Terrain::GetAllTiles()
{
    return mAllTiles;
}

std::vector<Tile*>& Terrain::GetVisibleTiles()
{
   
    return mVisibleTiles;
}

void Terrain::BuildTree()
{
    //float halfSize = mWorldSize * 0.5f;
    
    mRoot = std::make_unique<QuadTreeNode>();
    mRoot->boundingBox= CalculateAABB(mTerrainOffset, mWorldSize, minHeight, maxHeight);
    mRoot->size = mWorldSize;
    mRoot->depth = 0;
    mRoot->isLeaf = false;

    BuildNode(mRoot.get(), mTerrainOffset.x, mTerrainOffset.z, 0);
}

void Terrain::BuildNode(QuadTreeNode* node, float x, float z, int depth)
{
    CreateTileForNode(node, x, z, depth);

    // Если достигли максимальной глубины - останавливаемся
    if (depth >= mMaxLOD) 
    {
        node->isLeaf = true;
        return;
    }

    float childSize = node->size * 0.5f;

    for (int i = 0; i < 4; ++i) {
        node->children[i] = std::make_unique<QuadTreeNode>();
        auto& child = node->children[i];

        float childX = x + (i % 2) * childSize;
        float childZ = z + (i / 2) * childSize;

        child->boundingBox= CalculateAABB(XMFLOAT3(childX, mTerrainOffset.y, childZ), childSize, minHeight, maxHeight);
        child->size = childSize;
        child->depth = depth + 1;
        child->isLeaf = false;

        // Рекурсивно строим дочерний узел
        BuildNode(child.get(), childX, childZ, depth +1);
    }
}

void Terrain::UpdateBoundainBoxes(XMFLOAT3 offset)
{
    if (!mRoot) return;

    // Обновляем позицию корневого узла
    XMFLOAT3 rootActualPos = XMFLOAT3(
        mRoot->tile->worldPos.x + offset.x,
        mRoot->tile->worldPos.y + offset.y,
        mRoot->tile->worldPos.z + offset.z
    );

    // Обновляем bounding box корня
    mRoot->boundingBox = CalculateAABB(rootActualPos, mWorldSize, minHeight, maxHeight);
    mRoot->tile->boundingBox = mRoot->boundingBox;
    //mRoot->tile->worldPos = rootActualPos; // Важно: обновляем позицию тайла!

    // Рекурсивно обновляем дочерние узлы
    RecurseUpdatingBB(mRoot.get(), rootActualPos);
}

void Terrain::RecurseUpdatingBB(QuadTreeNode* node, XMFLOAT3 parentPos)
{
    if (!node || !node->tile) return;

    // Для корневого узла позиция уже вычислена, для дочерних - вычисляем
    XMFLOAT3 actualPos;
    if (node == mRoot.get())
    {
        actualPos = parentPos; // Корневой узел уже имеет правильную позицию
    }
    else
    {
        // Вычисляем позицию дочернего узла относительно родителя
        float childSize = node->size;
        // Находим смещение от родителя (предполагая что parentPos - центр родителя)
        float parentSize = node->size * 2.0f; // Размер родителя в 2 раза больше
        float offsetX = ((node->tile->worldPos.x - parentPos.x) / parentSize) * childSize;
        float offsetZ = ((node->tile->worldPos.z - parentPos.z) / parentSize) * childSize;

        actualPos = XMFLOAT3(
            parentPos.x + offsetX,
            parentPos.y,
            parentPos.z + offsetZ
        );

        // Обновляем позицию тайла
        //node->tile->worldPos = actualPos;
    }

    // Обновляем bounding box
    node->boundingBox = CalculateAABB(actualPos, node->size, minHeight, maxHeight);
    node->tile->boundingBox = node->boundingBox;

    // Рекурсивно обновляем дочерние узлы
    if (!node->isLeaf) {
        for (int i = 0; i < 4; ++i) {
            if (node->children[i] && node->children[i]->tile)
            {
                RecurseUpdatingBB(node->children[i].get(), actualPos);
            }
        }
    }
}
BoundingBox Terrain::CalculateAABB(const XMFLOAT3& pos, float size, float minHeight, float maxHeight)
{
    BoundingBox aabb;
    auto minPoint = XMFLOAT3(pos.x, minHeight, pos.z);
    auto maxPoint = XMFLOAT3(pos.x + size, maxHeight, pos.z + size);
    XMVECTOR pt1 = XMLoadFloat3(&minPoint);
    XMVECTOR pt2 = XMLoadFloat3(&maxPoint);
    BoundingBox::CreateFromPoints(aabb, pt1, pt2);
    return aabb;
}

// Создание тайла для узла
void Terrain::CreateTileForNode(QuadTreeNode* node, float x, float z, int depth)
{
    auto tile = std::make_shared<Tile>();

    tile->worldPos = XMFLOAT3(x, mTerrainOffset.y, z);

    tile->lodLevel = depth;
    tile->tileSize = mWorldSize / (1 << depth);
    tile->tileIndex = tileIndex++;
    tile->boundingBox = node->boundingBox;
    tile->isVisible = true;
    mAllTiles.push_back(std::move(tile));
    node->tile = mAllTiles.back().get();
}


// Вспомогательная функция для скрытия всех дочерних тайлов
void Terrain::HideChildrenTiles(QuadTreeNode* node)
{
    if (!node) return;

    for (int i = 0; i < 4; ++i) {
        if (node->children[i]) {
            if (node->children[i]->tile) {
                node->children[i]->tile->isVisible = false;
            }
            // Рекурсивно скрываем всех потомков
            HideChildrenTiles(node->children[i].get());
        }
    }
}

// Обновленная функция сбора видимых тайлов
void Terrain::CollectVisibleTiles(QuadTreeNode* node, const BoundingFrustum& frustum, std::vector<Tile*>& visibleTiles)
{
    if (!node) return;

    // Если узел видим и проходит фрустум-тест
    if (node->tile && node->tile->isVisible) {
        if (frustum.Intersects(node->boundingBox)) {
            visibleTiles.push_back(node->tile);
        }
    }

    // Рекурсивно проверяем дочерние узлы (только если узел активен и не является листом)
    if (!node->isLeaf) {
        for (int i = 0; i < 4; ++i) {
            CollectVisibleTiles(node->children[i].get(), frustum, visibleTiles);
        }
    }
}