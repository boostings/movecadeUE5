#include "Net/QrCode.h"

namespace MovecadeKart::Qr
{
	namespace
	{
		/** ISO 18004 table 9, level M: ECC codewords per block, then (blocks, data codewords) for two groups. */
		struct FVersionInfo
		{
			int32 EccPerBlock;
			int32 Blocks1;
			int32 Data1;
			int32 Blocks2;
			int32 Data2;

			int32 DataCodewords() const { return Blocks1 * Data1 + Blocks2 * Data2; }
			int32 NumBlocks() const { return Blocks1 + Blocks2; }
		};

		const FVersionInfo GVersionsM[MaxVersion + 1] = {
			{ 0, 0, 0, 0, 0 },
			{ 10, 1, 16, 0, 0 },  // 1: 26 codewords
			{ 16, 1, 28, 0, 0 },  // 2: 44
			{ 26, 1, 44, 0, 0 },  // 3: 70
			{ 18, 2, 32, 0, 0 },  // 4: 100
			{ 24, 2, 43, 0, 0 },  // 5: 134
			{ 16, 4, 27, 0, 0 },  // 6: 172
			{ 18, 4, 31, 0, 0 },  // 7: 196
			{ 22, 2, 38, 2, 39 }, // 8: 242
			{ 22, 3, 36, 2, 37 }, // 9: 292
			{ 26, 4, 43, 1, 44 }, // 10: 346
		};

		/** Annex E alignment pattern centres. */
		TArray<int32> AlignmentPositions(int32 Version)
		{
			static const TArray<int32> Table[MaxVersion + 1] = {
				{}, {}, { 6, 18 }, { 6, 22 }, { 6, 26 }, { 6, 30 }, { 6, 34 },
				{ 6, 22, 38 }, { 6, 24, 42 }, { 6, 26, 46 }, { 6, 28, 50 },
			};
			return Table[Version];
		}

		/** GF(256) multiply, reducing by x^8 + x^4 + x^3 + x^2 + 1 (0x11D). */
		uint8 GfMul(uint8 X, uint8 Y)
		{
			uint32 Z = 0;
			for (int32 i = 7; i >= 0; --i)
			{
				Z = (Z << 1) ^ ((Z >> 7) * 0x11D);
				Z ^= ((Y >> i) & 1) * X;
			}
			return static_cast<uint8>(Z);
		}

		/** Generator polynomial coefficients for Degree ECC codewords, highest power first (leading 1 dropped). */
		TArray<uint8> RsDivisor(int32 Degree)
		{
			TArray<uint8> Result;
			Result.Init(0, Degree);
			Result[Degree - 1] = 1;
			uint8 Root = 1;
			for (int32 i = 0; i < Degree; ++i)
			{
				for (int32 j = 0; j < Degree; ++j)
				{
					Result[j] = GfMul(Result[j], Root);
					if (j + 1 < Degree)
					{
						Result[j] ^= Result[j + 1];
					}
				}
				Root = GfMul(Root, 0x02);
			}
			return Result;
		}

		TArray<uint8> RsRemainder(TConstArrayView<uint8> Data, const TArray<uint8>& Divisor)
		{
			TArray<uint8> Result;
			Result.Init(0, Divisor.Num());
			for (const uint8 B : Data)
			{
				const uint8 Factor = B ^ Result[0];
				Result.RemoveAt(0);
				Result.Add(0);
				for (int32 i = 0; i < Result.Num(); ++i)
				{
					Result[i] ^= GfMul(Divisor[i], Factor);
				}
			}
			return Result;
		}

		bool Bit(uint32 Value, int32 Index) { return ((Value >> Index) & 1) != 0; }

		bool MaskBit(int32 Mask, int32 X, int32 Y)
		{
			switch (Mask)
			{
			case 0: return (X + Y) % 2 == 0;
			case 1: return Y % 2 == 0;
			case 2: return X % 3 == 0;
			case 3: return (X + Y) % 3 == 0;
			case 4: return (X / 3 + Y / 2) % 2 == 0;
			case 5: return X * Y % 2 + X * Y % 3 == 0;
			case 6: return (X * Y % 2 + X * Y % 3) % 2 == 0;
			case 7: return ((X + Y) % 2 + X * Y % 3) % 2 == 0;
			default: return false;
			}
		}

		class FBuilder
		{
		public:
			FBuilder(int32 InVersion)
				: Version(InVersion)
				, Size(InVersion * 4 + 17)
			{
				Modules.Init(0, Size * Size);
				Function.Init(0, Size * Size);
			}

