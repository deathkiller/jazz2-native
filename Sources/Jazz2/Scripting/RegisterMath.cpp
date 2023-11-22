#if defined(WITH_ANGELSCRIPT)

#include "RegisterMath.h"
#include "../../Common.h"
#include "../../nCine/Primitives/Colorf.h"
#include "../../nCine/Primitives/Vector2.h"
#include "../../nCine/Primitives/Vector3.h"
#include "../../nCine/Primitives/Vector4.h"

#include <cstring>
#include <new>

#include <Containers/String.h>

using namespace Death::Containers;
using namespace nCine;

namespace Jazz2::Scripting
{
	void RegisterMembers_Vector2i(asIScriptEngine* engine, const char* className)
	{
		engine->RegisterObjectMethod(className, "int get_length() const", asMETHODPR(Vector2i, Length, () const, int), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec2i opMul(int) const", asMETHODPR(Vector2i, operator*, (int) const, Vector2i), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec2i opMul(const vec2i&in) const", asMETHODPR(Vector2i, operator*, (const Vector2i&) const, Vector2i), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec2i& opMulAssign(int)", asMETHODPR(Vector2i, operator*=, (int), Vector2i&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec2i& opMulAssign(const vec2i&in)", asMETHODPR(Vector2i, operator*=, (const Vector2i&), Vector2i&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec2i opAdd(const vec2i&in) const", asMETHODPR(Vector2i, operator+, (const Vector2i&) const, Vector2i), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec2i& opAddAssign(const vec2i&in)", asMETHODPR(Vector2i, operator+=, (const Vector2i&), Vector2i&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec2i opNeg() const", asMETHODPR(Vector2i, operator-, () const, Vector2i), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec2i opSub(const vec2i&in) const", asMETHODPR(Vector2i, operator-, (const Vector2i&) const, Vector2i), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec2i& opSubAssign(const vec2i&in)", asMETHODPR(Vector2i, operator-=, (const Vector2i&), Vector2i&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec2i opDiv(int) const", asMETHODPR(Vector2i, operator/, (int) const, Vector2i), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec2i opDiv(const vec2i&in) const", asMETHODPR(Vector2i, operator/, (const Vector2i&) const, Vector2i), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec2i& opDivAssign(int)", asMETHODPR(Vector2i, operator/=, (int), Vector2i&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec2i& opDivAssign(const vec2i&in)", asMETHODPR(Vector2i, operator/=, (const Vector2i&), Vector2i&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec2i& opAssign(const vec2i&in)", asMETHODPR(Vector2i, operator=, (const Vector2i&), Vector2i&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "bool opEquals(const vec2i&in) const", asMETHODPR(Vector2i, operator==, (const Vector2i&) const, bool), asCALL_THISCALL);

		engine->RegisterObjectProperty(className, "int x", offsetof(Vector2i, X));
		engine->RegisterObjectProperty(className, "int y", offsetof(Vector2i, Y));
	}

	void RegisterMembers_Vector3i(asIScriptEngine* engine, const char* className)
	{
		engine->RegisterObjectMethod(className, "int get_length() const", asMETHODPR(Vector3i, Length, () const, int), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec3i opMul(int) const", asMETHODPR(Vector3i, operator*, (int) const, Vector3i), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec3i opMul(const vec3i&in) const", asMETHODPR(Vector3i, operator*, (const Vector3i&) const, Vector3i), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec3i& opMulAssign(int)", asMETHODPR(Vector3i, operator*=, (int), Vector3i&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec3i& opMulAssign(const vec3i&in)", asMETHODPR(Vector3i, operator*=, (const Vector3i&), Vector3i&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec3i opAdd(const vec3i&in) const", asMETHODPR(Vector3i, operator+, (const Vector3i&) const, Vector3i), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec3i& opAddAssign(const vec3i&in)", asMETHODPR(Vector3i, operator+=, (const Vector3i&), Vector3i&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec3i opNeg() const", asMETHODPR(Vector3i, operator-, () const, Vector3i), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec3i opSub(const vec3i&in) const", asMETHODPR(Vector3i, operator-, (const Vector3i&) const, Vector3i), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec3i& opSubAssign(const vec3i&in)", asMETHODPR(Vector3i, operator-=, (const Vector3i&), Vector3i&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec3i opDiv(int) const", asMETHODPR(Vector3i, operator/, (int) const, Vector3i), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec3i opDiv(const vec3i&in) const", asMETHODPR(Vector3i, operator/, (const Vector3i&) const, Vector3i), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec3i& opDivAssign(int)", asMETHODPR(Vector3i, operator/=, (int), Vector3i&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec3i& opDivAssign(const vec3i&in)", asMETHODPR(Vector3i, operator/=, (const Vector3i&), Vector3i&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec3i& opAssign(const vec3i&in)", asMETHODPR(Vector3i, operator=, (const Vector3i&), Vector3i&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "bool opEquals(const vec3i&in) const", asMETHODPR(Vector3i, operator==, (const Vector3i&) const, bool), asCALL_THISCALL);

		engine->RegisterObjectProperty(className, "int x", offsetof(Vector3i, X));
		engine->RegisterObjectProperty(className, "int y", offsetof(Vector3i, Y));
		engine->RegisterObjectProperty(className, "int z", offsetof(Vector3i, Z));
	}

	void RegisterMembers_Vector4i(asIScriptEngine* engine, const char* className)
	{
		engine->RegisterObjectMethod(className, "int get_length() const", asMETHODPR(Vector4i, Length, () const, int), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec4i opMul(int) const", asMETHODPR(Vector4i, operator*, (int) const, Vector4i), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec4i opMul(const vec4i&in) const", asMETHODPR(Vector4i, operator*, (const Vector4i&) const, Vector4i), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec4i& opMulAssign(int)", asMETHODPR(Vector4i, operator*=, (int), Vector4i&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec4i& opMulAssign(const vec4i&in)", asMETHODPR(Vector4i, operator*=, (const Vector4i&), Vector4i&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec4i opAdd(const vec4i&in) const", asMETHODPR(Vector4i, operator+, (const Vector4i&) const, Vector4i), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec4i& opAddAssign(const vec4i&in)", asMETHODPR(Vector4i, operator+=, (const Vector4i&), Vector4i&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec4i opNeg() const", asMETHODPR(Vector4i, operator-, () const, Vector4i), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec4i opSub(const vec4i&in) const", asMETHODPR(Vector4i, operator-, (const Vector4i&) const, Vector4i), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec4i& opSubAssign(const vec4i&in)", asMETHODPR(Vector4i, operator-=, (const Vector4i&), Vector4i&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec4i opDiv(int) const", asMETHODPR(Vector4i, operator/, (int) const, Vector4i), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec4i opDiv(const vec4i&in) const", asMETHODPR(Vector4i, operator/, (const Vector4i&) const, Vector4i), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec4i& opDivAssign(int)", asMETHODPR(Vector4i, operator/=, (int), Vector4i&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec4i& opDivAssign(const vec4i&in)", asMETHODPR(Vector4i, operator/=, (const Vector4i&), Vector4i&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec4i& opAssign(const vec4i&in)", asMETHODPR(Vector4i, operator=, (const Vector4i&), Vector4i&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "bool opEquals(const vec4i&in) const", asMETHODPR(Vector4i, operator==, (const Vector4i&) const, bool), asCALL_THISCALL);

		engine->RegisterObjectProperty(className, "int x", offsetof(Vector4i, X));
		engine->RegisterObjectProperty(className, "int y", offsetof(Vector4i, Y));
		engine->RegisterObjectProperty(className, "int z", offsetof(Vector4i, Z));
		engine->RegisterObjectProperty(className, "int w", offsetof(Vector4i, W));
	}

	void RegisterMembers_Vector2f(asIScriptEngine* engine, const char* className)
	{
		engine->RegisterObjectMethod(className, "float get_length() const", asMETHODPR(Vector2f, Length, () const, float), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "float get_lengthSquared() const", asMETHODPR(Vector2f, SqrLength, () const, float), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "void normalize()", asMETHODPR(Vector2f, Normalize, (), Vector2f&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec2 normalized() const", asMETHODPR(Vector2f, Normalized, () const, Vector2f), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec2 opMul(float) const", asMETHODPR(Vector2f, operator*, (float) const, Vector2f), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec2 opMul(const vec2&in) const", asMETHODPR(Vector2f, operator*, (const Vector2f&) const, Vector2f), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec2& opMulAssign(float)", asMETHODPR(Vector2f, operator*=, (float), Vector2f&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec2& opMulAssign(const vec2&in)", asMETHODPR(Vector2f, operator*=, (const Vector2f&), Vector2f&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec2 opAdd(const vec2&in) const", asMETHODPR(Vector2f, operator+, (const Vector2f&) const, Vector2f), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec2& opAddAssign(const vec2&in)", asMETHODPR(Vector2f, operator+=, (const Vector2f&), Vector2f&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec2 opNeg() const", asMETHODPR(Vector2f, operator-, () const, Vector2f), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec2 opSub(const vec2&in) const", asMETHODPR(Vector2f, operator-, (const Vector2f&) const, Vector2f), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec2& opSubAssign(const vec2&in)", asMETHODPR(Vector2f, operator-=, (const Vector2f&), Vector2f&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec2 opDiv(float) const", asMETHODPR(Vector2f, operator/, (float) const, Vector2f), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec2 opDiv(const vec2&in) const", asMETHODPR(Vector2f, operator/, (const Vector2f&) const, Vector2f), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec2& opDivAssign(float)", asMETHODPR(Vector2f, operator/=, (float), Vector2f&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec2& opDivAssign(const vec2&in)", asMETHODPR(Vector2f, operator/=, (const Vector2f&), Vector2f&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec2& opAssign(const vec2&in)", asMETHODPR(Vector2f, operator=, (const Vector2f&), Vector2f&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "bool opEquals(const vec2&in) const", asMETHODPR(Vector2f, operator==, (const Vector2f&) const, bool), asCALL_THISCALL);

		engine->RegisterObjectProperty(className, "float x", offsetof(Vector2f, X));
		engine->RegisterObjectProperty(className, "float y", offsetof(Vector2f, Y));
	}

	void RegisterMembers_Vector3f(asIScriptEngine* engine, const char* className)
	{
		engine->RegisterObjectMethod(className, "float get_length() const", asMETHODPR(Vector3f, Length, () const, float), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "float get_lengthSquared() const", asMETHODPR(Vector3f, SqrLength, () const, float), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "void normalize()", asMETHODPR(Vector3f, Normalize, (), Vector3f&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec3 normalized() const", asMETHODPR(Vector3f, Normalized, () const, Vector3f), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec3 opMul(float) const", asMETHODPR(Vector3f, operator*, (float) const, Vector3f), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec3 opMul(const vec3&in) const", asMETHODPR(Vector3f, operator*, (const Vector3f&) const, Vector3f), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec3& opMulAssign(float)", asMETHODPR(Vector3f, operator*=, (float), Vector3f&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec3& opMulAssign(const vec3&in)", asMETHODPR(Vector3f, operator*=, (const Vector3f&), Vector3f&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec3 opAdd(const vec3&in) const", asMETHODPR(Vector3f, operator+, (const Vector3f&) const, Vector3f), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec3& opAddAssign(const vec3&in)", asMETHODPR(Vector3f, operator+=, (const Vector3f&), Vector3f&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec3 opNeg() const", asMETHODPR(Vector3f, operator-, () const, Vector3f), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec3 opSub(const vec3&in) const", asMETHODPR(Vector3f, operator-, (const Vector3f&) const, Vector3f), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec3& opSubAssign(const vec3&in)", asMETHODPR(Vector3f, operator-=, (const Vector3f&), Vector3f&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec3 opDiv(float) const", asMETHODPR(Vector3f, operator/, (float) const, Vector3f), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec3 opDiv(const vec3&in) const", asMETHODPR(Vector3f, operator/, (const Vector3f&) const, Vector3f), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec3& opDivAssign(float)", asMETHODPR(Vector3f, operator/=, (float), Vector3f&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec3& opDivAssign(const vec3&in)", asMETHODPR(Vector3f, operator/=, (const Vector3f&), Vector3f&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec3& opAssign(const vec3&in)", asMETHODPR(Vector3f, operator=, (const Vector3f&), Vector3f&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "bool opEquals(const vec3&in) const", asMETHODPR(Vector3f, operator==, (const Vector3f&) const, bool), asCALL_THISCALL);

		engine->RegisterObjectProperty(className, "float x", offsetof(Vector3f, X));
		engine->RegisterObjectProperty(className, "float y", offsetof(Vector3f, Y));
		engine->RegisterObjectProperty(className, "float z", offsetof(Vector3f, Z));
	}

	void RegisterMembers_Vector4f(asIScriptEngine* engine, const char* className)
	{
		engine->RegisterObjectMethod(className, "float get_length() const", asMETHODPR(Vector4f, Length, () const, float), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "float get_lengthSquared() const", asMETHODPR(Vector4f, SqrLength, () const, float), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "void normalize()", asMETHODPR(Vector4f, Normalize, (), Vector4f&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec4 normalized() const", asMETHODPR(Vector4f, Normalized, () const, Vector4f), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec4 opMul(float) const", asMETHODPR(Vector4f, operator*, (float) const, Vector4f), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec4 opMul(const vec4&in) const", asMETHODPR(Vector4f, operator*, (const Vector4f&) const, Vector4f), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec4& opMulAssign(float)", asMETHODPR(Vector4f, operator*=, (float), Vector4f&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec4& opMulAssign(const vec4&in)", asMETHODPR(Vector4f, operator*=, (const Vector4f&), Vector4f&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec4 opAdd(const vec4&in) const", asMETHODPR(Vector4f, operator+, (const Vector4f&) const, Vector4f), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec4& opAddAssign(const vec4&in)", asMETHODPR(Vector4f, operator+=, (const Vector4f&), Vector4f&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec4 opNeg() const", asMETHODPR(Vector4f, operator-, () const, Vector4f), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec4 opSub(const vec4&in) const", asMETHODPR(Vector4f, operator-, (const Vector4f&) const, Vector4f), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec4& opSubAssign(const vec4&in)", asMETHODPR(Vector4f, operator-=, (const Vector4f&), Vector4f&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec4 opDiv(float) const", asMETHODPR(Vector4f, operator/, (float) const, Vector4f), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec4 opDiv(const vec4&in) const", asMETHODPR(Vector4f, operator/, (const Vector4f&) const, Vector4f), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec4& opDivAssign(float)", asMETHODPR(Vector4f, operator/=, (float), Vector4f&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec4& opDivAssign(const vec4&in)", asMETHODPR(Vector4f, operator/=, (const Vector4f&), Vector4f&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "vec4& opAssign(const vec4&in)", asMETHODPR(Vector4f, operator=, (const Vector4f&), Vector4f&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "bool opEquals(const vec4&in) const", asMETHODPR(Vector4f, operator==, (const Vector4f&) const, bool), asCALL_THISCALL);

		engine->RegisterObjectProperty(className, "float x", offsetof(Vector4f, X));
		engine->RegisterObjectProperty(className, "float y", offsetof(Vector4f, Y));
		engine->RegisterObjectProperty(className, "float z", offsetof(Vector4f, Z));
		engine->RegisterObjectProperty(className, "float w", offsetof(Vector4f, W));
	}

	void RegisterMembers_Color(asIScriptEngine* engine, const char* className)
	{
		engine->RegisterObjectMethod(className, "Color opMul(float) const", asMETHODPR(Colorf, operator*, (float) const, Colorf), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "Color opMul(const Color&in) const", asMETHODPR(Colorf, operator*, (const Colorf&) const, Colorf), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "Color& opMulAssign(float)", asMETHODPR(Colorf, operator*=, (float), Colorf&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "Color& opMulAssign(const Color&in)", asMETHODPR(Colorf, operator*=, (const Colorf&), Colorf&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "Color opAdd(const Color&in) const", asMETHODPR(Colorf, operator+, (const Colorf&) const, Colorf), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "Color& opAddAssign(const Color&in)", asMETHODPR(Colorf, operator+=, (const Colorf&), Colorf&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "Color opSub(const Color&in) const", asMETHODPR(Colorf, operator-, (const Colorf&) const, Colorf), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "Color& opSubAssign(const Color&in)", asMETHODPR(Colorf, operator-=, (const Colorf&), Colorf&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "Color& opAssign(const Color&in)", asMETHODPR(Colorf, operator=, (const Colorf&), Colorf&), asCALL_THISCALL);
		engine->RegisterObjectMethod(className, "bool opEquals(const Color&in) const", asMETHODPR(Colorf, operator==, (const Colorf&) const, bool), asCALL_THISCALL);

		engine->RegisterObjectProperty(className, "float r", offsetof(Colorf, R));
		engine->RegisterObjectProperty(className, "float g", offsetof(Colorf, G));
		engine->RegisterObjectProperty(className, "float b", offsetof(Colorf, B));
		engine->RegisterObjectProperty(className, "float a", offsetof(Colorf, A));
	}

	void RegisterMath(asIScriptEngine* engine)
	{
		// Vectors (int)
		engine->RegisterObjectType("vec2i", sizeof(Vector2i), asOBJ_VALUE | asGetTypeTraits<Vector2i>() | asOBJ_POD | asOBJ_APP_CLASS_ALLINTS);
		engine->RegisterObjectBehaviour("vec2i", asBEHAVE_CONSTRUCT, "void f(const vec2i&in)", asFUNCTION(+[](Vector2i* _ptr, const Vector2i& v) {
			new(_ptr) Vector2i(v.X, v.Y); }), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectBehaviour("vec2i", asBEHAVE_CONSTRUCT, "void f(int, int)", asFUNCTION(+[](Vector2i* _ptr, std::int32_t x, std::int32_t y) {
			new(_ptr) Vector2i(x, y); }), asCALL_CDECL_OBJFIRST);
		RegisterMembers_Vector2i(engine, "vec2i");

		engine->RegisterObjectType("vec3i", sizeof(Vector3i), asOBJ_VALUE | asGetTypeTraits<Vector3i>() | asOBJ_POD | asOBJ_APP_CLASS_ALLINTS);
		engine->RegisterObjectBehaviour("vec3i", asBEHAVE_CONSTRUCT, "void f(const vec3i&in)", asFUNCTION(+[](Vector3i* _ptr, const Vector3i& v) {
			new(_ptr) Vector3i(v.X, v.Y, v.Z); }), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectBehaviour("vec3i", asBEHAVE_CONSTRUCT, "void f(int, int, int)", asFUNCTION(+[](Vector3i* _ptr, std::int32_t x, std::int32_t y, std::int32_t z) {
			new(_ptr) Vector3i(x, y, z); }), asCALL_CDECL_OBJFIRST);
		RegisterMembers_Vector3i(engine, "vec3i");

		engine->RegisterObjectType("vec4i", sizeof(Vector4i), asOBJ_VALUE | asGetTypeTraits<Vector4i>() | asOBJ_POD | asOBJ_APP_CLASS_ALLINTS);
		engine->RegisterObjectBehaviour("vec4i", asBEHAVE_CONSTRUCT, "void f(const vec4i&in)", asFUNCTION(+[](Vector4i* _ptr, const Vector4i& v) {
			new(_ptr) Vector4i(v.X, v.Y, v.Z, v.W); }), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectBehaviour("vec4i", asBEHAVE_CONSTRUCT, "void f(int, int, int, int)", asFUNCTION(+[](Vector4i* _ptr, std::int32_t x, std::int32_t y, std::int32_t z, std::int32_t w) {
			new(_ptr) Vector4i(x, y, z, w); }), asCALL_CDECL_OBJFIRST);
		RegisterMembers_Vector4i(engine, "vec4i");

		// Vectors (float)
		engine->RegisterObjectType("vec2", sizeof(Vector2f), asOBJ_VALUE | asGetTypeTraits<Vector2f>() | asOBJ_POD | asOBJ_APP_CLASS_ALLFLOATS);
		engine->RegisterObjectBehaviour("vec2", asBEHAVE_CONSTRUCT, "void f(const vec2&in)", asFUNCTION(+[](Vector2f* _ptr, const Vector2f& v) {
			new(_ptr) Vector2f(v); }), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectBehaviour("vec2", asBEHAVE_CONSTRUCT, "void f(const vec2i&in)", asFUNCTION(+[](Vector2f* _ptr, const Vector2i& v) {
			new(_ptr) Vector2f(v.X, v.Y); }), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectBehaviour("vec2", asBEHAVE_CONSTRUCT, "void f(float, float)", asFUNCTION(+[](Vector2f* _ptr, float x, float y) {
			new(_ptr) Vector2f(x, y); }), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectBehaviour("vec2", asBEHAVE_CONSTRUCT, "void f(int, int)", asFUNCTION(+[](Vector2f* _ptr, std::int32_t x, std::int32_t y) {
			new(_ptr) Vector2f(x, y); }), asCALL_CDECL_OBJFIRST);
		RegisterMembers_Vector2f(engine, "vec2");

		engine->RegisterObjectType("vec3", sizeof(Vector3f), asOBJ_VALUE | asGetTypeTraits<Vector3f>() | asOBJ_POD | asOBJ_APP_CLASS_ALLFLOATS);
		engine->RegisterObjectBehaviour("vec3", asBEHAVE_CONSTRUCT, "void f(const vec3&in)", asFUNCTION(+[](Vector3f* _ptr, const Vector3f& v) {
			new(_ptr) Vector3f(v); }), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectBehaviour("vec3", asBEHAVE_CONSTRUCT, "void f(const vec3i&in)", asFUNCTION(+[](Vector3f* _ptr, const Vector3i& v) {
			new(_ptr) Vector3f(v.X, v.Y, v.Z); }), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectBehaviour("vec3", asBEHAVE_CONSTRUCT, "void f(float, float, float)", asFUNCTION(+[](Vector3f* _ptr, float x, float y, float z) {
			new(_ptr) Vector3f(x, y, z); }), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectBehaviour("vec3", asBEHAVE_CONSTRUCT, "void f(int, int, int)", asFUNCTION(+[](Vector3f* _ptr, std::int32_t x, std::int32_t y, std::int32_t z) {
			new(_ptr) Vector3f(x, y, z); }), asCALL_CDECL_OBJFIRST);
		RegisterMembers_Vector2f(engine, "vec3");

		engine->RegisterObjectType("vec4", sizeof(Vector4f), asOBJ_VALUE | asGetTypeTraits<Vector4f>() | asOBJ_POD | asOBJ_APP_CLASS_ALLFLOATS);
		engine->RegisterObjectBehaviour("vec4", asBEHAVE_CONSTRUCT, "void f(const vec4&in)", asFUNCTION(+[](Vector4f* _ptr, const Vector4f& v) {
			new(_ptr) Vector4f(v); }), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectBehaviour("vec4", asBEHAVE_CONSTRUCT, "void f(const vec4i&in)", asFUNCTION(+[](Vector4f* _ptr, const Vector4i& v) {
			new(_ptr) Vector4f(v.X, v.Y, v.Z, v.W); }), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectBehaviour("vec4", asBEHAVE_CONSTRUCT, "void f(float, float, float, float)", asFUNCTION(+[](Vector4f* _ptr, float x, float y, float z, float w) {
			new(_ptr) Vector4f(x, y, z, w); }), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectBehaviour("vec4", asBEHAVE_CONSTRUCT, "void f(int, int, int, int)", asFUNCTION(+[](Vector4f* _ptr, std::int32_t x, std::int32_t y, std::int32_t z, std::int32_t w) {
			new(_ptr) Vector4f(x, y, z, w); }), asCALL_CDECL_OBJFIRST);
		RegisterMembers_Vector2f(engine, "vec4");

		// Color
		engine->RegisterObjectType("Color", sizeof(Colorf), asOBJ_VALUE | asGetTypeTraits<Colorf>() | asOBJ_POD | asOBJ_APP_CLASS_ALLFLOATS);
		engine->RegisterObjectBehaviour("Color", asBEHAVE_CONSTRUCT, "void f(const Color&in)", asFUNCTION(+[](Vector4f* _ptr, const Colorf& v) {
			new(_ptr) Colorf(v); }), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectBehaviour("Color", asBEHAVE_CONSTRUCT, "void f(const vec3&in, float)", asFUNCTION(+[](Vector4f* _ptr, const Vector3f& v, float a) {
			new(_ptr) Colorf(v.X, v.Y, v.Z, a); }), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectBehaviour("Color", asBEHAVE_CONSTRUCT, "void f(const vec4&in)", asFUNCTION(+[](Vector4f* _ptr, const Vector4f& v) {
			new(_ptr) Colorf(v.X, v.Y, v.Z, v.W); }), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectBehaviour("Color", asBEHAVE_CONSTRUCT, "void f(float, float, float)", asFUNCTION(+[](Vector4f* _ptr, float r, float g, float b) {
			new(_ptr) Colorf(r, g, b); }), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectBehaviour("Color", asBEHAVE_CONSTRUCT, "void f(float, float, float, float)", asFUNCTION(+[](Vector4f* _ptr, float r, float g, float b, float a) {
			new(_ptr) Colorf(r, g, b, a); }), asCALL_CDECL_OBJFIRST);
		RegisterMembers_Color(engine, "Color");
	}
}

#endif