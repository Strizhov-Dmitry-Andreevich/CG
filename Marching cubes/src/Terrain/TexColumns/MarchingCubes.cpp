#include "MarchingCubes.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace MarchingCubes
{



ScalarField::ScalarField(int sx, int sy, int sz, float cellSize)
    : SizeX(sx), SizeY(sy), SizeZ(sz), CellSize(cellSize)
    , Data(sx * sy * sz, 0.0f)
{}

float& ScalarField::At(int x, int y, int z)
{
    return Data[z * SizeY * SizeX + y * SizeX + x];
}

float ScalarField::At(int x, int y, int z) const
{
    return Data[z * SizeY * SizeX + y * SizeX + x];
}

void ScalarField::Set(int x, int y, int z, float v)
{
    At(x, y, z) = v;
}


PerlinNoise::PerlinNoise(uint32_t seed)
{
    for (int i = 0; i < 256; ++i) p[i] = (uint8_t)i;

    uint32_t lcg = seed;
    for (int i = 255; i > 0; --i)
    {
        lcg = lcg * 1664525u + 1013904223u;
        int j = (int)(lcg >> 24) % (i + 1);
        std::swap(p[i], p[j]);
    }
    for (int i = 0; i < 256; ++i) p[i + 256] = p[i];
}

float PerlinNoise::Fade(float t)
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float PerlinNoise::Lerp(float t, float a, float b)
{
    return a + t * (b - a);
}

float PerlinNoise::Grad(int hash, float x, float y, float z)
{
    int   h = hash & 15;
    float u = h < 8 ? x : y;
    float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

float PerlinNoise::Noise(float x, float y, float z) const
{
    int X = (int)floorf(x) & 255;
    int Y = (int)floorf(y) & 255;
    int Z = (int)floorf(z) & 255;

    float fx = x - floorf(x);
    float fy = y - floorf(y);
    float fz = z - floorf(z);

    float u = Fade(fx), v = Fade(fy), w = Fade(fz);

    int A  = p[X]   + Y,  AA = p[A]   + Z,  AB = p[A+1] + Z;
    int B  = p[X+1] + Y,  BA = p[B]   + Z,  BB = p[B+1] + Z;

    return Lerp(w,
        Lerp(v,
            Lerp(u, Grad(p[AA],   fx,   fy,   fz  ),
                    Grad(p[BA],   fx-1, fy,   fz  )),
            Lerp(u, Grad(p[AB],   fx,   fy-1, fz  ),
                    Grad(p[BB],   fx-1, fy-1, fz  ))),
        Lerp(v,
            Lerp(u, Grad(p[AA+1], fx,   fy,   fz-1),
                    Grad(p[BA+1], fx-1, fy,   fz-1)),
            Lerp(u, Grad(p[AB+1], fx,   fy-1, fz-1),
                    Grad(p[BB+1], fx-1, fy-1, fz-1))));
}

float PerlinNoise::fBm(float x, float y, float z, float frequency, int octaves) const
{
    float value    = 0.0f;
    float amplitude = 1.0f;
    float maxAmp   = 0.0f;

    for (int i = 0; i < octaves; ++i)
    {
        value   += Noise(x * frequency, y * frequency, z * frequency) * amplitude;
        maxAmp  += amplitude;
        frequency *= 2.0f;
        amplitude *= 0.5f;
    }
    return value / maxAmp;
}

// =============================================================================

void ComputeSmoothNormals(Result& res)
{
    std::vector<XMFLOAT3> acc(res.Vertices.size(), {0,0,0});

    for (size_t i = 0; i + 2 < res.Indices.size(); i += 3)
    {
        uint32_t i0 = res.Indices[i];
        uint32_t i1 = res.Indices[i+1];
        uint32_t i2 = res.Indices[i+2];

        XMVECTOR p0 = XMLoadFloat3(&res.Vertices[i0].Pos);
        XMVECTOR p1 = XMLoadFloat3(&res.Vertices[i1].Pos);
        XMVECTOR p2 = XMLoadFloat3(&res.Vertices[i2].Pos);

        // Area-weighted cross product (not normalized) so larger triangles
        // contribute proportionally more to the final normal.
        XMVECTOR n = XMVector3Cross(
            XMVectorSubtract(p1, p0),
            XMVectorSubtract(p2, p0));

        XMFLOAT3 nf;
        XMStoreFloat3(&nf, n);

        acc[i0].x += nf.x;  acc[i0].y += nf.y;  acc[i0].z += nf.z;
        acc[i1].x += nf.x;  acc[i1].y += nf.y;  acc[i1].z += nf.z;
        acc[i2].x += nf.x;  acc[i2].y += nf.y;  acc[i2].z += nf.z;
    }

    for (size_t i = 0; i < res.Vertices.size(); ++i)
    {
        XMVECTOR n = XMVector3Normalize(XMLoadFloat3(&acc[i]));
        XMStoreFloat3(&res.Vertices[i].Normal, n);
    }
}


// Polygonize
// =============================================================================

static XMFLOAT3 Lerp3(XMFLOAT3 a, XMFLOAT3 b, float t)
{
    return { a.x + t*(b.x-a.x), a.y + t*(b.y-a.y), a.z + t*(b.z-a.z) };
}

static XMFLOAT3 VertexInterp(float iso,
    XMFLOAT3 p1, XMFLOAT3 p2, float v1, float v2)
{
    if (fabsf(v1 - v2) < 1e-6f) return p1;
    float t = (iso - v1) / (v2 - v1);
    return Lerp3(p1, p2, t);
}

Result Polygonize(const ScalarField& f, float isovalue)
{
    Result res;

    // Pre-allocate to avoid reallocation spikes on large fields.
    {
        size_t surfaceCells = (size_t)f.SizeX * f.SizeZ * 2
                            + (size_t)f.SizeX * f.SizeY * 2
                            + (size_t)f.SizeZ * f.SizeY * 2;
        size_t estimate = std::min(surfaceCells * 15, (size_t)2'000'000);
        res.Vertices.reserve(estimate);
        res.Indices.reserve(estimate);
    }

    const float cs = f.CellSize;
    const float maxX = (f.SizeX - 1) * cs;
    const float maxZ = (f.SizeZ - 1) * cs;

    for (int z = 0; z < f.SizeZ - 1; ++z)
    for (int y = 0; y < f.SizeY - 1; ++y)
    for (int x = 0; x < f.SizeX - 1; ++x)
    {
        float val[8];
        XMFLOAT3 pos[8];
        for (int i = 0; i < 8; ++i)
        {
            int cx = x + kVOff[i][0];
            int cy = y + kVOff[i][1];
            int cz = z + kVOff[i][2];
            val[i] = f.At(cx, cy, cz);
            pos[i] = { cx * cs, cy * cs, cz * cs };
        }

        int cubeIndex = 0;
        for (int i = 0; i < 8; ++i)
            if (val[i] >= isovalue) cubeIndex |= (1 << i);

        if (kEdgeTable[cubeIndex] == 0) continue;

        XMFLOAT3 edgePos[12];
        for (int e = 0; e < 12; ++e)
        {
            if (kEdgeTable[cubeIndex] & (1 << e))
            {
                int v0 = kEVert[e][0], v1 = kEVert[e][1];
                edgePos[e] = VertexInterp(isovalue, pos[v0], pos[v1], val[v0], val[v1]);
            }
        }

        for (int i = 0; kTriTable[cubeIndex][i] != -1; i += 3)
        {
            XMFLOAT3 p0 = edgePos[kTriTable[cubeIndex][i+0]];
            XMFLOAT3 p1 = edgePos[kTriTable[cubeIndex][i+1]];
            XMFLOAT3 p2 = edgePos[kTriTable[cubeIndex][i+2]];

            XMVECTOR e1v = XMVectorSubtract(XMLoadFloat3(&p1), XMLoadFloat3(&p0));
            XMVECTOR e2v = XMVectorSubtract(XMLoadFloat3(&p2), XMLoadFloat3(&p0));
            XMVECTOR n   = XMVector3Normalize(XMVector3Cross(e1v, e2v));
            XMFLOAT3 normal;
            XMStoreFloat3(&normal, n);

            uint32_t baseIdx = (uint32_t)res.Vertices.size();

            auto makeVert = [&](XMFLOAT3 p)
            {
                Vertex v{};
                v.Pos     = p;
                v.Normal  = normal;
                v.TexC    = { p.x / maxX, p.z / maxZ };
                v.Tangent = { 1.0f, 0.0f, 0.0f };
                res.Vertices.push_back(v);
            };

            makeVert(p0);
            makeVert(p1);
            makeVert(p2);

            res.Indices.push_back(baseIdx + 0);
            res.Indices.push_back(baseIdx + 2);
            res.Indices.push_back(baseIdx + 1);
        }
    }

    ComputeSmoothNormals(res);
    return res;
}

// PerlinScalarField

ScalarField PerlinScalarField(
    int fieldX, int fieldY, int fieldZ,
    float cellSize,
    const NoiseParams& np)
{
    ScalarField field(fieldX, fieldY, fieldZ, cellSize);

    PerlinNoise terrainNoise(np.seed);
    PerlinNoise caveNoise(np.caveSeed);  // prime offset -> uncorrelated

    const float threshSq = np.caveThreshold * np.caveThreshold;

    for (int z = 0; z < fieldZ; ++z)
    for (int x = 0; x < fieldX; ++x)
    {
        float wx = np.worldOffsetX + x * cellSize;
        float wz = np.worldOffsetZ + z * cellSize;

        for (int y = 0; y < fieldY; ++y)
        {
            float wy = (float)y;

            // Layer 1: terrain surface
            float tn = terrainNoise.fBm(wx, wy, wz, np.frequency, np.octaves);
            float d =  np.baseHeight - wy + tn * np.amplitude;

            // Layer 2: cave carving
            if (np.caveEnabled)
            {
                float cf = np.caveFrequency;
                float cy = wy * np.caveYScale;

                float nA = caveNoise.Noise(wx * cf, cy * cf, wz * cf);

                float nB = caveNoise.Noise(
                    wx * cf + 17.31f,
                    cy * cf * 0.67f + 5.93f,
                    wz * cf + 43.17f);

                float tube  = nA * nA + nB * nB;
                float carve = (threshSq > 0.0f) ? (1.0f - tube / threshSq) : 0.0f;
                carve = (std::max)(0.0f, carve);

                float depthBelow = np.baseHeight - wy;
                float depthMask  = (std::max)(0.0f, std::min(1.0f,
                                       depthBelow / np.caveSurfaceFade));

                float floorMask  = (std::max)(0.0f, std::min(1.0f,
                                       wy / np.caveFloorFade));
                float ceilingMask = (depthBelow > np.minCaveDepth) ? 1.0f : 0.0f;
                d -= carve * np.caveStrength * depthMask * floorMask * ceilingMask;
            }
            field.Set(x, y, z, d);
            //field.Set(x, y, z, (std::max)(0.0f, std::min(1.0f, d)));
        }
    }

    return field;
}

// HeightmapToScalarField

ScalarField HeightmapToScalarField(
    const float* heightData, int W, int H,
    int fieldX, int fieldY, int fieldZ,
    float plateauScale,
    float cellSize)
{
    ScalarField field(fieldX, fieldY, fieldZ, cellSize);

    for (int z = 0; z < fieldZ; ++z)
    for (int x = 0; x < fieldX; ++x)
    {
        float fx = (float)x / (fieldX - 1) * (W - 1);
        float fz = (float)z / (fieldZ - 1) * (H - 1);
        int   ix = std::min((int)fx, W - 2);
        int   iz = std::min((int)fz, H - 2);
        float tx = fx - ix, tz = fz - iz;

        float h = heightData[ iz    * W + ix    ] * (1-tx) * (1-tz)
                + heightData[ iz    * W + ix + 1] *    tx  * (1-tz)
                + heightData[(iz+1) * W + ix    ] * (1-tx) *    tz
                + heightData[(iz+1) * W + ix + 1] *    tx  *    tz;

        float solidY = h * plateauScale;

        for (int y = 0; y < fieldY; ++y)
        {
            float dist    = solidY - (float)y;
            float density = (std::max)(0.0f, std::min(1.0f, 0.5f + dist));
            field.Set(x, y, z, density);
        }
    }

    return field;
}

} 
