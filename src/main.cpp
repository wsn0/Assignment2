// 05506014 Computer Graphics - Project: "FUTURE ME"
// Theme : Achievement display case (Nintendo-style achievement page, Animal Crossing low-poly look)
// Tech  : Modern OpenGL 3.3 core, vertex-built meshes (no external models needed),
//         Blinn-Phong lighting + directional shadow map (PCF), 800 x 600 window.
//
// Controls:  Arrow keys = orbit camera | Space = toggle gentle camera sway | Esc = quit

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

#include "Mesh.h"

using glm::mat4;
using glm::vec3;

static const int WIN_W = 800;
static const int WIN_H = 600;
static const int SHADOW_RES = 2048;
static const float PI = 3.14159265359f;

// ---------------------------------------------------------------------------
// Shaders (embedded so the program has no file-path dependencies)
// ---------------------------------------------------------------------------
static const char* kSceneVert = R"GLSL(
#version 330 core
layout(location = 0) in vec3 pos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec3 aNormal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 lightSpace;

out vec3 FragPos;
out vec3 Normal;
out vec4 FragPosLight;

void main()
{
    vec4 wp = model * vec4(pos, 1.0);
    FragPos = wp.xyz;
    Normal = mat3(transpose(inverse(model))) * aNormal;
    FragPosLight = lightSpace * wp;
    gl_Position = projection * view * wp;
}
)GLSL";

static const char* kSceneFrag = R"GLSL(
#version 330 core
in vec3 FragPos;
in vec3 Normal;
in vec4 FragPosLight;
out vec4 colour;

uniform vec3 objColour;
uniform float shininess;
uniform vec3 lightDir;      // from the surface TOWARDS the light
uniform vec3 lightColour;
uniform vec3 viewPos;
uniform sampler2D shadowMap;

float shadowAmount(vec3 n)
{
    vec3 p = FragPosLight.xyz / FragPosLight.w;
    p = p * 0.5 + 0.5;
    if (p.z > 1.0) return 0.0;
    float bias = max(0.004 * (1.0 - dot(n, lightDir)), 0.0008);
    vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0));
    float s = 0.0;
    for (int x = -1; x <= 1; x++)
        for (int y = -1; y <= 1; y++)
        {
            float d = texture(shadowMap, p.xy + vec2(x, y) * texel).r;
            s += (p.z - bias > d) ? 1.0 : 0.0;
        }
    return s / 9.0;
}

void main()
{
    vec3 n = normalize(Normal);
    vec3 L = normalize(lightDir);
    vec3 V = normalize(viewPos - FragPos);

    // soft sky/ground ambient (gives the cosy Animal Crossing feel)
    float hemi = n.y * 0.5 + 0.5;
    vec3 ambient = mix(vec3(0.40, 0.36, 0.36), vec3(0.62, 0.64, 0.72), hemi) * 0.60;

    float diff = max(dot(n, L), 0.0);
    vec3 H = normalize(L + V);
    float spec = (diff > 0.0) ? pow(max(dot(n, H), 0.0), shininess) : 0.0;

    float sh = shadowAmount(n);
    vec3 lit = ambient + (1.0 - sh) * (diff * lightColour + 0.25 * spec * lightColour);
    colour = vec4(min(objColour * lit, vec3(1.0)), 1.0);
}
)GLSL";

static const char* kDepthVert = R"GLSL(
#version 330 core
layout(location = 0) in vec3 pos;
uniform mat4 model;
uniform mat4 lightSpace;
void main() { gl_Position = lightSpace * model * vec4(pos, 1.0); }
)GLSL";

static const char* kDepthFrag = R"GLSL(
#version 330 core
void main() {}
)GLSL";

static GLuint compileShader(GLenum type, const char* src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::cerr << "Shader compile error:\n" << log << std::endl;
    }
    return s;
}

static GLuint makeProgram(const char* vs, const char* fs)
{
    GLuint v = compileShader(GL_VERTEX_SHADER, vs);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs);
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[1024];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        std::cerr << "Program link error:\n" << log << std::endl;
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

// ---------------------------------------------------------------------------
// Procedural low-poly meshes. Every triangle gets its own flat normal.
// ---------------------------------------------------------------------------
struct Builder
{
    std::vector<VertexPNU> v;
    std::vector<unsigned int> idx;

