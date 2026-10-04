#include "Mpeg1Encoder.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace Jazz2::AssetPacker
{
	namespace
	{

		// ── Syntax ───────────────────────────────────────────────────────────────────────────────

		constexpr std::uint8_t PictureStartCode = 0x00;
		constexpr std::uint8_t SequenceHeaderCode = 0xB3;
		constexpr std::uint8_t SequenceEndCode = 0xB7;
		constexpr std::uint8_t GroupStartCode = 0xB8;

		constexpr std::int32_t PictureTypeI = 1;
		constexpr std::int32_t PictureTypeP = 2;

		/** @brief Highest bitrate the `bit_rate` field of the sequence header can express (0x3FFFE times 400 bit/s) */
		constexpr std::int32_t MaxBitrateKbps = 104857;

		struct Vlc
		{
			std::uint16_t Code;
			std::uint8_t Length;
		};

		/** @brief Frame rates by `frame_rate_code`, with the whole rate the time codes count in */
		struct FrameRate
		{
			std::int32_t Num, Den, TimeCodeRate;
		};

		constexpr FrameRate FrameRates[9] = {
			{ 0, 1, 0 }, { 24000, 1001, 24 }, { 24, 1, 24 }, { 25, 1, 25 }, { 30000, 1001, 30 },
			{ 30, 1, 30 }, { 50, 1, 50 }, { 60000, 1001, 60 }, { 60, 1, 60 }
		};

		/** @brief Coefficients in the order of the zigzag scan, as raster indices (the row is the vertical frequency) */
		constexpr std::uint8_t ZigZag[64] = {
			0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5,
			12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
			35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
			58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63
		};

		/** @brief Default intra quantizer matrix, in raster order */
		constexpr std::uint8_t IntraMatrix[64] = {
			8, 16, 19, 22, 26, 27, 29, 34,
			16, 16, 22, 24, 27, 29, 34, 37,
			19, 22, 26, 27, 29, 34, 34, 38,
			22, 22, 26, 27, 29, 34, 37, 40,
			22, 26, 27, 29, 32, 35, 40, 48,
			26, 27, 29, 32, 35, 40, 48, 58,
			26, 27, 29, 34, 38, 46, 56, 69,
			27, 29, 35, 38, 46, 56, 69, 83
		};

		/** @brief Weight of every coefficient in the default non-intra quantizer matrix */
		constexpr std::int32_t NonIntraWeight = 16;

		/** @brief `macroblock_address_increment` by increment (1-33) */
		constexpr Vlc AddressIncrementVlc[34] = {
			{ 0, 0 },
			{ 0x1, 1 }, { 0x3, 3 }, { 0x2, 3 }, { 0x3, 4 }, { 0x2, 4 }, { 0x3, 5 }, { 0x2, 5 }, { 0x7, 7 },
			{ 0x6, 7 }, { 0xB, 8 }, { 0xA, 8 }, { 0x9, 8 }, { 0x8, 8 }, { 0x7, 8 }, { 0x6, 8 }, { 0x17, 10 },
			{ 0x16, 10 }, { 0x15, 10 }, { 0x14, 10 }, { 0x13, 10 }, { 0x12, 10 }, { 0x23, 11 }, { 0x22, 11 }, { 0x21, 11 },
			{ 0x20, 11 }, { 0x1F, 11 }, { 0x1E, 11 }, { 0x1D, 11 }, { 0x1C, 11 }, { 0x1B, 11 }, { 0x1A, 11 }, { 0x19, 11 },
			{ 0x18, 11 }
		};
		constexpr Vlc AddressEscapeVlc = { 0x08, 11 };

		/** @brief `coded_block_pattern` by pattern (1-63), the most significant bit is the first luma block */
		constexpr Vlc CodedBlockPatternVlc[64] = {
			{ 0, 0 },
			{ 0x0B, 5 }, { 0x09, 5 }, { 0x0D, 6 }, { 0x0D, 4 }, { 0x17, 7 }, { 0x13, 7 }, { 0x1F, 8 }, { 0x0C, 4 },
			{ 0x16, 7 }, { 0x12, 7 }, { 0x1E, 8 }, { 0x13, 5 }, { 0x1B, 8 }, { 0x17, 8 }, { 0x13, 8 }, { 0x0B, 4 },
			{ 0x15, 7 }, { 0x11, 7 }, { 0x1D, 8 }, { 0x11, 5 }, { 0x19, 8 }, { 0x15, 8 }, { 0x11, 8 }, { 0x0F, 6 },
			{ 0x0F, 8 }, { 0x0D, 8 }, { 0x03, 9 }, { 0x0F, 5 }, { 0x0B, 8 }, { 0x07, 8 }, { 0x07, 9 }, { 0x0A, 4 },
			{ 0x14, 7 }, { 0x10, 7 }, { 0x1C, 8 }, { 0x0E, 6 }, { 0x0E, 8 }, { 0x0C, 8 }, { 0x02, 9 }, { 0x10, 5 },
			{ 0x18, 8 }, { 0x14, 8 }, { 0x10, 8 }, { 0x0E, 5 }, { 0x0A, 8 }, { 0x06, 8 }, { 0x06, 9 }, { 0x12, 5 },
			{ 0x1A, 8 }, { 0x16, 8 }, { 0x12, 8 }, { 0x0D, 5 }, { 0x09, 8 }, { 0x05, 8 }, { 0x05, 9 }, { 0x0C, 5 },
			{ 0x08, 8 }, { 0x04, 8 }, { 0x04, 9 }, { 0x07, 3 }, { 0x0A, 5 }, { 0x08, 5 }, { 0x0C, 6 }
		};

		/** @brief `motion_code` by magnitude (0-16), followed by a sign bit unless it is zero */
		constexpr Vlc MotionCodeVlc[17] = {
			{ 0x1, 1 }, { 0x1, 2 }, { 0x1, 3 }, { 0x1, 4 }, { 0x3, 6 }, { 0x5, 7 }, { 0x4, 7 }, { 0x3, 7 },
			{ 0xB, 9 }, { 0xA, 9 }, { 0x9, 9 }, { 0x11, 10 }, { 0x10, 10 }, { 0xF, 10 }, { 0xE, 10 }, { 0xD, 10 },
			{ 0xC, 10 }
		};

		/** @brief `dct_dc_size_luminance` and `dct_dc_size_chrominance` by size (0-8) */
		constexpr Vlc DcSizeLumaVlc[9] = {
			{ 0x4, 3 }, { 0x0, 2 }, { 0x1, 2 }, { 0x5, 3 }, { 0x6, 3 }, { 0xE, 4 }, { 0x1E, 5 }, { 0x3E, 6 }, { 0x7E, 7 }
		};
		constexpr Vlc DcSizeChromaVlc[9] = {
			{ 0x0, 2 }, { 0x1, 2 }, { 0x2, 2 }, { 0x6, 3 }, { 0xE, 4 }, { 0x1E, 5 }, { 0x3E, 6 }, { 0x7E, 7 }, { 0xFE, 8 }
		};

		/** @brief One run/level pair of `dct_coeff_next`, without its sign bit */
		struct RunLevelCode
		{
			std::uint8_t Run, Level;
			std::uint16_t Code;
			std::uint8_t Length;
		};

		constexpr RunLevelCode RunLevelCodes[] = {
			{ 0, 1, 0x3, 2 }, { 1, 1, 0x3, 3 }, { 0, 2, 0x4, 4 }, { 2, 1, 0x5, 4 }, { 0, 3, 0x5, 5 }, { 3, 1, 0x7, 5 },
			{ 4, 1, 0x6, 5 }, { 1, 2, 0x6, 6 }, { 5, 1, 0x7, 6 }, { 6, 1, 0x5, 6 }, { 7, 1, 0x4, 6 }, { 0, 4, 0x6, 7 },
			{ 2, 2, 0x4, 7 }, { 8, 1, 0x7, 7 }, { 9, 1, 0x5, 7 }, { 0, 5, 0x26, 8 }, { 0, 6, 0x21, 8 }, { 1, 3, 0x25, 8 },
			{ 3, 2, 0x24, 8 }, { 10, 1, 0x27, 8 }, { 11, 1, 0x23, 8 }, { 12, 1, 0x22, 8 }, { 13, 1, 0x20, 8 },
			{ 0, 7, 0xA, 10 }, { 1, 4, 0xC, 10 }, { 2, 3, 0xB, 10 }, { 4, 2, 0xF, 10 }, { 5, 2, 0x9, 10 }, { 14, 1, 0xE, 10 },
			{ 15, 1, 0xD, 10 }, { 16, 1, 0x8, 10 },
			{ 0, 8, 0x1D, 12 }, { 0, 9, 0x18, 12 }, { 0, 10, 0x13, 12 }, { 0, 11, 0x10, 12 }, { 1, 5, 0x1B, 12 },
			{ 2, 4, 0x14, 12 }, { 3, 3, 0x1C, 12 }, { 4, 3, 0x12, 12 }, { 6, 2, 0x1E, 12 }, { 7, 2, 0x15, 12 },
			{ 8, 2, 0x11, 12 }, { 17, 1, 0x1F, 12 }, { 18, 1, 0x1A, 12 }, { 19, 1, 0x19, 12 }, { 20, 1, 0x17, 12 },
			{ 21, 1, 0x16, 12 },
			{ 0, 12, 0x1A, 13 }, { 0, 13, 0x19, 13 }, { 0, 14, 0x18, 13 }, { 0, 15, 0x17, 13 }, { 1, 6, 0x16, 13 },
			{ 1, 7, 0x15, 13 }, { 2, 5, 0x14, 13 }, { 3, 4, 0x13, 13 }, { 5, 3, 0x12, 13 }, { 9, 2, 0x11, 13 },
			{ 10, 2, 0x10, 13 }, { 22, 1, 0x1F, 13 }, { 23, 1, 0x1E, 13 }, { 24, 1, 0x1D, 13 }, { 25, 1, 0x1C, 13 },
			{ 26, 1, 0x1B, 13 },
			{ 0, 16, 0x1F, 14 }, { 0, 17, 0x1E, 14 }, { 0, 18, 0x1D, 14 }, { 0, 19, 0x1C, 14 }, { 0, 20, 0x1B, 14 },
			{ 0, 21, 0x1A, 14 }, { 0, 22, 0x19, 14 }, { 0, 23, 0x18, 14 }, { 0, 24, 0x17, 14 }, { 0, 25, 0x16, 14 },
			{ 0, 26, 0x15, 14 }, { 0, 27, 0x14, 14 }, { 0, 28, 0x13, 14 }, { 0, 29, 0x12, 14 }, { 0, 30, 0x11, 14 },
			{ 0, 31, 0x10, 14 },
			{ 0, 32, 0x18, 15 }, { 0, 33, 0x17, 15 }, { 0, 34, 0x16, 15 }, { 0, 35, 0x15, 15 }, { 0, 36, 0x14, 15 },
			{ 0, 37, 0x13, 15 }, { 0, 38, 0x12, 15 }, { 0, 39, 0x11, 15 }, { 0, 40, 0x10, 15 }, { 1, 8, 0x1F, 15 },
			{ 1, 9, 0x1E, 15 }, { 1, 10, 0x1D, 15 }, { 1, 11, 0x1C, 15 }, { 1, 12, 0x1B, 15 }, { 1, 13, 0x1A, 15 },
			{ 1, 14, 0x19, 15 },
			{ 1, 15, 0x13, 16 }, { 1, 16, 0x12, 16 }, { 1, 17, 0x11, 16 }, { 1, 18, 0x10, 16 }, { 6, 3, 0x14, 16 },
			{ 11, 2, 0x1A, 16 }, { 12, 2, 0x19, 16 }, { 13, 2, 0x18, 16 }, { 14, 2, 0x17, 16 }, { 15, 2, 0x16, 16 },
			{ 16, 2, 0x15, 16 }, { 27, 1, 0x1F, 16 }, { 28, 1, 0x1E, 16 }, { 29, 1, 0x1D, 16 }, { 30, 1, 0x1C, 16 },
			{ 31, 1, 0x1B, 16 }
		};

		constexpr std::int32_t MaxTableRun = 31;
		constexpr std::int32_t MaxTableLevel = 40;
		/** @brief Lengths of an escaped coefficient: `escape`, run, and an 8-bit or a 16-bit level */
		constexpr std::int32_t EscapeShortLength = 6 + 6 + 8;
		constexpr std::int32_t EscapeLongLength = 6 + 6 + 16;
		constexpr std::int32_t EndOfBlockLength = 2;

		/** @brief `dct_coeff_next` indexed by run and level, a length of zero meaning the pair has to be escaped */
		struct RunLevelTable
		{
			Vlc Codes[MaxTableRun + 1][MaxTableLevel + 1];
			/** @brief Bits of every run/level pair including the sign bit, escapes included */
			std::uint8_t Bits[MaxTableRun + 1][MaxTableLevel + 1];
		};

		constexpr RunLevelTable BuildRunLevelTable()
		{
			RunLevelTable table {};
			for (std::int32_t run = 0; run <= MaxTableRun; run++) {
				for (std::int32_t level = 0; level <= MaxTableLevel; level++) {
					table.Bits[run][level] = EscapeShortLength;
				}
			}
			for (const RunLevelCode& entry : RunLevelCodes) {
				table.Codes[entry.Run][entry.Level] = { entry.Code, entry.Length };
				table.Bits[entry.Run][entry.Level] = std::uint8_t(entry.Length + 1);
			}
			return table;
		}

		constexpr RunLevelTable RunLevels = BuildRunLevelTable();

		/** @brief Bits taken by one coefficient, @p first marking the first one of a non-intra block */
		inline std::int32_t CoefficientBits(std::int32_t run, std::int32_t absLevel, bool first)
		{
			if (run <= MaxTableRun && absLevel <= MaxTableLevel) {
				if (first && run == 0 && absLevel == 1) {
					return 2;
				}
				return RunLevels.Bits[run][absLevel];
			}
			return (absLevel < 128 ? EscapeShortLength : EscapeLongLength);
		}

		inline std::int32_t AddressIncrementBits(std::int32_t increment)
		{
			std::int32_t bits = 0;
			while (increment > 33) {
				bits += AddressEscapeVlc.Length;
				increment -= 33;
			}
			return bits + AddressIncrementVlc[increment].Length;
		}

		/** @brief Brings a motion vector difference into the range the `f_code` can express, as the decoder wraps it */
		inline std::int32_t WrapMotionDelta(std::int32_t delta, std::int32_t fCode)
		{
			const std::int32_t f = 1 << (fCode - 1);
			if (delta > 16 * f - 1) {
				delta -= 32 * f;
			} else if (delta < -16 * f) {
				delta += 32 * f;
			}
			return delta;
		}

		inline std::int32_t MotionComponentBits(std::int32_t delta, std::int32_t fCode)
		{
			if (delta == 0) {
				return 1;
			}
			const std::int32_t f = 1 << (fCode - 1);
			const std::int32_t magnitude = (std::abs(delta) + f - 1) / f;
			return MotionCodeVlc[magnitude].Length + 1 + (fCode - 1);
		}

		inline std::int32_t DcSize(std::int32_t value)
		{
			std::int32_t a = std::abs(value), size = 0;
			while (a != 0) {
				a >>= 1;
				size++;
			}
			return size;
		}

		inline std::int32_t DcBits(std::int32_t difference, bool chroma)
		{
			const std::int32_t size = DcSize(difference);
			return (chroma ? DcSizeChromaVlc[size].Length : DcSizeLumaVlc[size].Length) + size;
		}

		class BitWriter
		{
		public:
			std::vector<std::uint8_t> Bytes;

			void Put(std::uint32_t value, std::int32_t length)
			{
				_accumulator = (_accumulator << length) | (value & ((std::uint64_t(1) << length) - 1));
				_count += length;
				while (_count >= 8) {
					_count -= 8;
					Bytes.push_back(std::uint8_t(_accumulator >> _count));
				}
			}

			void Put(const Vlc& vlc)
			{
				Put(vlc.Code, vlc.Length);
			}

			/** @brief Pads to the next byte with zero bits, as `next_start_code()` expects */
			void Align()
			{
				if (_count > 0) {
					Put(0, 8 - _count);
				}
			}

			void StartCode(std::uint8_t code)
			{
				Align();
				Put(0x000001, 24);
				Put(code, 8);
			}

			std::int64_t GetBitCount() const
			{
				return std::int64_t(Bytes.size()) * 8 + _count;
			}

			/** @brief A position in the stream that can be returned to */
			struct Mark
			{
				std::size_t Size;
				std::uint64_t Accumulator;
				std::int32_t Count;
			};

			Mark GetMark() const
			{
				return { Bytes.size(), _accumulator, _count };
			}

			void Rewind(const Mark& mark)
			{
				Bytes.resize(mark.Size);
				_accumulator = mark.Accumulator;
				_count = mark.Count;
			}

			void Clear()
			{
				Bytes.clear();
				_accumulator = 0;
				_count = 0;
			}

		private:
			std::uint64_t _accumulator = 0;
			std::int32_t _count = 0;
		};

		void WriteMotionComponent(BitWriter& bw, std::int32_t delta, std::int32_t fCode)
		{
			if (delta == 0) {
				bw.Put(MotionCodeVlc[0]);
				return;
			}
			const std::int32_t f = 1 << (fCode - 1);
			const std::int32_t a = std::abs(delta);
			const std::int32_t magnitude = (a + f - 1) / f;
			bw.Put(MotionCodeVlc[magnitude]);
			bw.Put(delta < 0 ? 1 : 0, 1);
			if (fCode > 1) {
				bw.Put(std::uint32_t(f - 1 - (magnitude * f - a)), fCode - 1);
			}
		}

		void WriteDc(BitWriter& bw, std::int32_t difference, bool chroma)
		{
			const std::int32_t size = DcSize(difference);
			bw.Put(chroma ? DcSizeChromaVlc[size] : DcSizeLumaVlc[size]);
			if (size > 0) {
				bw.Put(std::uint32_t(difference > 0 ? difference : difference + (1 << size) - 1), size);
			}
		}

		/** @brief Writes the coefficients of a block from @p start on, in scan order, and its `end_of_block` */
		void WriteCoefficients(BitWriter& bw, const std::int16_t* levels, std::int32_t start)
		{
			// The first coefficient of a non-intra block has a shorter code for a lone ±1
			bool first = (start == 0);
			std::int32_t run = 0;
			for (std::int32_t i = start; i < 64; i++) {
				const std::int32_t level = levels[i];
				if (level == 0) {
					run++;
					continue;
				}
				const std::int32_t a = std::abs(level);
				if (first && run == 0 && a == 1) {
					bw.Put(1, 1);
					bw.Put(level < 0 ? 1 : 0, 1);
				} else if (run <= MaxTableRun && a <= MaxTableLevel && RunLevels.Codes[run][a].Length != 0) {
					bw.Put(RunLevels.Codes[run][a]);
					bw.Put(level < 0 ? 1 : 0, 1);
				} else {
					bw.Put(0x01, 6);
					bw.Put(std::uint32_t(run), 6);
					if (a < 128) {
						bw.Put(std::uint32_t(level) & 0xFF, 8);
					} else if (level > 0) {
						bw.Put(0x00, 8);
						bw.Put(std::uint32_t(level), 8);
					} else {
						bw.Put(0x80, 8);
						bw.Put(std::uint32_t(level + 256), 8);
					}
				}
				first = false;
				run = 0;
			}
			bw.Put(0x2, EndOfBlockLength);
		}

		// ── Transforms ───────────────────────────────────────────────────────────────────────────

		/**
			@brief Half the cosines of `j * pi / 16`, in 14-bit and in 20-bit fixed point

			Written out rather than computed with `std::cos()`, whose last bit is up to the C library - the forward
			transform only steers decisions, but the inverse one is the reconstruction every later picture is
			predicted from, and the encoder must produce the same stream everywhere.
		*/
		constexpr std::int32_t HalfCosines14[9] = { 8192, 8035, 7568, 6811, 5793, 4551, 3135, 1598, 0 };
		constexpr std::int32_t HalfCosines20[9] = { 524288, 514214, 484379, 435930, 370728, 291279, 200636, 102284, 0 };

		/** @brief Entry of the orthonormal DCT basis for frequency @p k at sample @p n */
		constexpr std::int32_t BasisEntry(const std::int32_t* halfCosines, std::int32_t k, std::int32_t n)
		{
			if (k == 0) {
				// sqrt(1/8) is half the cosine of pi/4
				return halfCosines[4];
			}
			std::int32_t m = ((2 * n + 1) * k) % 32;
			if (m > 16) {
				m = 32 - m;
			}
			return (m <= 8 ? halfCosines[m] : -halfCosines[16 - m]);
		}

		struct DctBasis
		{
			std::int16_t Forward[8][8];
			std::int32_t Inverse[8][8];
		};

		constexpr DctBasis BuildDctBasis()
		{
			DctBasis basis {};
			for (std::int32_t k = 0; k < 8; k++) {
				for (std::int32_t n = 0; n < 8; n++) {
					basis.Forward[k][n] = std::int16_t(BasisEntry(HalfCosines14, k, n));
					basis.Inverse[k][n] = BasisEntry(HalfCosines20, k, n);
				}
			}
			return basis;
		}

		constexpr DctBasis Basis = BuildDctBasis();

		/**
			@brief Forward DCT of an 8x8 block, scaled the way MPEG-1 defines it (orthonormal, the DC is 8 times the mean)

			Plain matrix products in fixed point: they keep enough precision for the quantizer and vectorize well.
		*/
		void ForwardDct(const std::int16_t* block, std::int32_t* coefficients)
		{
			// Both passes are dot products of 16-bit rows, the first one writes its result transposed for the second
			std::int16_t columns[64];
			for (std::int32_t y = 0; y < 8; y++) {
				for (std::int32_t u = 0; u < 8; u++) {
					std::int32_t sum = 0;
					for (std::int32_t x = 0; x < 8; x++) {
						sum += std::int32_t(block[y * 8 + x]) * Basis.Forward[u][x];
					}
					// Four fractional bits are kept for the second pass
					columns[u * 8 + y] = std::int16_t((sum + (1 << 9)) >> 10);
				}
			}
			for (std::int32_t v = 0; v < 8; v++) {
				for (std::int32_t u = 0; u < 8; u++) {
					std::int32_t sum = 0;
					for (std::int32_t y = 0; y < 8; y++) {
						sum += std::int32_t(columns[u * 8 + y]) * Basis.Forward[v][y];
					}
					coefficients[v * 8 + u] = (sum + (1 << 17)) >> 18;
				}
			}
		}

		/**
			@brief Inverse DCT of an 8x8 block of dequantized coefficients, rounded to the nearest integer

			Separable products in 64-bit fixed point without intermediate rounding, which matches the ideal transform
			of IEEE 1180 except where the exact result lies within a hair of a half - so the reconstruction the
			pictures are predicted from is what any accurate decoder computes, and the drift against it stays as
			small as the decoder's own transform allows.
		*/
		void InverseDct(const std::int32_t* coefficients, std::int32_t* samples)
		{
			std::int64_t columns[64];
			for (std::int32_t u = 0; u < 8; u++) {
				bool empty = true;
				for (std::int32_t v = 0; v < 8; v++) {
					if (coefficients[v * 8 + u] != 0) {
						empty = false;
						break;
					}
				}
				if (empty) {
					for (std::int32_t y = 0; y < 8; y++) {
						columns[y * 8 + u] = 0;
					}
					continue;
				}
				for (std::int32_t y = 0; y < 8; y++) {
					std::int64_t sum = 0;
					for (std::int32_t v = 0; v < 8; v++) {
						sum += std::int64_t(Basis.Inverse[v][y]) * coefficients[v * 8 + u];
					}
					columns[y * 8 + u] = sum;
				}
			}
			for (std::int32_t y = 0; y < 8; y++) {
				for (std::int32_t x = 0; x < 8; x++) {
					std::int64_t sum = 0;
					for (std::int32_t u = 0; u < 8; u++) {
						sum += std::int64_t(Basis.Inverse[u][x]) * columns[y * 8 + u];
					}
					samples[y * 8 + x] = std::int32_t((sum + (std::int64_t(1) << 39)) >> 40);
				}
			}
		}

		// ── Quantization ─────────────────────────────────────────────────────────────────────────

		/** @brief Reconstructs an intra AC coefficient as the standard does, including the oddification */
		inline std::int32_t DequantizeIntra(std::int32_t level, std::int32_t quantizer, std::int32_t weight)
		{
			std::int32_t value = (2 * level * quantizer * weight) / 16;
			if ((value & 1) == 0 && value != 0) {
				value -= (value > 0 ? 1 : -1);
			}
			return std::clamp(value, -2048, 2047);
		}

		inline std::int32_t DequantizeNonIntra(std::int32_t level, std::int32_t quantizer)
		{
			std::int32_t value = ((2 * level + (level > 0 ? 1 : -1)) * quantizer * NonIntraWeight) / 16;
			if ((value & 1) == 0 && value != 0) {
				value -= (value > 0 ? 1 : -1);
			}
			return std::clamp(value, -2048, 2047);
		}

		/** @brief Dequantizes the levels of a block (in scan order) into raster-ordered coefficients */
		void Dequantize(const std::int16_t* levels, bool intra, std::int32_t quantizer, std::int32_t* coefficients)
		{
			std::memset(coefficients, 0, sizeof(std::int32_t) * 64);
			std::int32_t start = 0;
			if (intra) {
				coefficients[0] = levels[0] * 8;
				start = 1;
			}
			for (std::int32_t i = start; i < 64; i++) {
				const std::int32_t level = levels[i];
				if (level != 0) {
					const std::int32_t index = ZigZag[i];
					coefficients[index] = (intra ? DequantizeIntra(level, quantizer, IntraMatrix[index]) : DequantizeNonIntra(level, quantizer));
				}
			}
		}

		/** @brief Lagrangian costs are distortion (squared error) times this plus the multiplier (fixed point) times bits */
		constexpr std::int32_t CostScale = 16;

		/** @brief Coefficients that could be non-zero the quantizer looks back over to find the previous non-zero one */
		constexpr std::int32_t MaxSkippedCandidates = 12;

		/** @brief Outcome of quantizing one block */
		struct BlockResult
		{
			/** @brief Squared error of the block if nothing is coded */
			std::int64_t ZeroDistortion;
			/** @brief Squared error with the chosen levels */
			std::int64_t Distortion;
			/** @brief Bits of the chosen levels including `end_of_block`, without the intra DC */
			std::int32_t Bits;
			/** @brief Whether any level (the intra DC aside) is non-zero */
			bool Coded;
		};

		/**
			@brief Quantizes a block, choosing the levels by their rate-distortion cost

			Each coefficient may take its nearest level, the next smaller one, or zero, and the choice is a shortest
			path over the coefficients that can be non-zero, since the bits of a level depend on the run of zeros
			before it. This is what makes isolated small coefficients disappear when they are not worth their code.
			The intra DC is quantized separately by the caller.
		*/
		BlockResult QuantizeBlock(const std::int32_t* coefficients, bool intra, std::int32_t quantizer, std::int64_t lambda, std::int16_t* levels)
		{
			struct Candidate
			{
				std::int32_t Position;
				std::int32_t Count;
				std::int32_t Level[2];
				/** @brief Change of the squared error against leaving the coefficient zero */
				std::int64_t Gain[2];
			};
			struct Node
			{
				std::int64_t Cost;
				std::int16_t From;
			};

			const std::int32_t start = (intra ? 1 : 0);
			Candidate candidates[64];
			std::int32_t candidateCount = 0;
			BlockResult result {};
			// The reconstruction of a non-intra level of 1, which a coefficient has to exceed half of to be worth more than zero
			const std::int32_t nonIntraFirst = 3 * quantizer - 1 + ((3 * quantizer) & 1);

			for (std::int32_t i = start; i < 64; i++) {
				levels[i] = 0;
				const std::int32_t index = ZigZag[i];
				const std::int32_t value = coefficients[index];
				const std::int32_t a = std::abs(value);
				const std::int64_t zeroError = std::int64_t(a) * a;
				result.ZeroDistortion += zeroError;
				// Magnitudes only, so the reconstruction needs no sign handling
				const std::int32_t scale = (intra ? 2 * quantizer * IntraMatrix[index] : quantizer * NonIntraWeight);
				const std::int32_t first = (intra ? (scale >> 4) - 1 + ((scale >> 4) & 1) : nonIntraFirst);
				if (2 * a <= first) {
					continue;
				}

				std::int32_t level;
				if (intra) {
					const std::int32_t step = quantizer * IntraMatrix[index];
					level = (a * 8 + step / 2) / step;
				} else {
					level = (a * 8) / (quantizer * NonIntraWeight);
				}
				level = std::min(level, 255);
				auto errorOf = [&](std::int32_t l) -> std::int64_t {
					std::int32_t r = ((intra ? l : 2 * l + 1) * scale) >> 4;
					r = std::min(r - 1 + (r & 1), 2047);
					const std::int64_t d = a - r;
					return d * d;
				};
				// The estimate can be one level short of the nearest, which the reconstruction rounding decides
				if (level < 255 && errorOf(level + 1) < (level > 0 ? errorOf(level) : zeroError)) {
					level++;
				}
				if (level == 0) {
					continue;
				}

				Candidate& c = candidates[candidateCount++];
				c.Position = i;
				c.Count = 0;
				const std::int32_t sign = (value < 0 ? -1 : 1);
				for (std::int32_t l = level; l >= std::max(1, level - 1); l--) {
					c.Level[c.Count] = sign * l;
					c.Gain[c.Count] = errorOf(l) - zeroError;
					c.Count++;
				}
			}

			result.Distortion = result.ZeroDistortion;
			if (candidateCount == 0) {
				result.Bits = (intra ? EndOfBlockLength : 0);
				return result;
			}

			// Shortest path: node (candidate, option) = cost of everything up to it, with it as the last level so far.
			// What follows a candidate only depends on its position, so only the cheaper of its options can be a predecessor.
			Node nodes[64][2];
			Node cheapest[64];
			for (std::int32_t n = 0; n < candidateCount; n++) {
				const Candidate& c = candidates[n];
				for (std::int32_t k = 0; k < c.Count; k++) {
					const std::int32_t a = std::abs(c.Level[k]);
					const std::int64_t own = c.Gain[k] * CostScale;
					std::int64_t best = own + lambda * CoefficientBits(c.Position - start, a, !intra);
					std::int16_t from = -1;
					// Paths that skip more than a few possible levels in a row are never the cheapest ones
					for (std::int32_t p = std::max(0, n - MaxSkippedCandidates); p < n; p++) {
						const std::int64_t cost = cheapest[p].Cost + own + lambda * CoefficientBits(c.Position - candidates[p].Position - 1, a, false);
						if (cost < best) {
							best = cost;
							from = cheapest[p].From;
						}
					}
					nodes[n][k] = { best, from };
				}
				const std::int32_t k = (c.Count > 1 && nodes[n][1].Cost < nodes[n][0].Cost ? 1 : 0);
				cheapest[n] = { nodes[n][k].Cost, std::int16_t(n * 2 + k) };
			}

			// Leaving everything zero costs nothing against the baseline; an intra block pays its end_of_block either way
			std::int64_t bestCost = (intra ? lambda * EndOfBlockLength : 0);
			std::int32_t bestNode = -1;
			for (std::int32_t n = 0; n < candidateCount; n++) {
				for (std::int32_t k = 0; k < candidates[n].Count; k++) {
					const std::int64_t cost = nodes[n][k].Cost + lambda * EndOfBlockLength;
					if (cost < bestCost) {
						bestCost = cost;
						bestNode = n * 2 + k;
					}
				}
			}
			if (bestNode < 0) {
				result.Bits = (intra ? EndOfBlockLength : 0);
				return result;
			}

			std::int32_t bits = EndOfBlockLength;
			std::int64_t distortion = result.ZeroDistortion;
			std::int32_t node = bestNode;
			while (node >= 0) {
				const Candidate& c = candidates[node >> 1];
				const std::int32_t k = node & 1;
				levels[c.Position] = std::int16_t(c.Level[k]);
				distortion += c.Gain[k];
				const std::int32_t from = nodes[node >> 1][k].From;
				const std::int32_t previousPosition = (from >= 0 ? candidates[from >> 1].Position : start - 1);
				bits += CoefficientBits(c.Position - previousPosition - 1, std::abs(c.Level[k]), from < 0 && !intra);
				node = from;
			}
			result.Distortion = distortion;
			result.Bits = bits;
			result.Coded = true;
			return result;
		}

		// ── Pictures ─────────────────────────────────────────────────────────────────────────────

		/** @brief Planes of one picture, padded to whole macroblocks */
		struct Picture
		{
			std::vector<std::uint8_t> Y, Cb, Cr;
		};

		/** @brief Pixels of one macroblock */
		struct Macroblock
		{
			std::uint8_t Y[256];
			std::uint8_t Cb[64];
			std::uint8_t Cr[64];
		};

		/** @brief A motion vector in half pixels */
		struct MotionVector
		{
			std::int16_t X, Y;
		};

		/** @brief Predicts a block at a motion vector in half pixels, interpolating with MPEG-1's rounding */
		template<std::int32_t Size>
		void PredictBlock(const std::uint8_t* reference, std::int32_t stride, std::int32_t x, std::int32_t y,
			std::int32_t mvX, std::int32_t mvY, std::uint8_t* target)
		{
			const std::uint8_t* s = reference + std::ptrdiff_t(y + (mvY >> 1)) * stride + (x + (mvX >> 1));
			const bool halfX = (mvX & 1) != 0, halfY = (mvY & 1) != 0;
			if (!halfX && !halfY) {
				for (std::int32_t row = 0; row < Size; row++, s += stride, target += Size) {
					std::memcpy(target, s, Size);
				}
			} else if (!halfY) {
				for (std::int32_t row = 0; row < Size; row++, s += stride, target += Size) {
					for (std::int32_t i = 0; i < Size; i++) {
						target[i] = std::uint8_t((s[i] + s[i + 1] + 1) >> 1);
					}
				}
			} else if (!halfX) {
				for (std::int32_t row = 0; row < Size; row++, s += stride, target += Size) {
					for (std::int32_t i = 0; i < Size; i++) {
						target[i] = std::uint8_t((s[i] + s[i + stride] + 1) >> 1);
					}
				}
			} else {
				for (std::int32_t row = 0; row < Size; row++, s += stride, target += Size) {
					for (std::int32_t i = 0; i < Size; i++) {
						target[i] = std::uint8_t((s[i] + s[i + 1] + s[i + stride] + s[i + stride + 1] + 2) >> 2);
					}
				}
			}
		}

		std::int32_t Sad16(const std::uint8_t* a, std::int32_t aStride, const std::uint8_t* b, std::int32_t bStride)
		{
			std::int32_t sum = 0;
			for (std::int32_t row = 0; row < 16; row++, a += aStride, b += bStride) {
				for (std::int32_t i = 0; i < 16; i++) {
					sum += std::abs(std::int32_t(a[i]) - std::int32_t(b[i]));
				}
			}
			return sum;
		}

		std::int64_t SquaredError(const std::uint8_t* a, const std::uint8_t* b, std::int32_t count)
		{
			std::int64_t sum = 0;
			for (std::int32_t i = 0; i < count; i++) {
				const std::int32_t d = std::int32_t(a[i]) - std::int32_t(b[i]);
				sum += d * d;
			}
			return sum;
		}

		/** @brief Coded form of one macroblock */
		enum class MacroblockKind : std::uint8_t
		{
			Skipped,
			Intra,
			/** @brief Motion-compensated, with or without residual */
			Forward,
			/** @brief Predicted from the co-located macroblock with a residual and no motion vector */
			NoMotion
		};

		struct MacroblockCoding
		{
			MacroblockKind Kind;
			std::int32_t MvX, MvY;
			std::int32_t Cbp;
			std::int64_t Cost;
			std::int16_t Levels[6][64];
		};

		/** @brief Returns the 8x8 block @p index of a macroblock (four luma blocks in raster order, then Cb and Cr) */
		inline const std::uint8_t* BlockPixels(const Macroblock& mb, std::int32_t index, std::int32_t& stride)
		{
			if (index < 4) {
				stride = 16;
				return mb.Y + (index >> 1) * 128 + (index & 1) * 8;
			}
			stride = 8;
			return (index == 4 ? mb.Cb : mb.Cr);
		}
	}

	struct Mpeg1Encoder::State
	{
		Options Settings;
		std::int32_t MbWidth = 0, MbHeight = 0;
		std::int32_t LumaWidth = 0, LumaHeight = 0;
		std::int32_t ChromaWidth = 0, ChromaHeight = 0;
		std::int32_t FrameRateCode = 0;
		std::int32_t GopLength = 0;
		/** @brief Largest `f_code` the search range needs */
		std::int32_t MaxFCode = 1;

		/** @brief Input frames not written yet, the storage of the ones already written is kept for reuse at the end */
		std::vector<Picture> Queue;
		std::int32_t QueueCount = 0;
		/** @brief Display index of the first queued frame */
		std::int64_t QueueStart = 0;
		/** @brief Frames encoded ahead beyond the group being written, which the rate control looks at */
		std::int32_t LookaheadFrames = 0;

		Picture Reference, Current;
		/** @brief Vectors of the picture being encoded and of the previous P-picture, in half pixels */
		std::vector<MotionVector> Vectors, PreviousVectors;
		/** @brief Best vector of every macroblock for each `f_code` */
		std::vector<MotionVector> ClassVectors;
		std::vector<std::uint8_t> SourceLowRes, ReferenceLowRes;
		/** @brief Decisions for every macroblock of the picture being encoded, and the quantizer of each slice */
		std::vector<MacroblockCoding> Codings;
		std::vector<std::int32_t> RowQuantizers;

		// Rate control, see PlanQuantizer()
		/** @brief Sum of bits times quantizer (in 1/16 steps) of the groups written so far, as bits fall roughly with the quantizer */
		std::int64_t ComplexitySum = 0;
		/** @brief Bits the groups written so far were meant to take, and actually took */
		std::int64_t TargetBits = 0;
		std::int64_t ActualBits = 0;
		/** @brief Complexity per frame of the last group written, the guess for a group when there is none ahead */
		std::int64_t LastComplexityPerFrame = 0;

		Statistics Stats;
		bool Started = false;

		/** @brief Outcome of encoding one picture */
		struct PictureStats
		{
			std::int32_t IntraMacroblocks = 0;
			std::int64_t QuantizerSum = 0;
			std::uint64_t LumaSquaredError = 0;
		};

		/** @brief One attempt at encoding a group of pictures */
		struct GopPass
		{
			BitWriter Stream;
			std::int32_t Count = 0;
			std::int32_t Quantizer16 = 0;
			std::int64_t QuantizerSum = 0;
			std::uint64_t LumaSquaredError = 0;

			std::int64_t GetComplexity() const
			{
				return Stream.GetBitCount() * Quantizer16;
			}
		};

		/** @brief Groups encoded ahead, in order from the one starting at the first queued frame */
		std::vector<GopPass> Planned;

		void AllocatePicture(Picture& p) const
		{
			p.Y.assign(std::size_t(LumaWidth) * LumaHeight, 0);
			p.Cb.assign(std::size_t(ChromaWidth) * ChromaHeight, 128);
			p.Cr.assign(std::size_t(ChromaWidth) * ChromaHeight, 128);
		}

		void LoadMacroblock(const Picture& p, std::int32_t mbX, std::int32_t mbY, Macroblock& mb) const
		{
			const std::uint8_t* y = &p.Y[std::size_t(mbY * 16) * LumaWidth + mbX * 16];
			for (std::int32_t row = 0; row < 16; row++) {
				std::memcpy(&mb.Y[row * 16], y + std::size_t(row) * LumaWidth, 16);
			}
			const std::size_t c = std::size_t(mbY * 8) * ChromaWidth + mbX * 8;
			for (std::int32_t row = 0; row < 8; row++) {
				std::memcpy(&mb.Cb[row * 8], &p.Cb[c + std::size_t(row) * ChromaWidth], 8);
				std::memcpy(&mb.Cr[row * 8], &p.Cr[c + std::size_t(row) * ChromaWidth], 8);
			}
		}

		void StoreMacroblock(Picture& p, std::int32_t mbX, std::int32_t mbY, const Macroblock& mb) const
		{
			std::uint8_t* y = &p.Y[std::size_t(mbY * 16) * LumaWidth + mbX * 16];
			for (std::int32_t row = 0; row < 16; row++) {
				std::memcpy(y + std::size_t(row) * LumaWidth, &mb.Y[row * 16], 16);
			}
			const std::size_t c = std::size_t(mbY * 8) * ChromaWidth + mbX * 8;
			for (std::int32_t row = 0; row < 8; row++) {
				std::memcpy(&p.Cb[c + std::size_t(row) * ChromaWidth], &mb.Cb[row * 8], 8);
				std::memcpy(&p.Cr[c + std::size_t(row) * ChromaWidth], &mb.Cr[row * 8], 8);
			}
		}

		void PredictMacroblock(const Picture& reference, std::int32_t mbX, std::int32_t mbY, std::int32_t mvX, std::int32_t mvY,
			Macroblock& mb) const
		{
			PredictBlock<16>(reference.Y.data(), LumaWidth, mbX * 16, mbY * 16, mvX, mvY, mb.Y);
			// The standard truncates the halved vector towards zero
			const std::int32_t cX = mvX / 2, cY = mvY / 2;
			PredictBlock<8>(reference.Cb.data(), ChromaWidth, mbX * 8, mbY * 8, cX, cY, mb.Cb);
			PredictBlock<8>(reference.Cr.data(), ChromaWidth, mbX * 8, mbY * 8, cX, cY, mb.Cr);
		}

		/** @brief Whether a vector keeps the reference block inside the picture, which MPEG-1 requires */
		bool IsVectorInside(std::int32_t mbX, std::int32_t mbY, std::int32_t mvX, std::int32_t mvY) const
		{
			const std::int32_t x = mbX * 16 + (mvX >> 1);
			const std::int32_t y = mbY * 16 + (mvY >> 1);
			return (x >= 0 && y >= 0 && x + 16 + (mvX & 1) <= LumaWidth && y + 16 + (mvY & 1) <= LumaHeight);
		}

		/**
			@brief Whether libdragon's decoder predicts the chroma of a vector the same as the standard

			Its RSP path halves the luma vector for chroma with an arithmetic shift, which differs from the
			standard's truncation for negative odd components. Such a vector is only safe where both roundings
			happen to give the same pixels, which is common in the flat areas of a cartoon.
		*/
		bool IsChromaSafe(const Picture& reference, std::int32_t mbX, std::int32_t mbY, std::int32_t mvX, std::int32_t mvY) const
		{
			if (!((mvX < 0 && (mvX & 1) != 0) || (mvY < 0 && (mvY & 1) != 0))) {
				return true;
			}
			Macroblock standard, rsp;
			const std::int32_t sX = mvX / 2, sY = mvY / 2, rX = mvX >> 1, rY = mvY >> 1;
			PredictBlock<8>(reference.Cb.data(), ChromaWidth, mbX * 8, mbY * 8, sX, sY, standard.Cb);
			PredictBlock<8>(reference.Cb.data(), ChromaWidth, mbX * 8, mbY * 8, rX, rY, rsp.Cb);
			if (std::memcmp(standard.Cb, rsp.Cb, 64) != 0) {
				return false;
			}
			PredictBlock<8>(reference.Cr.data(), ChromaWidth, mbX * 8, mbY * 8, sX, sY, standard.Cr);
			PredictBlock<8>(reference.Cr.data(), ChromaWidth, mbX * 8, mbY * 8, rX, rY, rsp.Cr);
			return std::memcmp(standard.Cr, rsp.Cr, 64) == 0;
		}

		std::int32_t VectorLimit(std::int32_t fCode) const
		{
			return 16 << (fCode - 1);
		}

		std::int32_t EstimateMotion(const Picture& source, const Picture& reference, std::int32_t quantizer);

		void EncodeIntraMacroblock(const Macroblock& source, std::int32_t quantizer, std::int64_t lambda, std::int32_t* dcPredictor,
			MacroblockCoding& coding) const;
		void EncodeInterMacroblock(const Macroblock& source, const Macroblock& prediction, std::int32_t quantizer, std::int64_t lambda,
			MacroblockCoding& coding, std::int64_t& distortion, std::int32_t& bits) const;
		void Reconstruct(const MacroblockCoding& coding, const Macroblock* prediction, std::int32_t quantizer, Macroblock& output) const;
		void WriteMacroblockBlocks(BitWriter& bw, const MacroblockCoding& coding, std::int32_t* dcPredictor) const;

		void EncodePicture(const Picture& source, std::int32_t type, std::int32_t temporalReference, std::int32_t quantizer16,
			std::int32_t ditherSeed, BitWriter& bw, PictureStats& stats);
		void WritePicture(BitWriter& bw, std::int32_t type, std::int32_t temporalReference, std::int32_t fCode) const;
		void EncodeGop(std::int32_t first, std::int32_t count, std::int32_t quantizer16, bool findEnd, bool lastFrames, GopPass& pass);
		std::int32_t PlanQuantizer(std::int64_t windowComplexity, std::int64_t windowTarget) const;
		std::int64_t ShareOf(std::int32_t frames) const;
		void PlanAhead(bool flush);
		void WriteFirstGop(std::vector<std::uint8_t>& output);
	};

	namespace
	{
		/**
			@brief Quantizer of the I-picture of a group, relative to its P-pictures (in sixteenths)

			Much of a cartoon stands still, and what stands still keeps the quality of the I-picture for the whole group.
		*/
		constexpr std::int32_t IntraQuantizerRatio = 8;
		/** @brief Lagrange multiplier per squared quantizer, in sixteenths (in squared error per bit) */
		constexpr std::int32_t LambdaNum = 12;
		/** @brief Multiplier of the motion search per quantizer, in sixteenths (in absolute difference per bit) */
		constexpr std::int32_t MotionLambdaNum = 16;

		/** @brief Finest quantizer of the P-pictures (in 1/16 steps), beyond which more bits hardly show */
		constexpr std::int32_t MinQuantizer16 = 2 * 16;
		/** @brief Finest quantizer of the I-pictures, which keeps the largest pictures within what the console decodes in time */
		constexpr std::int32_t MinIntraQuantizer16 = 24;
		/** @brief Quantizer of the first attempt at the first group */
		constexpr std::int32_t InitialQuantizer16 = 5 * 16;
		/** @brief Encodings of one group at most when it is written, the one made ahead included */
		constexpr std::int32_t MaxAttempts = 3;
		/** @brief A group is encoded again when the plan moved by more than this part (1/n) of its quantizer */
		constexpr std::int32_t QuantizerTolerance = 16;
		/** @brief How far ahead the rate control looks, and the most memory the frames it holds for that may take */
		constexpr std::int32_t LookaheadSeconds = 10;
		constexpr std::int64_t LookaheadMemory = 64 * 1024 * 1024;

		/**
			@brief Longest group of pictures

			MPEG-1 recommends coding every macroblock as intra at least once in 132 predicted pictures, which bounds how
			far the inverse transform of a decoder can drift from the encoder's.
		*/
		constexpr std::int32_t MaxGopLength = 132;

		/** @brief Pictures a scene cut has to be away from the start of a group to start one of its own */
		inline std::int32_t MinCutDistance(std::int32_t gopLength)
		{
			return std::max(2, gopLength / 16);
		}

		/** @brief Pictures a group may run longer than its length to end at a scene cut instead of just before one */
		inline std::int32_t GopExtension(std::int32_t gopLength)
		{
			return std::min(gopLength / 4, MaxGopLength - gopLength);
		}

		/** @brief Share of intra macroblocks (in sixteenths) that makes a P-picture a scene cut */
		constexpr std::int32_t CutIntraFraction = 9;

		/** @brief Lagrange multiplier for a quantizer, in @ref CostScale units per bit */
		inline std::int64_t LambdaFor(std::int32_t quantizer)
		{
			return std::int64_t(quantizer) * quantizer * LambdaNum;
		}

		/** @brief Absolute difference of a macroblock against the same place in the reference that ends its motion search */
		constexpr std::int32_t StillSad = 128;
		/** @brief Cost of the best candidate vector above which the coarse search runs */
		constexpr std::int32_t CoarseSearchSad = 1024;
		/** @brief Intra coding is tried in P-pictures where the residual exceeds this part (in sixteenths) of the detail */
		constexpr std::int32_t IntraTestFactor = 8;

		/** @brief Multiplier for the motion search, which weighs absolute differences against motion vector bits */
		inline std::int32_t MotionLambdaFor(std::int32_t quantizer)
		{
			return quantizer * MotionLambdaNum / 16;
		}

		/** @brief Quantizer of slice @p row, dithering the fractional part of @p quantizer16 over the rows */
		inline std::int32_t RowQuantizer(std::int32_t quantizer16, std::int32_t row, std::int32_t seed)
		{
			const std::int32_t base = quantizer16 >> 4;
			const std::int32_t fraction = quantizer16 & 15;
			// An ordered pattern that moves from picture to picture, so no row is always the coarse one
			const std::int32_t threshold = ((row + seed) * 7) & 15;
			return std::clamp(base + (fraction > threshold ? 1 : 0), 1, 31);
		}
	}

	std::int32_t Mpeg1Encoder::State::EstimateMotion(const Picture& source, const Picture& reference, std::int32_t quantizer)
	{
		const std::int32_t lambda = MotionLambdaFor(quantizer);
		const std::int32_t range = 2 * Settings.SearchRange;
		const std::int32_t classes = MaxFCode;
		const std::int32_t macroblocks = MbWidth * MbHeight;
		ClassVectors.resize(std::size_t(macroblocks) * classes);
		Vectors.resize(std::size_t(macroblocks));

		// Quarter-resolution pictures for a coarse full search, which finds fast motion the predictors know nothing of
		const std::int32_t lowWidth = LumaWidth / 4, lowHeight = LumaHeight / 4;
		auto downscale = [&](const Picture& p, std::vector<std::uint8_t>& low) {
			low.resize(std::size_t(lowWidth) * lowHeight);
			for (std::int32_t y = 0; y < lowHeight; y++) {
				for (std::int32_t x = 0; x < lowWidth; x++) {
					std::int32_t sum = 0;
					for (std::int32_t dy = 0; dy < 4; dy++) {
						const std::uint8_t* row = &p.Y[std::size_t(y * 4 + dy) * LumaWidth + x * 4];
						sum += row[0] + row[1] + row[2] + row[3];
					}
					low[std::size_t(y) * lowWidth + x] = std::uint8_t((sum + 8) >> 4);
				}
			}
		};
		downscale(source, SourceLowRes);
		downscale(reference, ReferenceLowRes);

		struct ClassBest
		{
			std::int32_t Cost;
			std::int16_t X, Y;
		};
		std::int64_t classTotals[8] = {};
		MotionVector predictors[8];

		for (std::int32_t mbY = 0; mbY < MbHeight; mbY++) {
			for (std::int32_t c = 1; c <= classes; c++) {
				predictors[c] = { 0, 0 };
			}
			for (std::int32_t mbX = 0; mbX < MbWidth; mbX++) {
				const std::size_t address = std::size_t(mbY) * MbWidth + mbX;
				const std::uint8_t* src = &source.Y[std::size_t(mbY * 16) * LumaWidth + mbX * 16];
				ClassBest best[8];
				for (std::int32_t c = 1; c <= classes; c++) {
					best[c] = { INT32_MAX, 0, 0 };
				}

				// Every vector tried counts for each f_code that can express it, with that f_code's bits
				auto consider = [&](std::int32_t mvX, std::int32_t mvY, std::int32_t sad) {
					std::int32_t needed = 1;
					while (mvX < -VectorLimit(needed) || mvX >= VectorLimit(needed) || mvY < -VectorLimit(needed) || mvY >= VectorLimit(needed)) {
						needed++;
					}
					for (std::int32_t c = needed; c <= classes; c++) {
						const std::int32_t cost = sad + lambda * (MotionComponentBits(WrapMotionDelta(mvX - predictors[c].X, c), c) +
							MotionComponentBits(WrapMotionDelta(mvY - predictors[c].Y, c), c));
						if (cost < best[c].Cost) {
							best[c] = { cost, std::int16_t(mvX), std::int16_t(mvY) };
						}
					}
				};
				auto isAllowed = [&](std::int32_t mvX, std::int32_t mvY) {
					return (std::abs(mvX) <= range && std::abs(mvY) <= range && mvX < VectorLimit(classes) && mvY < VectorLimit(classes) &&
						IsVectorInside(mbX, mbY, mvX, mvY));
				};
				auto fullSad = [&](std::int32_t x, std::int32_t y) {
					return Sad16(src, LumaWidth, &reference.Y[std::size_t(mbY * 16 + y) * LumaWidth + mbX * 16 + x], LumaWidth);
				};
				// The search itself is steered by the cost with the largest f_code
				auto topCost = [&](std::int32_t x, std::int32_t y, std::int32_t sad) {
					return sad + lambda * (MotionComponentBits(WrapMotionDelta(x * 2 - predictors[classes].X, classes), classes) +
						MotionComponentBits(WrapMotionDelta(y * 2 - predictors[classes].Y, classes), classes));
				};

				// Whole-pixel candidates from the neighbours and the previous picture, tried after the origin
				std::int32_t candidates[5][2];
				std::int32_t candidateCount = 0;
				auto addCandidate = [&](std::int32_t x, std::int32_t y) {
					candidates[candidateCount][0] = x;
					candidates[candidateCount][1] = y;
					candidateCount++;
				};
				if (mbX > 0) {
					const MotionVector& v = Vectors[address - 1];
					addCandidate(v.X >> 1, v.Y >> 1);
				}
				if (mbY > 0) {
					const MotionVector& v = Vectors[address - MbWidth];
					addCandidate(v.X >> 1, v.Y >> 1);
					if (mbX + 1 < MbWidth) {
						const MotionVector& w = Vectors[address - MbWidth + 1];
						addCandidate(w.X >> 1, w.Y >> 1);
					}
				}
				if (!PreviousVectors.empty()) {
					addCandidate(PreviousVectors[address].X >> 1, PreviousVectors[address].Y >> 1);
					if (mbY + 1 < MbHeight) {
						addCandidate(PreviousVectors[address + MbWidth].X >> 1, PreviousVectors[address + MbWidth].Y >> 1);
					}
				}
				std::int32_t bestX = 0, bestY = 0;
				std::int32_t sad = fullSad(0, 0);
				consider(0, 0, sad);
				std::int32_t bestCost = topCost(0, 0, sad);
				// Where nothing moved there is nothing to search for
				const bool settled = (sad <= StillSad);
				for (std::int32_t i = 0; i < candidateCount && !settled; i++) {
					const std::int32_t x = candidates[i][0], y = candidates[i][1];
					if ((x == bestX && y == bestY) || !isAllowed(x * 2, y * 2)) {
						continue;
					}
					sad = fullSad(x, y);
					consider(x * 2, y * 2, sad);
					const std::int32_t cost = topCost(x, y, sad);
					if (cost < bestCost) {
						bestCost = cost;
						bestX = x;
						bestY = y;
					}
				}
				// The coarse search is only needed when none of the candidates fits well
				if (!settled && bestCost > CoarseSearchSad) {
					const std::int32_t lowRange = Settings.SearchRange / 4;
					const std::uint8_t* lowSrc = &SourceLowRes[std::size_t(mbY * 4) * lowWidth + mbX * 4];
					std::int32_t lowBest = INT32_MAX, lowX = 0, lowY = 0;
					for (std::int32_t dy = -lowRange; dy <= lowRange; dy++) {
						const std::int32_t y = mbY * 4 + dy;
						if (y < 0 || y + 4 > lowHeight) {
							continue;
						}
						for (std::int32_t dx = -lowRange; dx <= lowRange; dx++) {
							const std::int32_t x = mbX * 4 + dx;
							if (x < 0 || x + 4 > lowWidth) {
								continue;
							}
							const std::uint8_t* lowRef = &ReferenceLowRes[std::size_t(y) * lowWidth + x];
							std::int32_t lowSad = std::abs(dx) + std::abs(dy);
							for (std::int32_t row = 0; row < 4; row++) {
								for (std::int32_t i = 0; i < 4; i++) {
									lowSad += std::abs(std::int32_t(lowSrc[row * lowWidth + i]) - lowRef[row * lowWidth + i]);
								}
							}
							if (lowSad < lowBest) {
								lowBest = lowSad;
								lowX = dx;
								lowY = dy;
							}
						}
					}
					const std::int32_t x = lowX * 4, y = lowY * 4;
					if ((x != bestX || y != bestY) && isAllowed(x * 2, y * 2)) {
						sad = fullSad(x, y);
						consider(x * 2, y * 2, sad);
						const std::int32_t cost = topCost(x, y, sad);
						if (cost < bestCost) {
							bestCost = cost;
							bestX = x;
							bestY = y;
						}
					}
				}

				// Small diamond until it settles, then the corners once
				for (std::int32_t step = 0; step < 32 && !settled; step++) {
					static constexpr std::int32_t Diamond[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
					std::int32_t nextX = bestX, nextY = bestY;
					for (const auto& d : Diamond) {
						const std::int32_t x = bestX + d[0], y = bestY + d[1];
						if (!isAllowed(x * 2, y * 2)) {
							continue;
						}
						sad = fullSad(x, y);
						consider(x * 2, y * 2, sad);
						const std::int32_t cost = topCost(x, y, sad);
						if (cost < bestCost) {
							bestCost = cost;
							nextX = x;
							nextY = y;
						}
					}
					if (nextX == bestX && nextY == bestY) {
						break;
					}
					bestX = nextX;
					bestY = nextY;
				}
				if (!settled) {
					static constexpr std::int32_t Corners[4][2] = { { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 } };
					const std::int32_t centerX = bestX, centerY = bestY;
					for (const auto& d : Corners) {
						const std::int32_t x = centerX + d[0], y = centerY + d[1];
						if (!isAllowed(x * 2, y * 2)) {
							continue;
						}
						sad = fullSad(x, y);
						consider(x * 2, y * 2, sad);
					}
				}

				// Half-pixel refinement around the best whole-pixel vector of every f_code, skipping repeated centres
				std::int32_t refined[8][2];
				std::int32_t refinedCount = 0;
				for (std::int32_t c = classes; c >= 1 && !settled; c--) {
					if (best[c].Cost == INT32_MAX || ((best[c].X | best[c].Y) & 1) != 0) {
						continue;
					}
					const std::int32_t centerX = best[c].X, centerY = best[c].Y;
					bool done = false;
					for (std::int32_t i = 0; i < refinedCount; i++) {
						done |= (refined[i][0] == centerX && refined[i][1] == centerY);
					}
					if (done) {
						continue;
					}
					refined[refinedCount][0] = centerX;
					refined[refinedCount][1] = centerY;
					refinedCount++;
					Macroblock prediction;
					for (std::int32_t dy = -1; dy <= 1; dy++) {
						for (std::int32_t dx = -1; dx <= 1; dx++) {
							const std::int32_t mvX = centerX + dx, mvY = centerY + dy;
							if ((dx == 0 && dy == 0) || !isAllowed(mvX, mvY) || !IsChromaSafe(reference, mbX, mbY, mvX, mvY)) {
								continue;
							}
							PredictBlock<16>(reference.Y.data(), LumaWidth, mbX * 16, mbY * 16, mvX, mvY, prediction.Y);
							consider(mvX, mvY, Sad16(src, LumaWidth, prediction.Y, 16));
						}
					}
				}

				for (std::int32_t c = 1; c <= classes; c++) {
					ClassVectors[address * classes + (c - 1)] = { best[c].X, best[c].Y };
					classTotals[c] += best[c].Cost;
					predictors[c] = { best[c].X, best[c].Y };
				}
				Vectors[address] = { best[classes].X, best[classes].Y };
			}
		}

		// The f_code applies to the whole picture: a longer one lets a few macroblocks follow fast motion, but costs
		// every other vector more bits
		std::int32_t fCode = 1;
		for (std::int32_t c = 2; c <= classes; c++) {
			if (classTotals[c] < classTotals[fCode]) {
				fCode = c;
			}
		}
		for (std::int32_t i = 0; i < macroblocks; i++) {
			Vectors[std::size_t(i)] = ClassVectors[std::size_t(i) * classes + (fCode - 1)];
		}
		return fCode;
	}

	void Mpeg1Encoder::State::EncodeIntraMacroblock(const Macroblock& source, std::int32_t quantizer, std::int64_t lambda,
		std::int32_t* dcPredictor, MacroblockCoding& coding) const
	{
		coding.Kind = MacroblockKind::Intra;
		coding.MvX = coding.MvY = 0;
		coding.Cbp = 63;
		std::int64_t cost = 0;
		std::int32_t predictors[3] = { dcPredictor[0], dcPredictor[1], dcPredictor[2] };
		for (std::int32_t b = 0; b < 6; b++) {
			std::int32_t stride;
			const std::uint8_t* pixels = BlockPixels(source, b, stride);
			std::int16_t block[64];
			for (std::int32_t y = 0; y < 8; y++) {
				for (std::int32_t x = 0; x < 8; x++) {
					block[y * 8 + x] = pixels[y * stride + x];
				}
			}
			std::int32_t coefficients[64];
			ForwardDct(block, coefficients);
			const BlockResult r = QuantizeBlock(coefficients, true, quantizer, lambda, coding.Levels[b]);
			const std::int32_t dc = std::clamp((coefficients[0] + 4) >> 3, 0, 255);
			coding.Levels[b][0] = std::int16_t(dc);
			const std::int32_t plane = (b < 4 ? 0 : b - 3);
			const std::int32_t dcError = coefficients[0] - dc * 8;
			cost += (r.Distortion + std::int64_t(dcError) * dcError) * CostScale +
				lambda * (r.Bits + DcBits(dc - predictors[plane], plane != 0));
			predictors[plane] = dc;
		}
		coding.Cost = cost;
	}

	void Mpeg1Encoder::State::EncodeInterMacroblock(const Macroblock& source, const Macroblock& prediction, std::int32_t quantizer,
		std::int64_t lambda, MacroblockCoding& coding, std::int64_t& distortion, std::int32_t& bits) const
	{
		coding.Cbp = 0;
		distortion = 0;
		bits = 0;
		for (std::int32_t b = 0; b < 6; b++) {
			std::int32_t stride;
			const std::uint8_t* pixels = BlockPixels(source, b, stride);
			const std::uint8_t* predicted = BlockPixels(prediction, b, stride);
			std::int16_t block[64];
			bool empty = true;
			for (std::int32_t y = 0; y < 8; y++) {
				for (std::int32_t x = 0; x < 8; x++) {
					const std::int32_t d = std::int32_t(pixels[y * stride + x]) - predicted[y * stride + x];
					block[y * 8 + x] = std::int16_t(d);
					empty &= (d == 0);
				}
			}
			if (empty) {
				std::memset(coding.Levels[b], 0, sizeof(coding.Levels[b]));
				continue;
			}
			std::int32_t coefficients[64];
			ForwardDct(block, coefficients);
			const BlockResult r = QuantizeBlock(coefficients, false, quantizer, lambda, coding.Levels[b]);
			distortion += r.Distortion;
			if (r.Coded) {
				coding.Cbp |= (32 >> b);
				bits += r.Bits;
			}
		}
		if (coding.Cbp != 0) {
			bits += CodedBlockPatternVlc[coding.Cbp].Length;
		}
	}

	void Mpeg1Encoder::State::Reconstruct(const MacroblockCoding& coding, const Macroblock* prediction, std::int32_t quantizer,
		Macroblock& output) const
	{
		const bool intra = (coding.Kind == MacroblockKind::Intra);
		for (std::int32_t b = 0; b < 6; b++) {
			std::int32_t stride;
			std::uint8_t* target = const_cast<std::uint8_t*>(BlockPixels(output, b, stride));
			const std::uint8_t* predicted = (prediction != nullptr ? BlockPixels(*prediction, b, stride) : nullptr);
			if (!intra && (coding.Cbp & (32 >> b)) == 0) {
				for (std::int32_t y = 0; y < 8; y++) {
					std::memcpy(target + y * stride, predicted + y * stride, 8);
				}
				continue;
			}
			std::int32_t coefficients[64], samples[64];
			Dequantize(coding.Levels[b], intra, quantizer, coefficients);
			InverseDct(coefficients, samples);
			for (std::int32_t y = 0; y < 8; y++) {
				for (std::int32_t x = 0; x < 8; x++) {
					const std::int32_t base = (intra ? 0 : predicted[y * stride + x]);
					target[y * stride + x] = std::uint8_t(std::clamp(base + samples[y * 8 + x], 0, 255));
				}
			}
		}
	}

	void Mpeg1Encoder::State::WriteMacroblockBlocks(BitWriter& bw, const MacroblockCoding& coding, std::int32_t* dcPredictor) const
	{
		if (coding.Kind == MacroblockKind::Intra) {
			for (std::int32_t b = 0; b < 6; b++) {
				const std::int32_t plane = (b < 4 ? 0 : b - 3);
				WriteDc(bw, coding.Levels[b][0] - dcPredictor[plane], plane != 0);
				dcPredictor[plane] = coding.Levels[b][0];
				WriteCoefficients(bw, coding.Levels[b], 1);
			}
		} else {
			for (std::int32_t b = 0; b < 6; b++) {
				if ((coding.Cbp & (32 >> b)) != 0) {
					WriteCoefficients(bw, coding.Levels[b], 0);
				}
			}
		}
	}

	void Mpeg1Encoder::State::EncodePicture(const Picture& source, std::int32_t type, std::int32_t temporalReference,
		std::int32_t quantizer16, std::int32_t ditherSeed, BitWriter& bw, PictureStats& stats)
	{
		const bool predicted = (type == PictureTypeP);
		std::int32_t fCode = 1;
		if (predicted) {
			fCode = EstimateMotion(source, Reference, RowQuantizer(quantizer16, MbHeight / 2, ditherSeed));
		}

		// Every macroblock is decided and reconstructed before anything is written, so the picture header can carry
		// the shortest f_code the vectors finally chosen need
		Codings.resize(std::size_t(MbWidth) * MbHeight);
		RowQuantizers.resize(std::size_t(MbHeight));
		Macroblock src, zeroPrediction, motionPrediction, output;
		MacroblockCoding trial;
		for (std::int32_t mbY = 0; mbY < MbHeight; mbY++) {
			const std::int32_t quantizer = RowQuantizer(quantizer16, mbY, ditherSeed);
			const std::int64_t lambda = LambdaFor(quantizer);
			RowQuantizers[std::size_t(mbY)] = quantizer;
			stats.QuantizerSum += quantizer;

			std::int32_t previousAddress = mbY * MbWidth - 1;
			std::int32_t predX = 0, predY = 0;
			std::int32_t dcPredictor[3] = { 128, 128, 128 };

			for (std::int32_t mbX = 0; mbX < MbWidth; mbX++) {
				const std::int32_t address = mbY * MbWidth + mbX;
				MacroblockCoding& best = Codings[std::size_t(address)];
				LoadMacroblock(source, mbX, mbY, src);
				const Macroblock* prediction = &zeroPrediction;

				if (!predicted) {
					EncodeIntraMacroblock(src, quantizer, lambda, dcPredictor, best);
				} else {
					const std::int32_t incrementBits = AddressIncrementBits(address - previousAddress);
					// Neither the first nor the last macroblock of a slice may be skipped
					const bool canSkip = (mbX > 0 && mbX + 1 < MbWidth);
					PredictMacroblock(Reference, mbX, mbY, 0, 0, zeroPrediction);
					const std::int64_t zeroError = SquaredError(src.Y, zeroPrediction.Y, 256) +
						SquaredError(src.Cb, zeroPrediction.Cb, 64) + SquaredError(src.Cr, zeroPrediction.Cr, 64);

					best.Kind = MacroblockKind::Skipped;
					best.Cost = (canSkip ? zeroError * CostScale : INT64_MAX);
					best.MvX = best.MvY = 0;
					best.Cbp = 0;

					// Each mode below is only tried when the fewest bits it could possibly take still beat the best
					// cost so far, which leaves the decisions as they are but saves most of the work in static areas.
					// No motion: the co-located macroblock, with a residual or (where skipping is not allowed) a zero vector
					if (best.Cost > lambda * (incrementBits + 5)) {
						std::int64_t distortion;
						std::int32_t bits;
						EncodeInterMacroblock(src, zeroPrediction, quantizer, lambda, trial, distortion, bits);
						trial.MvX = trial.MvY = 0;
						if (trial.Cbp != 0) {
							trial.Kind = MacroblockKind::NoMotion;
							trial.Cost = distortion * CostScale + lambda * (incrementBits + 2 + bits);
						} else {
							trial.Kind = MacroblockKind::Forward;
							trial.Cost = zeroError * CostScale + lambda * (incrementBits + 3 +
								MotionComponentBits(WrapMotionDelta(-predX, fCode), fCode) +
								MotionComponentBits(WrapMotionDelta(-predY, fCode), fCode));
						}
						if (trial.Cost < best.Cost) {
							std::swap(best, trial);
						}
					}

					const MotionVector mv = Vectors[std::size_t(address)];
					const std::int32_t mvBits = MotionComponentBits(WrapMotionDelta(mv.X - predX, fCode), fCode) +
						MotionComponentBits(WrapMotionDelta(mv.Y - predY, fCode), fCode);
					if ((mv.X != 0 || mv.Y != 0) && best.Cost > lambda * (incrementBits + 3 + mvBits)) {
						PredictMacroblock(Reference, mbX, mbY, mv.X, mv.Y, motionPrediction);
						std::int64_t distortion;
						std::int32_t bits;
						EncodeInterMacroblock(src, motionPrediction, quantizer, lambda, trial, distortion, bits);
						trial.Kind = MacroblockKind::Forward;
						trial.MvX = mv.X;
						trial.MvY = mv.Y;
						if (trial.Cbp != 0) {
							trial.Cost = distortion * CostScale + lambda * (incrementBits + 1 + mvBits + bits);
						} else {
							const std::int64_t error = SquaredError(src.Y, motionPrediction.Y, 256) +
								SquaredError(src.Cb, motionPrediction.Cb, 64) + SquaredError(src.Cr, motionPrediction.Cr, 64);
							trial.Cost = error * CostScale + lambda * (incrementBits + 3 + mvBits);
						}
						if (trial.Cost < best.Cost) {
							std::swap(best, trial);
							prediction = &motionPrediction;
						}
					}

					// An intra macroblock takes at least its type, six DC sizes and six end_of_block codes - and it is
					// not worth trying where the prediction left less to code than the macroblock's own detail
					bool tryIntra = (best.Cost > lambda * (incrementBits + 5 + 4 * 3 + 2 * 2 + 6 * EndOfBlockLength));
					if (tryIntra) {
						const Macroblock& basis = (best.Kind == MacroblockKind::Forward ? *prediction : zeroPrediction);
						std::int32_t residual = 0, detail = 0;
						for (std::int32_t b = 0; b < 6; b++) {
							std::int32_t stride;
							const std::uint8_t* pixels = BlockPixels(src, b, stride);
							const std::uint8_t* pred = BlockPixels(basis, b, stride);
							std::int32_t sum = 0;
							for (std::int32_t y = 0; y < 8; y++) {
								for (std::int32_t x = 0; x < 8; x++) {
									sum += pixels[y * stride + x];
									residual += std::abs(std::int32_t(pixels[y * stride + x]) - pred[y * stride + x]);
								}
							}
							const std::int32_t mean = (sum + 32) >> 6;
							for (std::int32_t y = 0; y < 8; y++) {
								for (std::int32_t x = 0; x < 8; x++) {
									detail += std::abs(std::int32_t(pixels[y * stride + x]) - mean);
								}
							}
						}
						tryIntra = (residual * 16 > detail * IntraTestFactor);
					}
					if (tryIntra) {
						EncodeIntraMacroblock(src, quantizer, lambda, dcPredictor, trial);
						trial.Cost += lambda * (incrementBits + 5);
						if (trial.Cost < best.Cost) {
							std::swap(best, trial);
						}
					}
				}

				switch (best.Kind) {
					case MacroblockKind::Skipped:
						output = zeroPrediction;
						break;
					case MacroblockKind::Intra:
						Reconstruct(best, nullptr, quantizer, output);
						stats.IntraMacroblocks++;
						break;
					case MacroblockKind::NoMotion:
						Reconstruct(best, &zeroPrediction, quantizer, output);
						break;
					case MacroblockKind::Forward:
						Reconstruct(best, prediction, quantizer, output);
						break;
				}
				StoreMacroblock(Current, mbX, mbY, output);

				// The predictors follow the decoder's rules
				if (best.Kind != MacroblockKind::Skipped) {
					previousAddress = address;
				}
				if (best.Kind == MacroblockKind::Forward) {
					predX = best.MvX;
					predY = best.MvY;
				} else {
					predX = predY = 0;
				}
				if (best.Kind == MacroblockKind::Intra) {
					for (std::int32_t b = 0; b < 6; b++) {
						dcPredictor[b < 4 ? 0 : b - 3] = best.Levels[b][0];
					}
				} else {
					dcPredictor[0] = dcPredictor[1] = dcPredictor[2] = 128;
				}
			}
		}

		if (predicted) {
			// The vectors the motion search kept for macroblocks that ended up skipped or intra no longer count
			fCode = 1;
			for (const MacroblockCoding& coding : Codings) {
				if (coding.Kind == MacroblockKind::Forward) {
					while (coding.MvX < -VectorLimit(fCode) || coding.MvX >= VectorLimit(fCode) ||
						coding.MvY < -VectorLimit(fCode) || coding.MvY >= VectorLimit(fCode)) {
						fCode++;
					}
				}
			}
			PreviousVectors = Vectors;
		}
		WritePicture(bw, type, temporalReference, fCode);

		for (std::int32_t y = 0; y < Settings.Height; y++) {
			const std::uint8_t* a = &source.Y[std::size_t(y) * LumaWidth];
			const std::uint8_t* b = &Current.Y[std::size_t(y) * LumaWidth];
			stats.LumaSquaredError += std::uint64_t(SquaredError(a, b, Settings.Width));
		}
	}

	void Mpeg1Encoder::State::WritePicture(BitWriter& bw, std::int32_t type, std::int32_t temporalReference, std::int32_t fCode) const
	{
		const bool predicted = (type == PictureTypeP);
		bw.StartCode(PictureStartCode);
		bw.Put(std::uint32_t(temporalReference & 1023), 10);
		bw.Put(std::uint32_t(type), 3);
		bw.Put(0xFFFF, 16);		// vbv_delay
		if (predicted) {
			bw.Put(0, 1);		// full_pel_forward_vector
			bw.Put(std::uint32_t(fCode), 3);
		}
		bw.Put(0, 1);			// extra_bit_picture

		for (std::int32_t mbY = 0; mbY < MbHeight; mbY++) {
			bw.StartCode(std::uint8_t(mbY + 1));
			bw.Put(std::uint32_t(RowQuantizers[std::size_t(mbY)]), 5);
			bw.Put(0, 1);		// extra_bit_slice

			std::int32_t previousAddress = mbY * MbWidth - 1;
			std::int32_t predX = 0, predY = 0;
			std::int32_t dcPredictor[3] = { 128, 128, 128 };
			for (std::int32_t mbX = 0; mbX < MbWidth; mbX++) {
				const std::int32_t address = mbY * MbWidth + mbX;
				const MacroblockCoding& coding = Codings[std::size_t(address)];
				if (coding.Kind == MacroblockKind::Skipped) {
					// Skipped macroblocks reset the predictors, as the decoder does
					predX = predY = 0;
					dcPredictor[0] = dcPredictor[1] = dcPredictor[2] = 128;
					continue;
				}

				std::int32_t increment = address - previousAddress;
				while (increment > 33) {
					bw.Put(AddressEscapeVlc);
					increment -= 33;
				}
				bw.Put(AddressIncrementVlc[increment]);
				previousAddress = address;

				switch (coding.Kind) {
					case MacroblockKind::Intra:
						if (predicted) {
							bw.Put(0x3, 5);
						} else {
							bw.Put(0x1, 1);
						}
						predX = predY = 0;
						WriteMacroblockBlocks(bw, coding, dcPredictor);
						break;
					case MacroblockKind::NoMotion:
						bw.Put(0x1, 2);
						bw.Put(CodedBlockPatternVlc[coding.Cbp]);
						predX = predY = 0;
						dcPredictor[0] = dcPredictor[1] = dcPredictor[2] = 128;
						WriteMacroblockBlocks(bw, coding, dcPredictor);
						break;
					case MacroblockKind::Forward:
						bw.Put(0x1, coding.Cbp != 0 ? 1 : 3);
						WriteMotionComponent(bw, WrapMotionDelta(coding.MvX - predX, fCode), fCode);
						WriteMotionComponent(bw, WrapMotionDelta(coding.MvY - predY, fCode), fCode);
						predX = coding.MvX;
						predY = coding.MvY;
						dcPredictor[0] = dcPredictor[1] = dcPredictor[2] = 128;
						if (coding.Cbp != 0) {
							bw.Put(CodedBlockPatternVlc[coding.Cbp]);
							WriteMacroblockBlocks(bw, coding, dcPredictor);
						}
						break;
					default:
						break;
				}
			}
		}
	}

	void Mpeg1Encoder::State::EncodeGop(std::int32_t first, std::int32_t count, std::int32_t quantizer16, bool findEnd, bool lastFrames,
		GopPass& pass)
	{
		pass.Stream.Clear();
		pass.Count = 0;
		pass.Quantizer16 = quantizer16;
		pass.QuantizerSum = 0;
		pass.LumaSquaredError = 0;
		BitWriter& bw = pass.Stream;

		// Repeated before every group, so a decoder can start at any of them. The rate is in units of 400 bit/s and the
		// buffer, a second of the stream, in units of 16 kbit; with vbv_delay left unspecified both are informative.
		const std::int32_t bitRate = std::clamp((Settings.BitrateKbps * 5 + 1) / 2, 1, 0x3FFFE);
		const std::int32_t vbvBufferSize = std::clamp((Settings.BitrateKbps * 1000 + 16383) / 16384, 1, 1023);
		bw.StartCode(SequenceHeaderCode);
		bw.Put(std::uint32_t(Settings.Width), 12);
		bw.Put(std::uint32_t(Settings.Height), 12);
		bw.Put(1, 4);			// Square pixels
		bw.Put(std::uint32_t(FrameRateCode), 4);
		bw.Put(std::uint32_t(bitRate), 18);
		bw.Put(1, 1);			// marker_bit
		bw.Put(std::uint32_t(vbvBufferSize), 10);
		bw.Put(0, 1);			// constrained_parameters_flag
		bw.Put(0, 1);			// load_intra_quantizer_matrix
		bw.Put(0, 1);			// load_non_intra_quantizer_matrix

		const std::int32_t rate = FrameRates[FrameRateCode].TimeCodeRate;
		const std::int64_t frame = QueueStart + first;
		bw.StartCode(GroupStartCode);
		bw.Put(0, 1);			// drop_frame_flag
		bw.Put(std::uint32_t((frame / (std::int64_t(rate) * 3600)) % 24), 5);
		bw.Put(std::uint32_t((frame / (std::int64_t(rate) * 60)) % 60), 6);
		bw.Put(1, 1);			// marker_bit
		bw.Put(std::uint32_t((frame / rate) % 60), 6);
		bw.Put(std::uint32_t(frame % rate), 6);
		bw.Put(1, 1);			// closed_gop
		bw.Put(0, 1);			// broken_link

		const std::int32_t intraQuantizer16 = std::max(MinIntraQuantizer16, (quantizer16 * IntraQuantizerRatio + 8) / 16);
		const std::int32_t macroblocks = MbWidth * MbHeight;
		const std::int64_t maxPictureBits = std::int64_t(Settings.BitrateKbps) * 1000;
		PreviousVectors.clear();
		// Where the group ends at its normal length, should no scene cut follow closely enough to extend it to
		BitWriter::Mark regularEnd {};
		GopPass regular;
		for (std::int32_t i = 0; i < count; i++) {
			const bool intra = (i == 0);
			const BitWriter::Mark mark = bw.GetMark();
			const std::int64_t startBits = bw.GetBitCount();
			std::int32_t pictureQuantizer16 = (intra ? intraQuantizer16 : quantizer16);
			PictureStats stats;
			EncodePicture(Queue[std::size_t(first + i)], intra ? PictureTypeI : PictureTypeP, i, pictureQuantizer16,
				std::int32_t((frame + i) % 16), bw, stats);
			// A picture larger than a second's worth of the bitrate would stall the decoder, so it is made coarser
			while (bw.GetBitCount() - startBits > maxPictureBits && pictureQuantizer16 < 31 * 16) {
				bw.Rewind(mark);
				pictureQuantizer16 = std::min(31 * 16, pictureQuantizer16 * 5 / 4 + 1);
				stats = PictureStats();
				EncodePicture(Queue[std::size_t(first + i)], intra ? PictureTypeI : PictureTypeP, i, pictureQuantizer16,
					std::int32_t((frame + i) % 16), bw, stats);
			}
			// A picture the motion search finds mostly new is a scene cut, which is better off starting a group of its own
			if (findEnd && i >= MinCutDistance(GopLength) && stats.IntraMacroblocks * 16 >= macroblocks * CutIntraFraction) {
				bw.Rewind(mark);
				break;
			}
			pass.Count++;
			pass.QuantizerSum += stats.QuantizerSum;
			pass.LumaSquaredError += stats.LumaSquaredError;
			std::swap(Reference, Current);
			if (pass.Count == GopLength) {
				regularEnd = bw.GetMark();
				regular.Count = pass.Count;
				regular.QuantizerSum = pass.QuantizerSum;
				regular.LumaSquaredError = pass.LumaSquaredError;
			}
		}
		// The pictures past the normal length were only encoded to look for a cut there, unless they are the last ones
		if (findEnd && !lastFrames && pass.Count > GopLength && pass.Count == count) {
			bw.Rewind(regularEnd);
			pass.Count = regular.Count;
			pass.QuantizerSum = regular.QuantizerSum;
			pass.LumaSquaredError = regular.LumaSquaredError;
		}
		bw.Align();
	}

	std::int64_t Mpeg1Encoder::State::ShareOf(std::int32_t frames) const
	{
		const FrameRate& rate = FrameRates[FrameRateCode];
		return std::int64_t(Settings.BitrateKbps) * 1000 * frames * rate.Den / rate.Num;
	}

	std::int32_t Mpeg1Encoder::State::PlanQuantizer(std::int64_t windowComplexity, std::int64_t windowTarget) const
	{
		// The quantizer that, used for the groups written so far and those encoded ahead, would put them all exactly
		// on target - so the groups get the bits their complexity asks for at a common quality, rather than equal
		// shares, and what lies ahead is taken into account before it arrives
		const std::int64_t quantizer = (ComplexitySum + windowComplexity) / std::max<std::int64_t>(TargetBits + windowTarget, 1);
		// What the groups so far took too many or too few is evened out over the window, or ten seconds at least
		const FrameRate& rate = FrameRates[FrameRateCode];
		const std::int64_t horizon = std::max(ShareOf(std::max(1, 10 * rate.Num / rate.Den)), windowTarget);
		const std::int64_t correction = std::clamp(horizon + (ActualBits - TargetBits), horizon / 2, horizon * 2);
		return std::int32_t(std::clamp<std::int64_t>(quantizer * correction / horizon, MinQuantizer16, 31 * 16));
	}

	void Mpeg1Encoder::State::PlanAhead(bool flush)
	{
		const std::int32_t maxLength = GopLength + GopExtension(GopLength);
		std::int32_t covered = 0;
		std::int64_t complexity = 0, target = 0;
		for (const GopPass& pass : Planned) {
			covered += pass.Count;
			complexity += pass.GetComplexity();
			target += ShareOf(pass.Count);
		}
		// A group is encoded as soon as there are enough frames to see whether it should end early at a scene cut or
		// run on to one - that first encoding decides where it ends, and measures how complex it is
		while (QueueCount - covered >= maxLength || (flush && QueueCount > covered)) {
			const std::int32_t count = std::min(QueueCount - covered, maxLength);
			std::int32_t quantizer16;
			if (Settings.FixedQuantizer > 0) {
				quantizer16 = Settings.FixedQuantizer * 16;
			} else if (LastComplexityPerFrame > 0 || !Planned.empty()) {
				const std::int64_t perFrame = (!Planned.empty() ? complexity / covered : LastComplexityPerFrame);
				quantizer16 = PlanQuantizer(complexity + perFrame * count, target + ShareOf(count));
			} else {
				quantizer16 = InitialQuantizer16;
			}
			Planned.emplace_back();
			GopPass& pass = Planned.back();
			EncodeGop(covered, count, quantizer16, true, flush && covered + count == QueueCount, pass);
			covered += pass.Count;
			complexity += pass.GetComplexity();
			target += ShareOf(pass.Count);
		}
	}

	void Mpeg1Encoder::State::WriteFirstGop(std::vector<std::uint8_t>& output)
	{
		GopPass& best = Planned.front();
		const std::int64_t share = ShareOf(best.Count);
		if (Settings.FixedQuantizer <= 0) {
			std::int64_t windowComplexity = 0, windowTarget = 0;
			for (std::size_t i = 1; i < Planned.size(); i++) {
				windowComplexity += Planned[i].GetComplexity();
				windowTarget += ShareOf(Planned[i].Count);
			}
			// The group is encoded again when the plan has moved away from the quantizer it was encoded ahead with,
			// and the plan is refined from what each attempt took, as bits fall more slowly than the quantizer rises
			GopPass trial;
			for (std::int32_t attempt = 1; attempt < MaxAttempts; attempt++) {
				const std::int32_t next = PlanQuantizer(windowComplexity + best.GetComplexity(), windowTarget + share);
				if (std::abs(next - best.Quantizer16) * QuantizerTolerance <= best.Quantizer16) {
					break;
				}
				EncodeGop(0, best.Count, next, false, false, trial);
				std::swap(best, trial);
			}
		}

		const std::int64_t bits = best.Stream.GetBitCount();
		ComplexitySum += best.GetComplexity();
		TargetBits += share;
		ActualBits += bits;
		LastComplexityPerFrame = best.GetComplexity() / std::max(1, best.Count);

		output.insert(output.end(), best.Stream.Bytes.begin(), best.Stream.Bytes.end());
		Stats.Frames += best.Count;
		Stats.IntraFrames++;
		Stats.Bytes += std::int64_t(best.Stream.Bytes.size());
		Stats.QuantizerSum += best.QuantizerSum;
		Stats.LumaSquaredError += best.LumaSquaredError;

		// The queue keeps the storage of the written frames for the ones to come
		const std::int32_t count = best.Count;
		std::rotate(Queue.begin(), Queue.begin() + count, Queue.begin() + QueueCount);
		QueueCount -= count;
		QueueStart += count;
		Planned.erase(Planned.begin());
	}

	Mpeg1Encoder::Mpeg1Encoder()
		: _state(new State())
	{
	}

	Mpeg1Encoder::~Mpeg1Encoder()
	{
		delete _state;
	}

	bool Mpeg1Encoder::Begin(const Options& options, std::vector<std::uint8_t>& output)
	{
		static_cast<void>(output);
		State& s = *_state;
		s = State();

		if (options.Width <= 0 || options.Height <= 0 || options.Width > 4095 || options.Height > 2800 ||
			(options.Width & 1) != 0 || (options.Height & 1) != 0 || options.FrameRateNum <= 0 || options.FrameRateDen <= 0 ||
			options.BitrateKbps <= 0 || options.BitrateKbps > MaxBitrateKbps || options.FixedQuantizer < 0 || options.FixedQuantizer > 31) {
			return false;
		}
		// A rate within 0.05 % of one of the standard ones is taken as it, so 23.976 or 29.97 written out works too
		for (std::int32_t code = 1; code < 9; code++) {
			const FrameRate& rate = FrameRates[code];
			const std::int64_t difference = std::int64_t(options.FrameRateNum) * rate.Den - std::int64_t(rate.Num) * options.FrameRateDen;
			if (std::abs(difference) * 2000 <= std::int64_t(rate.Num) * options.FrameRateDen) {
				s.FrameRateCode = code;
				break;
			}
		}
		if (s.FrameRateCode == 0) {
			return false;
		}

		s.Settings = options;
		s.Settings.SearchRange = std::clamp(options.SearchRange, 1, 64);
		s.MbWidth = (options.Width + 15) / 16;
		s.MbHeight = (options.Height + 15) / 16;
		s.LumaWidth = s.MbWidth * 16;
		s.LumaHeight = s.MbHeight * 16;
		s.ChromaWidth = s.MbWidth * 8;
		s.ChromaHeight = s.MbHeight * 8;
		s.MaxFCode = 1;
		while (s.MaxFCode < 7 && s.VectorLimit(s.MaxFCode) < 2 * s.Settings.SearchRange) {
			s.MaxFCode++;
		}

		const FrameRate& rate = FrameRates[s.FrameRateCode];
		s.GopLength = std::clamp(options.GopLength > 0 ? options.GopLength : std::int32_t((std::int64_t(rate.Num) * 2 + rate.Den / 2) / rate.Den),
			1, MaxGopLength);
		// The lookahead holds raw frames, so it is kept within a fixed amount of memory for large pictures
		const std::int64_t frameBytes = std::int64_t(s.LumaWidth) * s.LumaHeight * 3 / 2;
		s.LookaheadFrames = std::int32_t(std::min<std::int64_t>((std::int64_t(rate.Num) * LookaheadSeconds + rate.Den / 2) / rate.Den,
			LookaheadMemory / frameBytes));
		s.AllocatePicture(s.Reference);
		s.AllocatePicture(s.Current);
		s.Started = true;
		return true;
	}

	bool Mpeg1Encoder::EncodeFrame(const std::uint8_t* y, const std::uint8_t* u, const std::uint8_t* v, std::vector<std::uint8_t>& output)
	{
		State& s = *_state;
		if (!s.Started || y == nullptr || u == nullptr || v == nullptr) {
			return false;
		}

		if (std::size_t(s.QueueCount) == s.Queue.size()) {
			s.Queue.emplace_back();
			s.AllocatePicture(s.Queue.back());
		}
		Picture& p = s.Queue[std::size_t(s.QueueCount)];
		// Padding replicates the edges, which is what costs the fewest bits and predicts best
		auto copyPlane = [](const std::uint8_t* src, std::int32_t width, std::int32_t height, std::uint8_t* dst, std::int32_t paddedWidth, std::int32_t paddedHeight) {
			for (std::int32_t row = 0; row < height; row++) {
				std::memcpy(dst + std::size_t(row) * paddedWidth, src + std::size_t(row) * width, std::size_t(width));
				std::memset(dst + std::size_t(row) * paddedWidth + width, src[std::size_t(row) * width + width - 1], std::size_t(paddedWidth - width));
			}
			for (std::int32_t row = height; row < paddedHeight; row++) {
				std::memcpy(dst + std::size_t(row) * paddedWidth, dst + std::size_t(height - 1) * paddedWidth, std::size_t(paddedWidth));
			}
		};
		copyPlane(y, s.Settings.Width, s.Settings.Height, p.Y.data(), s.LumaWidth, s.LumaHeight);
		copyPlane(u, s.Settings.Width / 2, s.Settings.Height / 2, p.Cb.data(), s.ChromaWidth, s.ChromaHeight);
		copyPlane(v, s.Settings.Width / 2, s.Settings.Height / 2, p.Cr.data(), s.ChromaWidth, s.ChromaHeight);
		s.QueueCount++;

		s.PlanAhead(false);
		while (!s.Planned.empty() && s.QueueCount - s.Planned.front().Count >= s.LookaheadFrames + s.GopLength + GopExtension(s.GopLength)) {
			s.WriteFirstGop(output);
		}
		return true;
	}

	bool Mpeg1Encoder::End(std::vector<std::uint8_t>& output)
	{
		State& s = *_state;
		if (!s.Started) {
			return false;
		}
		s.PlanAhead(true);
		while (!s.Planned.empty()) {
			s.WriteFirstGop(output);
		}
		BitWriter bw;
		// libdragon's decoder reads every variable-length code through a 64-bit window that comes back empty within
		// the last 8 bytes of the file, which would garble the end of the last slice. Zero bytes are allowed before
		// any start code, so they keep the slice data far enough from the end.
		bw.Put(0, 32);
		bw.Put(0, 32);
		bw.StartCode(SequenceEndCode);
		output.insert(output.end(), bw.Bytes.begin(), bw.Bytes.end());
		s.Stats.Bytes += std::int64_t(bw.Bytes.size());
		s.Started = false;
		return true;
	}

	const Mpeg1Encoder::Statistics& Mpeg1Encoder::GetStatistics() const
	{
		return _state->Stats;
	}
}
