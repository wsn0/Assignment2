#undef GLFW_DLL

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WINDOWS_UTF8

#include <stb_image.h>
#include <stb_image_write.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <filesystem>
#include <vector>
#include <iostream>

#include "libs/Mesh.h"
#include "libs/Shader.h"
#include "libs/Window.h"
#include "ProjectPaths.h"

const GLint WIDTH = 800;
const GLint HEIGHT = 600;

const std::filesystem::path shaderDirectory  = std::filesystem::u8path(PROJECT_SHADER_DIR);
const std::filesystem::path textureDirectory = std::filesystem::u8path(PROJECT_TEXTURE_DIR);
const std::filesystem::path modelDirectory   = std::filesystem::u8path(PROJECT_MODEL_DIR);

// Camera settings
namespace Camera
{
    const float FOV = 40.0f;
    const float ASPECT = (float)WIDTH / (float)HEIGHT;
    const glm::vec3 POS(0.0, 5.5, 15.0f);
    const glm::vec3 TARGET(0.0f, 0.8f, -1.5f);
}

// Variable tuning
namespace Config
{
    // Spotlights
    const float SPOT_CEILING_GAP = 0.1f;
    const float SPOT_INNER_DEG = 25.0f;
    const float SPOT_OUTER_DEG = 50.0f;
    const float SPOT_RANGE = 14.0f;

    const float RUG_LIGHT_HEIGHT = 5.0f;
    const float RUG_LIGHT_FADE_DEG = 12.0f;

    // Stand size
    const float STAND_HX = 2.75f;
    const float STAND_HZ_FRONT = 2.75f;
    const float STAND_HZ_BACK = 2.25f;

    // Pedestal position (relate with the stand base position)
    const float PED_X = 0.9f; 
    const float PED_Z = 0.95f;

    // Gap between objects in the scene
    const float GAP_STAND_FRONT_X = 5.5f; 
    const float GAP_STAND_BACK_X = 1.5f;
    const float GAP_ROW_Z = 1.4f;
    const float GAP_WALL = 0.15f;
    const float GAP_SIDE = 1.0f;
    const float GAP_RUG = 0.25f;
    const float RUG_Z_START = 0.55f;

    const float FRONT_EDGE_Z = 5.95f;

    const float MARKER_Y = 5.0f;
    const float MARKER_R = 0.25f;
    const float MARKER_H = 0.50f;
    const int MARKER_SIDES = 4;
    const float MARKER_YAW = 0.0f; 

    const float SKIRT_THICK = 0.2f;
    const float VIEW_MARGIN = 0.3f;
    const float ROOM_HEAD_SCALE = 0.12f;
    const float OBJECT_HEAD_SCALE = 1.0f;

    // Character 
    const float CHAR_X = 0.0f;
    const float CHAR_Z = 5.0f;
    const float CHAR_HEIGHT = 2.5f;
    const float CHAR_YAW = 0.0f;

    const float CHAR_MIN_Y = -0.9519f;
    const float CHAR_MODEL_H = 1.8993f;
    const float RUG_TOP_Y = 0.09f;

    const float STAND_TOP_Y = 0.14f;

    // Plane and Globe on the same stand
    const int   PLANE_STAND = 2;
    const float PLANE_SIZE = 2.3f;
    const float PLANE_SCALE = 1.7f;
    const float PLANE_YAW = glm::radians(60.0f);
    const float PLANE_HOVER = 0.0f;
    const float PLANE_DX = -0.85f;
    const float PLANE_DZ = -0.2f;
    
    const int   GLOBE_STAND = 2;
    const float GLOBE_SIZE = 2.3f;
    const float GLOBE_YAW  = 0.0f;
    const float GLOBE_HOVER = 0.0f;
    const float GLOBE_DX = 0.8f;
    const float GLOBE_DZ = 0.35f;

    // Grad student
    const int   GRAD_STAND  = 0; 
    const float GRAD_HEIGHT = 3.0f;
    const float GRAD_YAW = glm::radians(45.0f);
    const float GRAD_HOVER = 0.0f;
    const float GRAD_DX = -0.0f;
    const float GRAD_DZ = 0.0f;

    // Photo frame on the back wall
    const float PHOTO_H = 1.5f;
    const float PHOTO_MAX_W = 1.3f;
    const float PHOTO_GAP = 0.01f;
}

