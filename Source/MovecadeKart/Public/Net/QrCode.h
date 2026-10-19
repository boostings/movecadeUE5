#pragma once

#include "CoreMinimal.h"

/**
 * A small QR Code Model 2 encoder (ISO/IEC 18004) for the lobby join URL: byte mode, error
 * correction level M, versions 1-10 (up to 213 bytes). Written from the standard; no third-party code.
 */
namespace MovecadeKart::Qr
{
	constexpr int32 MinVersion = 1;
	constexpr int32 MaxVersion = 10;

	struct FQrMatrix
	{
		int32 Version = 0;
		int32 Size = 0;
		/** 0..7, the mask actually applied. */
		int32 Mask = -1;
		/** Row-major, Size * Size, 1 = dark. */
		TArray<uint8> Modules;

		bool IsValid() const { return Size > 0; }
		bool Get(int32 X, int32 Y) const { return Modules[Y * Size + X] != 0; }
	};

	/** Byte capacity at ECC M for a version (14 for v1 ... 213 for v10). */
	MOVECADEKART_API int32 ByteCapacity(int32 Version);

	/**
	 * Encode Bytes in byte mode at ECC M with the smallest version that fits. ForcedMask -1 picks the
	 * mask with the lowest ISO penalty score. Returns an invalid matrix when the data is over 213 bytes.
	 */
	MOVECADEKART_API FQrMatrix Encode(TConstArrayView<uint8> Bytes, int32 ForcedMask = -1);
	/** UTF-8 encodes Text first. */
	MOVECADEKART_API FQrMatrix EncodeText(const FString& Text, int32 ForcedMask = -1);

	/** The 15 format bits (ECC M, Mask), BCH-coded and XOR-masked with 0x5412, bit 14 first. */
	MOVECADEKART_API uint32 FormatBits(int32 Mask);
	/** The 18 version bits for versions >= 7 (BCH(18,6), generator 0x1F25). */
	MOVECADEKART_API uint32 VersionBits(int32 Version);
	/** Reads the first copy of the format bits from a matrix (around the top-left finder). */
	MOVECADEKART_API uint32 ReadFormatBits(const FQrMatrix& Matrix);
}
