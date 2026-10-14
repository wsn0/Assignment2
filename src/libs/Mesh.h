#ifndef MESH____H
#define MESH____H

#include <string>
#include <vector>
#include <algorithm>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <GL/glew.h>
#include <glm/glm.hpp>


// Interleaved vertex: position (3), normal (3), uv (2)
struct VertexPNU 
{
    glm::vec3 position{};
    glm::vec3 normal{};
    glm::vec2 uv{};
};

class Mesh 
{
    public:
        Mesh();
        ~Mesh();

        // Create from client-provided interleaved vertex array (8 floats per vertex: P(3) N(3) UV(2))
        // Common in many tutorials; kept for compatibility.
        void CreateMesh(const GLfloat* vertices,
                const unsigned int* indices,
                unsigned int numOfFloats,
                unsigned int numOfIndices);

        // Create from interleaved vector of VertexPNU (preferred)
        void CreateMesh(const std::vector<VertexPNU>& vertices,
        const std::vector<unsigned int>& indices);

        // Load from .obj (supports v, vt, vn; faces in v/vt/vn, v//vn, v/vt, or v)
        // Handles negative indices; triangulates n-gons via fan.
        // flipV = true to flip the V coordinate (uv.y = 1 - uv.y)
        bool CreateMeshFromOBJ(const std::string& path, bool flipV = false);

        void RenderMesh() const;
        void ClearMesh();

        // Accessors
        inline GLsizei GetIndexCount() const { return indexCount; }
        inline GLuint GetVAO() const { return VAO; }
        inline glm::vec3 GetBoundsMin() const { return boundsMin; }
        inline glm::vec3 GetBoundsMax() const { return boundsMax; }

    private:
        // GL resources
        GLuint VAO{0};
        GLuint VBO{0};
        GLuint IBO{0};

        GLsizei indexCount{0};

        // Bounding box of the uploaded vertices
        glm::vec3 boundsMin{0.0f};
        glm::vec3 boundsMax{0.0f};

        // Internal helper used by OBJ loader
        void uploadInterleaved_(const std::vector<VertexPNU>& vertices,
        const std::vector<unsigned int>& indices);
};

struct MeshData 
{
    std::vector<VertexPNU> vertices;
    std::vector<unsigned int> indices;
};

// Create quad shape
inline void AddQuad(MeshData& m, glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3)
{
    glm::vec3 n = glm::normalize(glm::cross(p1 - p0, p2 - p0));
    unsigned int base = (unsigned int)m.vertices.size();

    m.vertices.push_back({p0, n, {0.0f, 0.0f}});
    m.vertices.push_back({p1, n, {1.0f, 0.0f}});
    m.vertices.push_back({p2, n, {1.0f, 1.0f}});
    m.vertices.push_back({p3, n, {0.0f, 1.0f}});

    unsigned int idx[] = {0, 1, 2, 0, 2, 3};
    for (unsigned int i : idx) {
        m.indices.push_back(base + i);
    }
}

// Create box shape
inline void AddBox(MeshData& m, glm::vec3 a, glm::vec3 b)
{
    glm::vec3 p000(a.x, a.y, a.z), p100(b.x, a.y, a.z), p110(b.x, b.y, a.z), p010(a.x, b.y, a.z);
    glm::vec3 p001(a.x, a.y, b.z), p101(b.x, a.y, b.z), p111(b.x, b.y, b.z), p011(a.x, b.y, b.z);

    AddQuad(m, p001, p101, p111, p011);
    AddQuad(m, p100, p000, p010, p110);
    AddQuad(m, p101, p100, p110, p111);
    AddQuad(m, p000, p001, p011, p010);
    AddQuad(m, p011, p111, p110, p010);
    AddQuad(m, p000, p100, p101, p001);
}

inline void AddWedge(MeshData& m, glm::vec3 a, glm::vec3 b, float yFront, float yBack)
{
    float x0 = a.x, x1 = b.x, z0 = a.z, z1 = b.z, y0 = a.y;
    
    AddQuad(m, {x0, y0, z1}, {x1, y0, z1}, {x1, yFront, z1}, {x0, yFront, z1});       // หน้า (+Z)
    AddQuad(m, {x1, y0, z0}, {x0, y0, z0}, {x0, yBack,  z0}, {x1, yBack,  z0});       // หลัง (-Z)
    AddQuad(m, {x1, y0, z1}, {x1, y0, z0}, {x1, yBack,  z0}, {x1, yFront, z1});       // ขวา (+X)
    AddQuad(m, {x0, y0, z0}, {x0, y0, z1}, {x0, yFront, z1}, {x0, yBack,  z0});       // ซ้าย (-X)
    AddQuad(m, {x0, yFront, z1}, {x1, yFront, z1}, {x1, yBack, z0}, {x0, yBack, z0}); // ผิวบนเอียง
}

inline void AddTrialFlat(MeshData& m, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 center) {
    glm::vec3 n = glm::cross(b-a, c-a);
    glm::vec3 mid = (a+b+c) / 3.0f;
    if(glm::dot(n, mid-center) < 0.0f) {
        std::swap(b,c); 
        n = -n;
    }
    n = glm::normalize(n);

    unsigned int base = (unsigned int)m.vertices.size();
    m.vertices.push_back({a, n, {0.0f, 0.0f}});
    m.vertices.push_back({b, n, {1.0f, 0.0f}});
    m.vertices.push_back({c, n, {0.5f, 1.0f}});
    m.indices.push_back(base);
    m.indices.push_back(base+1);
    m.indices.push_back(base+2);
}