			void Set(int32 X, int32 Y, bool bDark) { Modules[Y * Size + X] = bDark ? 1 : 0; }
			bool Get(int32 X, int32 Y) const { return Modules[Y * Size + X] != 0; }
			void SetFunction(int32 X, int32 Y, bool bDark)
			{
				Set(X, Y, bDark);
				Function[Y * Size + X] = 1;
			}

			void DrawFunctionPatterns()
			{
				for (int32 i = 0; i < Size; ++i)
				{
					SetFunction(6, i, i % 2 == 0);
					SetFunction(i, 6, i % 2 == 0);
				}
				DrawFinder(3, 3);
				DrawFinder(Size - 4, 3);
				DrawFinder(3, Size - 4);
				const TArray<int32> Align = AlignmentPositions(Version);
				const int32 N = Align.Num();
				for (int32 i = 0; i < N; ++i)
				{
					for (int32 j = 0; j < N; ++j)
					{
						if ((i == 0 && j == 0) || (i == 0 && j == N - 1) || (i == N - 1 && j == 0))
						{
							continue;
						}
						for (int32 Dy = -2; Dy <= 2; ++Dy)
						{
							for (int32 Dx = -2; Dx <= 2; ++Dx)
							{
								SetFunction(Align[i] + Dx, Align[j] + Dy, FMath::Max(FMath::Abs(Dx), FMath::Abs(Dy)) != 1);
							}
						}
					}
				}
				DrawFormat(0); // reserves the area; redrawn with the real mask later
				if (Version >= 7)
				{
					const uint32 Bits = VersionBits(Version);
					for (int32 i = 0; i < 18; ++i)
					{
						const int32 A = Size - 11 + i % 3;
						const int32 B = i / 3;
						SetFunction(A, B, Bit(Bits, i));
						SetFunction(B, A, Bit(Bits, i));
					}
				}
			}

			void DrawFinder(int32 Cx, int32 Cy)
			{
				for (int32 Dy = -4; Dy <= 4; ++Dy)
				{
					for (int32 Dx = -4; Dx <= 4; ++Dx)
					{
						const int32 X = Cx + Dx;
						const int32 Y = Cy + Dy;
						if (X >= 0 && X < Size && Y >= 0 && Y < Size)
						{
							const int32 Dist = FMath::Max(FMath::Abs(Dx), FMath::Abs(Dy));
							SetFunction(X, Y, Dist != 2 && Dist != 4);
						}
					}
				}
			}

			void DrawFormat(int32 Mask)
			{
				const uint32 Bits = FormatBits(Mask);
				for (int32 i = 0; i <= 5; ++i) SetFunction(8, i, Bit(Bits, i));
				SetFunction(8, 7, Bit(Bits, 6));
				SetFunction(8, 8, Bit(Bits, 7));
				SetFunction(7, 8, Bit(Bits, 8));
				for (int32 i = 9; i < 15; ++i) SetFunction(14 - i, 8, Bit(Bits, i));
				for (int32 i = 0; i < 8; ++i) SetFunction(Size - 1 - i, 8, Bit(Bits, i));
				for (int32 i = 8; i < 15; ++i) SetFunction(8, Size - 15 + i, Bit(Bits, i));
				SetFunction(8, Size - 8, true); // the dark module
			}

			void DrawCodewords(const TArray<uint8>& Codewords)
			{
				int32 i = 0;
				const int32 TotalBits = Codewords.Num() * 8;
				for (int32 Right = Size - 1; Right >= 1; Right -= 2)
				{
					if (Right == 6)
					{
						Right = 5; // skip the vertical timing column
					}
					for (int32 Vert = 0; Vert < Size; ++Vert)
					{
						for (int32 J = 0; J < 2; ++J)
						{
							const int32 X = Right - J;
							const bool bUpward = ((Right + 1) & 2) == 0;
							const int32 Y = bUpward ? Size - 1 - Vert : Vert;
							if (!Function[Y * Size + X] && i < TotalBits)
							{
								Set(X, Y, Bit(Codewords[i >> 3], 7 - (i & 7)));
								++i;
							}
						}
					}
				}
			}

			void ApplyMask(int32 Mask)
			{
				for (int32 Y = 0; Y < Size; ++Y)
				{
					for (int32 X = 0; X < Size; ++X)
					{
						if (!Function[Y * Size + X] && MaskBit(Mask, X, Y))
						{
							Modules[Y * Size + X] ^= 1;
						}
					}
				}
			}

