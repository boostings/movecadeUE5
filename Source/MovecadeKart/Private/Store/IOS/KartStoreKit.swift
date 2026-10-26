// StoreKit 2 for the cosmetics store (Store/IOS/AppStoreBackend.cpp calls this through the generated
// MovecadeKart-Swift.h). StoreKit 2 checks Apple's signature on every transaction on the device: only
// `.verified` transactions, never revoked (refunded), count. Every completion runs on the main thread;
// the C++ side moves the result to the game thread.

import Foundation
import StoreKit

/// One product as the App Store prices it for this player's storefront.
@objc(MovecadeStoreKitProduct)
public final class MovecadeStoreKitProduct: NSObject
{
	@objc public let productId: String
	/// StoreKit's own formatted price ("1,09 €"): exactly what the purchase sheet shows.
	@objc public let displayPrice: String
	@objc public let currencyCode: String
	/// The price in the currency's minor units (cents).
	@objc public let priceMinorUnits: Int64

	init(productId: String, displayPrice: String, currencyCode: String, priceMinorUnits: Int64)
	{
		self.productId = productId
		self.displayPrice = displayPrice
		self.currencyCode = currencyCode
		self.priceMinorUnits = priceMinorUnits
	}
}

/// How a purchase ended. Kept in step with EAppStorePurchase in AppStoreBackend.cpp.
@objc(MovecadeStoreKitResult)
public enum MovecadeStoreKitResult: Int
{
	case purchased = 0
	case cancelled = 1
	case pending = 2
	case failed = 3
	case unverified = 4
}

@objc(MovecadeStoreKit)
public final class MovecadeStoreKit: NSObject
{
	/// Started and stopped only by the store backend on the game thread, one at a time.
	nonisolated(unsafe) private static var updatesTask: Task<Void, Never>?

	private static func onMain(_ work: @escaping () -> Void)
	{
		DispatchQueue.main.async(execute: work)
	}

	/// False when the device can't pay at all (Screen Time content restrictions).
	@objc(canMakePayments)
	public static func canMakePayments() -> Bool
	{
		return AppStore.canMakePayments
	}

	/// Calls onChange whenever a transaction arrives outside a purchase: an Ask to Buy approval, a purchase made on
	/// another device, a refund or a revocation. Finishes verified transactions (StoreKit redelivers unfinished ones).
	@objc(startListening:)
	public static func startListening(_ onChange: @escaping () -> Void)
	{
		updatesTask?.cancel()
		updatesTask = Task.detached
		{
			for await update in Transaction.updates
			{
				if case .verified(let transaction) = update
				{
					await transaction.finish()
				}
				onMain(onChange)
			}
		}
	}

	@objc(stopListening)
	public static func stopListening()
	{
		updatesTask?.cancel()
		updatesTask = nil
	}

	/// The products and their prices for this player's storefront. ok is false when the App Store couldn't be reached.
	@objc(requestProducts:completion:)
	public static func requestProducts(_ productIds: [String], completion: @escaping ([MovecadeStoreKitProduct], Bool) -> Void)
	{
		Task
		{
			do
			{
				let products = try await Product.products(for: productIds)
				let out = products.map
				{ product -> MovecadeStoreKitProduct in
					let code = product.priceFormatStyle.currencyCode
					let formatter = NumberFormatter()
					formatter.numberStyle = .currency
					formatter.currencyCode = code
					let minor = (product.price as NSDecimalNumber).multiplying(byPowerOf10: Int16(formatter.maximumFractionDigits))
					return MovecadeStoreKitProduct(productId: product.id, displayPrice: product.displayPrice, currencyCode: code, priceMinorUnits: minor.int64Value)
				}
				onMain { completion(out, true) }
			}
			catch
			{
				onMain { completion([], false) }
			}
		}
	}

	/// The non-consumables this Apple Account owns now: verified by StoreKit, not refunded or revoked. Works offline
	/// (StoreKit keeps the signed transactions on the device) and never shows a sign-in sheet.
	@objc(currentEntitlements:)
	public static func currentEntitlements(_ completion: @escaping ([String]) -> Void)
	{
		Task
		{
			var owned: [String] = []
			for await result in Transaction.currentEntitlements
			{
				if case .verified(let transaction) = result, transaction.revocationDate == nil, transaction.productType == .nonConsumable
				{
					owned.append(transaction.productID)
				}
			}
			onMain { completion(owned) }
		}
	}

	/// Restore purchases: asks the App Store for this account's transactions. Only when the player asks: it may show
	/// a sign-in sheet. ok is false when it failed or the player closed the sheet.
	@objc(sync:)
	public static func sync(_ completion: @escaping (Bool) -> Void)
	{
		Task
		{
			do
			{
				try await AppStore.sync()
				onMain { completion(true) }
			}
			catch
			{
				onMain { completion(false) }
			}
		}
	}

	/// Shows Apple's purchase sheet for one product. result is a MovecadeStoreKitResult raw value.
	@objc(purchase:completion:)
	public static func purchase(_ productId: String, completion: @escaping (Int, String) -> Void)
	{
		Task
		{
			let finish = { (result: MovecadeStoreKitResult, detail: String) in onMain { completion(result.rawValue, detail) } }
			do
			{
				guard let product = try await Product.products(for: [productId]).first else
				{
					finish(.failed, "product not found in the App Store")
					return
				}
				switch try await product.purchase()
				{
				case .success(let verification):
					switch verification
					{
					case .verified(let transaction):
						await transaction.finish()
						finish(.purchased, "")
					case .unverified(_, let error):
						finish(.unverified, error.localizedDescription)
					}
				case .userCancelled:
					finish(.cancelled, "")
				case .pending:
					finish(.pending, "")
				@unknown default:
					finish(.failed, "unknown purchase result")
				}
			}
			catch
			{
				finish(.failed, error.localizedDescription)
			}
		}
	}
}
