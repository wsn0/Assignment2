#include "Mesh.h"

#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <iostream>
#include <unordered_map>
#include <cstring>
#include <map>
#include <tuple>
#include <cmath>

// Lock the attribute locations we’ll use everywhere
static constexpr GLuint ATTR_POS = 0;
static constexpr GLuint ATTR_UV  = 1;  // << UV at 1
static constexpr GLuint ATTR_NRM = 2;  // << normal at 2

// ----------------------------
// Mesh: lifecycle
// ----------------------------
Mesh::Mesh() {}

Mesh::~Mesh() { ClearMesh(); }

// ----------------------------
// Create from raw arrays (8 floats per vertex)
// ----------------------------
void Mesh::CreateMesh(const GLfloat* vertices,
                    const unsigned int* indices,
                    unsigned int numOfFloats,
                    unsigned int numOfIndices) 
{
    // Build vectors and forward to preferred path
    std::vector<VertexPNU> vtx;

    if (numOfFloats % 8 == 0) 
    {
        // Layout: P(3) N(3) UV(2)
        unsigned int numVerts = numOfFloats / 8;
        vtx.reserve(numVerts);
        for (unsigned int i = 0; i < numVerts; ++i) 
        {
            const GLfloat* base = vertices + i * 8;
            VertexPNU v{};
            v.position = { base[0], base[1], base[2] };
            v.normal   = { base[3], base[4], base[5] };
            v.uv       = { base[6], base[7] };
            vtx.push_back(v);
        }
    }
    else if (numOfFloats % 5 == 0) 
    {
        // Layout: P(3) UV(2) — normals default to (0,0,1)
        unsigned int numVerts = numOfFloats / 5;
        vtx.reserve(numVerts);
        for (unsigned int i = 0; i < numVerts; ++i) 
        {
            const GLfloat* base = vertices + i * 5;
            VertexPNU v{};
            v.position = { base[0], base[1], base[2] };
            v.normal   = { 0.0f, 0.0f, 1.0f };
            v.uv       = { base[3], base[4] };
            vtx.push_back(v);
        }
    }
    else 
    {
        std::cerr << "[Mesh] Error: unsupported vertex float count " 
                  << numOfFloats << "\\n";
        return;
    }

    std::vector<unsigned int> idx(indices, indices + numOfIndices);
    CreateMesh(vtx, idx);
}

// ----------------------------
// Create from std::vector (preferred)
// ----------------------------
void Mesh::CreateMesh(const std::vector<VertexPNU>& vertices,
                    const std::vector<unsigned int>& indices) 
{
    uploadInterleaved_(vertices, indices);
}

// ----------------------------
// Upload helper (interleaved VBO + EBO)
// ----------------------------
void Mesh::uploadInterleaved_(const std::vector<VertexPNU>& vertices,
                            const std::vector<unsigned int>& indices) 
{
    // Clean previous
    ClearMesh();

    // Bounding Box
    if (!vertices.empty()) {
        boundsMin = boundsMax = vertices[0].position;

        for (const VertexPNU& v : vertices) {
            boundsMin = glm::min(boundsMin, v.position);
            boundsMax = glm::max(boundsMax, v.position);
        }
    }

    glGenVertexArrays(1, &VAO);
    glBindVertexArray(VAO);

    glGenBuffers(1, &VBO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(vertices.size() * sizeof(VertexPNU)),
                vertices.data(), GL_STATIC_DRAW);

    glGenBuffers(1, &IBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, IBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned int)),
                indices.data(), GL_STATIC_DRAW);

    const GLsizei stride = static_cast<GLsizei>(sizeof(VertexPNU));

    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,stride,(void*)offsetof(VertexPNU,position)); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,stride,(void*)offsetof(VertexPNU,uv));       glEnableVertexAttribArray(1);
    glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,stride,(void*)offsetof(VertexPNU,normal));   glEnableVertexAttribArray(2);


    indexCount = static_cast<GLsizei>(indices.size());

    // unbind VAO (EBO remains associated with VAO)
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// ----------------------------
// .OBJ loader (robust tuples + dedup)
// ----------------------------
namespace
{
    struct Triple
    {
        int v, vt, vn;
        bool operator==(const Triple& o) const { return v==o.v && vt==o.vt && vn==o.vn; }
    };
    struct TripleHash
    {
        size_t operator()(const Triple& t) const
        {
            auto mix = [](uint64_t x) { x ^= x >> 33; x *= 0xff51afd7ed558ccdULL; x ^= x >> 33; return x; };
            return (size_t)mix(((uint64_t)(uint32_t)(t.v + 1) << 32 | (uint32_t)(t.vt + 1)) ^ mix((uint32_t)(t.vn + 1)));
        }
    };
}