			/** ISO 18004 §7.8.3 penalty: N1 runs, N2 blocks, N3 finder-like patterns, N4 balance. */
			int32 Penalty() const
			{
				int32 Result = 0;
				auto At = [this](int32 X, int32 Y) { return Modules[Y * Size + X]; };
				for (int32 Pass = 0; Pass < 2; ++Pass)
				{
					for (int32 A = 0; A < Size; ++A)
					{
						auto Line = [&](int32 B) -> int32
						{
							if (B < 0 || B >= Size) return 0; // light outside the symbol
							return Pass == 0 ? At(B, A) : At(A, B);
						};
						int32 Run = 1;
						for (int32 B = 1; B <= Size; ++B)
						{
							if (B < Size && Line(B) == Line(B - 1))
							{
								++Run;
								continue;
							}
							if (Run >= 5) Result += 3 + (Run - 5);
							Run = 1;
						}
						static const int32 PatternA[11] = { 1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0 };
						static const int32 PatternB[11] = { 0, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1 };
						for (int32 Start = -4; Start + 11 <= Size + 4; ++Start)
						{
							bool bA = true;
							bool bB = true;
							for (int32 k = 0; k < 11 && (bA || bB); ++k)
							{
								const int32 V = Line(Start + k);
								bA = bA && V == PatternA[k];
								bB = bB && V == PatternB[k];
							}
							Result += (bA ? 40 : 0) + (bB ? 40 : 0);
						}
					}
				}
				int32 Dark = 0;
				for (int32 Y = 0; Y < Size; ++Y)
				{
					for (int32 X = 0; X < Size; ++X)
					{
						Dark += At(X, Y);
						if (X + 1 < Size && Y + 1 < Size)
						{
							const int32 C = At(X, Y);
							if (C == At(X + 1, Y) && C == At(X, Y + 1) && C == At(X + 1, Y + 1))
							{
								Result += 3;
							}
						}
					}
				}
				const int64 Total = int64(Size) * Size;
				const int64 K = (FMath::Abs(int64(Dark) * 20 - Total * 10) + Total - 1) / Total - 1;
				Result += static_cast<int32>(K) * 10;
				return Result;
			}

			int32 Version;
			int32 Size;
			TArray<uint8> Modules;
			TArray<uint8> Function;
		};
	}

	int32 ByteCapacity(int32 Version)
	{
		if (Version < MinVersion || Version > MaxVersion)
		{
			return 0;
		}
		const int32 CountBits = Version < 10 ? 8 : 16;
		return (GVersionsM[Version].DataCodewords() * 8 - 4 - CountBits) / 8;
	}

	uint32 FormatBits(int32 Mask)
	{
		const uint32 EccBitsM = 0; // L=1, M=0, Q=3, H=2
		const uint32 Data = (EccBitsM << 3) | static_cast<uint32>(Mask & 7);
		uint32 Rem = Data;
		for (int32 i = 0; i < 10; ++i)
		{
			Rem = (Rem << 1) ^ ((Rem >> 9) * 0x537);
		}
		return ((Data << 10) | (Rem & 0x3FF)) ^ 0x5412;
	}

	uint32 VersionBits(int32 Version)
	{
		uint32 Rem = static_cast<uint32>(Version);
		for (int32 i = 0; i < 12; ++i)
		{
			Rem = (Rem << 1) ^ ((Rem >> 11) * 0x1F25);
		}
		return (static_cast<uint32>(Version) << 12) | (Rem & 0xFFF);
	}

	uint32 ReadFormatBits(const FQrMatrix& M)
	{
		if (!M.IsValid())
		{
			return 0;
		}
		uint32 Bits = 0;
		auto Put = [&Bits](int32 Index, bool bDark) { if (bDark) Bits |= 1u << Index; };
		for (int32 i = 0; i <= 5; ++i) Put(i, M.Get(8, i));
		Put(6, M.Get(8, 7));
		Put(7, M.Get(8, 8));
		Put(8, M.Get(7, 8));
		for (int32 i = 9; i < 15; ++i) Put(i, M.Get(14 - i, 8));
		return Bits;
	}

