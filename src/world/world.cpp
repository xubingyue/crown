/*
 * Copyright (c) 2012-2016 Daniele Bartolini and individual contributors.
 * License: https://github.com/taylor001/crown/blob/master/LICENSE
 */

#include "debug_line.h"
#include "error.h"
#include "hash.h"
#include "level.h"
#include "lua_environment.h"
#include "matrix4x4.h"
#include "physics_world.h"
#include "render_world.h"
#include "resource_manager.h"
#include "scene_graph.h"
#include "sound_world.h"
#include "temp_allocator.h"
#include "unit_manager.h"
#include "unit_resource.h"
#include "vector3.h"
#include "vector4.h"
#include "world.h"

namespace crown
{
World::World(Allocator& a, ResourceManager& rm, ShaderManager& sm, MaterialManager& mm, UnitManager& um, LuaEnvironment& env)
	: _marker(MARKER)
	, _allocator(&a)
	, _resource_manager(&rm)
	, _lua_environment(&env)
	, _unit_manager(&um)
	, _lines(NULL)
	, _scene_graph(NULL)
	, _render_world(NULL)
	, _physics_world(NULL)
	, _sound_world(NULL)
	, _units(a)
	, _levels(a)
	, _camera(a)
	, _camera_map(a)
	, _events(a)
{
	_lines = create_debug_line(true);
	_scene_graph = CE_NEW(*_allocator, SceneGraph)(*_allocator);
	_render_world = CE_NEW(*_allocator, RenderWorld)(*_allocator, rm, sm, mm, um);
	_physics_world = PhysicsWorld::create(*_allocator, rm, um, *_lines);
	_sound_world = SoundWorld::create(*_allocator);
}

World::~World()
{
	destroy_debug_line(*_lines);
	SoundWorld::destroy(*_allocator, _sound_world);
	PhysicsWorld::destroy(*_allocator, _physics_world);
	CE_DELETE(*_allocator, _render_world);
	CE_DELETE(*_allocator, _scene_graph);

	for (u32 i = 0; i < array::size(_levels); ++i)
	{
		CE_DELETE(*_allocator, _levels[i]);
	}

	_marker = 0;
}

UnitId World::spawn_unit(const UnitResource& ur, const Vector3& pos, const Quaternion& rot)
{
	TempAllocator512 ta;
	UnitId* unit_lookup = (UnitId*)ta.allocate(sizeof(UnitId) * ur.num_units);

	for (u32 i = 0; i < ur.num_units; ++i)
		unit_lookup[i] = _unit_manager->create();

	// First component data
	const char* component_data = (const char*)(&ur + 1);

	for (u32 cc = 0; cc < ur.num_component_types; ++cc)
	{
		const ComponentData* component = (const ComponentData*)component_data;
		const u32* unit_index = (const u32*)(component + 1);
		const char* data = (const char*)(unit_index + component->num_instances);

		if (component->type == StringId32("transform")._id)
		{
			const TransformDesc* td = (const TransformDesc*)data;
			for (u32 i = 0; i < component->num_instances; ++i)
			{
				Matrix4x4 matrix = matrix4x4(rot, pos);
				Matrix4x4 matrix_res = matrix4x4(td->rotation, td->position);
				_scene_graph->create(unit_lookup[unit_index[i]], matrix_res*matrix);
				++td;
			}
		}

		if (component->type == StringId32("camera")._id)
		{
			const CameraDesc* cd = (const CameraDesc*)data;
			for (u32 i = 0; i < component->num_instances; ++i)
			{
				create_camera(unit_lookup[unit_index[i]], *cd);
				++cd;
			}
		}

		if (component->type == StringId32("collider")._id)
		{
			const ShapeDesc* sd = (const ShapeDesc*)data;
			for (u32 i = 0; i < component->num_instances; ++i)
			{
				physics_world()->create_collider(unit_lookup[unit_index[i]], sd);
				++sd;
			}
		}

		if (component->type == StringId32("actor")._id)
		{
			const ActorResource* ar = (const ActorResource*)data;
			for (u32 i = 0; i < component->num_instances; ++i)
			{
				Matrix4x4 tm = _scene_graph->world_pose(_scene_graph->get(unit_lookup[unit_index[i]]));
				physics_world()->create_actor(unit_lookup[unit_index[i]], ar, tm);
				++ar;
			}
		}

		if (component->type == StringId32("controller")._id)
		{
			const ControllerDesc* cd = (const ControllerDesc*)data;
			for (u32 i = 0; i < component->num_instances; ++i)
			{
				Matrix4x4 tm = _scene_graph->world_pose(_scene_graph->get(unit_lookup[unit_index[i]]));
				physics_world()->create_controller(unit_lookup[unit_index[i]], *cd, tm);
				++cd;
			}
		}

		if (component->type == StringId32("mesh_renderer")._id)
		{
			const MeshRendererDesc* mrd = (const MeshRendererDesc*)data;
			for (u32 i = 0; i < component->num_instances; ++i)
			{
				Matrix4x4 tm = _scene_graph->world_pose(_scene_graph->get(unit_lookup[unit_index[i]]));
				render_world()->create_mesh(unit_lookup[unit_index[i]], *mrd, tm);
				++mrd;
			}
		}

		if (component->type == StringId32("sprite_renderer")._id)
		{
			const SpriteRendererDesc* srd = (const SpriteRendererDesc*)data;
			for (u32 i = 0; i < component->num_instances; ++i)
			{
				Matrix4x4 tm = _scene_graph->world_pose(_scene_graph->get(unit_lookup[unit_index[i]]));
				render_world()->create_sprite(unit_lookup[unit_index[i]], *srd, tm);
				++srd;
			}
		}

		if (component->type == StringId32("light")._id)
		{
			const LightDesc* ld = (const LightDesc*)data;
			for (u32 i = 0; i < component->num_instances; ++i)
			{
				Matrix4x4 tm = _scene_graph->world_pose(_scene_graph->get(unit_lookup[unit_index[i]]));
				render_world()->create_light(unit_lookup[unit_index[i]], *ld, tm);
				++ld;
			}
		}

		component_data += component->size + sizeof(ComponentData);
	}

	array::push(_units, &unit_lookup[0], ur.num_units);

	// Post events
	for (u32 i = 0; i < ur.num_units; ++i)
		post_unit_spawned_event(unit_lookup[i]);

	return unit_lookup[0];
}

UnitId World::spawn_unit(StringId64 name, const Vector3& pos, const Quaternion& rot)
{
	const UnitResource* ur = (const UnitResource*)_resource_manager->get(RESOURCE_TYPE_UNIT, name);
	return spawn_unit(*ur, pos, rot);
}

void World::spawn_empty_unit(UnitId id)
{
	array::push_back(_units, id);
	post_unit_spawned_event(id);
}

void World::destroy_unit(UnitId id)
{
	_unit_manager->destroy(id);
	post_unit_destroyed_event(id);
}

u32 World::num_units() const
{
	return array::size(_units);
}

void World::units(Array<UnitId>& units) const
{
	array::reserve(units, array::size(_units));
	array::push(units, array::begin(_units), array::size(_units));
}

void World::update_animations(f32 /*dt*/)
{
}

void World::update_scene(f32 dt)
{
	TempAllocator4096 ta;
	Array<UnitId> changed_units(ta);
	Array<Matrix4x4> changed_world(ta);

	_scene_graph->get_changed(changed_units, changed_world);

	_physics_world->update_actor_world_poses(array::begin(changed_units)
		, array::end(changed_units)
		, array::begin(changed_world)
		);

	_physics_world->update(dt);

	// Process physics events
	EventStream& physics_events = _physics_world->events();

	const u32 size = array::size(physics_events);
	u32 read = 0;
	while (read < size)
	{
		event_stream::Header h;

		const char* data = &physics_events[read];
		const char* ev   = data + sizeof(h);

		h = *(event_stream::Header*)data;

		switch (h.type)
		{
			case EventType::PHYSICS_TRANSFORM:
			{
				const PhysicsTransformEvent& ptev = *(PhysicsTransformEvent*)ev;
				const TransformInstance ti = _scene_graph->get(ptev.unit_id);
				_scene_graph->set_world_pose(ti, ptev.world_tm);
				break;
			}
			case EventType::PHYSICS_COLLISION:
			{
				break;
			}
			case EventType::PHYSICS_TRIGGER:
			{
				break;
			}
		}

		read += sizeof(h);
		read += h.size;
	}
	array::clear(physics_events);

	array::clear(changed_units);
	array::clear(changed_world);

	_scene_graph->get_changed(changed_units, changed_world);
	_scene_graph->clear_changed();

	_render_world->update_transforms(array::begin(changed_units)
		, array::end(changed_units)
		, array::begin(changed_world)
		);

	_sound_world->update();

	array::clear(_events);
}

void World::update(f32 dt)
{
	update_animations(dt);
	update_scene(dt);
}

void World::render(CameraInstance i)
{
	const Camera& camera = _camera[i.i];

	_render_world->render(camera_view_matrix(i)
		, camera.projection
		, camera.view_x
		, camera.view_y
		, camera.view_width
		, camera.view_height
		);

	_physics_world->draw_debug();
	_render_world->draw_debug(*_lines);

	_lines->submit();
	_lines->reset();
}

CameraInstance World::create_camera(UnitId id, const CameraDesc& cd)
{
	Camera camera;
	camera.unit            = id;
	camera.projection_type = (ProjectionType::Enum)cd.type;
	camera.fov             = cd.fov;
	camera.near            = cd.near_range;
	camera.far             = cd.far_range;

	const u32 last = array::size(_camera);
	array::push_back(_camera, camera);

	hash::set(_camera_map, id.encode(), last);
	return make_camera_instance(last);
}

void World::destroy_camera(CameraInstance i)
{
	const u32 last = array::size(_camera) - 1;
	const UnitId u = _camera[i.i].unit;
	const UnitId last_u = _camera[last].unit;

	_camera[i.i] = _camera[last];

	hash::set(_camera_map, last_u.encode(), i.i);
	hash::remove(_camera_map, u.encode());
}

CameraInstance World::camera(UnitId id)
{
	return make_camera_instance(hash::get(_camera_map, id.encode(), UINT32_MAX));
}

void World::set_camera_projection_type(CameraInstance i, ProjectionType::Enum type)
{
	_camera[i.i].projection_type = type;
	_camera[i.i].update_projection_matrix();
}

ProjectionType::Enum World::camera_projection_type(CameraInstance i) const
{
	return _camera[i.i].projection_type;
}

const Matrix4x4& World::camera_projection_matrix(CameraInstance i) const
{
	return _camera[i.i].projection;
}

Matrix4x4 World::camera_view_matrix(CameraInstance i) const
{
	const TransformInstance ti = _scene_graph->get(_camera[i.i].unit);
	Matrix4x4 view = _scene_graph->world_pose(ti);
	invert(view);
	return view;
}

f32 World::camera_fov(CameraInstance i) const
{
	return _camera[i.i].fov;
}

void World::set_camera_fov(CameraInstance i, f32 fov)
{
	_camera[i.i].fov = fov;
	_camera[i.i].update_projection_matrix();
}

f32 World::camera_aspect(CameraInstance i) const
{
	return _camera[i.i].aspect;
}

void World::set_camera_aspect(CameraInstance i, f32 aspect)
{
	_camera[i.i].aspect = aspect;
	_camera[i.i].update_projection_matrix();
}

f32 World::camera_near_clip_distance(CameraInstance i) const
{
	return _camera[i.i].near;
}

void World::set_camera_near_clip_distance(CameraInstance i, f32 near)
{
	_camera[i.i].near = near;
	_camera[i.i].update_projection_matrix();
}

f32 World::camera_far_clip_distance(CameraInstance i) const
{
	return _camera[i.i].far;
}

void World::set_camera_far_clip_distance(CameraInstance i, f32 far)
{
	_camera[i.i].far = far;
	_camera[i.i].update_projection_matrix();
}

void World::set_camera_orthographic_metrics(CameraInstance i, f32 left, f32 right, f32 bottom, f32 top)
{
	_camera[i.i].left = left;
	_camera[i.i].right = right;
	_camera[i.i].bottom = bottom;
	_camera[i.i].top = top;

	_camera[i.i].update_projection_matrix();
}

void World::set_camera_viewport_metrics(CameraInstance i, u16 x, u16 y, u16 width, u16 height)
{
	_camera[i.i].view_x = x;
	_camera[i.i].view_y = y;
	_camera[i.i].view_width = width;
	_camera[i.i].view_height = height;
}

Vector3 World::camera_screen_to_world(CameraInstance i, const Vector3& pos)
{
	const Camera& c = _camera[i.i];

	const TransformInstance ti = _scene_graph->get(_camera[i.i].unit);
	Matrix4x4 world_inv = _scene_graph->world_pose(ti);
	invert(world_inv);
	Matrix4x4 mvp = world_inv * c.projection;
	invert(mvp);

	Vector4 ndc;
	ndc.x = (2.0f * (pos.x - 0.0f)) / c.view_width - 1.0f;
	ndc.y = (2.0f * (c.view_height - pos.y)) / c.view_height - 1.0f;
	ndc.z = (2.0f * pos.z) - 1.0f;
	ndc.w = 1.0f;

	Vector4 tmp = ndc * mvp;
	tmp *= 1.0f / tmp.w;

	return vector3(tmp.x, tmp.y, tmp.z);
}

Vector3 World::camera_world_to_screen(CameraInstance i, const Vector3& pos)
{
	const Camera& c = _camera[i.i];

	const TransformInstance ti = _scene_graph->get(_camera[i.i].unit);
	Matrix4x4 world_inv = _scene_graph->world_pose(ti);
	invert(world_inv);

	Vector4 xyzw;
	xyzw.x = pos.x;
	xyzw.y = pos.y;
	xyzw.z = pos.z;
	xyzw.w = 1.0f;

	Vector4 clip = xyzw * (world_inv * c.projection);

	Vector4 ndc;
	ndc.x = clip.x / clip.w;
	ndc.y = clip.y / clip.w;

	Vector3 screen;
	screen.x = (c.view_x + c.view_width  * (ndc.x + 1.0f)) / 2.0f;
	screen.y = (c.view_y + c.view_height * (1.0f - ndc.y)) / 2.0f;
	screen.z = 0.0f;

	return screen;
}

SoundInstanceId World::play_sound(const SoundResource& sr, const bool loop, const f32 volume, const Vector3& pos, const f32 range)
{
	return _sound_world->play(sr, loop, volume, pos);
}

SoundInstanceId World::play_sound(StringId64 name, const bool loop, const f32 volume, const Vector3& pos, const f32 range)
{
	const SoundResource* sr = (const SoundResource*)_resource_manager->get(RESOURCE_TYPE_SOUND, name);
	return play_sound(*sr, loop, volume, pos, range);
}

void World::stop_sound(SoundInstanceId id)
{
	_sound_world->stop(id);
}

void World::link_sound(SoundInstanceId /*id*/, UnitId /*unit*/, s32 /*node*/)
{
	CE_ASSERT(false, "Not implemented yet");
}

void World::set_listener_pose(const Matrix4x4& pose)
{
	_sound_world->set_listener_pose(pose);
}

void World::set_sound_position(SoundInstanceId id, const Vector3& pos)
{
	_sound_world->set_sound_positions(1, &id, &pos);
}

void World::set_sound_range(SoundInstanceId id, f32 range)
{
	_sound_world->set_sound_ranges(1, &id, &range);
}

void World::set_sound_volume(SoundInstanceId id, f32 vol)
{
	_sound_world->set_sound_volumes(1, &id, &vol);
}

DebugLine* World::create_debug_line(bool depth_test)
{
	return CE_NEW(*_allocator, DebugLine)(depth_test);
}

void World::destroy_debug_line(DebugLine& line)
{
	CE_DELETE(*_allocator, &line);
}

Level* World::load_level(const LevelResource& lr, const Vector3& pos, const Quaternion& rot)
{
	Level* level = CE_NEW(*_allocator, Level)(*_allocator, *this, lr);
	level->load(pos, rot);

	array::push_back(_levels, level);
	post_level_loaded_event();
	return level;
}

Level* World::load_level(StringId64 name, const Vector3& pos, const Quaternion& rot)
{
	const LevelResource* lr = (LevelResource*)_resource_manager->get(RESOURCE_TYPE_LEVEL, name);
	return load_level(*lr, pos, rot);
}

EventStream& World::events()
{
	return _events;
}

SceneGraph* World::scene_graph()
{
	return _scene_graph;
}

RenderWorld* World::render_world()
{
	return _render_world;
}

PhysicsWorld* World::physics_world()
{
	return _physics_world;
}

SoundWorld* World::sound_world()
{
	return _sound_world;
}

void World::post_unit_spawned_event(UnitId id)
{
	UnitSpawnedEvent ev;
	ev.unit = id;
	event_stream::write(_events, EventType::UNIT_SPAWNED, ev);
}

void World::post_unit_destroyed_event(UnitId id)
{
	UnitDestroyedEvent ev;
	ev.unit = id;
	event_stream::write(_events, EventType::UNIT_DESTROYED, ev);
}

void World::post_level_loaded_event()
{
	LevelLoadedEvent ev;
	event_stream::write(_events, EventType::LEVEL_LOADED, ev);
}

void World::Camera::update_projection_matrix()
{
	switch (projection_type)
	{
		case ProjectionType::ORTHOGRAPHIC:
		{
			orthographic(projection
				, left
				, right
				, bottom
				, top
				, near
				, far
				);
			break;
		}
		case ProjectionType::PERSPECTIVE:
		{
			perspective(projection
				, fov
				, aspect
				, near
				, far
				);
			break;
		}
		default:
		{
			CE_FATAL("Oops, unknown projection type");
			break;
		}
	}
}

} // namespace crown
