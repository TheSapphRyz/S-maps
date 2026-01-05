#define _CRT_SECURE_NO_WARNINGS
#define RAYGUI_IMPLEMENTATION
#define GRAPHICS_API_VULKAN
#include <raylib.h>
#include <raygui.h>
#include <cstddef>
#include <map>
#include <vector>
#include <string>
#include "json.hpp"
#include <functional>
#include <nfd.h>
#include <rlgl.h>
#include <float.h>
#include <cmath>
#include <random>
#include <raymath.h>
using json = nlohmann::json;

json r;
float seed;
std::map<std::string, std::pair<Texture2D, std::string>> texs; // текстуры и их base64 данные для сохранения в json
std::vector<const char*> texs_for_list;
enum TOOL {TILE, OBJP, SELECT};

struct Tile {
	int id;
	int bid; // 0 - ничего ++++++ 1 - холм +++++++++ 2 - лесок ++++++ 3 - лес ++++++++ 4 - море +++++++++ 5 - речка
	std::string tid; // texs 
	float h;
	json j;
};
struct OBJ {
	int id;
	int tid;
	int proch; // 0-100 прочность предмета. Подходит и для еды
	Vector3 vec3; // x, Y(выс), z 
	float pov; // поворот 
	float razm; // размер
	void (*onclck)(OBJ& self); // колбек на клик 
	json j;
	std::string anim; //потом заменить на нормальную анимацию объекта, а пока это просто текстуры по кадрам 1_3_5 (tid)
};
Vector3 playerPos = { 0.0f, 10.0f, 0.0f };
std::vector<OBJ>OBJS;
std::vector<Tile>tiles;
TOOL ct;
bool sjw = false;
char jb[4096] = "{\n\tur json\n}";
Tile* st = nullptr;
OBJ* so = nullptr;
int MAP_W = 1000;
int MAP_H = 1000;
int tool_s;
int sc_idx;
int act_idx;
int foc_idx;

float GetVertexHeight(int x, int z) {
	if (x < 0 || x >= MAP_W || z < 0 || z >= MAP_H) return 0.0f;
	return (float)tiles[z * MAP_W + x].h;
}
float GetInterpolatedHeight(float x, float z) {
	int x0 = (int)std::floor(x);
	int z0 = (int)std::floor(z);
	float h00 = GetVertexHeight(x0, z0);
	float h10 = GetVertexHeight(x0 + 1, z0);
	float h01 = GetVertexHeight(x0, z0 + 1);
	float h11 = GetVertexHeight(x0 + 1, z0 + 1);
	float sx = x - (float)x0;
	float sz = z - (float)z0;
	float lerpTop = h00 + sx * (h10 - h00);
	float lerpBottom = h01 + sx * (h11 - h01);
	return lerpTop + sz * (lerpBottom - lerpTop);
}
void DrawMap() {
	for (int z = 0; z < MAP_H - 1; z++) {
		for (int x = 0; x < MAP_W - 1; x++) {
			int idx = z * MAP_W + x;
			Tile& t = tiles[idx];
			float h00 = GetVertexHeight(x, z);        
			float h10 = GetVertexHeight(x + 1, z);    
			float h11 = GetVertexHeight(x + 1, z + 1); 
			float h01 = GetVertexHeight(x, z + 1);    
			Texture2D texture = { 0 };
			auto it = texs.find(t.tid);
			if (it != texs.end()) texture = it->second.first;
			rlSetTexture(texture.id);
			rlBegin(RL_QUADS);
			rlColor4ub(255, 255, 255, 255);
			rlTexCoord2f(0.0f, 0.0f);
			rlVertex3f((float)x, h00, (float)z);
			rlTexCoord2f(0.0f, 1.0f);
			rlVertex3f((float)x, h01, (float)z + 1.0f);
			rlTexCoord2f(1.0f, 1.0f);
			rlVertex3f((float)x + 1.0f, h11, (float)z + 1.0f);
			rlTexCoord2f(1.0f, 0.0f);
			rlVertex3f((float)x + 1.0f, h10, (float)z);
			rlEnd();
			rlSetTexture(0);
			DrawLine3D({ (float)x, h00, (float)z }, { (float)x + 1, h10, (float)z }, DARKGRAY);
			DrawLine3D({ (float)x, h00, (float)z }, { (float)x, h01, (float)z + 1 }, DARKGRAY);
		}
	}
}
float rnd_seed() {
	std::random_device dev;
	std::mt19937 rng(dev());
	std::uniform_real_distribution<float> dist(-10.0f, 100000.0f);
	return dist(rng);
}



float get_noise(float x, float z) {

	float h = sin(x * PI + z - seed) * PI + cos(seed);
	return h - std::floor(h);
}
float smooth_noise(float x, float z) {
	float ix = std::floor(x);
	float iz = std::floor(z);
	float fx = x - ix;
	float fz = z - iz;
	float ux = fx * fx * (3.0f - 2.0f * fx);
	float uz = fz * fz * (3.0f - 2.0f * fz);
	float res =
		(1.0f - ux) * (1.0f - uz) * get_noise(ix, iz) +
		ux * (1.0f - uz) * get_noise(ix + 1.0f, iz) +
		(1.0f - ux) * uz * get_noise(ix, iz + 1.0f) +
		ux * uz * get_noise(ix + 1.0f, iz + 1.0f);
	return res;
}