    void push(const vec3& a, const vec3& b, const vec3& c, const vec3& n)
    {
        const vec3 p[3] = { a, b, c };
        for (const vec3& q : p)
        {
            VertexPNU vert;
            vert.position = q;
            vert.normal = n;
            vert.uv = glm::vec2(0.0f);
            v.push_back(vert);
            idx.push_back((unsigned int)(v.size() - 1));
        }
    }

    // Triangle for a convex solid: winding/normal is fixed so it faces away from 'inside'.
    void tri(vec3 a, vec3 b, vec3 c, const vec3& inside)
    {
        vec3 n = glm::cross(b - a, c - a);
        if (glm::length(n) < 1e-7f) return;
        n = glm::normalize(n);
        vec3 centre = (a + b + c) / 3.0f;
        if (glm::dot(n, centre - inside) < 0.0f)
        {
            std::swap(b, c);
            n = -n;
        }
        push(a, b, c, n);
    }

    void quad(const vec3& a, const vec3& b, const vec3& c, const vec3& d, const vec3& inside)
    {
        tri(a, b, c, inside);
        tri(a, c, d, inside);
    }

    // Triangle with explicit counter-clockwise winding (normal = (b-a)x(c-a)).
    void triCCW(const vec3& a, const vec3& b, const vec3& c)
    {
        vec3 n = glm::cross(b - a, c - a);
        if (glm::length(n) < 1e-7f) return;
        push(a, b, c, glm::normalize(n));
    }

    std::unique_ptr<Mesh> upload()
    {
        std::unique_ptr<Mesh> m(new Mesh());
        m->CreateMesh(v, idx);
        return m;
    }
};

// 1 x 1 x 1 cube centred at the origin
static std::unique_ptr<Mesh> makeBox()
{
    Builder b;
    const vec3 p[8] = {
        { -.5f, -.5f, -.5f }, { .5f, -.5f, -.5f }, { .5f, .5f, -.5f }, { -.5f, .5f, -.5f },
        { -.5f, -.5f, .5f },  { .5f, -.5f, .5f },  { .5f, .5f, .5f },  { -.5f, .5f, .5f }
    };
    const int f[6][4] = { { 0, 1, 2, 3 }, { 4, 5, 6, 7 }, { 0, 1, 5, 4 },
                          { 3, 2, 6, 7 }, { 0, 3, 7, 4 }, { 1, 2, 6, 5 } };
    for (int i = 0; i < 6; i++)
        b.quad(p[f[i][0]], p[f[i][1]], p[f[i][2]], p[f[i][3]], vec3(0.0f));
    return b.upload();
}

// Cylinder / cone / flared cup: bottom radius 1 at y=0, top radius 'topRatio' at y=1
static std::unique_ptr<Mesh> makeFrustum(int seg, float topRatio)
{
    Builder b;
    const vec3 inside(0.0f, 0.5f, 0.0f);
    const vec3 cb(0.0f, 0.0f, 0.0f), ct(0.0f, 1.0f, 0.0f);
    for (int i = 0; i < seg; i++)
    {
        float a0 = 2.0f * PI * i / seg;
        float a1 = 2.0f * PI * (i + 1) / seg;
        vec3 b0(std::cos(a0), 0.0f, std::sin(a0));
        vec3 b1(std::cos(a1), 0.0f, std::sin(a1));
        vec3 t0(topRatio * std::cos(a0), 1.0f, topRatio * std::sin(a0));
        vec3 t1(topRatio * std::cos(a1), 1.0f, topRatio * std::sin(a1));
        b.quad(b0, b1, t1, t0, inside);
        b.tri(cb, b0, b1, inside);
        if (topRatio > 0.0f) b.tri(ct, t0, t1, inside);
    }
    return b.upload();
}

// Low-poly UV sphere of radius 1
static std::unique_ptr<Mesh> makeSphere(int seg, int rings)
{
    Builder b;
    auto P = [&](int i, int j)
    {
        float phi = PI * i / rings;
        float th = 2.0f * PI * j / seg;
        return vec3(std::sin(phi) * std::cos(th), std::cos(phi), std::sin(phi) * std::sin(th));
    };
    for (int i = 0; i < rings; i++)
        for (int j = 0; j < seg; j++)
            b.quad(P(i, j), P(i, j + 1), P(i + 1, j + 1), P(i + 1, j), vec3(0.0f));
    return b.upload();
}

