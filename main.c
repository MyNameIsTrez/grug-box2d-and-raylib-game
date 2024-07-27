#include "box2d/box2d.h"
#include "grug.h"
#include "raylib.h"
#include "raymath.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct Conversion
{
	float scale;
	float tileSize;
	float screenWidth;
	float screenHeight;
} Conversion;

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

static Vector2 ConvertWorldToScreen(b2Vec2 p, Conversion cv)
{
	Vector2 result = { cv.scale * p.x + 0.5f * cv.screenWidth, 0.5f * cv.screenHeight - cv.scale * p.y };
	return result;
}

static void DrawEntity(const Entity* entity, Conversion cv)
{
	b2Vec2 p = b2Body_GetWorldPoint(entity->bodyId, (b2Vec2) { -0.5f * cv.tileSize, 0.5f * cv.tileSize });
	float radians = b2Body_GetAngle(entity->bodyId);

	Vector2 ps = ConvertWorldToScreen(p, cv);

	float textureScale = cv.tileSize * cv.scale / (float)entity->texture.width;

	// Have to negate rotation to account for y-flip
	DrawTextureEx(entity->texture, ps, -RAD2DEG * radians, textureScale, WHITE);

	// I used these circles to ensure the coordinate transformation was correct
	// DrawCircleV(ps, 5.0f, BLACK);
	// p = b2Body_GetWorldPoint(entity->bodyId, (b2Vec2){0.0f, 0.0f});
	// ps = ConvertWorldToScreen(p, cv);
	// DrawCircleV(ps, 5.0f, BLUE);
	// p = b2Body_GetWorldPoint(entity->bodyId, (b2Vec2){0.5f * cv.tileSize, -0.5f * cv.tileSize});
	// ps = ConvertWorldToScreen(p, cv);
	// DrawCircleV(ps, 5.0f, RED);
}

static void reload_grug_entities(void) {
	for (size_t reload_index = 0; reload_index < grug_reloads_size; reload_index++) {
		struct grug_modified reload = grug_reloads[reload_index];

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
	int width = 1280, height = 720;
	InitWindow(width, height, "box2d-raylib");

	SetTargetFPS(60);

	float tileSize = 1.0f;
	float scale = 50.0f;

	Conversion cv = { scale, tileSize, (float)width, (float)height };

	b2WorldDef worldDef = b2DefaultWorldDef();
	b2WorldId worldId = b2CreateWorld(&worldDef);

	Texture textures[2] = { 0 };
	textures[0] = LoadTexture("ground.png");
	textures[1] = LoadTexture("box.png");

	b2Polygon tilePolygon = b2MakeSquare(0.5f * tileSize);

	Entity groundEntities[20] = { 0 };
	for (int i = 0; i < 20; ++i)
	{
		Entity* entity = groundEntities + i;
		b2BodyDef bodyDef = b2DefaultBodyDef();
		bodyDef.position = (b2Vec2){ (1.0f * i - 10.0f) * tileSize, -4.5f - 0.5f * tileSize };

		// I used this rotation to test the world to screen transformation
		//bodyDef.angle = 0.25f * b2_pi * i;

		entity->bodyId = b2CreateBody(worldId, &bodyDef);
		entity->texture = textures[0];
		b2ShapeDef shapeDef = b2DefaultShapeDef();
		b2CreatePolygonShape(entity->bodyId, &shapeDef, &tilePolygon);
	}

	Entity boxEntities[10] = { 10 };
	for (int i = 0; i < 10; ++i)
	{
		Entity* entity = boxEntities + i;
		b2BodyDef bodyDef = b2DefaultBodyDef();
		bodyDef.type = b2_dynamicBody;
		bodyDef.position = (b2Vec2){ 0.5f * tileSize * i, -4.0f + tileSize * i };
		entity->bodyId = b2CreateBody(worldId, &bodyDef);
		entity->texture = textures[1];
		b2ShapeDef shapeDef = b2DefaultShapeDef();
		shapeDef.restitution = 0.1f;
		b2CreatePolygonShape(entity->bodyId, &shapeDef, &tilePolygon);
	}

	bool pause = false;

	while (!WindowShouldClose())
	{
		if (grug_regenerate_modified_mods()) {
			fprintf(stderr, "%s in %s:%d\n", grug_error.msg, grug_error.filename, grug_error.line_number);
			exit(EXIT_FAILURE);
		}

		reload_grug_entities();

		if (IsKeyPressed(KEY_P))
		{
			pause = !pause;
		}

		if (pause == false)
		{
			float deltaTime = GetFrameTime();
			b2World_Step(worldId, deltaTime, 4);
		}

		BeginDrawing();
		ClearBackground(DARKGRAY);

		const char* message = "Hello Box2D!";
		int fontSize = 36;
		int textWidth = MeasureText("Hello Box2D!", fontSize);
		DrawText(message, (width - textWidth) / 2, 50, fontSize, LIGHTGRAY);

		for (int i = 0; i < 20; ++i)
		{
			DrawEntity(groundEntities + i, cv);
		}

		for (int i = 0; i < 10; ++i)
		{
			DrawEntity(boxEntities + i, cv);
		}

		EndDrawing();
	}

	UnloadTexture(textures[0]);
	UnloadTexture(textures[1]);

	CloseWindow();

	return 0;
}