void gen_l() {
	seed = rnd_seed();
	for (int i = 0; i < MAP_W * MAP_H; i++) {
		int x = i % MAP_W;
		int z = i / MAP_W;
		float biome = smooth_noise(x * 0.05f, z * 0.05f);

		float finalHeight = 0.0f;
		if (biome < 0.4f) finalHeight = smooth_noise(x * 0.1f, z * 0.1f) * 2.0f;
		else if (biome < 0.07) {
			float riverWarpX = smooth_noise(x * 0.03f, z * 0.03f) * 10.0f; // Сильное искривление
			float riverWarpZ = smooth_noise(z * 0.03f, x * 0.03f) * 10.0f;
			float river = std::abs(smooth_noise((x + riverWarpX) * 0.06f, (z + riverWarpZ) * 0.06f) - 0.5f);		if (river < 0.05f) finalHeight -= 5.0f;
		}
		else if (biome < 0.7f) {
			float h = smooth_noise(x * 0.1f, z * 0.1f);
			finalHeight = std::pow(h, 2.0f) * 8.0f;
		}
		else {
			float h = 1.0f - std::abs(smooth_noise(x * 0.15f, z * 0.15f) * 2.0f - 1.2f);
			finalHeight = h * 15.0f;
		}
		
		tiles[i] = { i, 0, "default", finalHeight, json::object() };
	}
}

int main() {
	SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
	InitWindow(1920, 1000, "S-maps");
	SetTargetFPS(120);
	tiles.resize(MAP_W * MAP_H);
	//gen_l();
	Image img = GenImageColor(64, 64, WHITE);
	texs["default"] = { LoadTextureFromImage(img), "" };
	UnloadImage(img);
	Camera3D camera = { 0 };
	camera.position = { (float)MAP_W * 0.8f, (float)MAP_W * 0.8f, (float)MAP_H * 0.8f };
	camera.target = { MAP_W / 2.0f, 0.0f, MAP_H / 2.0f }; 
	camera.up = { 0.0f, 1.0f, 0.0f };
	camera.fovy = 30.0f; 
	camera.projection = CAMERA_ORTHOGRAPHIC;  
	Vector2 move;
	while (!WindowShouldClose()) {
		Vector2 ws = {GetScreenWidth(), GetScreenHeight()};
		float dt = GetFrameTime();
		if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
			Vector2 mouseDelta = GetMouseDelta();
			if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
				float rotationAngle = mouseDelta.x * -0.2f * DEG2RAD;
				float tiltAngle = mouseDelta.y * -0.2f * DEG2RAD;
				camera.position = Vector3RotateByAxisAngle(camera.position, { 0, 1, 0 }, rotationAngle);
				camera.up = Vector3RotateByAxisAngle(camera.up, { 0, 1, 0 }, rotationAngle);
				Vector3 camRight = Vector3CrossProduct(camera.up, Vector3Subtract(camera.target, camera.position));
				camRight = Vector3Normalize(camRight);
				camera.position = Vector3RotateByAxisAngle(camera.position, camRight, tiltAngle);
				camera.up = Vector3RotateByAxisAngle(camera.up, camRight, tiltAngle);
			}
			else {
				float panSpeed = camera.fovy / 100.0f;
				Vector3 forward = Vector3Subtract(camera.target, camera.position);
				Vector3 right = Vector3CrossProduct(forward, camera.up);
				right = Vector3Normalize(right);
				Vector3 up = Vector3CrossProduct(right, forward);
				up = Vector3Normalize(up);
				Vector3 rightMove = Vector3Scale(right, -mouseDelta.x * panSpeed * dt * 50.0f);
				Vector3 upMove = Vector3Scale(up, mouseDelta.y * panSpeed * dt * 50.0f); 
				camera.position = Vector3Add(camera.position, rightMove);
				camera.target = Vector3Add(camera.target, rightMove);
				camera.position = Vector3Add(camera.position, upMove);
				camera.target = Vector3Add(camera.target, upMove);
			}
		}
		camera.fovy = std::clamp(camera.fovy - GetMouseWheelMove() * 2.0f, 2.0f, 100.0f);
		 

		BeginDrawing();
		BeginMode3D(camera);
		ClearBackground(SKYBLUE);
		// тут рисовка
		DrawMap();

		EndMode3D();

		//GuiPanel({ 10.0f, 10.0f, ws.x*0.1f, ws.y*0.26f}, "TOOLS");
		//printf("%f  %f", ws.x, ws.y);
		GuiToggleGroup({ 10.0f, 10.0f, ws.x * 0.1f, ws.y * 0.03f }, "TILE;OBJ;SLCT", &tool_s);
		if (GuiButton({ 10.0f, 50.0f, ws.x * 0.05f, ws.y * 0.03f }, "Generate")) gen_l();
		if (GuiButton({ 10.0f, 90.0f, ws.x * 0.05f, ws.y * 0.03f }, "Load height map"));
		GuiPanel({ ws.x - ws.x * 0.1f, 10.0f, ws.x * 0.1f, ws.y * 0.3f }, "Textures");
		if (GuiButton({ ws.x - ws.x * 0.1f + 1.0f, 30.0f, ws.x * 0.1f - 1.0f, ws.y * 0.03f }, "Load texture"));
		GuiListViewEx({ ws.x - ws.x * 0.1f + 1.0f, 70.0f, ws.x * 0.1f - 1.0f, ws.y * 0.26f }, texs_for_list.data(), texs_for_list.size(), &sc_idx, &act_idx, &foc_idx);

		EndDrawing();
	}
	CloseWindow();
	return 0;
}