// Torus in the XZ plane: ring radius 1, tube radius 'tube'
static std::unique_ptr<Mesh> makeTorus(int seg, int sides, float tube)
{
    Builder b;
    auto P = [&](int i, int j)
    {
        float th = 2.0f * PI * i / seg;
        float ph = 2.0f * PI * j / sides;
        float r = 1.0f + tube * std::cos(ph);
        return vec3(r * std::cos(th), tube * std::sin(ph), r * std::sin(th));
    };
    for (int i = 0; i < seg; i++)
    {
        float tm = 2.0f * PI * (i + 0.5f) / seg;
        vec3 inside(std::cos(tm), 0.0f, std::sin(tm));
        for (int j = 0; j < sides; j++)
            b.quad(P(i, j), P(i + 1, j), P(i + 1, j + 1), P(i, j + 1), inside);
    }
    return b.upload();
}

// Five-point star prism in the XY plane (outer radius 1, thickness 0.3)
static std::unique_ptr<Mesh> makeStar()
{
    Builder b;
    const int n = 10;
    const float t = 0.15f;
    vec3 pt[n];
    for (int k = 0; k < n; k++)
    {
        float a = PI / 2.0f + k * PI / 5.0f;
        float r = (k % 2 == 0) ? 1.0f : 0.45f;
        pt[k] = vec3(r * std::cos(a), r * std::sin(a), 0.0f);
    }
    vec3 cf(0, 0, t), cbk(0, 0, -t);
    for (int k = 0; k < n; k++)
    {
        vec3 p0 = pt[k], p1 = pt[(k + 1) % n];
        vec3 f0 = p0 + vec3(0, 0, t), f1 = p1 + vec3(0, 0, t);
        vec3 k0 = p0 - vec3(0, 0, t), k1 = p1 - vec3(0, 0, t);
        b.triCCW(cf, f0, f1);       // front
        b.triCCW(cbk, k1, k0);      // back
        b.triCCW(k0, k1, f1);       // side
        b.triCCW(k0, f1, f0);
    }
    return b.upload();
}

// Gable roof prism: ridge along X, base 1 x 1 on y=0, ridge height 1
static std::unique_ptr<Mesh> makeGable()
{
    Builder b;
    vec3 A(-.5f, 0, -.5f), B(.5f, 0, -.5f), C(.5f, 0, .5f), D(-.5f, 0, .5f);
    vec3 R1(-.5f, 1, 0), R2(.5f, 1, 0);
    vec3 inside(0.0f, 0.3f, 0.0f);
    b.quad(D, C, R2, R1, inside);
    b.quad(A, B, R2, R1, inside);
    b.tri(A, D, R1, inside);
    b.tri(B, C, R2, inside);
    b.quad(A, B, C, D, inside);
    return b.upload();
}

struct Meshes
{
    std::unique_ptr<Mesh> box, cyl, flare, cone, sphere, torus, star, gable;
};
static Meshes G;

// ---------------------------------------------------------------------------
// Scene description: a flat list of (mesh, transform, colour) draw items
// ---------------------------------------------------------------------------
struct Draw
{
    Mesh* mesh;
    mat4 model;
    vec3 colour;
    float shin;
};

static vec3 rgb(int r, int g, int b) { return vec3((float)r, (float)g, (float)b) / 255.0f; }

struct Scene
{
    std::vector<Draw> draws;

    // rotDeg is applied yaw(Y) -> pitch(X) -> roll(Z), after scaling
    void add(Mesh* m, const mat4& parent, vec3 pos, vec3 rotDeg, vec3 scale, vec3 col, float shin = 16.0f)
    {
        mat4 M = glm::translate(parent, pos);
        M = glm::rotate(M, glm::radians(rotDeg.z), vec3(0, 0, 1));
        M = glm::rotate(M, glm::radians(rotDeg.x), vec3(1, 0, 0));
        M = glm::rotate(M, glm::radians(rotDeg.y), vec3(0, 1, 0));
        M = glm::scale(M, scale);
        draws.push_back({ m, M, col, shin });
    }
};