struct Layout
{
    float frontCX, frontCZ;
    float backCX, backCZ;
    float x0, x1, z0, z1, H;   // Room size
    float rugX, rugZ0;         // Rug position
};

struct Object
{
    Mesh* mesh;
    glm::vec3 color;
    float headScale;
};

std::vector<Object> sceneObj;
Object markerObj{nullptr, {1.0f, 0.90f, 0.65f}};
std::vector<Shader*> shaderList;
std::vector<glm::vec3> spotPos;
std::vector<glm::vec3> markerPos; 
Mesh* charMesh = nullptr;
GLuint charTex = 0;

struct Exhibit
{
    Mesh* mesh = nullptr;
    GLuint tex = 0;
    glm::vec3 color{1.0f};
    glm::mat4 matrix{1.0f};
};

std::vector<Exhibit> exhibits;
std::vector<float> frameCX;

Layout ComputeLayout()
{
    using namespace Config;
    Layout l;

    // Stand position
    l.frontCX = GAP_STAND_FRONT_X * 0.5f + STAND_HX;
    l.backCX = GAP_STAND_BACK_X * 0.5f + STAND_HX;
    l.frontCZ = FRONT_EDGE_Z - STAND_HZ_FRONT;

    const float frontZ = l.frontCZ - STAND_HZ_FRONT;
    l.backCZ = frontZ - GAP_ROW_Z - STAND_HZ_BACK;
    const float backZ = l.backCZ - STAND_HZ_BACK;

    l.z0 = backZ - GAP_WALL - SKIRT_THICK;

    // Room size
    const float halfFov = glm::radians(Camera::FOV * 0.5f);
    const float pitch = glm::atan(Camera::POS.y - Camera::TARGET.y, Camera::POS.z - Camera::TARGET.z);

    // Floor disapear
    const float floorZ = Camera::POS.z - Camera::POS.y / glm::tan(pitch + halfFov);
    l.z1 = floorZ + VIEW_MARGIN;

    // Floor perspective
    const float hDist = Camera::POS.z - l.z0;
    const float depth = hDist * glm::cos(pitch) + Camera::POS.y * glm::sin(pitch);
    const float viewHalfW = depth * glm::tan(halfFov) * Camera::ASPECT + VIEW_MARGIN;
    const float standHalfW = glm::max(l.frontCX, l.backCX) + STAND_HX;

    l.x1 = glm::max(standHalfW + GAP_SIDE, viewHalfW);
    l.x0 = -l.x1;

    // Wall height
    const float topY = Camera::POS.y + hDist * glm::tan(halfFov - pitch);
    l.H = glm::max(5.0f, topY + VIEW_MARGIN);

    // Rug
    l.rugX = GAP_STAND_FRONT_X * 0.5f - GAP_RUG;
    l.rugZ0 = frontZ + RUG_Z_START;

    return l;
}

const Layout layout = ComputeLayout();

void AddObj(const MeshData& data, glm::vec3 color, float headScale = Config::ROOM_HEAD_SCALE)
{
    sceneObj.push_back({BuildMesh(data), color, headScale});
}

// Draw item display stand
void AddDisplayStand(MeshData& platOuter, MeshData& platInner, MeshData& pedestal, MeshData& plaque, float cx, float cz, float hz, float pedX)
{
    const float hx = Config::STAND_HX;
    const float rim = 0.3f;

    AddBox(platOuter, {cx - hx, 0.0f, cz - hz},  {cx + hx, 0.08f, cz + hz});
    AddBox(platInner, {cx - (hx - rim), 0.08f, cz - (hz - rim)}, {cx + (hx - rim), 0.14f, cz + (hz - rim)});

    spotPos.push_back({cx, layout.H - Config::SPOT_CEILING_GAP, cz});

    const float px = cx + pedX;
    const float pz = cz + hz - Config::PED_Z;

    markerPos.push_back({cx, Config::MARKER_Y, cz});

    const float hw = 0.5f, hd = 0.45f;            
    const float yFront = 0.9f, yBack = 1.3f;
    
    AddWedge(pedestal, {px - hw, 0.14f, pz - hd}, {px + hw, 0.0f, pz + hd}, yFront, yBack);

    auto slopeY = [&](float z) { return yFront + (yBack - yFront) * ((pz + hd - z) / (2.0f * hd)) + 0.012f; };

    float zf = pz + hd - 0.12f, zb = pz - hd + 0.12f;
    AddQuad(
        plaque, 
        {px - 0.32f, slopeY(zf), zf}, 
        {px + 0.32f, slopeY(zf), zf}, 
        {px + 0.32f, slopeY(zb), zb}, 
        {px - 0.32f, slopeY(zb), zb}
    );
}

