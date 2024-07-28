#pragma GCC diagnostic push 
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#include "box2d/box2d.h"
#pragma GCC diagnostic pop

#include "grug.h"
#include "raylib.h"
#include "raymath.h"

#include <stdio.h>
#include <stdlib.h>

static int width = 1280;
static int height = 720;
static float scale = 5.0f;

typedef struct Entity
{
	b2BodyId bodyId;
	Texture texture;
} Entity;

struct gun {
	char *name;
};

static struct gun gun_definition;

void define_gun(char *name) {
	gun_definition = (struct gun){
		.name = name,
	};
}

static void DrawEntity(const Entity* entity)
{
	b2Vec2 pos = b2Body_GetWorldPoint(entity->bodyId, (b2Vec2) { 0.0f, -33.0f });
	float radians = b2Body_GetAngle(entity->bodyId);

	Rectangle rect = {pos.x, pos.y, entity->texture.width * scale, entity->texture.height * scale};
	Vector2 origin = {0, 0};
	Color color = {.r=42, .g=42, .b=242, .a=100};
	DrawRectanglePro(rect, origin, -radians * RAD2DEG, color);

	Vector2 posRaylib = {.x=pos.x, .y=pos.y};
	DrawTextureEx(entity->texture, posRaylib, -radians * RAD2DEG, scale, WHITE);
}

static void reload_grug_entities(void) {
	for (size_t reload_index = 0; reload_index < grug_reloads_size; reload_index++) {
		// struct grug_modified reload = grug_reloads[reload_index];

		// for (size_t i = 0; i < 2; i++) {
		// 	if (reload.old_dll == data.human_dlls[i]) {
		// 		data.human_dlls[i] = reload.new_dll;

		// 		free(data.human_globals[i]);
		// 		data.human_globals[i] = malloc(reload.globals_size);
		// 		reload.init_globals_fn(data.human_globals[i]);
		// 	}
		// }
		// for (size_t i = 0; i < 2; i++) {
		// 	if (reload.old_dll == data.tool_dlls[i]) {
		// 		data.tool_dlls[i] = reload.new_dll;

		// 		free(data.tool_globals[i]);
		// 		data.tool_globals[i] = malloc(reload.globals_size);
		// 		reload.init_globals_fn(data.tool_globals[i]);

		// 		data.tools[i].on_fns = reload.on_fns;
		// 	}
		// }
	}
}

int main(void)
{
	InitWindow(width, height, "box2d-raylib");

	// SetTargetFPS(60);
	SetConfigFlags(FLAG_VSYNC_HINT);

	b2WorldDef worldDef = b2DefaultWorldDef();
	b2WorldId worldId = b2CreateWorld(&worldDef);

	// Texture texture = LoadTexture("mods/vanilla/kar98k/kar98k.png");
	// Texture texture = LoadTexture("mods/vanilla/m16a2/m16a2.png");
	Texture texture = LoadTexture("mods/vanilla/thumper/thumper.png");

	b2BodyDef bodyDef = b2DefaultBodyDef();
	bodyDef.type = b2_staticBody;
	bodyDef.position = (b2Vec2){ width / 2, height / 2 };
	// bodyDef.fixedRotation = true; // TODO: Maybe use?
	Entity entity;
	entity.bodyId = b2CreateBody(worldId, &bodyDef);
	entity.texture = texture;
	b2ShapeDef shapeDef = b2DefaultShapeDef();
	b2Polygon squarePolygon = b2MakeBox(0.5f, 0.5f); // TODO: width is normally != height
	b2CreatePolygonShape(entity.bodyId, &shapeDef, &squarePolygon);

	while (!WindowShouldClose())
	{
		if (grug_regenerate_modified_mods()) {
			fprintf(stderr, "%s in %s:%d\n", grug_error.msg, grug_error.filename, grug_error.line_number);
			exit(EXIT_FAILURE);
		}

		reload_grug_entities();

		float deltaTime = GetFrameTime();
		b2World_Step(worldId, deltaTime, 4);

		// TODO: Fire bullet
		// if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))

		b2Vec2 gunPos = b2Body_GetPosition(entity.bodyId);
		Vector2 mousePos = GetMousePosition();
		Color red = {.r=242, .g=42, .b=42, .a=255};
		DrawLine(gunPos.x, gunPos.y, mousePos.x, mousePos.y, red);

		Vector2 gunPosRaylib = {.x=gunPos.x, .y=gunPos.y};
		Vector2 gunToMouse = Vector2Subtract(mousePos, gunPosRaylib);
		float angle = atan2(-gunToMouse.y, gunToMouse.x);
		b2Body_SetTransform(entity.bodyId, gunPos, angle);

		BeginDrawing();
		ClearBackground(SKYBLUE);

		DrawFPS(0, 0);

		DrawEntity(&entity);

		EndDrawing();
	}

	UnloadTexture(texture);

	CloseWindow();

	return 0;
}