// ---- Achievement 1: Graduation (books, cap with tassel, diploma) -----------
static void itemGraduation(Scene& s, const mat4& P)
{
    Mesh *box = G.box.get(), *cyl = G.cyl.get(), *sph = G.sphere.get();
    vec3 navy = rgb(44, 52, 100), gold = rgb(255, 208, 64), cream = rgb(250, 240, 214);
    vec3 red = rgb(214, 84, 84), blue = rgb(84, 140, 214);

    s.add(box, P, { 0, 0.11f, 0 }, { 0, 0, 0 }, { 1.5f, 0.22f, 1.1f }, red);
    s.add(box, P, { 0, 0.32f, 0 }, { 0, 8, 0 }, { 1.35f, 0.2f, 1.0f }, blue);
    s.add(cyl, P, { 0, 0.42f, 0 }, { 0, 0, 0 }, { 0.55f, 0.4f, 0.55f }, navy);            // cap skull
    s.add(box, P, { 0, 0.86f, 0 }, { 0, 20, 0 }, { 1.55f, 0.08f, 1.55f }, navy * 1.25f);  // mortarboard
    s.add(sph, P, { 0, 0.92f, 0 }, { 0, 0, 0 }, vec3(0.08f), gold, 64);                   // button
    s.add(box, P, { 0.475f, 0.93f, 0 }, { 0, 0, 0 }, { 0.95f, 0.035f, 0.035f }, gold);    // tassel cord
    s.add(cyl, P, { 0.95f, 0.64f, 0 }, { 0, 0, 0 }, { 0.06f, 0.28f, 0.06f }, gold);       // tassel

    s.add(cyl, P, { 0.55f, 0.16f, 0.72f }, { 0, 0, 90 }, { 0.13f, 1.1f, 0.13f }, cream);  // diploma
    s.add(cyl, P, { 0.08f, 0.16f, 0.72f }, { 0, 0, 90 }, { 0.14f, 0.16f, 0.14f }, red);   // ribbon
}

// ---- Achievement 2: Travel around the world (globe + airplane) --------------
static void itemGlobe(Scene& s, const mat4& P)
{
    Mesh *cyl = G.cyl.get(), *sph = G.sphere.get(), *box = G.box.get();
    Mesh *cone = G.cone.get(), *tor = G.torus.get();
    vec3 wood = rgb(190, 140, 90), brass = rgb(240, 190, 80), ocean = rgb(70, 140, 230);
    vec3 land = rgb(120, 200, 105), white = rgb(246, 246, 250), red = rgb(230, 80, 80);

    s.add(cyl, P, { 0, 0, 0 }, { 0, 0, 0 }, { 0.5f, 0.1f, 0.5f }, wood);
    s.add(cyl, P, { 0, 0.1f, 0 }, { 0, 0, 0 }, { 0.07f, 0.5f, 0.07f }, brass, 64);
    const vec3 gc(0, 1.2f, 0);
    s.add(sph, P, gc, { 0, 0, 0 }, vec3(0.62f), ocean, 48);

    const vec3 dirs[5] = { { 0.2f, 0.35f, 0.9f }, { -0.5f, 0.0f, 0.85f }, { 0.55f, -0.25f, 0.8f },
                           { -0.15f, -0.5f, 0.85f }, { 0.1f, 0.8f, 0.55f } };
    const vec3 size[5] = { { 0.26f, 0.2f, 0.1f }, { 0.2f, 0.28f, 0.1f }, { 0.22f, 0.16f, 0.1f },
                           { 0.18f, 0.14f, 0.1f }, { 0.2f, 0.12f, 0.1f } };
    for (int i = 0; i < 5; i++)
        s.add(sph, P, gc + glm::normalize(dirs[i]) * 0.6f, { 0, 0, 0 }, size[i], land);

    s.add(tor, P, gc, { 90, 0, 23 }, vec3(0.8f), brass, 64);   // meridian ring, tilted like a real globe

    s.add(cyl, P, { 0.4f, 1.55f, 0 }, { 0, 0, 0 }, { 0.025f, 0.6f, 0.025f }, rgb(200, 200, 210));  // stick

    mat4 A = glm::translate(P, vec3(0.4f, 2.25f, 0));
    A = glm::rotate(A, glm::radians(10.0f), vec3(0, 0, 1));
    A = glm::rotate(A, glm::radians(-25.0f), vec3(0, 1, 0));
    A = glm::scale(A, vec3(0.85f));
    s.add(cyl, A, { 0.35f, 0, 0 }, { 0, 0, 90 }, { 0.1f, 0.8f, 0.1f }, white);        // fuselage
    s.add(cone, A, { 0.35f, 0, 0 }, { 0, 0, -90 }, { 0.1f, 0.25f, 0.1f }, red);       // nose
    s.add(box, A, { 0, 0, 0 }, { 0, 0, 0 }, { 0.3f, 0.04f, 1.3f }, red);              // wings
    s.add(box, A, { -0.38f, 0.2f, 0 }, { 0, 0, 0 }, { 0.2f, 0.3f, 0.04f }, red);      // tail fin
    s.add(box, A, { -0.4f, 0.05f, 0 }, { 0, 0, 0 }, { 0.16f, 0.03f, 0.5f }, red);     // stabiliser
}

