#include "FX/KartFx.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKartHexTest, "MovecadeKart.Fx.Hex",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKartHexTest::RunTest(const FString& Parameters)
{
	using namespace MovecadeKart::Fx;

	// Every length FromHex treats differently, with and without '#', valid digits in both cases, and characters it
	// reads as 0 (including a second '#').
	const TCHAR Alphabet[] = TEXT("0123456789abcdefABCDEF#gz -");
	FRandomStream Random(1234);
	TArray<FString> Inputs = { TEXT(""), TEXT("#"), TEXT("##"), TEXT("ffe2a0"), TEXT("#ffe2a0"), TEXT("FFE2A0"), TEXT("fff"), TEXT("#0f0"),
		TEXT("11223344"), TEXT("#11223344"), TEXT("1234567"), TEXT("#12345"), TEXT("##ffe2a0"), TEXT("ffe2a0 ") };
	for (int32 i = 0; i < 4000; ++i)
	{
		FString Value;
		const int32 Len = Random.RandRange(0, 10);
		for (int32 c = 0; c < Len; ++c)
		{
			Value.AppendChar(Alphabet[Random.RandRange(0, UE_ARRAY_COUNT(Alphabet) - 2)]);
		}
		Inputs.Add(Value);
	}

	int32 Mismatches = 0;
	for (const FString& Value : Inputs)
	{
		const FColor Expected = FColor::FromHex(Value);
		const FColor Got = HexToColor(Value);
		const FLinearColor ExpectedLinear = FLinearColor::FromSRGBColor(Expected);
		const FLinearColor GotLinear = Hex(Value);
		if (Got != Expected || FMemory::Memcmp(&GotLinear, &ExpectedLinear, sizeof(FLinearColor)) != 0)
		{
			if (++Mismatches <= 5)
			{
				AddError(FString::Printf(TEXT("\"%s\": expected %s, got %s"), *Value, *Expected.ToString(), *Got.ToString()));
			}
		}
	}
	TestEqual(TEXT("matches FColor::FromHex on every input"), Mismatches, 0);
	TestEqual(TEXT("ffe2a0"), HexToColor(TEXT("ffe2a0")), FColor(0xff, 0xe2, 0xa0, 0xff));
	TestEqual(TEXT("short form"), HexToColor(TEXT("#0f8")), FColor(0x00, 0xff, 0x88, 0xff));
	return true;
}

#endif