void CreateDisplays()
{
    MeshData platOuter, platInner, pedestal, plaque;

    const float fx = layout.frontCX, fz = layout.frontCZ;
    const float bx = layout.backCX,  bz = layout.backCZ;
    const float ped = Config::STAND_HX - Config::PED_X;

    AddDisplayStand(platOuter, platInner, pedestal, plaque, -fx, fz, Config::STAND_HZ_FRONT, ped);      // Front-left
    AddDisplayStand(platOuter, platInner, pedestal, plaque,  fx, fz, Config::STAND_HZ_FRONT, -ped);     // Front-Right
    AddDisplayStand(platOuter, platInner, pedestal, plaque, -bx, bz, Config::STAND_HZ_BACK, ped);       // Back-Left
    AddDisplayStand(platOuter, platInner, pedestal, plaque,  bx, bz, Config::STAND_HZ_BACK, -ped);      // Back-Right

    AddObj(platOuter, {0.45f, 0.50f, 0.40f}, Config::OBJECT_HEAD_SCALE);
    AddObj(platInner, {0.66f, 0.70f, 0.54f}, Config::OBJECT_HEAD_SCALE);
    AddObj(pedestal,  {0.15f, 0.22f, 0.42f}, Config::OBJECT_HEAD_SCALE);
    AddObj(plaque,    {0.88f, 0.78f, 0.52f}, Config::OBJECT_HEAD_SCALE);
}

