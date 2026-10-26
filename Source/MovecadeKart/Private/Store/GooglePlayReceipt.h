#pragma once

#include "CoreMinimal.h"

/**
 * The on-device check of a Google Play purchase. Google signs every purchase (the purchase JSON, SHA1withRSA) with the
 * app's own key; the public half is the app's "license key" from Play Console (Monetization setup), set as
 * GooglePlayLicenseKey in the Android runtime settings. A purchase counts only if that signature verifies, it is for
 * this app, and it is PURCHASED (not PENDING). This stops the tools that answer the billing calls themselves (they
 * can't sign with Google's key); a fully rooted device can still lie about everything the app sees, which only a
 * server can rule out (Docs/Store/README.md "Receipt validation").
 *
 * Plain C++ on OpenSSL so it is built and tested on Windows (MovecadeKart.Store.PlayReceipt) as well as Android.
 */
namespace MovecadeKart::Store::GooglePlay
{
	enum class EReceiptCheck : uint8
	{
		/** Signed by Google for this app, and paid. */
		Valid,
		/** Signed, but the payment hasn't cleared (cash at a shop, a slow bank): not owned yet. */
		Pending,
		/** No license key configured: nothing can be verified, so nothing counts (fail closed). */
		NoKey,
		BadSignature,
		/** Not a purchase JSON the check understands. */
		Malformed,
		/** Signed, but for another app. */
		WrongPackage,
		/** Signed, but cancelled or in a state that isn't owned. */
		NotOwned,
	};

	/** The fields of a purchase the store uses. */
	struct FPurchase
	{
		TArray<FString> ProductIds;
		FString PurchaseToken;
		FString OrderId;
		bool bAcknowledged = false;
	};

	/**
	 * Checks a purchase as the engine's Google Play purchase interface reports it: a line item's ValidationInfo, the JSON
	 * {"receiptData": base64(purchase JSON), "signature": base64}. ExpectedPackage empty skips the package check.
	 * Out is filled for Valid and Pending.
	 */
	EReceiptCheck CheckValidationInfo(const FString& ValidationInfo, const FString& LicenseKeyBase64, const FString& ExpectedPackage, FPurchase& Out);

	/** The same check on the raw purchase JSON bytes and their signature. */
	EReceiptCheck CheckPurchase(const TArray<uint8>& PurchaseJson, const FString& SignatureBase64, const FString& LicenseKeyBase64, const FString& ExpectedPackage, FPurchase& Out);

	/** SHA1withRSA over Data, against a base64 X.509 SubjectPublicKeyInfo key (Play Console's license key). */
	bool VerifySignature(const TArray<uint8>& Data, const FString& SignatureBase64, const FString& LicenseKeyBase64);

	/** False on platforms built without the verifier: every check then fails closed. */
	bool IsVerifierAvailable();

	const TCHAR* CheckId(EReceiptCheck Check);
}
