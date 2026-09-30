#pragma once
class IGame_Level;

// Winter-only local mesh. No work or allocation when the feature is disabled.
class CSnowRenderer
{
    static constexpr int Side = 256;
    struct Sample
    {
        int x = 0, z = 0;
        bool known = false, valid = false, edgeDirty = true;
        float edge = 0.f;
        float probeHeight = 0.f;
        float previousScopeY = 0.f;
        bool scopeHistory = false;
        float y = 0.f, remaining = 1.f, previousY = 0.f, publishedRemaining = -1.f, depth = 0.f;
        u32 fieldRevision = 0;
        Fvector normal;
    };
    struct Vertex { Fvector p, n; Fvector4 data; };
    xr_vector<Sample> samples;
    xr_vector<Fvector2> offsets;
    xr_vector<Vertex> vertices;
    xr_vector<u16> indices;
    ref_shader shader;
    ref_geom geometry;
    ID3DVertexBuffer* vertexBuffer=nullptr;
    ID3DIndexBuffer* indexBuffer=nullptr;
    ref_rt basePosition[2];
    ref_texture positionTexture;
    int boundPositionBank = -1;
    IGame_Level* level = nullptr;
    float step = .5f, distance = 0.f;
    int centerX = 0, centerZ = 0, radius = 0;
    u32 updateTime = u32(-1);
    unsigned probe = 0;
    bool enabled = false;
    u32 fieldRevision = 1;
    Fvector4 parameters = {};

    Sample& Cell(int x, int z) { return samples[(unsigned(x)&255) + (unsigned(z)&255)*Side]; }
    void Probe(int x, int z, float cameraY);
    void Update();
    void Rebuild();
    bool Valid(int x, int z, float ground);
    void CopyGround();
    void CreateGeometry();
public:
    ~CSnowRenderer() { Clear(); }
    void Render();
    void Clear();
};
