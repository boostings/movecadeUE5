#include "Store/GooglePlayReceipt.h"

#include "Dom/JsonObject.h"
#include "Misc/Base64.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// MovecadeKart.Build.cs defines MOVECADE_WITH_PLAY_VERIFY=1 (and links OpenSSL) for Android and Win64 (the tests).
#if MOVECADE_WITH_PLAY_VERIFY
// OpenSSL declares a type named UI, which clashes with the engine's UI namespace (the engine's own SSL code does the same).
#define UI UI_ST
THIRD_PARTY_INCLUDES_START
#include <openssl/evp.h>
#include <openssl/x509.h>
THIRD_PARTY_INCLUDES_END
#undef UI
#endif

namespace MovecadeKart::Store::GooglePlay
{
	namespace
	{
		/** A purchase JSON is a few hundred bytes; a key a few hundred more. Anything far bigger isn't one. */
		constexpr int32 MaxInputBytes = 64 * 1024;

		/** Google's purchaseState in the purchase JSON: 0 purchased, 4 pending (the billing library's own mapping). */
		constexpr int32 JsonStatePurchased = 0;
		constexpr int32 JsonStatePending = 4;

		bool DecodeBase64(const FString& In, TArray<uint8>& Out)
		{
			Out.Reset();
			return !In.IsEmpty() && In.Len() <= MaxInputBytes && FBase64::Decode(In.TrimStartAndEnd(), Out) && Out.Num() > 0;
		}
	}

	bool IsVerifierAvailable()
	{
		return MOVECADE_WITH_PLAY_VERIFY != 0;
	}

	bool VerifySignature(const TArray<uint8>& Data, const FString& SignatureBase64, const FString& LicenseKeyBase64)
	{
#if MOVECADE_WITH_PLAY_VERIFY
		TArray<uint8> Key, Signature;
		if (Data.Num() == 0 || Data.Num() > MaxInputBytes || !DecodeBase64(LicenseKeyBase64, Key) || !DecodeBase64(SignatureBase64, Signature))
		{
			return false;
		}
		const unsigned char* KeyCursor = Key.GetData();
		EVP_PKEY* PublicKey = d2i_PUBKEY(nullptr, &KeyCursor, Key.Num());
		if (!PublicKey)
		{
			return false;
		}
		bool bValid = false;
		if (EVP_MD_CTX* Context = EVP_MD_CTX_new())
		{
			bValid = EVP_DigestVerifyInit(Context, nullptr, EVP_sha1(), nullptr, PublicKey) == 1
				&& EVP_DigestVerifyUpdate(Context, Data.GetData(), Data.Num()) == 1
				&& EVP_DigestVerifyFinal(Context, Signature.GetData(), Signature.Num()) == 1;
			EVP_MD_CTX_free(Context);
		}
		EVP_PKEY_free(PublicKey);
		return bValid;
#else
		return false;
#endif
	}

	EReceiptCheck CheckPurchase(const TArray<uint8>& PurchaseJson, const FString& SignatureBase64, const FString& LicenseKeyBase64, const FString& ExpectedPackage, FPurchase& Out)
	{
		Out = FPurchase();
		if (LicenseKeyBase64.TrimStartAndEnd().IsEmpty() || !IsVerifierAvailable())
		{
			return EReceiptCheck::NoKey;
		}
		// The signature covers the exact bytes, so it is checked before anything is parsed.
		if (!VerifySignature(PurchaseJson, SignatureBase64, LicenseKeyBase64))
		{
			return EReceiptCheck::BadSignature;
		}
		FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(PurchaseJson.GetData()), PurchaseJson.Num());
		const FString JsonText(Text.Length(), Text.Get());
		TSharedPtr<FJsonObject> Json;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonText), Json) || !Json.IsValid())
		{
			return EReceiptCheck::Malformed;
		}
		// One product ("productId"), or the newer list ("productIds").
		const TArray<TSharedPtr<FJsonValue>>* Ids = nullptr;
		if (Json->TryGetArrayField(TEXT("productIds"), Ids))
		{
			for (const TSharedPtr<FJsonValue>& Id : *Ids)
			{
				FString Value;
				if (Id.IsValid() && Id->TryGetString(Value) && !Value.IsEmpty())
				{
					Out.ProductIds.Add(Value);
				}
			}
		}
		else if (FString Single; Json->TryGetStringField(TEXT("productId"), Single) && !Single.IsEmpty())
		{
			Out.ProductIds.Add(Single);
		}
		Json->TryGetStringField(TEXT("purchaseToken"), Out.PurchaseToken);
		Json->TryGetStringField(TEXT("orderId"), Out.OrderId);
		Json->TryGetBoolField(TEXT("acknowledged"), Out.bAcknowledged);
		if (Out.ProductIds.Num() == 0 || Out.PurchaseToken.IsEmpty())
		{
			return EReceiptCheck::Malformed;
		}
		FString Package;
		if (!ExpectedPackage.IsEmpty() && (!Json->TryGetStringField(TEXT("packageName"), Package) || Package != ExpectedPackage))
		{
			return EReceiptCheck::WrongPackage;
		}
		// Google always writes the state; the billing library reads a missing one as purchased, and so do we.
		int32 State = JsonStatePurchased;
		Json->TryGetNumberField(TEXT("purchaseState"), State);
		return State == JsonStatePurchased ? EReceiptCheck::Valid : State == JsonStatePending ? EReceiptCheck::Pending : EReceiptCheck::NotOwned;
	}

	EReceiptCheck CheckValidationInfo(const FString& ValidationInfo, const FString& LicenseKeyBase64, const FString& ExpectedPackage, FPurchase& Out)
	{
		Out = FPurchase();
		TSharedPtr<FJsonObject> Json;
		FString ReceiptData, Signature;
		TArray<uint8> PurchaseJson;
		if (ValidationInfo.IsEmpty() || ValidationInfo.Len() > MaxInputBytes
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ValidationInfo), Json) || !Json.IsValid()
			|| !Json->TryGetStringField(TEXT("receiptData"), ReceiptData) || !Json->TryGetStringField(TEXT("signature"), Signature)
			|| !DecodeBase64(ReceiptData, PurchaseJson))
		{
			return EReceiptCheck::Malformed;
		}
		return CheckPurchase(PurchaseJson, Signature, LicenseKeyBase64, ExpectedPackage, Out);
	}

	const TCHAR* CheckId(EReceiptCheck Check)
	{
		switch (Check)
		{
		case EReceiptCheck::Valid: return TEXT("valid");
		case EReceiptCheck::Pending: return TEXT("pending");
		case EReceiptCheck::NoKey: return TEXT("no-license-key");
		case EReceiptCheck::BadSignature: return TEXT("bad-signature");
		case EReceiptCheck::Malformed: return TEXT("malformed");
		case EReceiptCheck::WrongPackage: return TEXT("wrong-package");
		default: return TEXT("not-owned");
		}
	}
}