inline void AddInvertedPyramid(MeshData& m, glm::vec3 center, float radius, float height, int sides = 4, float tipRadius = 0.0f, int tipSegs = 8) {
    const float PI2 = 6.28318530718f;
    glm::vec3 apex = center + glm::vec3(0.0f, -height * 0.5f, 0.0f);
    glm::vec3 topMid = center + glm::vec3(0.0f, height * 0.5f,0.0f);

    // Sharp tip (tipRadius = 0) -> same as the old version
    if (tipRadius <= 0.0f || tipSegs < 1) {
        for(int i=0 ;i<sides; i++) {
            float a0 = PI2 * i /sides;
            float a1 = PI2 *(i+1) / sides;
            glm::vec3 p0 = topMid + glm::vec3(radius * cosf(a0), 0.0f, radius * sinf(a0));
            glm::vec3 p1 = topMid + glm::vec3(radius * cosf(a1), 0.0f, radius * sinf(a1));

            AddTrialFlat(m, apex, p0, p1, center); // ด้านข้าง
            AddTrialFlat(m, topMid, p0, p1, center); // ด้านบน
        }
        return;
    }

    const float apothem = radius * cosf(PI2 * 0.5f / sides);
    const float slant   = sqrtf(height * height + apothem * apothem);
    tipRadius = std::min(tipRadius, 0.5f * apothem * slant / height);

    const float sphereY = tipRadius * slant / apothem;      
    const float cutApo  = tipRadius * height / slant;
    const float capAng  = atan2f(height, apothem); 

    const glm::vec3 sphereC = apex + glm::vec3(0.0f, sphereY, 0.0f);
    const float faceAng = PI2 / sides;

    auto addTri = [&](unsigned a, unsigned b, unsigned c) {  
        const glm::vec3 g = glm::cross(m.vertices[b].position - m.vertices[a].position, m.vertices[c].position - m.vertices[a].position);
        const glm::vec3 n = m.vertices[a].normal + m.vertices[b].normal + m.vertices[c].normal;
        if (glm::dot(g, n) < 0.0f) std::swap(b, c);
        m.indices.push_back(a); m.indices.push_back(b); m.indices.push_back(c);
    };

    const int S = tipSegs;
    for (int i = 0; i < sides; i++) {
        const float a0 = faceAng * i, a1 = faceAng * (i + 1), am = 0.5f * (a0 + a1);
        const glm::vec3 nf(height / slant * cosf(am), -apothem / slant, height / slant * sinf(am));

        // Cap: ring 0 = pole, rings 1..S (S+1 points each) go up to the cut polygon
        const unsigned pole = (unsigned)m.vertices.size();
        m.vertices.push_back({sphereC + glm::vec3(0.0f, -tipRadius, 0.0f), {0.0f, -1.0f, 0.0f}, {0.5f, 0.0f}});

        const unsigned ringBase = (unsigned)m.vertices.size();
        for (int k = 1; k <= S; k++) {
            const float t = (float)k / S, w = t * t;                 // w: 0 = sphere, 1 = flat face
            const float th = capAng * t;
            for (int j = 0; j <= S; j++) {
                const float phi = a0 + (a1 - a0) * j / S;
                const glm::vec3 ns(sinf(th) * cosf(phi), -cosf(th), sinf(th) * sinf(phi));
                const float rho = cutApo / cosf(phi - am);           // distance to the cut polygon at this angle
                const float sphereR = tipRadius * sinf(th);
                const float r = sphereR * (1.0f + (rho / (tipRadius * sinf(capAng)) - 1.0f) * w);

                const glm::vec3 pos = sphereC + glm::vec3(r * cosf(phi), -tipRadius * cosf(th), r * sinf(phi));
                const glm::vec3 nrm = glm::normalize(ns * (1.0f - w) + nf * w);
                m.vertices.push_back({pos, nrm, {(float)j / S, t}});
            }
        }
        auto ring = [&](int k, int j) { return ringBase + (unsigned)((k - 1) * (S + 1) + j); };

        for (int j = 0; j < S; j++) addTri(pole, ring(1, j), ring(1, j + 1));
        for (int k = 1; k < S; k++)
            for (int j = 0; j < S; j++) {
                addTri(ring(k, j), ring(k + 1, j), ring(k + 1, j + 1));
                addTri(ring(k, j), ring(k + 1, j + 1), ring(k, j + 1));
            }

        // Flat face: from the cut polygon edge (ring S) up to the top edge
        const glm::vec3 b0 = topMid + glm::vec3(radius * cosf(a0), 0.0f, radius * sinf(a0));
        const glm::vec3 b1 = topMid + glm::vec3(radius * cosf(a1), 0.0f, radius * sinf(a1));
        const unsigned ib0 = (unsigned)m.vertices.size();
        m.vertices.push_back({b0, nf, {0.0f, 1.0f}});
        m.vertices.push_back({b1, nf, {1.0f, 1.0f}});
        addTri(ib0, ib0 + 1, ring(S, S));
        for (int j = S; j > 0; j--) addTri(ib0, ring(S, j), ring(S, j - 1));

        // Top cap
        AddTrialFlat(m, topMid, b0, b1, center);
    }
}

inline Mesh* BuildMesh(const MeshData& m)
{
    Mesh* mesh = new Mesh();
    mesh->CreateMesh(m.vertices, m.indices);

    return mesh;
}

#endif