// ---- Achievement 3: Own a cosy home ------------------------------------------
static void itemHouse(Scene& s, const mat4& P)
{
    Mesh *box = G.box.get(), *sph = G.sphere.get(), *gab = G.gable.get();
    vec3 cream = rgb(252, 232, 180), roof = rgb(230, 100, 80), wood = rgb(120, 80, 50);
    vec3 sky = rgb(150, 210, 250), white = rgb(250, 250, 250), brick = rgb(190, 90, 70);
    vec3 leaf = rgb(110, 190, 100), gold = rgb(255, 208, 64), path = rgb(225, 200, 150);

    s.add(box, P, { 0, 0.45f, 0 }, { 0, 0, 0 }, { 1.5f, 0.9f, 1.2f }, cream);
    s.add(gab, P, { 0, 0.9f, 0 }, { 0, 0, 0 }, { 1.8f, 0.75f, 1.5f }, roof);
    s.add(box, P, { 0.5f, 1.25f, -0.15f }, { 0, 0, 0 }, { 0.2f, 0.8f, 0.2f }, brick);
    s.add(box, P, { 0, 0.275f, 0.62f }, { 0, 0, 0 }, { 0.3f, 0.55f, 0.05f }, wood);
    s.add(sph, P, { 0.09f, 0.28f, 0.66f }, { 0, 0, 0 }, vec3(0.03f), gold, 64);
    for (int i = -1; i <= 1; i += 2)
    {
        s.add(box, P, { 0.5f * i, 0.55f, 0.61f }, { 0, 0, 0 }, { 0.34f, 0.34f, 0.04f }, white);
        s.add(box, P, { 0.5f * i, 0.55f, 0.63f }, { 0, 0, 0 }, { 0.26f, 0.26f, 0.04f }, sky, 64);
    }
    s.add(sph, P, { -0.88f, 0.18f, 0.55f }, { 0, 0, 0 }, { 0.22f, 0.2f, 0.22f }, leaf);
    s.add(sph, P, { 0.9f, 0.15f, 0.6f }, { 0, 0, 0 }, { 0.18f, 0.16f, 0.18f }, leaf);
    s.add(box, P, { 0, 0.01f, 0.85f }, { 0, 0, 0 }, { 0.3f, 0.02f, 0.5f }, path);
}

// ---- Achievement 4: Adopt a cat -------------------------------------------------
static void itemCat(Scene& s, const mat4& P)
{
    Mesh *cyl = G.cyl.get(), *sph = G.sphere.get(), *cone = G.cone.get(), *tor = G.torus.get();
    vec3 orange = rgb(240, 160, 90), white = rgb(255, 250, 240), pink = rgb(250, 160, 170);
    vec3 dark = rgb(40, 30, 30), red = rgb(220, 70, 80), gold = rgb(255, 208, 64);

    s.add(sph, P, { 0, 0.55f, 0 }, { 0, 0, 0 }, { 0.62f, 0.55f, 0.55f }, orange);
    s.add(sph, P, { 0, 1.35f, 0.05f }, { 0, 0, 0 }, { 0.55f, 0.48f, 0.5f }, orange);
    for (int i = -1; i <= 1; i += 2)
    {
        s.add(cone, P, { 0.3f * i, 1.62f, 0 }, { 0, 0, -15.0f * i }, { 0.17f, 0.3f, 0.17f }, orange);
        s.add(cone, P, { 0.3f * i, 1.64f, 0.08f }, { 0, 0, -15.0f * i }, { 0.09f, 0.2f, 0.09f }, pink);
        s.add(sph, P, { 0.2f * i, 1.38f, 0.47f }, { 0, 0, 0 }, vec3(0.06f), dark, 64);
        s.add(sph, P, { 0.22f * i, 0.13f, 0.45f }, { 0, 0, 0 }, { 0.16f, 0.12f, 0.2f }, white);
    }
    s.add(sph, P, { 0, 1.27f, 0.54f }, { 0, 0, 0 }, { 0.07f, 0.05f, 0.05f }, pink);
    s.add(cyl, P, { 0.5f, 0.15f, -0.25f }, { 0, 0, -35 }, { 0.09f, 0.75f, 0.09f }, orange);   // tail
    s.add(sph, P, { 0.93f, 0.76f, -0.25f }, { 0, 0, 0 }, vec3(0.1f), white);                   // tail tip
    s.add(tor, P, { 0, 1.0f, 0.02f }, { 0, 0, 0 }, vec3(0.4f), red);                           // collar
    s.add(sph, P, { 0, 0.95f, 0.4f }, { 0, 0, 0 }, vec3(0.07f), gold, 64);                     // bell
}