// Draw room (the background)
void CreateRoom() 
{
    // Volume of the room (W x H x Depth)
    const float X0 = layout.x0, X1 = layout.x1;
    const float Z0 = layout.z0, Z1 = layout.z1;
    const float H = layout.H;

    // Room floor
    MeshData grout, tileA, tileB;
    AddBox(grout, {X0, -0.1f, Z0}, {X1, 0.0f, Z1});

    const int dx = (int)glm::ceil(X1 - X0);
    const int dz = (int)glm::ceil(Z1 - Z0);
    const float tw = (X1 - X0) / dx;
    const float td = (Z1 - Z0) / dz;

    for (int i = 0; i < dx; i++) {
        for (int j = 0; j < dz; j++) {
            float x = X0 + i * tw;
            float z = Z0 + j * td;

            MeshData& t = ((i + j) % 2 == 0)? tileA : tileB;
            AddBox(t, {x + 0.03f, 0.0f, z + 0.03f}, {x + tw - 0.03f, 0.04f, z + td - 0.03f});
        }
    }

    AddObj(grout, {0.55f, 0.42f, 0.30f});
    AddObj(tileA, {0.93f, 0.82f, 0.62f});
    AddObj(tileB, {0.84f, 0.70f, 0.50f});

    // Rug at the center of the room
    MeshData border, inner;
    const float rx = layout.rugX, rz = layout.rugZ0;

    AddBox(border, {-rx, 0.04f, rz}, {rx, 0.07f, Z1});
    AddBox(inner,  {-rx + 0.4f, 0.07f, rz + 0.4f}, {rx - 0.4f, 0.09f, Z1});
    AddObj(border, {0.86f, 0.45f, 0.40f});
    AddObj(inner,  {0.95f, 0.88f, 0.72f});

    // Room wall
    const float D = 0.3f;       // Wall Depth
    const float BASE = 1.7f;    // Base height
    MeshData wall, wain, rail, skirt, panels;

    // Wall at the back
    AddBox(wall, {X0 - D, 0.0f, Z0 - D}, {X1 + D, H, Z0 });
    AddBox(wain, {X0, 0.0f, Z0}, {X1, BASE, Z0 + 0.12f});
    AddBox(rail, {X0, BASE, Z0}, {X1, BASE + 0.15f, Z0 + 0.2f});
    AddBox(skirt, {X0, 0.0f, Z0 + 0.12f}, {X1, 0.3f, Z0 + 0.2f});

    // Wall at the left/right side
    AddBox(wall, {X0 - D, 0.0f, Z0}, {X0, H, Z1});
    AddBox(wall, {X1, 0.0f, Z0}, {X1 + D, H, Z1});
    AddBox(wain, {X0, 0.0f, Z0}, {X0 + 0.12f, BASE, Z1});
    AddBox(wain, {X1 - 0.12f, 0.0f, Z0}, {X1, BASE, Z1});
    AddBox(rail, {X0, BASE, Z0}, {X0 + 0.2f, BASE + 0.15f, Z1});
    AddBox(rail, {X1 - 0.2f, BASE, Z0}, {X1, BASE + 0.15f, Z1});
    AddBox(skirt, {X0 + 0.12f, 0.0f, Z0}, {X0 + 0.2f, 0.3f, Z1});
    AddBox(skirt, {X1 - 0.2f, 0.0f, Z0}, {X1 - 0.12f, 0.3f, Z1});

    // Panels
    const float W = X1 - X0;
    const int numB = glm::max(1, (int)(W / 3.6f));
    const int stepB = W / numB;
    frameCX.clear();
    for (int i = 0; i < numB; i++)
    {
        float cx = X0 + stepB * (i + 0.5f);
        frameCX.push_back(cx);
        AddBox(panels, {cx - 1.1f, 2.5f, Z0}, {cx + 1.1f, 4.4f, Z0 + 0.08f});
    }

    const float Dz = Z1 - Z0;
    const int numS = glm::max(1, (int)(Dz / 4.2f));
    const float stepS = Dz / numS;
    for (int i = 0; i < numS; i++)
    {
        float cz = Z0 + stepS * (i + 0.5f);

        AddBox(panels, {X0, 2.5f, cz - 1.1f }, {X0 + 0.08f, 4.4f, cz + 1.1f});
        AddBox(panels, {X1 - 0.08f, 2.5f, cz - 1.1f}, {X1, 4.4f, cz + 1.1f});
    }

    AddObj(wall, {0.98f, 0.93f, 0.80f});
    AddObj(wain, {0.62f, 0.42f, 0.27f});
    AddObj(rail, {0.78f, 0.58f, 0.38f});
    AddObj(skirt, {0.45f, 0.30f, 0.20f});
    AddObj(panels, {0.93f, 0.86f, 0.70f});
}

void CreateMarker() 
{
    MeshData m;
    AddInvertedPyramid(m , {0.0f, 0.0f, 0.0f}, Config::MARKER_R, Config::MARKER_H, Config::MARKER_SIDES);
    markerObj.mesh = BuildMesh(m);
}

GLuint LoadTexture(const std::filesystem::path& path)
{
    int w = 0, h = 0, channels = 0;
    unsigned char* data = stbi_load(path.u8string().c_str(), &w, &h, &channels, 4);
    if (!data) {
        std::cerr << "Failed to load texture: " << path.u8string() << "\n";
        return 0;
    }

    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);

    return id;
}

void CreateShaders()
{
    Shader* shader1 = new Shader();
    shader1->CreateFromFiles(
        shaderDirectory / "shader.vert",
        shaderDirectory / "shader.frag"
    );

    shaderList.push_back(shader1);
}

void CreateCharater()
{
    charMesh = new Mesh();
    if (!charMesh->CreateMeshFromOBJ((modelDirectory / "Character.obj").string(), true)) {
        delete charMesh;
        charMesh = nullptr;
    }

    charTex = LoadTexture(textureDirectory / "character" / "Character_basecolor.jpg");
}

glm::vec2 StandCenter(int i)
{
    switch (i)
    {
        case 0:  return {-layout.frontCX, layout.frontCZ};
        case 1:  return { layout.frontCX, layout.frontCZ};
        case 2:  return {-layout.backCX,  layout.backCZ};
        default: return { layout.backCX,  layout.backCZ};
    }
}

