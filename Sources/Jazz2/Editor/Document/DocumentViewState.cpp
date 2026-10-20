#include "DocumentViewState.h"

#if defined(WITH_EDITOR)

#include "LevelDocument.h"

#include <cstring>

namespace Jazz2::Editor
{
	namespace
	{
		// u8 version, u16 flags, f32 camera X, f32 camera Y, f32 zoom, i32 layer, u8 mask mode, u8 picker,
		// u8 character, u8 difficulty, u8 game mode, u8 players, i32 reserved, i32 reserved --- a newer version
		// may add more at the end, what's missing at the end of an older one keeps its default.
		// The two reserved values were the position of a play-start marker, which was dropped. They're still written,
		// as 0, so whatever is added later stays where it would be, and the flag of the marker is never set, so builds
		// that still have the marker see none.
		constexpr std::uint8_t Version = 1;

		enum Flags : std::uint16_t
		{
			FlagCamera = 0x01,
			FlagGrid = 0x02,
			FlagEvents = 0x04,
			FlagPlayPath = 0x08,
			FlagFlat = 0x10,
			FlagPlaySettings = 0x20
			// 0x40 was the play-start marker, it's not used for anything else, older builds would take it as one
		};

		class Writer
		{
		public:
			explicit Writer(SmallVectorImpl<std::uint8_t>& data) : _data(data) {}

			void U8(std::uint8_t value) {
				_data.push_back(value);
			}
			void U16(std::uint16_t value) {
				U8(std::uint8_t(value));
				U8(std::uint8_t(value >> 8));
			}
			void U32(std::uint32_t value) {
				U16(std::uint16_t(value));
				U16(std::uint16_t(value >> 16));
			}
			void F32(float value) {
				std::uint32_t bits;
				std::memcpy(&bits, &value, sizeof(bits));
				U32(bits);
			}

		private:
			SmallVectorImpl<std::uint8_t>& _data;
		};

		class Reader
		{
		public:
			Reader(const std::uint8_t* data, std::size_t size) : _p(data), _end(data + size) {}

			bool U8(std::uint8_t& value) {
				if (_p >= _end) {
					return false;
				}
				value = *_p++;
				return true;
			}
			bool U16(std::uint16_t& value) {
				std::uint8_t a, b;
				if (!U8(a) || !U8(b)) {
					return false;
				}
				value = std::uint16_t(a | (b << 8));
				return true;
			}
			bool U32(std::uint32_t& value) {
				std::uint16_t a, b;
				if (!U16(a) || !U16(b)) {
					return false;
				}
				value = std::uint32_t(a) | (std::uint32_t(b) << 16);
				return true;
			}
			bool I32(std::int32_t& value) {
				std::uint32_t bits;
				if (!U32(bits)) {
					return false;
				}
				value = std::int32_t(bits);
				return true;
			}
			bool F32(float& value) {
				std::uint32_t bits;
				if (!U32(bits)) {
					return false;
				}
				std::memcpy(&value, &bits, sizeof(value));
				return true;
			}

		private:
			const std::uint8_t* _p;
			const std::uint8_t* _end;
		};
	}

	bool DocumentViewState::Read(const LevelDocument& document, DocumentViewState& state)
	{
		state = DocumentViewState();
		const Formats::ChunkRecord* chunk = document.FindChunk(Chunk);
		if (chunk == nullptr) {
			return false;
		}

		Reader r(chunk->Data.data(), chunk->Data.size());
		std::uint8_t version;
		std::uint16_t flags;
		if (!r.U8(version) || version == 0 || !r.U16(flags)) {
			return false;
		}
		state.ShowGrid = (flags & FlagGrid) != 0;
		state.ShowEvents = (flags & FlagEvents) != 0;
		state.ShowPlayPath = (flags & FlagPlayPath) != 0;
		state.FlatLayers = (flags & FlagFlat) != 0;

		Vector2f center;
		float zoom;
		if (!r.F32(center.X) || !r.F32(center.Y) || !r.F32(zoom)) {
			return true;
		}
		// Anything that isn't a number is left out, the level opens as it would without it
		if ((flags & FlagCamera) != 0 && center.X == center.X && center.Y == center.Y && zoom > 0.0f && zoom < 1000.0f) {
			state.HasCamera = true;
			state.CameraCenter = center;
			state.CameraZoom = zoom;
		}
		if (!r.I32(state.CurrentLayer) || !r.U8(state.MaskMode) || !r.U8(state.PickerPanel)) {
			return true;
		}
		std::uint8_t character, difficulty, gameMode, playerCount;
		if (!r.U8(character) || !r.U8(difficulty) || !r.U8(gameMode) || !r.U8(playerCount)) {
			return true;
		}
		if ((flags & FlagPlaySettings) != 0) {
			state.HasPlaySettings = true;
			state.PlayCharacter = character;
			state.PlayDifficulty = difficulty;
			state.PlayGameMode = gameMode;
			state.PlayPlayerCount = playerCount;
		}
		// The place of the play-start marker of older builds is skipped, what's added later will come after it
		std::int32_t reserved;
		r.I32(reserved);
		r.I32(reserved);
		return true;
	}

	void DocumentViewState::Write(LevelDocument& document) const
	{
		std::uint16_t flags = 0;
		if (HasCamera) flags |= FlagCamera;
		if (ShowGrid) flags |= FlagGrid;
		if (ShowEvents) flags |= FlagEvents;
		if (ShowPlayPath) flags |= FlagPlayPath;
		if (FlatLayers) flags |= FlagFlat;
		if (HasPlaySettings) flags |= FlagPlaySettings;

		SmallVector<std::uint8_t, 0> data;
		Writer w(data);
		w.U8(Version);
		w.U16(flags);
		w.F32(CameraCenter.X);
		w.F32(CameraCenter.Y);
		w.F32(CameraZoom);
		w.U32(std::uint32_t(CurrentLayer));
		w.U8(MaskMode);
		w.U8(PickerPanel);
		w.U8(PlayCharacter);
		w.U8(PlayDifficulty);
		w.U8(PlayGameMode);
		w.U8(PlayPlayerCount);
		// Reserved, see Version
		w.U32(0);
		w.U32(0);
		document.SetChunk(Chunk, data);
	}
}

#endif