// ---- Achievement 5: Career success (trophy) ------------------------------------
static void itemTrophy(Scene& s, const mat4& P)
{
    Mesh *cyl = G.cyl.get(), *flare = G.flare.get(), *sph = G.sphere.get();
    Mesh *box = G.box.get(), *tor = G.torus.get(), *star = G.star.get();
    vec3 gold = rgb(255, 200, 50), wood = rgb(120, 80, 55), ruby = rgb(200, 60, 80);

    s.add(cyl, P, { 0, 0, 0 }, { 0, 0, 0 }, { 0.6f, 0.18f, 0.6f }, wood);
    s.add(cyl, P, { 0, 0.18f, 0 }, { 0, 0, 0 }, { 0.38f, 0.14f, 0.38f }, gold, 64);
    s.add(cyl, P, { 0, 0.32f, 0 }, { 0, 0, 0 }, { 0.1f, 0.55f, 0.1f }, gold, 64);
    s.add(sph, P, { 0, 0.6f, 0 }, { 0, 0, 0 }, { 0.17f, 0.12f, 0.17f }, ruby, 64);
    s.add(flare, P, { 0, 0.87f, 0 }, { 0, 0, 0 }, { 0.26f, 0.95f, 0.26f }, gold, 96);        // cup
    s.add(tor, P, { 0, 1.82f, 0 }, { 0, 0, 0 }, vec3(0.6f), rgb(255, 225, 110), 96);         // rim
    for (int i = -1; i <= 1; i += 2)
        s.add(tor, P, { 0.55f * i, 1.4f, 0 }, { 90, 0, 0 }, vec3(0.3f), gold, 96);          // handles
    s.add(box, P, { 0, 0.09f, 0.58f }, { 0, 0, 0 }, { 0.34f, 0.1f, 0.06f }, rgb(230, 170, 60), 64);  // plaque
    s.add(star, P, { 0, 2.3f, 0 }, { 0, 0, 0 }, vec3(0.38f), rgb(255, 238, 110), 96);       // spinning star
}