glm::mat4 MakeExhibitMatrix(const Mesh& mesh, float x, float z, float size, float yaw, float hover)
{
    const glm::vec3 mn = mesh.GetBoundsMin();
    const glm::vec3 mx = mesh.GetBoundsMax();
    const glm::vec3 ext = mx - mn;
    const float s = size / glm::max(ext.x, glm::max(ext.y, ext.z));

    glm::mat4 m(1.0f);
    m = glm::translate(m, glm::vec3(x, Config::STAND_TOP_Y + hover, z));
    m = glm::rotate(m, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    m = glm::scale(m, glm::vec3(s));
    m = glm::translate(m, glm::vec3(-(mn.x + mx.x) * 0.5f, -mn.y, -(mn.z + mx.z) * 0.5f));

    return m;
}

bool LoadExhibit(Exhibit& e, const char* objFile, const std::filesystem::path& texFile)
{
    e.mesh = new Mesh();
    if (!e.mesh->CreateMeshFromOBJ((modelDirectory / objFile).string(), true)) {
        delete e.mesh;
        e.mesh = nullptr;

        return false;
    }
    e.tex = LoadTexture(texFile);

    return true;
}

void CreateExhibits()
{
    using namespace Config;

    // Plane
    Exhibit plane;
    if (LoadExhibit(plane, "Plane.obj", textureDirectory / "plane" / "Plane_basecolor.png"))
    {
        const glm::vec2 c = StandCenter(PLANE_STAND);
        plane.matrix = MakeExhibitMatrix(*plane.mesh, c.x + PLANE_DX, c.y + PLANE_DZ, PLANE_SIZE * PLANE_SCALE, PLANE_YAW, PLANE_HOVER);
        exhibits.push_back(plane);
    }

    // Globe
    Exhibit globe;
    if (LoadExhibit(globe, "Globe.obj", textureDirectory / "globe" / "Globe_Color.png"))
    {
        const glm::vec2 c = StandCenter(GLOBE_STAND);
        globe.matrix = MakeExhibitMatrix(*globe.mesh, c.x + GLOBE_DX, c.y + GLOBE_DZ, GLOBE_SIZE, GLOBE_YAW, GLOBE_HOVER);

        exhibits.push_back(globe);
    }

    // Grad student
    Exhibit grad;
    if (spotPos.size() > (size_t)GRAD_STAND && LoadExhibit(grad, "Graduate.obj", textureDirectory / "gradute" / "texture.jpg"))
    {
        const glm::vec3 light = spotPos[GRAD_STAND];
        grad.matrix = MakeExhibitMatrix(*grad.mesh, light.x + GRAD_DX, light.z + GRAD_DZ, GRAD_HEIGHT, GRAD_YAW, GRAD_HOVER);
        exhibits.push_back(grad);
    }
}

void CreatePhotos()
{
    const char* files[] = {"food.jpg", "flower.jpg", "deers.jpg", "sky.jpg", "lake.jpg"};
    const int numFiles = 5;

    const float panelCY = (2.5f + 4.4f) * 0.5f;          
    const float zFront  = layout.z0 + 0.08f + Config::PHOTO_GAP;

    for (size_t i = 0; i < frameCX.size(); i++)
    {
        const std::filesystem::path path = textureDirectory / "images" / files[i % numFiles];

        int w = 0, h = 0, ch = 0;
        if (!stbi_info(path.u8string().c_str(), &w, &h, &ch)) continue;

        const float aspect = (float)w / (float)h;
        const float ph = Config::PHOTO_H;
        const float pw = glm::min(ph * aspect, Config::PHOTO_MAX_W);

        const float cx = frameCX[i];
        const float y0 = panelCY - ph * 0.5f, y1 = panelCY + ph * 0.5f;
        const glm::vec3 n(0.0f, 0.0f, 1.0f);

        MeshData q;
        q.vertices.push_back({{cx - pw * 0.5f, y0, zFront}, n, {0.0f, 1.0f}});
        q.vertices.push_back({{cx + pw * 0.5f, y0, zFront}, n, {1.0f, 1.0f}});
        q.vertices.push_back({{cx + pw * 0.5f, y1, zFront}, n, {1.0f, 0.0f}});
        q.vertices.push_back({{cx - pw * 0.5f, y1, zFront}, n, {0.0f, 0.0f}});
        q.indices = {0, 1, 2, 0, 2, 3};

        Exhibit e;
        e.mesh = BuildMesh(q);
        e.tex  = LoadTexture(path);
        exhibits.push_back(e);
    }
}

void Cleanup()
{
    for (Object& o : sceneObj) { delete o.mesh; }    
    sceneObj.clear();

    delete markerObj.mesh;
    markerObj.mesh = nullptr;

    delete charMesh;
    charMesh = nullptr;
    if (charTex) { glDeleteTextures(1, &charTex); charTex = 0; }

    for (Exhibit& e : exhibits) {
        delete e.mesh;
        if (e.tex) { glDeleteTextures(1, &e.tex); }
    }
    exhibits.clear();

    for (Shader* shader : shaderList) { delete shader; }
    shaderList.clear();
}

// Press P to save the rendered image (PNG format)
void SaveScreenshot(const char* filename, int w, int h)
{
    std::vector<unsigned char> pixels((size_t)w * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    stbi_flip_vertically_on_write(1);
    stbi_write_png(filename, w, h, 3, pixels.data(), w * 3);

    std::cout << "Saved " << filename << std::endl;
}
#ifdef _WIN32
extern "C" {
    __declspec(dllexport) unsigned long NvOptimusEnablement = 1;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

int main()
{
    Window mainWindow(WIDTH, HEIGHT, 3, 3);
    if (mainWindow.initialise() != 0)
        return 1;

    CreateRoom();
    CreateDisplays();
    CreateMarker();
    CreateCharater();
    CreateExhibits();
    CreatePhotos();
    CreateShaders();

    glm::mat4 projection = glm::perspective(
        glm::radians(Camera::FOV),
        (GLfloat)mainWindow.getBufferWidth() / (GLfloat)mainWindow.getBufferHeight(),
        0.1f,
        100.0f
    );

    // Camera viewport 
    glm::vec3 cameraPos = Camera::POS;
    glm::vec3 cameraTarget = Camera::TARGET;
    glm::mat4 view = glm::lookAt(cameraPos, cameraTarget, glm::vec3(0.0f, 1.0f, 0.0f));

    // Light
    glm::vec3 spotDir(0.0f, -1.0f, 0.0f);
    glm::vec3 spotColour(2.0f, 1.8f, 1.3f);
    glm::vec3 ambientColour(0.25f, 0.24f, 0.23f);
    float spotInnerCos = glm::cos(glm::radians(Config::SPOT_INNER_DEG));
    float spotOuterCos = glm::cos(glm::radians(Config::SPOT_OUTER_DEG));
    float spotRadius   = Config::SPOT_RANGE;

    // Headlight
    glm::vec3 headDir = glm::normalize(cameraTarget - cameraPos);
    glm::vec3 headColour(0.45f, 0.43f, 0.40f);

    // Center light
    const float rugCZ = (layout.rugZ0 + layout.z1) * 0.5f;
    glm::vec3 rugPos(0.0f, Config::RUG_LIGHT_HEIGHT, rugCZ);
    glm::vec3 rugColour(2.2f, 1.8f, 1.2f);

    const float rugHalfw = layout.rugX;
    const float rugHalfD = (layout.z1 - layout.rugZ0) * 0.5f;
    const float rugCorner = glm::atan(glm::sqrt(rugHalfw * rugHalfw + rugHalfD * rugHalfD), Config::RUG_LIGHT_HEIGHT - 0.09f);

    float rugInnerCos = glm::cos(rugCorner);
    float rugOuterCos = glm::cos(rugCorner + glm::radians(Config::RUG_LIGHT_FADE_DEG));
    float rugRadius = 14.0f;

    // Character
    const float charScale = Config::CHAR_HEIGHT / Config::CHAR_MODEL_H;
    glm::mat4 character(1.0f);
    character = glm::translate(character, glm::vec3(Config::CHAR_X, Config::RUG_TOP_Y - Config::CHAR_MIN_Y * charScale, Config::CHAR_Z));
    character = glm::rotate(character, Config::CHAR_YAW, glm::vec3(0.0f, 1.0f, 0.0f));
    character = glm::scale(character, glm::vec3(charScale));

    bool pressP = false;

    while (!mainWindow.getShouldClose()) {
        glfwPollEvents();

        glClearColor(0.20f, 0.14f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        Shader* sh = shaderList[0];
        sh->UseShader();

        glUniformMatrix4fv(sh->GetUniformLocation("view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(sh->GetUniformLocation("projection"), 1, GL_FALSE, glm::value_ptr(projection));
        glUniform3fv(sh->GetUniformLocation("spotPos"), (GLsizei)spotPos.size(), glm::value_ptr(spotPos[0]));
        glUniform3fv(sh->GetUniformLocation("spotDir"), 1, glm::value_ptr(spotDir));
        glUniform3fv(sh->GetUniformLocation("spotColour"), 1, glm::value_ptr(spotColour));
        glUniform3fv(sh->GetUniformLocation("ambientColour"), 1, glm::value_ptr(ambientColour));
        glUniform1f(sh->GetUniformLocation("spotInnerCos"), spotInnerCos);
        glUniform1f(sh->GetUniformLocation("spotOuterCos"), spotOuterCos);
        glUniform1f(sh->GetUniformLocation("spotRadius"), spotRadius);
        glUniform3fv(sh->GetUniformLocation("rugPos"), 1, glm::value_ptr(rugPos));
        glUniform3fv(sh->GetUniformLocation("rugColour"), 1, glm::value_ptr(rugColour));
        glUniform1f(sh->GetUniformLocation("rugInnerCos"), rugInnerCos);
        glUniform1f(sh->GetUniformLocation("rugOuterCos"), rugOuterCos);
        glUniform1f(sh->GetUniformLocation("rugRadius"), rugRadius);
        glUniform3fv(sh->GetUniformLocation("headDir"), 1, glm::value_ptr(headDir));
        glUniform3fv(sh->GetUniformLocation("headColour"), 1, glm::value_ptr(headColour));
        glUniform1i(sh->GetUniformLocation("useTexture"), 0);

        glm::mat4 model(1.0f);
        glUniformMatrix4fv(sh->GetUniformLocation("model"), 1, GL_FALSE, glm::value_ptr(model));

        for (const Object& o : sceneObj) {
            glUniform3fv(sh->GetUniformLocation("uColor"), 1, glm::value_ptr(o.color));
            glUniform1f(sh->GetUniformLocation("headScale"), o.headScale);

            o.mesh->RenderMesh();
        }

        glUniform1i(sh -> GetUniformLocation("unlit"), 1);
        glUniform3fv(sh -> GetUniformLocation("uColor"), 1, glm::value_ptr(markerObj.color));
        for(size_t i =0; i<markerPos.size(); i++) {
            glm::mat4 mm(1.0f);
            mm = glm::translate(mm, glm::vec3(markerPos[i].x, Config::MARKER_Y, markerPos[i].z));
            mm = glm::rotate(mm,Config::MARKER_YAW, glm::vec3(0.0f, 1.0f, 0.0f));
            glUniformMatrix4fv(sh -> GetUniformLocation("model"), 1, GL_FALSE, glm::value_ptr(mm));
            markerObj.mesh -> RenderMesh();
        }
        glUniform1i(sh -> GetUniformLocation("unlit"), 0);

        // Character with texture mapped
        if (charMesh && charTex) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, charTex);
            glUniform1i(sh->GetUniformLocation("texture1"), 0);
            glUniform1i(sh->GetUniformLocation("useTexture"), 1);
            glUniformMatrix4fv(sh->GetUniformLocation("model"), 1, GL_FALSE, glm::value_ptr(character));

            charMesh->RenderMesh();

            glUniform1i(sh->GetUniformLocation("useTexture"), 0);
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        // Plane, globe and photo frames
        for (const Exhibit& e : exhibits) {
            if (!e.mesh || !e.tex) continue;
            
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, e.tex);
            glUniform1i(sh->GetUniformLocation("texture1"), 0);
            glUniform1i(sh->GetUniformLocation("useTexture"), 1);
            glUniformMatrix4fv(sh->GetUniformLocation("model"), 1, GL_FALSE, glm::value_ptr(e.matrix));

            e.mesh->RenderMesh();
        }

        glUniform1i(sh->GetUniformLocation("useTexture"), 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        
        glUseProgram(0);

        bool pDown = glfwGetKey(mainWindow.getWindow(), GLFW_KEY_P) == GLFW_PRESS;
        if (pDown && !pressP)
            SaveScreenshot("screenshot.png", mainWindow.getBufferWidth(), mainWindow.getBufferHeight());
        pressP = pDown;

        mainWindow.swapBuffers();
    }

    Cleanup();
    return 0;
}