	FQrMatrix Encode(TConstArrayView<uint8> Bytes, int32 ForcedMask)
	{
		int32 Version = MinVersion;
		while (Version <= MaxVersion && ByteCapacity(Version) < Bytes.Num())
		{
			++Version;
		}
		if (Version > MaxVersion)
		{
			return FQrMatrix();
		}
		const FVersionInfo& Info = GVersionsM[Version];
		const int32 DataCodewords = Info.DataCodewords();

		// Segment: mode 0100, character count, bytes; then terminator, byte alignment and pad codewords.
		TArray<uint8> BitBuffer;
		auto Append = [&BitBuffer](uint32 Value, int32 Length)
		{
			for (int32 i = Length - 1; i >= 0; --i) BitBuffer.Add((Value >> i) & 1);
		};
		Append(0x4, 4);
		Append(Bytes.Num(), Version < 10 ? 8 : 16);
		for (const uint8 B : Bytes) Append(B, 8);
		const int32 CapacityBits = DataCodewords * 8;
		Append(0, FMath::Min(4, CapacityBits - BitBuffer.Num()));
		Append(0, (8 - BitBuffer.Num() % 8) % 8);
		for (uint8 Pad = 0xEC; BitBuffer.Num() < CapacityBits; Pad ^= 0xEC ^ 0x11)
		{
			Append(Pad, 8);
		}
		TArray<uint8> Data;
		Data.Init(0, DataCodewords);
		for (int32 i = 0; i < BitBuffer.Num(); ++i)
		{
			Data[i >> 3] |= BitBuffer[i] << (7 - (i & 7));
		}

		// Blocks, error correction and interleaving.
		const TArray<uint8> Divisor = RsDivisor(Info.EccPerBlock);
		TArray<TArray<uint8>> DataBlocks;
		TArray<TArray<uint8>> EccBlocks;
		int32 Offset = 0;
		for (int32 b = 0; b < Info.NumBlocks(); ++b)
		{
			const int32 Len = b < Info.Blocks1 ? Info.Data1 : Info.Data2;
			TArray<uint8> Block(Data.GetData() + Offset, Len);
			Offset += Len;
			EccBlocks.Add(RsRemainder(Block, Divisor));
			DataBlocks.Add(MoveTemp(Block));
		}
		TArray<uint8> Codewords;
		const int32 MaxData = FMath::Max(Info.Data1, Info.Data2);
		for (int32 i = 0; i < MaxData; ++i)
		{
			for (const TArray<uint8>& Block : DataBlocks)
			{
				if (i < Block.Num()) Codewords.Add(Block[i]);
			}
		}
		for (int32 i = 0; i < Info.EccPerBlock; ++i)
		{
			for (const TArray<uint8>& Block : EccBlocks) Codewords.Add(Block[i]);
		}

		FBuilder Builder(Version);
		Builder.DrawFunctionPatterns();
		Builder.DrawCodewords(Codewords);

		int32 Mask = ForcedMask;
		if (Mask < 0 || Mask > 7)
		{
			int32 Best = MAX_int32;
			for (int32 Candidate = 0; Candidate < 8; ++Candidate)
			{
				Builder.ApplyMask(Candidate);
				Builder.DrawFormat(Candidate);
				const int32 Score = Builder.Penalty();
				if (Score < Best)
				{
					Best = Score;
					Mask = Candidate;
				}
				Builder.ApplyMask(Candidate); // XOR undoes it
			}
		}
		Builder.ApplyMask(Mask);
		Builder.DrawFormat(Mask);

		FQrMatrix Out;
		Out.Version = Version;
		Out.Size = Builder.Size;
		Out.Mask = Mask;
		Out.Modules = MoveTemp(Builder.Modules);
		return Out;
	}

	FQrMatrix EncodeText(const FString& Text, int32 ForcedMask)
	{
		const FTCHARToUTF8 Utf8(*Text);
		return Encode(TConstArrayView<uint8>(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length()), ForcedMask);
	}

	void RasterizeBgra(const FQrMatrix& Matrix, int32 QuietModules, int32 PixelsPerModule, TArray64<uint8>& OutPixels, int32& OutSize)
	{
		check(Matrix.IsValid() && QuietModules >= 0 && PixelsPerModule > 0);
		OutSize = (Matrix.Size + QuietModules * 2) * PixelsPerModule;
		OutPixels.SetNumUninitialized(int64(OutSize) * OutSize * 4);
		for (int32 Y = 0; Y < OutSize; ++Y)
		{
			const int32 ModuleY = Y / PixelsPerModule - QuietModules;
			for (int32 X = 0; X < OutSize; ++X)
			{
				const int32 ModuleX = X / PixelsPerModule - QuietModules;
				const bool bDark = ModuleX >= 0 && ModuleY >= 0 && ModuleX < Matrix.Size && ModuleY < Matrix.Size && Matrix.Get(ModuleX, ModuleY);
				const uint8 Value = bDark ? 0 : 255;
				uint8* Pixel = &OutPixels[(int64(Y) * OutSize + X) * 4];
				Pixel[0] = Value;
				Pixel[1] = Value;
				Pixel[2] = Value;
				Pixel[3] = 255;
			}
		}
	}
}