// ---------------------------------------------------------------------------
// The whole display case
// ---------------------------------------------------------------------------
static void buildScene(Scene& s)
{
    s.draws.clear();
    const mat4 I(1.0f);
    Mesh *box = G.box.get(), *cyl = G.cyl.get(), *sph = G.sphere.get(), *star = G.star.get();

    vec3 wallpaper = rgb(238, 224, 190), wainscot = rgb(176, 138, 100), trim = rgb(150, 112, 78);
    vec3 frameWood = rgb(128, 84, 52), divider = rgb(184, 134, 84), leaf = rgb(112, 190, 100);
    vec3 header = rgb(140, 198, 130), felt = rgb(104, 76, 56), gold = rgb(255, 208, 64);
    vec3 cell[5] = { rgb(178, 230, 200), rgb(255, 214, 180), rgb(180, 218, 250), rgb(250, 200, 220), rgb(255, 242, 178) };

    // room behind the case
    s.add(box, I, { 0, 0, -0.8f }, { 0, 0, 0 }, { 60, 40, 0.2f }, wallpaper, 4);
    s.add(box, I, { 0, -9.0f, -0.65f }, { 0, 0, 0 }, { 60, 10, 0.3f }, wainscot, 8);
    s.add(box, I, { 0, -3.9f, -0.5f }, { 0, 0, 0 }, { 60, 0.35f, 0.5f }, trim, 8);

    // frame (outer 13 x 6, depth 1.2) and backing
    s.add(box, I, { 0, 0.5f, -0.05f }, { 0, 0, 0 }, { 12.2f, 5.2f, 0.1f }, felt, 4);
    s.add(box, I, { 0, -2.25f, 0.5f }, { 0, 0, 0 }, { 13, 0.5f, 1.2f }, frameWood, 24);
    s.add(box, I, { 0, 3.25f, 0.5f }, { 0, 0, 0 }, { 13, 0.5f, 1.2f }, frameWood, 24);
    s.add(box, I, { -6.25f, 0.5f, 0.5f }, { 0, 0, 0 }, { 0.5f, 6, 1.2f }, frameWood, 24);
    s.add(box, I, { 6.25f, 0.5f, 0.5f }, { 0, 0, 0 }, { 0.5f, 6, 1.2f }, frameWood, 24);

    // header strip with one gold star per achievement
    s.add(box, I, { 0, 2.5f, 0.04f }, { 0, 0, 0 }, { 12, 1.0f, 0.08f }, header, 8);
    s.add(box, I, { 0, 2.0f, 0.25f }, { 0, 0, 0 }, { 12, 0.1f, 0.5f }, divider, 16);

    for (int i = 0; i < 5; i++)
    {
        float cx = -6.0f + 1.2f + 2.4f * i;
        s.add(box, I, { cx, 0, 0.025f }, { 0, 0, 0 }, { 2.4f, 4.0f, 0.05f }, cell[i], 8);     // coloured back panel
        s.add(star, I, { cx, 2.5f, 0.2f }, { 0, 0, 0 }, vec3(0.33f), gold, 96);
        s.add(cyl, I, { cx, -2.0f, 0.55f }, { 0, 0, 0 }, { 1.0f, 0.15f, 1.0f }, rgb(248, 240, 222), 16);   // pedestal
    }
    for (int i = 1; i < 5; i++)
        s.add(box, I, { -6.0f + 2.4f * i, 0, 0.55f }, { 0, 0, 0 }, { 0.12f, 4.0f, 1.1f }, divider, 16);

    // Animal-Crossing style leaves on the top of the frame
    s.add(sph, I, { -5.9f, 3.58f, 0.5f }, { 0, 0, 25 }, { 0.5f, 0.14f, 0.3f }, leaf, 24);
    s.add(sph, I, { -5.3f, 3.6f, 0.5f }, { 0, 0, -20 }, { 0.45f, 0.13f, 0.28f }, leaf, 24);
    s.add(sph, I, { 5.9f, 3.58f, 0.5f }, { 0, 0, -25 }, { 0.5f, 0.14f, 0.3f }, leaf, 24);
    s.add(sph, I, { 5.3f, 3.6f, 0.5f }, { 0, 0, 20 }, { 0.45f, 0.13f, 0.28f }, leaf, 24);

    // the five collectibles (each sways gently so the depth is easy to see)
    for (int i = 0; i < 5; i++)
    {
        float cx = -6.0f + 1.2f + 2.4f * i;
        mat4 P = glm::translate(I, vec3(cx, -1.85f, 0.55f));
        P = glm::rotate(P, glm::radians(0.0f), vec3(0, 1, 0));
        switch (i)
        {
            case 0: itemGraduation(s, P); break;
            case 1: itemGlobe(s, P); break;
            case 2: itemHouse(s, P); break;
            case 3: itemCat(s, P); break;
            default: itemTrophy(s, P); break;
        }
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main()
{
    if (!glfwInit())
    {
        std::cerr << "Failed to init GLFW\n";
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    GLFWwindow* window = glfwCreateWindow(WIN_W, WIN_H, "FUTURE ME - Achievement Case", nullptr, nullptr);
    if (!window)
    {
        std::cerr << "Failed to create window\n";
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK)
    {
        std::cerr << "Failed to init GLEW\n";
        return 1;
    }
    glGetError();  // GLEW may leave a spurious error behind
    glEnable(GL_MULTISAMPLE);

    GLuint sceneProg = makeProgram(kSceneVert, kSceneFrag);
    GLuint depthProg = makeProgram(kDepthVert, kDepthFrag);

    // meshes (built from vertices in code)
    G.box = makeBox();
    G.cyl = makeFrustum(14, 1.0f);
    G.flare = makeFrustum(14, 2.3f);
    G.cone = makeFrustum(10, 0.0f);
    G.sphere = makeSphere(10, 7);
    G.torus = makeTorus(16, 6, 0.12f);
    G.star = makeStar();
    G.gable = makeGable();

    // shadow map
    GLuint shadowFBO = 0, shadowTex = 0;
    glGenFramebuffers(1, &shadowFBO);
    glGenTextures(1, &shadowTex);
    glBindTexture(GL_TEXTURE_2D, shadowTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, SHADOW_RES, SHADOW_RES, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float border[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTex, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "Shadow framebuffer is not complete\n";
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // light (warm sun from the upper-left, in front of the case)
    const vec3 lightPos(-8.0f, 6.5f, 14.0f);
    const vec3 lightTarget(0.0f, 0.5f, 0.3f);
    const vec3 lightDir = glm::normalize(lightPos - lightTarget);
    const vec3 lightColour(0.95f, 0.88f, 0.76f);
    const mat4 lightSpace = glm::ortho(-11.0f, 11.0f, -9.0f, 9.0f, 1.0f, 40.0f) *
                            glm::lookAt(lightPos, lightTarget, vec3(0, 1, 0));

    // camera
    const vec3 target(0.0f, 0.5f, 0.4f);
    const float dist = 12.5f;
    float yawManual = 0.0f, pitchManual = 0.0f;
    bool sway = false, spaceWas = false;
    const mat4 projection = glm::perspective(glm::radians(45.0f), (float)WIN_W / (float)WIN_H, 0.1f, 100.0f);

    std::cout << "Arrow keys: orbit | Space: toggle sway | Esc: quit\n";

    Scene scene;
    double last = glfwGetTime();
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        double now = glfwGetTime();
        float dt = (float)(now - last);
        last = now;
        float t = (float)now;

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(window, GLFW_TRUE);
        if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) yawManual -= 40.0f * dt;
        if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) yawManual += 40.0f * dt;
        if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) pitchManual += 30.0f * dt;
        if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) pitchManual -= 30.0f * dt;
        yawManual = glm::clamp(yawManual, -45.0f, 45.0f);
        pitchManual = glm::clamp(pitchManual, -10.0f, 30.0f);
        bool spaceNow = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
        if (spaceNow && !spaceWas) sway = !sway;
        spaceWas = spaceNow;

        float yaw = glm::radians(yawManual + (sway ? 7.0f * std::sin(t * 0.4f) : 0.0f));
        float pitch = glm::radians(6.0f + pitchManual);
        vec3 camPos = target + dist * vec3(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch));
        mat4 view = glm::lookAt(camPos, target, vec3(0, 1, 0));

        buildScene(scene);

        // pass 1: depth from the light
        glViewport(0, 0, SHADOW_RES, SHADOW_RES);
        glBindFramebuffer(GL_FRAMEBUFFER, shadowFBO);
        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(2.0f, 4.0f);
        glUseProgram(depthProg);
        glUniformMatrix4fv(glGetUniformLocation(depthProg, "lightSpace"), 1, GL_FALSE, glm::value_ptr(lightSpace));
        GLint dModel = glGetUniformLocation(depthProg, "model");
        for (const Draw& d : scene.draws)
        {
            glUniformMatrix4fv(dModel, 1, GL_FALSE, glm::value_ptr(d.model));
            d.mesh->RenderMesh();
        }
        glDisable(GL_POLYGON_OFFSET_FILL);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // pass 2: the scene from the camera
        int fbw, fbh;
        glfwGetFramebufferSize(window, &fbw, &fbh);
        glViewport(0, 0, fbw, fbh);
        glClearColor(0.96f, 0.90f, 0.78f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(sceneProg);
        glUniformMatrix4fv(glGetUniformLocation(sceneProg, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(sceneProg, "projection"), 1, GL_FALSE, glm::value_ptr(projection));
        glUniformMatrix4fv(glGetUniformLocation(sceneProg, "lightSpace"), 1, GL_FALSE, glm::value_ptr(lightSpace));
        glUniform3fv(glGetUniformLocation(sceneProg, "lightDir"), 1, glm::value_ptr(lightDir));
        glUniform3fv(glGetUniformLocation(sceneProg, "lightColour"), 1, glm::value_ptr(lightColour));
        glUniform3fv(glGetUniformLocation(sceneProg, "viewPos"), 1, glm::value_ptr(camPos));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, shadowTex);
        glUniform1i(glGetUniformLocation(sceneProg, "shadowMap"), 0);
        GLint uModel = glGetUniformLocation(sceneProg, "model");
        GLint uCol = glGetUniformLocation(sceneProg, "objColour");
        GLint uShin = glGetUniformLocation(sceneProg, "shininess");
        for (const Draw& d : scene.draws)
        {
            glUniformMatrix4fv(uModel, 1, GL_FALSE, glm::value_ptr(d.model));
            glUniform3fv(uCol, 1, glm::value_ptr(d.colour));
            glUniform1f(uShin, d.shin);
            d.mesh->RenderMesh();
        }

        glfwSwapBuffers(window);
    }

    G = Meshes();   // free GL objects before the context goes away
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
