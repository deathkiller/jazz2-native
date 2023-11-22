#if defined(WITH_ANGELSCRIPT) && defined(WITH_IMGUI)

#include "RegisterImGuiBindings.h"
#include "RegisterArray.h"
#include "../../Common.h"
#include "../../nCine/Primitives/Vector2.h"

#include <cstring>
#include <new>

#include <imgui.h>

#include <Containers/String.h>

using namespace Death::Containers;
using namespace nCine;

namespace Jazz2::Scripting
{
	void RegisterImGuiBindings(asIScriptEngine* engine)
	{
		engine->SetDefaultNamespace("ImGui");
		
		engine->RegisterGlobalFunction("bool Begin(const string&in, bool, int=0)", asFUNCTION(+[](const String& name, bool opened, int flags) {
			return ImGui::Begin(name.data(), &opened, flags); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void End()", asFUNCTIONPR(ImGui::End, (), void), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool BeginChild(const string&in)", asFUNCTION(+[](const String& name) { 
			return ImGui::Begin(name.data()); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void EndChild()", asFUNCTIONPR(ImGui::EndChild, (), void), asCALL_CDECL);

		engine->RegisterGlobalFunction("vec2 GetContentRegionMax()", asFUNCTION(+[]() -> Vector2f { 
			return ImGui::GetContentRegionMax(); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("vec2 GetContentRegionAvail()", asFUNCTION(+[]() -> Vector2f {
			return ImGui::GetContentRegionAvail(); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("vec2 GetWindowContentRegionMin()", asFUNCTION(+[]() -> Vector2f {
			return ImGui::GetWindowContentRegionMin(); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("vec2 GetWindowContentRegionMax()", asFUNCTION(+[]() -> Vector2f {
			return ImGui::GetWindowContentRegionMax(); }), asCALL_CDECL);

		engine->RegisterGlobalFunction("vec2 GetWindowPos()", asFUNCTION(+[]() -> Vector2f {
			return ImGui::GetWindowPos(); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("vec2 GetWindowSize()", asFUNCTION(+[]() -> Vector2f {
			return ImGui::GetWindowSize(); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("float GetWindowWedth()", asFUNCTIONPR(ImGui::GetWindowWidth, (), float), asCALL_CDECL);
		engine->RegisterGlobalFunction("float GetWindowHeight()", asFUNCTIONPR(ImGui::GetWindowHeight, (), float), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool IsWindowCollapsed()", asFUNCTIONPR(ImGui::IsWindowCollapsed, (), bool), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool IsWindowAppearing()", asFUNCTIONPR(ImGui::IsWindowAppearing, (), bool), asCALL_CDECL);
		engine->RegisterGlobalFunction("void SetWindowFontScale(float)", asFUNCTIONPR(ImGui::SetWindowFontScale, (float), void), asCALL_CDECL);

		engine->RegisterGlobalFunction("void SetNextWindowPos(const vec2&in)", asFUNCTION(+[](const Vector2f& v) {
			ImGui::SetNextWindowPos(v); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void SetNextWindowSize(const vec2&in)", asFUNCTION(+[](const Vector2f& v) {
			ImGui::SetNextWindowSize(v); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void SetNextWindowContentSize(const vec2&in)", asFUNCTION(+[](const Vector2f& v) { 
			ImGui::SetNextWindowContentSize(v); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void SetNextWindowCollapsed(bool)", asFUNCTION(+[](bool v) { 
			ImGui::SetNextWindowCollapsed(v); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void SetNextWindowFocus()", asFUNCTION(+[]() { 
			ImGui::SetNextWindowFocus(); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void SetWindowPos(const vec2&in)", asFUNCTION(+[](const Vector2f& v) { 
			ImGui::SetWindowPos(v); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void SetWindowSize(const vec2&in)", asFUNCTION(+[](const Vector2f& v) { 
			ImGui::SetWindowSize(v); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void SetWindowCollapsed(bool)", asFUNCTION(+[](bool v) { 
			ImGui::SetWindowCollapsed(v); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void SetWindowFocus()", asFUNCTION(+[]() { ImGui::SetWindowFocus(); }), asCALL_CDECL);

		engine->RegisterGlobalFunction("void SetWindowPos(const string&in, const vec2&in)", asFUNCTION(+[](const String& name, const Vector2f& v) {
			ImGui::SetWindowPos(name.data(), v); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void SetWindowSize(const string&in, const vec2&in)", asFUNCTION(+[](const String& name, const Vector2f& v) {
			ImGui::SetWindowSize(name.data(), v); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void SetWindowCollapsed(const string&in, bool)", asFUNCTION(+[](const String& name, bool v) { 
			ImGui::SetWindowCollapsed(name.data(), v); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void SetWindowFocus(const string&in)", asFUNCTION(+[](const String& v) { 
			ImGui::SetWindowFocus(v.data()); }), asCALL_CDECL);

		engine->RegisterGlobalFunction("float GetScrollX()", asFUNCTIONPR(ImGui::GetScrollX, (), float), asCALL_CDECL);
		engine->RegisterGlobalFunction("float GetScrollY()", asFUNCTIONPR(ImGui::GetScrollY, (), float), asCALL_CDECL);
		engine->RegisterGlobalFunction("float GetScrollMaxX()", asFUNCTIONPR(ImGui::GetScrollMaxX, (), float), asCALL_CDECL);
		engine->RegisterGlobalFunction("float GetScrollMaxY()", asFUNCTIONPR(ImGui::GetScrollMaxY, (), float), asCALL_CDECL);
		engine->RegisterGlobalFunction("void SetScrollX(float)", asFUNCTIONPR(ImGui::SetScrollX, (float), void), asCALL_CDECL);
		engine->RegisterGlobalFunction("void SetScrollY(float)", asFUNCTIONPR(ImGui::SetScrollY, (float), void), asCALL_CDECL);
		engine->RegisterGlobalFunction("void SetScrollFromPosY(float, float = 0.5f)", asFUNCTIONPR(ImGui::SetScrollFromPosY, (float,float), void), asCALL_CDECL);

		engine->RegisterGlobalFunction("void Separator()", asFUNCTIONPR(ImGui::Separator, (), void), asCALL_CDECL);
		engine->RegisterGlobalFunction("void SameLine(float = 0.0f, float = -1.0f)", asFUNCTIONPR(ImGui::SameLine, (float,float), void), asCALL_CDECL);
		engine->RegisterGlobalFunction("void NewLine()", asFUNCTIONPR(ImGui::NewLine, (), void), asCALL_CDECL);
		engine->RegisterGlobalFunction("void Spacing()", asFUNCTIONPR(ImGui::Spacing, (), void), asCALL_CDECL);
		engine->RegisterGlobalFunction("void Dummy(const vec2&in)", asFUNCTION(+[](const Vector2f& v) { ImGui::Dummy(v); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void Indent(float = 0.0f)", asFUNCTIONPR(ImGui::Indent, (float), void), asCALL_CDECL);
		engine->RegisterGlobalFunction("void Unindent(float = 0.0f)", asFUNCTIONPR(ImGui::Unindent, (float), void), asCALL_CDECL);
		engine->RegisterGlobalFunction("void BeginGroup()", asFUNCTIONPR(ImGui::BeginGroup, (), void), asCALL_CDECL);
		engine->RegisterGlobalFunction("void EndGroup()", asFUNCTIONPR(ImGui::EndGroup, (), void), asCALL_CDECL);
		engine->RegisterGlobalFunction("vec2 GetCursorPos()", asFUNCTION(+[]() { return ImGui::GetCursorPos(); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("float GetCursorPosX()", asFUNCTIONPR(ImGui::GetCursorPosX, (), float), asCALL_CDECL);
		engine->RegisterGlobalFunction("float GetCursorPosY()", asFUNCTIONPR(ImGui::GetCursorPosY, (), float), asCALL_CDECL);
		engine->RegisterGlobalFunction("void SetCursorPos(const vec2&in)", asFUNCTION(+[](const Vector2f& v) { ImGui::SetCursorPos(v); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void SetCursorPosX(float)", asFUNCTIONPR(ImGui::SetCursorPosX, (float), void), asCALL_CDECL);
		engine->RegisterGlobalFunction("void SetCursorPosY(float)", asFUNCTIONPR(ImGui::SetCursorPosY, (float), void), asCALL_CDECL);
		engine->RegisterGlobalFunction("vec2 GetCursorStartPos()", asFUNCTION(+[]() { return ImGui::GetCursorStartPos(); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("vec2 GetCursorScreenPos()", asFUNCTION(+[]() { return ImGui::GetCursorScreenPos(); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void SetCursorScreenPos(const vec2&in)", asFUNCTION(+[](const Vector2f& v) { ImGui::SetCursorScreenPos(v); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void AlignTextToFramePadding()", asFUNCTIONPR(ImGui::AlignTextToFramePadding, (), void), asCALL_CDECL);
		engine->RegisterGlobalFunction("float GetTextLineHeight()", asFUNCTIONPR(ImGui::GetTextLineHeight, (), float), asCALL_CDECL);
		engine->RegisterGlobalFunction("float GetTextLineHeightWithSpacing()", asFUNCTIONPR(ImGui::GetTextLineHeightWithSpacing, (), float), asCALL_CDECL);
		engine->RegisterGlobalFunction("float GetFrameHeight()", asFUNCTIONPR(ImGui::GetFrameHeight, (), float), asCALL_CDECL);
		engine->RegisterGlobalFunction("float GetFrameHeightWithSpacing()", asFUNCTIONPR(ImGui::GetFrameHeightWithSpacing, (), float), asCALL_CDECL);

		// Columns
		engine->RegisterGlobalFunction("void Columns(int = 1, const string&in = string(), bool = true)", asFUNCTION(+[](int a, const String& b, bool c) { 
			ImGui::Columns(a, !b.empty() ? b.data() : nullptr, c); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void NextColumn()", asFUNCTION(+[]() { ImGui::NextColumn(); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("int GetColumnIndex()", asFUNCTION(+[]() { return ImGui::GetColumnIndex(); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("float GetColumnWidth(int = -1)", asFUNCTION(+[](int a) { return ImGui::GetColumnWidth(a); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void SetColumnWidth(int, float)", asFUNCTION(+[](int a, float b) { ImGui::SetColumnWidth(a, b); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("float GetColumnOffset(int = -1)", asFUNCTION(+[](int a) { return ImGui::GetColumnOffset(a); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void SetColumnOffset(int, float)", asFUNCTION(+[](int a, float b) { ImGui::SetColumnOffset(a, b); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("int GetColumnsCount()", asFUNCTION(+[]() { return ImGui::GetColumnsCount(); }), asCALL_CDECL);

		// ID scopes
		engine->RegisterGlobalFunction("void PushID(const string&in)", asFUNCTION(+[](const String& n) { ImGui::PushID(n.data()); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void PushID(int int_id)", asFUNCTION(+[](int id) { ImGui::PushID(id); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void PopID()", asFUNCTIONPR(ImGui::PopID, (), void), asCALL_CDECL);
		engine->RegisterGlobalFunction("uint GetID(const string&in)", asFUNCTION(+[](const String& n) { return ImGui::GetID(n.data()); }), asCALL_CDECL);

		// Widgets: Text
		engine->RegisterGlobalFunction("void Text(const string&in)", asFUNCTION(+[](const String& n) {
			ImGui::TextUnformatted(n.data()); }), asCALL_CDECL);
		// TODO
		engine->RegisterGlobalFunction("void TextColored(const Color&in, const string&in)", asFUNCTION(+[](const Colorf& c, const String& n) {
			ImGui::TextColored(c, n.data()); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void TextWrapped(const string&in)", asFUNCTION(+[](const String& n) {
			ImGui::TextWrapped(n.data()); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void LabelText(const string&in, const string&in)", asFUNCTION(+[](const String& l, const String& n) { 
			ImGui::LabelText(l.data(), n.data()); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void Bullet()", asFUNCTIONPR(ImGui::Bullet, (), void), asCALL_CDECL);
		engine->RegisterGlobalFunction("void BulletText(const string&in)", asFUNCTION(+[](const String& n) { 
			ImGui::BulletText(n.data()); }), asCALL_CDECL);

		// Widgets: Main
		engine->RegisterGlobalFunction("bool Button(const string&in, const vec2&in = vec2(0,0))", asFUNCTION(+[](const String& n, const Vector2f& v) {
			return ImGui::Button(n.data(), v); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool SmallButton(const string&in)", asFUNCTION(+[](const String& n) { 
			return ImGui::SmallButton(n.data()); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool InvisibleButton(const string&in, const vec2&in)", asFUNCTION(+[](const String& id, const Vector2f& v) {
			return ImGui::InvisibleButton(id.data(), v); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void Image(uint, const vec2&in)", asFUNCTION(+[](unsigned u, const Vector2f& v) {
			ImGui::Image((ImTextureID)u, v); }), asCALL_CDECL);
		//engine->RegisterGlobalFunction("bool Checkbox(const string&in, bool&inout)", asFUNCTION(+[](const String& n, bool& v) { 
		//	return ImGui::Checkbox(n.data(), &v); }), asCALL_CDECL);
		//engine->RegisterGlobalFunction("bool CheckboxFlags(const string&in, uint&inout, uint)", asFUNCTION(+[](const String& n, unsigned& f, unsigned v) { 
		//	return ImGui::CheckboxFlags(n.data(), &f, v); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool RadioButton(const string&in, bool)", asFUNCTION(+[](const String& n, bool v) { 
			return ImGui::RadioButton(n.data(), v); }), asCALL_CDECL);
		//engine->RegisterGlobalFunction("bool RadioButton(const string&in, int&inout, int)", asFUNCTION(+[](const String& n, int& v, int vv) { 
		//	return ImGui::RadioButton(n.data(), &v, vv); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void ProgressBar(float)", asFUNCTION(+[](float v) { 
			ImGui::ProgressBar(v); }), asCALL_CDECL);


		// Widgets: Combo Box
		engine->RegisterGlobalFunction("bool BeginCombo(const string&in, const string&in, int = 0)", asFUNCTION(+[](const String& id, const String& prevItem, int flags) {
			return ImGui::BeginCombo(id.data(), prevItem.data(), flags); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void EndCombo()", asFUNCTIONPR(ImGui::EndCombo, (), void), asCALL_CDECL);

		/*static char imgui_comboItem[4096];
		engine->RegisterGlobalFunction("bool Combo(const string&in, int&inout, const array<string>@+)", asFUNCTION(+[](const String& lbl, int& index, const CScriptArray* items) {
			std::memset(imgui_comboItem, 0, sizeof(char) * 4096);
			std::size_t offset = 0;
			for (unsigned i = 0; i < items->GetSize(); ++i) {
				String* str = ((String*)items->At(i));
				strcpy(imgui_comboItem + offset, str->data());
				offset += str->size() + 1;
			}
			return ImGui::Combo(lbl.data(), &index, imgui_comboItem, -1);
		}), asCALL_CDECL);*/

		// Widgets: Drags 
		/*engine->RegisterGlobalFunction("bool DragFloat(const string&in, float&inout, float = 1.0f, float = 0.0f, float = 0.0f)", asFUNCTION(+[](const String& n, float& v, float speed, float mn, float mx) { 
			return ImGui::DragFloat(n.data(), &v, speed, mn, mx); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool DragFloat2(const string&in, vec2&inout)", asFUNCTION(+[](const String& n, Vector2f& v) { 
			return ImGui::DragFloat2(n.data(), &v.X); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool DragFloat3(const string&in, vec3&inout)", asFUNCTION(+[](const String& n, Vector3f& v) { 
			return ImGui::DragFloat3(n.data(), &v.X); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool DragFloat4(const string&in, vec4&inout)", asFUNCTION(+[](const String& n, Vector4f& v) { 
			return ImGui::DragFloat4(n.data(), &v.X); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool DragInt(const string&in, int&inout, int = 0, int = 0)", asFUNCTIONPR(+[](const String& n, int& v, int mn, int mx) { 
			return ImGui::DragInt(n.data(), &v, 1.0f, mn, mx); }, (const String&, int&, int, int), bool), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool DragInt2(const string&in, vec2i&inout, int = 0, int = 0)", asFUNCTION(+[](const String& n, Vector2i& v, int mn, int mx) { 
			return ImGui::DragInt2(n.data(), &v.X, 1.0f, mn, mx); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool DragInt3(const string&in, vec3i&inout, int = 0, int = 0)", asFUNCTION(+[](const String& n, Vector3i& v, int mn, int mx) { 
			return ImGui::DragInt3(n.data(), &v.X, 1.0f, mn, mx); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool DragInt4(const string&in, vec4i&inout, int = 0, int = 0)", asFUNCTION(+[](const String& n, Vector4i& v, int mn, int mx) {
			return ImGui::DragInt4(n.data(), &v.X, 1.0f, mn, mx); }), asCALL_CDECL);

		engine->RegisterGlobalFunction("bool DragFloatRange2(const string&in, float&inout, float&inout, float = 0.0f, float = 1.0f)", asFUNCTION(+[](const String& n, float& v0, float& v1, float mn, float mx) { 
			return ImGui::DragFloatRange2(n.data(), &v0, &v1, 1.0f, mn, mx); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool DragIntRange2(const string&in, int&inout, int&inout, int, int)", asFUNCTION(+[](const String& n, int& v0, int& v1, int mn, int mx) { 
			return ImGui::DragIntRange2(n.data(), &v0, &v1, 1.0f, mn, mx); }), asCALL_CDECL);*/

		// Widgets: Input with Keyboard
		/*static char imgui_text_buffer[4096]; // shared with multiple widgets
		engine->RegisterGlobalFunction("bool InputText(const string&in, string&inout)", asFUNCTION(+[](const String& id, String& val) {
			std::memset(imgui_text_buffer, 0, sizeof(char) * 4096);
			strcpy(imgui_text_buffer, val.data());
			if (ImGui::InputText(id.data(), imgui_text_buffer, 4096)) {
				val = imgui_text_buffer;
				return true;
			}
			return false;
		}), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool InputTextMultiline(const string&in, string&inout)", asFUNCTION(+[](const String& id, String& val) {
			std::memset(imgui_text_buffer, 0, sizeof(char) * 4096);
			strcpy(imgui_text_buffer, val.data());
			if (ImGui::InputTextMultiline(id.data(), imgui_text_buffer, 4096)) {
				val = imgui_text_buffer;
				return true;
			}
			return false;
		}), asCALL_CDECL);*/
		/*engine->RegisterGlobalFunction("bool InputFloat(const string&, float&inout)", asFUNCTION(+[](const String& id, float& val) {
			return ImGui::InputFloat(id.data(), &val); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool InputFloat2(const string&, vec2&inout)", asFUNCTION(+[](const String& id, Vector2f& val) {
			return ImGui::InputFloat2(id.data(), &val.X); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool InputFloat3(const string&, vec3&inout)", asFUNCTION(+[](const String& id, Vector3f& val) {
			return ImGui::InputFloat3(id.data(), &val.X); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool InputFloat4(const string&, vec4&inout)", asFUNCTION(+[](const String& id, Vector4f& val) {
			return ImGui::InputFloat4(id.data(), &val.X); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool InputInt(const string&, int&inout)", asFUNCTION(+[](const String& id, int& val) {
			return ImGui::InputInt(id.data(), &val); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool InputInt2(const string&, vec2i&inout)", asFUNCTION(+[](const String& id, Vector2i& val) {
			return ImGui::InputInt2(id.data(), &val.X); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool InputInt3(const string&, vec3i&inout)", asFUNCTION(+[](const String& id, Vector3i& val) {
			return ImGui::InputInt3(id.data(), &val.X); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool InputInt4(const string&, vec4i&inout)", asFUNCTION(+[](const String& id, Vector4i& val) {
			return ImGui::InputInt4(id.data(), &val.X); }), asCALL_CDECL);*/

		// Widgets: Sliders (tip: ctrl+click on a slider to input with keyboard. manually input values aren't clamped, can go off-bounds)
		/*engine->RegisterGlobalFunction("bool SliderFloat(const string&in, float&inout, float = 0.0f, float = 0.0f)", asFUNCTION(+[](const String& n, float& v, float mn, float mx) { 
			return ImGui::SliderFloat(n.data(), &v, mn, mx); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool SliderFloat2(const string&in, vec2&inout, float, float)", asFUNCTION(+[](const String& n, Vector2f& v, float mn, float mx) { 
			return ImGui::SliderFloat2(n.data(), &v.X, mn, mx); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool SliderFloat3(const string&in, vec3&inout, float, float)", asFUNCTION(+[](const String& n, Vector3f& v, float mn, float mx) { 
			return ImGui::SliderFloat3(n.data(), &v.X, mn, mx); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool SliderFloat4(const string&in, vec4&inout, float, float)", asFUNCTION(+[](const String& n, Vector4f& v, float mn, float mx) { 
			return ImGui::SliderFloat4(n.data(), &v.X, mn, mx); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool SliderInt(const string&in, int&inout, int = 0, int = 0)", asFUNCTION(+[](const String& n, int& v, int mn, int mx) { 
			return ImGui::SliderInt(n.data(), &v, mn, mx); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool SliderInt2(const string&in, vec2i&inout, int = 0, int = 0)", asFUNCTION(+[](const String& n, Vector2i& v, int mn, int mx) { 
			return ImGui::SliderInt2(n.data(), &v.X, mn, mx); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool SliderInt3(const string&in, vec3i&inout, int = 0, int = 0)", asFUNCTION(+[](const String& n, Vector3i& v, int mn, int mx) {
			return ImGui::SliderInt3(n.data(), &v.X, mn, mx); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool SliderInt3(const string&in, vec4i&inout, int = 0, int = 0)", asFUNCTION(+[](const String& n, Vector4i& v, int mn, int mx) {
			return ImGui::SliderInt4(n.data(), &v.X, mn, mx); }), asCALL_CDECL);*/

		// Widgets: Color Editor/Picker
		/*engine->RegisterGlobalFunction("bool ColorEdit3(const string&in, Color&inout)", asFUNCTION(+[](const String& id, Colorf& v) {
			return ImGui::ColorEdit3(id.data(), &v.R); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool ColorEdit4(const string&in, Color&inout)", asFUNCTION(+[](const String& id, Colorf& v) {
			return ImGui::ColorEdit4(id.data(), &v.R); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool ColorPicker3(const string&in, Color&inout)", asFUNCTION(+[](const String& id, Colorf& v) {
			return ImGui::ColorPicker3(id.data(), &v.R); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool ColorPicker4(const string&in, Color&inout)", asFUNCTION(+[](const String& id, Colorf& v) {
			return ImGui::ColorPicker4(id.data(), &v.R); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool ColorButton(const string&in, const Color&in)", asFUNCTION(+[](const String& id, const Colorf& v) {
			return ImGui::ColorButton(id.data(), v); }), asCALL_CDECL);*/

		// Widgets: Trees
		engine->RegisterGlobalFunction("bool TreeNode(const string&in)", asFUNCTION(+[](const String& id) {
			return ImGui::TreeNode(id.data()); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void TreePush(const string&in)", asFUNCTION(+[](const String& id) {
			ImGui::TreePush(id.data()); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void TreePop()", asFUNCTIONPR(ImGui::TreePop, (), void), asCALL_CDECL);
		// TODO engine->RegisterGlobalFunction("void TreeAdvanceToLabelPos()", asFUNCTIONPR(ImGui::TreeAdvanceToLabelPos, (), void), asCALL_CDECL);
		engine->RegisterGlobalFunction("float GetTreeNodeToLabelSpacing()", asFUNCTIONPR(ImGui::GetTreeNodeToLabelSpacing, (), float), asCALL_CDECL);
		// TODO engine->RegisterGlobalFunction("void SetNextTreeNodeOpen(bool)", asFUNCTION(+[](bool val) {
		//	ImGui::SetNextTreeNodeOpen(val); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool CollapsingHeader(const string&in)", asFUNCTION(+[](const String& n) {
			return ImGui::CollapsingHeader(n.data()); }), asCALL_CDECL);
		//engine->RegisterGlobalFunction("bool CollapsingHeader(const string&in, bool&inout)", asFUNCTION(+[](const String& n, bool& v) {
		//	return ImGui::CollapsingHeader(n.data(), &v); }), asCALL_CDECL);

		// Widgets: Selectable / Lists
		engine->RegisterGlobalFunction("bool Selectable(const string&in, bool = false)", asFUNCTION(+[](const String& n, bool v) {
			return ImGui::Selectable(n.data(), v); }), asCALL_CDECL);
		
		// Values
		engine->RegisterGlobalFunction("void Value(const string&in, bool)", asFUNCTION(+[](const String& n, bool v) {
			ImGui::Value(n.data(), v); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void Value(const string&in, int)", asFUNCTION(+[](const String& n, int v) {
			ImGui::Value(n.data(), v); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void Value(const string&in, uint)", asFUNCTION(+[](const String& n, unsigned v) {
			ImGui::Value(n.data(), v); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void Value(const string&in, float)", asFUNCTION(+[](const String& n, float v) {
			ImGui::Value(n.data(), v); }), asCALL_CDECL);

		// Tooltips
		engine->RegisterGlobalFunction("bool BeginTooltip()", asFUNCTIONPR(ImGui::BeginTooltip, (), bool), asCALL_CDECL);
		engine->RegisterGlobalFunction("void EndTooltip()", asFUNCTIONPR(ImGui::EndTooltip, (), void), asCALL_CDECL);
		engine->RegisterGlobalFunction("void SetTooltip(const string&in)", asFUNCTION(+[](const String& t) {
			ImGui::SetTooltip(t.data()); }), asCALL_CDECL);

		// Menus (disabled for now)
		/*engine->RegisterGlobalFunction("bool BeginMainMenuBar()", asFUNCTIONPR(+[]() { return ImGui::BeginMainMenuBar(); }, (), bool), asCALL_CDECL);
		engine->RegisterGlobalFunction("void EndMainMenuBar()", asFUNCTIONPR(+[]() { ImGui::EndMainMenuBar(); }, (), void), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool BeginMenuBar()", asFUNCTIONPR(+[]() { return ImGui::BeginMenuBar(); }, (), bool), asCALL_CDECL);
		engine->RegisterGlobalFunction("void EndMenuBar()", asFUNCTIONPR(+[]() { ImGui::EndMenuBar(); }, (), void), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool BeginMenu(const string&in, bool = true)", asFUNCTIONPR(+[](const String& a, bool b) { 
			return ImGui::BeginMenu(!a.empty() ? a.data() : nullptr, b); }, (const String&, bool), bool), asCALL_CDECL);
		engine->RegisterGlobalFunction("void EndMenu()", asFUNCTIONPR(+[]() { ImGui::EndMenu(); }, (), void), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool MenuItem(const string&in, const string&in = string(), bool = false, bool = true)", asFUNCTIONPR(+[](const String& a, const String& b, bool c, bool d) { 
			return ImGui::MenuItem(!a.empty() ? a.data() : nullptr, !b.empty() ? b.data() : nullptr, c, d); }, (const String&, const String&, bool, bool), bool), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool MenuItem(const string&in, const string&in, bool&inout, bool = true)", asFUNCTIONPR(+[](const String& a, const String& b, bool& c, bool d) { 
			return ImGui::MenuItem(!a.empty() ? a.data() : nullptr, !b.empty() ? b.data() : nullptr, &c, d); }, (const String&, const String&, bool&, bool), bool), asCALL_CDECL);*/

		// Popups
		engine->RegisterGlobalFunction("void OpenPopup(const string&in)", asFUNCTION(+[](const String& a) {
			ImGui::OpenPopup(!a.empty() ? a.data() : nullptr); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool BeginPopup(const string&in, int = 0)", asFUNCTION(+[](const String& a, int b) { 
			return ImGui::BeginPopup(!a.empty() ? a.data() : nullptr, (ImGuiWindowFlags)b); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool BeginPopupContextItem(const string&in = string(), int = 1)", asFUNCTION(+[](const String& a, int b) { 
			return ImGui::BeginPopupContextItem(!a.empty() ? a.data() : nullptr, b); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool BeginPopupContextWindow(const string&in = string(), int = 1)", asFUNCTION(+[](const String& a, int b) { 
			return ImGui::BeginPopupContextWindow(!a.empty() ? a.data() : nullptr, b); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool BeginPopupContextVoid(const string&in = string(), int = 1)", asFUNCTION(+[](const String& a, int b) { 
			return ImGui::BeginPopupContextVoid(!a.empty() ? a.data() : nullptr, b); }), asCALL_CDECL);
		//engine->RegisterGlobalFunction("bool BeginPopupModal(const string&in, bool&inout = null, int = 0)", asFUNCTION(+[](const String& a, bool& b, int c) { 
		//	return ImGui::BeginPopupModal(!a.empty() ? a.data() : nullptr, &b, (ImGuiWindowFlags)c); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void EndPopup()", asFUNCTION(+[]() {
			ImGui::EndPopup(); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool OpenPopupOnItemClick(const string&in = string(), int = 1)", asFUNCTION(+[](const String& a, int b) { 
			return ImGui::OpenPopupOnItemClick(!a.empty() ? a.data() : nullptr, b); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool IsPopupOpen(const string&in)", asFUNCTION(+[](const String& a) { 
			return ImGui::IsPopupOpen(!a.empty() ? a.data() : nullptr); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void CloseCurrentPopup()", asFUNCTION(+[]() {
			ImGui::CloseCurrentPopup(); }), asCALL_CDECL);

		// Clip-rects
		engine->RegisterGlobalFunction("void PushClipRect(const vec2&in, const vec2&in, bool)", asFUNCTION(+[](const Vector2f& a, const Vector2f& b, bool c) {
			ImGui::PushClipRect(a, b, c); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void PopClipRect()", asFUNCTION(+[]() {
			ImGui::PopClipRect(); }), asCALL_CDECL);

		// Focus
		engine->RegisterGlobalFunction("void SetItemDefaultFocus()", asFUNCTION(+[]() {
			ImGui::SetItemDefaultFocus(); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void SetKeyboardFocusHere(int = 0)", asFUNCTION(+[](int a) {
			ImGui::SetKeyboardFocusHere(a); }), asCALL_CDECL);

		// Utilities
		engine->RegisterGlobalFunction("bool IsItemHovered(int = 0)", asFUNCTION(+[](int a) {
			return ImGui::IsItemHovered((ImGuiHoveredFlags)a); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool IsItemActive()", asFUNCTION(+[]() {
			return ImGui::IsItemActive(); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool IsItemClicked(int = 0)", asFUNCTION(+[](int a) {
			return ImGui::IsItemClicked(a); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool IsItemVisible()", asFUNCTION(+[]() {
			return ImGui::IsItemVisible(); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool IsAnyItemHovered()", asFUNCTION(+[]() {
			return ImGui::IsAnyItemHovered(); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool IsAnyItemActive()", asFUNCTION(+[]() {
			return ImGui::IsAnyItemActive(); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("vec2 GetItemRectMin()", asFUNCTION(+[]() {
			return ImGui::GetItemRectMin(); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("vec2 GetItemRectMax()", asFUNCTION(+[]() {
			return ImGui::GetItemRectMax(); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("vec2 GetItemRectSize()", asFUNCTION(+[]() {
			return ImGui::GetItemRectSize(); }), asCALL_CDECL);
		// TODO engine->RegisterGlobalFunction("void SetItemAllowOverlap()", asFUNCTION(+[]() {
		//	ImGui::SetItemAllowOverlap(); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool IsWindowFocused(int = 0)", asFUNCTION(+[](int a) {
			return ImGui::IsWindowFocused((ImGuiFocusedFlags)a); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool IsWindowHovered(int = 0)", asFUNCTION(+[](int a) {
			return ImGui::IsWindowHovered((ImGuiHoveredFlags)a); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool IsRectVisible(const vec2&in)", asFUNCTION(+[](const Vector2f& a) {
			return ImGui::IsRectVisible(a); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool IsRectVisible(const vec2&in, const vec2&in)", asFUNCTION(+[](const Vector2f& a, const Vector2f& b) {
			return ImGui::IsRectVisible(a, b); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("float GetTime()", asFUNCTION(+[]() {
			return ImGui::GetTime(); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("int GetFrameCount()", asFUNCTION(+[]() {
			return ImGui::GetFrameCount(); }), asCALL_CDECL);

		engine->RegisterGlobalFunction("vec2 CalcTextSize(const string&in, const string&in = string(), bool = false, float = -1.0f)", asFUNCTION(+[](const String& a, const String& b, bool c, float d) { 
			return ImGui::CalcTextSize(!a.empty() ? a.data() : nullptr, !b.empty() ? b.data() : nullptr, c, d); }), asCALL_CDECL);
		// TODO engine->RegisterGlobalFunction("void CalcListClipping(int, float, int&inout, int&inout)", asFUNCTION(+[](int a, float b, int& c, int& d) { 
		//	ImGui::CalcListClipping(a, b, &c, &d); }), asCALL_CDECL);
		// TODO engine->RegisterGlobalFunction("bool BeginChildFrame(uint, const vec2&, int = 0)", asFUNCTION(+[](unsigned a, const Vector2f& b, int c) { 
		//	return ImGui::BeginChildFrame(a, b, (ImGuiWindowFlags)c); }), asCALL_CDECL);
		// TODO engine->RegisterGlobalFunction("void EndChildFrame()", asFUNCTION(+[]() {
		//	ImGui::EndChildFrame(); }), asCALL_CDECL);

		engine->RegisterGlobalFunction("int GetKeyIndex(int)", asFUNCTION(+[](int a) {
			return ImGui::GetKeyIndex((ImGuiKey)a); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool IsKeyDown(int)", asFUNCTION(+[](int a) {
			return ImGui::IsKeyDown((ImGuiKey)a); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool IsKeyPressed(int, bool = true)", asFUNCTION(+[](int a, bool b) {
			return ImGui::IsKeyPressed((ImGuiKey)a, b); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool IsKeyReleased(int)", asFUNCTION(+[](int a) {
			return ImGui::IsKeyReleased((ImGuiKey)a); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("int GetKeyPressedAmount(int, float, float)", asFUNCTION(+[](int a, float b, float c) {
			return ImGui::GetKeyPressedAmount((ImGuiKey)a, b, c); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool IsMouseDown(int)", asFUNCTION(+[](int a) {
			return ImGui::IsMouseDown(a); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool IsMouseClicked(int, bool = false)", asFUNCTION(+[](int a, bool b) {
			return ImGui::IsMouseClicked(a, b); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool IsMouseDoubleClicked(int)", asFUNCTION(+[](int a) {
			return ImGui::IsMouseDoubleClicked(a); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool IsMouseReleased(int)", asFUNCTION(+[](int a) {
			return ImGui::IsMouseReleased(a); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool IsMouseDragging(int = 0, float = -1.0f)", asFUNCTION(+[](int a, float b) {
			return ImGui::IsMouseDragging(a, b); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool IsMouseHoveringRect(const vec2&in, const vec2&in, bool = true)", asFUNCTION(+[](const Vector2f& a, const Vector2f& b, bool c) {
			return ImGui::IsMouseHoveringRect(a, b, c); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool IsMousePosValid(const vec2&in)", asFUNCTION(+[](const Vector2f& a) {
			ImVec2 v = a; return ImGui::IsMousePosValid(&v); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("vec2 GetMousePos()", asFUNCTION(+[]() {
			return ImGui::GetMousePos(); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("vec2 GetMousePosOnOpeningCurrentPopup()", asFUNCTION(+[]() { 
			return ImGui::GetMousePosOnOpeningCurrentPopup(); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("vec2 GetMouseDragDelta(int = 0, float = -1.0f)", asFUNCTION(+[](int a, float b) {
			return ImGui::GetMouseDragDelta(a, b); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void ResetMouseDragDelta(int = 0)", asFUNCTION(+[](int a) {
			ImGui::ResetMouseDragDelta(a); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("int GetMouseCursor()", asFUNCTION(+[]() {
			return ImGui::GetMouseCursor(); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void SetMouseCursor(int)", asFUNCTION(+[](ImGuiMouseCursor a) {
			ImGui::SetMouseCursor(a); }), asCALL_CDECL);

		engine->RegisterGlobalFunction("string GetClipboardText()", asFUNCTION(+[]() -> String {
			return ImGui::GetClipboardText(); }), asCALL_CDECL);
		engine->RegisterGlobalFunction("void SetClipboardText(const string&in)", asFUNCTION(+[](const String& a) {
			ImGui::SetClipboardText(!a.empty() ? a.data() : nullptr); }), asCALL_CDECL);

		engine->SetDefaultNamespace("");
	}
}

#endif