bool Mesh::CreateMeshFromOBJ(const std::string& path, bool flipV)
{
    // Read the whole file in one go (+ '\0' so strtof can't run past the end)
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in)
    {
        std::cerr << "[Mesh] Failed to open OBJ: " << path << "\n";
        return false;
    }
    const std::streamsize fileSize = in.tellg();
    in.seekg(0, std::ios::beg);

    std::vector<char> buf(static_cast<size_t>(fileSize) + 1);
    in.read(buf.data(), fileSize);
    buf[static_cast<size_t>(fileSize)] = '\0';

    // Rough upper bound so the vectors rarely reallocate
    const size_t est = static_cast<size_t>(fileSize) / 40 + 16;

    std::vector<glm::vec3> V;  V.reserve(est);
    std::vector<glm::vec2> VT; VT.reserve(est);
    std::vector<glm::vec3> VN; VN.reserve(est);

    std::vector<VertexPNU> interleaved; interleaved.reserve(est);
    std::vector<unsigned int> indices;  indices.reserve(est * 3);
    std::unordered_map<Triple, unsigned int, TripleHash> dedup;
    dedup.reserve(est);

    struct FaceV { int v, vt, vn; };
    std::vector<FaceV> fv; fv.reserve(16);

    auto skipBlank = [](const char* p, const char* end) {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) ++p;
        return p;
    };
    // Parse an int ([-]digits). Returns false if there are no digits.
    auto parseInt = [](const char*& p, const char* end, int& out) {
        bool neg = false;
        if (p < end && (*p == '-' || *p == '+')) { neg = (*p == '-'); ++p; }
        if (p >= end || *p < '0' || *p > '9') return false;
        int x = 0;
        while (p < end && *p >= '0' && *p <= '9') { x = x * 10 + (*p - '0'); ++p; }
        out = neg ? -x : x;
        return true;
    };
    // Parse n floats from p; false if the line had fewer than n numbers
    auto parseFloats = [&](const char* p, const char* end, float* out, int n) {
        for (int i = 0; i < n; ++i)
        {
            p = skipBlank(p, end);
            if (p >= end) return false;
            char* next = nullptr;
            out[i] = std::strtof(p, &next);
            if (next == p || next > end) return false;
            p = next;
        }
        return true;
    };
    auto fixIndex = [](int idx, int count) { if (idx > 0) return idx - 1; if (idx < 0) return count + idx; return -1; };

    const char* p   = buf.data();
    const char* end = buf.data() + fileSize;

    while (p < end)
    {
        const char* lineEnd = static_cast<const char*>(std::memchr(p, '\n', static_cast<size_t>(end - p)));
        if (!lineEnd) lineEnd = end;

        const char* s = skipBlank(p, lineEnd);
        p = lineEnd + 1;                              // advance for next iteration
        if (s >= lineEnd || s[0] == '#') continue;

        if (s[0] == 'v' && (s[1] == ' ' || s[1] == '\t'))
        {
            float f[3];
            if (parseFloats(s + 1, lineEnd, f, 3)) V.push_back({f[0], f[1], f[2]});
        }
        else if (s[0] == 'v' && s[1] == 't')
        {
            float f[2];
            if (parseFloats(s + 2, lineEnd, f, 2)) VT.push_back({f[0], flipV ? 1.0f - f[1] : f[1]});
        }
        else if (s[0] == 'v' && s[1] == 'n')
        {
            float f[3];
            if (parseFloats(s + 2, lineEnd, f, 3)) VN.push_back({f[0], f[1], f[2]});
        }
        else if (s[0] == 'f' && (s[1] == ' ' || s[1] == '\t'))
        {
            fv.clear();
            const char* q = s + 1;
            bool ok = true;
            while (true)
            {
                q = skipBlank(q, lineEnd);
                if (q >= lineEnd) break;

                int a = 0, b = 0, c = 0;
                bool hasB = false, hasC = false;
                if (!parseInt(q, lineEnd, a)) { ok = false; break; }
                if (q < lineEnd && *q == '/')
                {
                    ++q;
                    hasB = parseInt(q, lineEnd, b);          // false for "v//vn"
                    if (q < lineEnd && *q == '/') { ++q; hasC = parseInt(q, lineEnd, c); }
                }
                FaceV f;
                f.v  = fixIndex(a, (int)V.size());
                f.vt = hasB ? fixIndex(b, (int)VT.size()) : -1;
                f.vn = hasC ? fixIndex(c, (int)VN.size()) : -1;

                // Out-of-range index -> drop this face instead of reading garbage
                if (f.v < 0 || f.v >= (int)V.size() || f.vt >= (int)VT.size() || f.vn >= (int)VN.size()) { ok = false; break; }
                fv.push_back(f);
            }
            if (!ok || fv.size() < 3) continue;

            auto addFaceVertex = [&](const FaceV& fvx)
            {
                Triple key{fvx.v, fvx.vt, fvx.vn};
                auto it = dedup.find(key);
                if (it != dedup.end()) { indices.push_back(it->second); return; }

                VertexPNU out{};
                out.position = V[fvx.v];
                if (fvx.vt >= 0) out.uv = VT[fvx.vt];
                if (fvx.vn >= 0) out.normal = VN[fvx.vn];

                unsigned int id = (unsigned)interleaved.size();
                interleaved.push_back(out);
                dedup.emplace(key, id);
                indices.push_back(id);
            };

            for (size_t i = 1; i + 1 < fv.size(); ++i)
            {
                addFaceVertex(fv[0]);
                addFaceVertex(fv[i]);
                addFaceVertex(fv[i + 1]);
            }
        }
    }

    if (interleaved.empty() || indices.empty())
    {
        std::cerr << "[Mesh] OBJ produced no geometry: " << path << "\n";
        return false;
    }

    {
        std::map<std::tuple<long long, long long, long long>, unsigned int> weld;
        std::vector<unsigned int> weldId(interleaved.size());
        for (size_t i = 0; i < interleaved.size(); ++i)
        {
            const glm::vec3& P = interleaved[i].position;
            auto key = std::make_tuple((long long)std::llround(P.x * 1e5),
                                       (long long)std::llround(P.y * 1e5),
                                       (long long)std::llround(P.z * 1e5));
            const unsigned int next = (unsigned int)weld.size();
            weldId[i] = weld.emplace(key, next).first->second;
        }

        // Area-weighted face normals (cross product length = 2 * area)
        std::vector<glm::vec3> acc(weld.size(), glm::vec3(0.0f));
        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            const unsigned int ia = indices[i], ib = indices[i + 1], ic = indices[i + 2];
            const glm::vec3 fn = glm::cross(interleaved[ib].position - interleaved[ia].position,
                                            interleaved[ic].position - interleaved[ia].position);
            acc[weldId[ia]] += fn;
            acc[weldId[ib]] += fn;
            acc[weldId[ic]] += fn;
        }

        for (size_t i = 0; i < interleaved.size(); ++i)
        {
            if (glm::dot(interleaved[i].normal, interleaved[i].normal) > 1e-8f) continue; // already has a normal
            const glm::vec3 n = acc[weldId[i]];
            interleaved[i].normal = (glm::dot(n, n) > 1e-16f) ? glm::normalize(n) : glm::vec3(0.0f, 1.0f, 0.0f);
        }
    }

    uploadInterleaved_(interleaved, indices);
    return true;
}

// ----------------------------
// Render
// ----------------------------
void Mesh::RenderMesh() const 
{
    if (VAO == 0 || indexCount == 0) return;
    glBindVertexArray(VAO);
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

// ----------------------------
// Clear GL resources
// ----------------------------
void Mesh::ClearMesh() 
{
    if (IBO != 0) 
    {
        glDeleteBuffers(1, &IBO);
        IBO = 0;
    }
    if (VBO != 0) 
    {
        glDeleteBuffers(1, &VBO);
        VBO = 0;
    }
    if (VAO != 0) 
    {
        glDeleteVertexArrays(1, &VAO);
        VAO = 0;
    }
    indexCount = 0;
}

