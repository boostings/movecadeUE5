// The Steam Inventory Service backend. Compiled only when the build has Steamworks (MOVECADE_WITH_STEAM=1, set by
// MovecadeKart.Build.cs when MOVECADE_STEAM=1 is in the environment); see Docs/Store/README.md.
#include "Store/SteamInventoryBackend.h"

#if MOVECADE_WITH_STEAM

#include "Containers/Queue.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Store/KartStoreCatalog.h"

THIRD_PARTY_INCLUDES_START
#include "steam/steam_api.h"
#include "steam/isteaminventory.h"
THIRD_PARTY_INCLUDES_END

DEFINE_LOG_CATEGORY_STATIC(LogKartSteamStore, Log, All);

namespace MovecadeKart::Store
{
	namespace
	{
		/** steam_api64.dll is delay-loaded; load it from the engine's copy before the first Steam call. */
		bool LoadSteamApi()
		{
			static void* Handle = nullptr;
			if (Handle)
			{
				return true;
			}
#if PLATFORM_WINDOWS
			const FString Dir = FPaths::EngineDir() / TEXT("Binaries/ThirdParty/Steamworks") / STEAM_SDK_VER_PATH / TEXT("Win64/");
			FPlatformProcess::PushDllDirectory(*Dir);
			Handle = FPlatformProcess::GetDllHandle(*(Dir + TEXT("steam_api64.dll")));
			FPlatformProcess::PopDllDirectory(*Dir);
			if (!Handle)
			{
				// Packaged next to the executable.
				Handle = FPlatformProcess::GetDllHandle(TEXT("steam_api64.dll"));
			}
#else
			Handle = reinterpret_cast<void*>(1); // linked normally elsewhere
#endif
			return Handle != nullptr;
		}

		/** A result counts only when Steam signed it for the account that is signed in (not a copied or forged one). */
		bool IsOwnResult(SteamInventoryResult_t Handle, EResult Status)
		{
			// Expired: a cached result (Steam offline, or a saved proof older than an hour). Still signed, still ours.
			const bool bStatusOk = Status == k_EResultOK || Status == k_EResultExpired;
			return bStatusOk && SteamUser() && SteamInventory()->CheckResultSteamID(Handle, SteamUser()->GetSteamID());
		}

		TSet<int32> DefsOf(SteamInventoryResult_t Handle)
		{
			TSet<int32> Out;
			uint32 Count = 0;
			if (SteamInventory()->GetResultItems(Handle, nullptr, &Count) && Count > 0)
			{
				TArray<SteamItemDetails_t> Items;
				Items.SetNumZeroed(Count);
				if (SteamInventory()->GetResultItems(Handle, Items.GetData(), &Count))
				{
					for (uint32 i = 0; i < Count; ++i)
					{
						// A consumed or removed instance stays in the result with the removed flag.
						if ((Items[i].m_unFlags & k_ESteamItemRemoved) == 0 && Items[i].m_unQuantity > 0)
						{
							Out.Add(static_cast<int32>(Items[i].m_iDefinition));
						}
					}
				}
			}
			return Out;
		}
	}

	class FSteamInventoryBackend final : public IKartStoreBackend
	{
	public:
		explicit FSteamInventoryBackend(bool bInOwnsApi) : bOwnsApi(bInOwnsApi)
		{
			SteamInventory()->LoadItemDefinitions();
		}

		virtual ~FSteamInventoryBackend() override
		{
			PurchaseCall.Cancel();
			PricesCall.Cancel();
			if (bOwnsApi)
			{
				SteamAPI_Shutdown();
			}
		}

		virtual FName GetName() const override { return TEXT("steam"); }
		virtual EStorefront GetStorefront() const override { return EStorefront::Steam; }
		virtual bool IsOnline() const override { return SteamUser() && SteamUser()->BLoggedOn(); }

		virtual void Tick(double DeltaSeconds) override
		{
			if (bOwnsApi)
			{
				SteamAPI_RunCallbacks();
			}
			// Steam may call back on another thread (the online subsystem's pump): the work runs here.
			TFunction<void()> Work;
			while (GameThreadWork.Dequeue(Work))
			{
				Work();
			}
			PollProof(DeltaSeconds);
			if (Purchase.Def != 0)
			{
				Purchase.Age += DeltaSeconds;
				if (Purchase.Age > PurchaseTimeoutSeconds)
				{
					EndPurchase(EPurchaseResult::Failed, TEXT("checkout timed out"));
				}
			}
		}

		virtual void RefreshInventory(TFunction<void(bool, const TSet<int32>&)> Done) override
		{
			SteamInventoryResult_t Handle = k_SteamInventoryResultInvalid;
			if (!SteamInventory()->GetAllItems(&Handle))
			{
				LoadProof(MoveTemp(Done));
				return;
			}
			// Steam usually answers from its own cache when offline; if even that fails, the last signed inventory.
			Results.Add(Handle, [this, Done = MoveTemp(Done)](bool bOk, const TSet<int32>& Defs) mutable
			{
				if (bOk)
				{
					Done(true, Defs);
				}
				else
				{
					LoadProof(MoveTemp(Done));
				}
			});
		}

		virtual void RequestPrices(const TArray<int32>& DefIds, TFunction<void(bool, const TMap<int32, FPrice>&)> Done) override
		{
			PricesDone = MoveTemp(Done);
			PricesFor = DefIds;
			PricesCall.Set(SteamInventory()->RequestPrices(), this, &FSteamInventoryBackend::OnPrices);
		}

		virtual void StartPurchase(int32 DefId, TFunction<void(EPurchaseResult, const FString&)> Done) override
		{
			if (Purchase.Def != 0)
			{
				Done(EPurchaseResult::Unavailable, TEXT("another checkout is open"));
				return;
			}
			if (SteamUtils() && !SteamUtils()->IsOverlayEnabled())
			{
				Done(EPurchaseResult::Failed, TEXT("the Steam overlay is off (Steam > Settings > In Game)"));
				return;
			}
			Purchase = FPurchase();
			Purchase.Def = DefId;
			Purchase.Done = MoveTemp(Done);
			const SteamItemDef_t Defs[] = { DefId };
			const uint32 Quantities[] = { 1 };
			PurchaseCall.Set(SteamInventory()->StartPurchase(Defs, Quantities, 1), this, &FSteamInventoryBackend::OnStartPurchase);
		}

		virtual void GrantEarned(int32 DefId, FName Achievement, TFunction<void(bool)> Done) override
		{
			// The itemdef's promo rule is "ach:<Achievement>": unlock the achievement, then ask for the promo item.
			if (!Achievement.IsNone() && SteamUserStats())
			{
				SteamUserStats()->SetAchievement(TCHAR_TO_UTF8(*Achievement.ToString()));
				SteamUserStats()->StoreStats();
			}
			SteamInventoryResult_t Handle = k_SteamInventoryResultInvalid;
			if (!SteamInventory()->AddPromoItem(&Handle, DefId))
			{
				Done(false);
				return;
			}
			Results.Add(Handle, [DefId, Done = MoveTemp(Done)](bool bOk, const TSet<int32>& Defs) { Done(bOk && Defs.Contains(DefId)); });
		}

		virtual void TriggerPlaytimeDrop(int32 GeneratorDef, TFunction<void(bool, const TArray<int32>&)> Done) override
		{
			SteamInventoryResult_t Handle = k_SteamInventoryResultInvalid;
			if (!SteamInventory()->TriggerItemDrop(&Handle, GeneratorDef))
			{
				Done(false, {});
				return;
			}
			Results.Add(Handle, [Done = MoveTemp(Done)](bool bOk, const TSet<int32>& Defs) { Done(bOk, Defs.Array()); });
		}

	private:
		struct FPurchase
		{
			int32 Def = 0;
			uint64 OrderId = 0;
			bool bAuthorized = false;
			double Age = 0.0;
			TFunction<void(EPurchaseResult, const FString&)> Done;
		};

		static constexpr double PurchaseTimeoutSeconds = 15.0 * 60.0;
		/** A serialised inventory is a few hundred bytes per item; anything near this is not one. */
		static constexpr int32 MaxProofBytes = 1 << 20;
		static constexpr double ProofTimeoutSeconds = 10.0;

		struct FProof
		{
			TFunction<void(bool, const TSet<int32>&)> Done;
			double Age = 0.0;
		};

		void EndPurchase(EPurchaseResult Result, const FString& Detail)
		{
			TFunction<void(EPurchaseResult, const FString&)> Done = MoveTemp(Purchase.Done);
			Purchase = FPurchase();
			if (Done)
			{
				Done(Result, Detail);
			}
		}

		void OnStartPurchase(SteamInventoryStartPurchaseResult_t* Result, bool bIOFailure)
		{
			if (Purchase.Def == 0)
			{
				return;
			}
			if (bIOFailure || !Result || Result->m_result != k_EResultOK)
			{
				EndPurchase(EPurchaseResult::Failed, FString::Printf(TEXT("StartPurchase failed (EResult %d)"), Result ? static_cast<int32>(Result->m_result) : -1));
				return;
			}
			// The overlay shows the checkout now; MicroTxnAuthorizationResponse_t says how it ended.
			Purchase.OrderId = Result->m_ulOrderID;
			UE_LOG(LogKartSteamStore, Log, TEXT("Checkout open for %d, order %llu"), Purchase.Def, Purchase.OrderId);
		}

		void OnPrices(SteamInventoryRequestPricesResult_t* Result, bool bIOFailure)
		{
			TMap<int32, FPrice> Prices;
			const bool bOk = !bIOFailure && Result && Result->m_result == k_EResultOK;
			if (bOk)
			{
				const FString Currency = UTF8_TO_TCHAR(Result->m_rgchCurrency);
				for (int32 Def : PricesFor)
				{
					uint64 Current = 0, Base = 0;
					if (SteamInventory()->GetItemPrice(Def, &Current, &Base))
					{
						Prices.Add(Def, { Current, Base, Currency });
					}
				}
			}
			if (PricesDone)
			{
				TFunction<void(bool, const TMap<int32, FPrice>&)> Done = MoveTemp(PricesDone);
				Done(bOk, Prices);
			}
		}

		/** The last full inventory, as Steam serialised and signed it, per account. */
		static FString ProofPath()
		{
			const uint64 Account = SteamUser() ? SteamUser()->GetSteamID().ConvertToUint64() : 0;
			return FPaths::ProjectSavedDir() / TEXT("SaveGames") / FString::Printf(TEXT("KartStoreSteam_%llu.proof"), Account);
		}

		/** Called with a full inventory result (any thread): keeps Steam's signed copy for offline launches. */
		void SaveProof(SteamInventoryResult_t Handle)
		{
			uint32 Size = 0;
			if (!SteamInventory()->SerializeResult(Handle, nullptr, &Size) || Size == 0 || Size > MaxProofBytes)
			{
				return;
			}
			TArray<uint8> Bytes;
			Bytes.SetNumUninitialized(Size);
			if (SteamInventory()->SerializeResult(Handle, Bytes.GetData(), &Size))
			{
				GameThreadWork.Enqueue([Bytes = MoveTemp(Bytes)]() { FFileHelper::SaveArrayToFile(Bytes, *ProofPath()); });
			}
		}

		/** The saved signed inventory, checked by Steam (signature) and against the signed-in account. */
		void LoadProof(TFunction<void(bool, const TSet<int32>&)> Done)
		{
			TArray<uint8> Bytes;
			SteamInventoryResult_t Handle = k_SteamInventoryResultInvalid;
			if (Proof.Done || !FFileHelper::LoadFileToArray(Bytes, *ProofPath()) || Bytes.Num() > MaxProofBytes
				|| !SteamInventory()->DeserializeResult(&Handle, Bytes.GetData(), Bytes.Num()))
			{
				Done(false, {});
				return;
			}
			{
				FScopeLock Guard(&ProofLock);
				ProofHandle = Handle;
			}
			Proof.Done = MoveTemp(Done);
			Proof.Age = 0.0;
		}

		/** A deserialised result is polled here, not through the result callback (which ignores it). */
		void PollProof(double DeltaSeconds)
		{
			if (!Proof.Done)
			{
				return;
			}
			Proof.Age += DeltaSeconds;
			const EResult Status = SteamInventory()->GetResultStatus(ProofHandle);
			if (Status == k_EResultPending && Proof.Age < ProofTimeoutSeconds)
			{
				return;
			}
			const bool bOk = IsOwnResult(ProofHandle, Status);
			const TSet<int32> Defs = bOk ? DefsOf(ProofHandle) : TSet<int32>();
			SteamInventory()->DestroyResult(ProofHandle);
			{
				FScopeLock Guard(&ProofLock);
				ProofHandle = k_SteamInventoryResultInvalid;
			}
			UE_LOG(LogKartSteamStore, Log, TEXT("Saved Steam inventory: %s (%d items)"), bOk ? TEXT("verified") : TEXT("rejected"), Defs.Num());
			TFunction<void(bool, const TSet<int32>&)> Done = MoveTemp(Proof.Done);
			Proof = FProof();
			Done(bOk, Defs);
		}

		bool IsProofHandle(SteamInventoryResult_t Handle)
		{
			FScopeLock Guard(&ProofLock);
			return Handle == ProofHandle;
		}

		STEAM_CALLBACK(FSteamInventoryBackend, OnResultReady, SteamInventoryResultReady_t);
		STEAM_CALLBACK(FSteamInventoryBackend, OnFullUpdate, SteamInventoryFullUpdate_t);
		STEAM_CALLBACK(FSteamInventoryBackend, OnMicroTxn, MicroTxnAuthorizationResponse_t);

		void FinishPurchaseIfOwned(const TSet<int32>& Owned)
		{
			if (Purchase.Def == 0 || !Purchase.bAuthorized)
			{
				return;
			}
			const FItem* Item = FindItem(Purchase.Def);
			bool bHave = Owned.Contains(Purchase.Def);
			if (Item && Item->Slot == ESlot::Bundle)
			{
				bHave = Item->Contents.Num() > 0;
				for (int32 Def : Item->Contents)
				{
					bHave &= Owned.Contains(Def);
				}
			}
			if (bHave)
			{
				EndPurchase(EPurchaseResult::Success, FString());
			}
		}

		const bool bOwnsApi;
		TMap<SteamInventoryResult_t, TFunction<void(bool, const TSet<int32>&)>> Results;
		TQueue<TFunction<void()>, EQueueMode::Mpsc> GameThreadWork;
		FPurchase Purchase;
		CCallResult<FSteamInventoryBackend, SteamInventoryStartPurchaseResult_t> PurchaseCall;
		CCallResult<FSteamInventoryBackend, SteamInventoryRequestPricesResult_t> PricesCall;
		TFunction<void(bool, const TMap<int32, FPrice>&)> PricesDone;
		TArray<int32> PricesFor;
		FProof Proof;
		/** Read on the callback thread too (OnResultReady skips it). */
		SteamInventoryResult_t ProofHandle = k_SteamInventoryResultInvalid;
		FCriticalSection ProofLock;
	};

	void FSteamInventoryBackend::OnResultReady(SteamInventoryResultReady_t* Event)
	{
		const SteamInventoryResult_t Handle = Event->m_handle;
		if (IsProofHandle(Handle))
		{
			return; // PollProof owns it
		}
		const bool bOk = IsOwnResult(Handle, Event->m_result);
		const TSet<int32> Defs = bOk ? DefsOf(Handle) : TSet<int32>();
		SteamInventory()->DestroyResult(Handle);
		GameThreadWork.Enqueue([this, Handle, bOk, Defs]()
		{
			if (TFunction<void(bool, const TSet<int32>&)> Done; Results.RemoveAndCopyValue(Handle, Done) && Done)
			{
				Done(bOk, Defs);
			}
		});
	}

	void FSteamInventoryBackend::OnFullUpdate(SteamInventoryFullUpdate_t* Event)
	{
		// A full inventory (from GetAllItems) newer than the last one: the authoritative owned set.
		if (!IsOwnResult(Event->m_handle, SteamInventory()->GetResultStatus(Event->m_handle)))
		{
			return;
		}
		SaveProof(Event->m_handle);
		const TSet<int32> Defs = DefsOf(Event->m_handle);
		GameThreadWork.Enqueue([this, Defs]()
		{
			FinishPurchaseIfOwned(Defs);
			if (OnInventoryChanged)
			{
				OnInventoryChanged(Defs);
			}
		});
	}

	void FSteamInventoryBackend::OnMicroTxn(MicroTxnAuthorizationResponse_t* Event)
	{
		const uint64 Order = Event->m_ulOrderID;
		const bool bAuthorized = Event->m_bAuthorized != 0;
		GameThreadWork.Enqueue([this, Order, bAuthorized]()
		{
			if (Purchase.Def == 0 || (Purchase.OrderId != 0 && Purchase.OrderId != Order))
			{
				return;
			}
			if (!bAuthorized)
			{
				EndPurchase(EPurchaseResult::Cancelled, TEXT("declined in the overlay"));
				return;
			}
			// Paid: the item lands in the inventory shortly. Ask until it's there (the full update ends the purchase).
			Purchase.bAuthorized = true;
			RefreshInventory([this](bool bOk, const TSet<int32>& Owned) { if (bOk) { FinishPurchaseIfOwned(Owned); } });
		});
	}

	TUniquePtr<IKartStoreBackend> MakeSteamBackend(int32 DevAppId)
	{
		if (!LoadSteamApi())
		{
			UE_LOG(LogKartSteamStore, Display, TEXT("steam_api64.dll not found: using the offline store"));
			return nullptr;
		}
		bool bOwnsApi = false;
		if (!SteamInventory())
		{
			if (!SteamAPI_IsSteamRunning())
			{
				UE_LOG(LogKartSteamStore, Display, TEXT("Steam isn't running: using the offline store"));
				return nullptr;
			}
#if !UE_BUILD_SHIPPING
			// Development runs outside Steam need an app id: Spacewar (480) until the game's own exists.
			if (FPlatformMisc::GetEnvironmentVariable(TEXT("SteamAppId")).IsEmpty())
			{
				FPlatformMisc::SetEnvironmentVar(TEXT("SteamAppId"), *FString::FromInt(DevAppId));
				FPlatformMisc::SetEnvironmentVar(TEXT("SteamGameId"), *FString::FromInt(DevAppId));
			}
#endif
			SteamErrMsg Error;
			if (SteamAPI_InitEx(&Error) != k_ESteamAPIInitResult_OK)
			{
				UE_LOG(LogKartSteamStore, Warning, TEXT("SteamAPI_InitEx failed: %s"), UTF8_TO_TCHAR(Error));
				return nullptr;
			}
			bOwnsApi = true;
		}
		if (!SteamInventory())
		{
			return nullptr;
		}
		UE_LOG(LogKartSteamStore, Display, TEXT("Steam Inventory ready (app %u)"), SteamUtils() ? SteamUtils()->GetAppID() : 0u);
		return MakeUnique<FSteamInventoryBackend>(bOwnsApi);
	}
}

#else // MOVECADE_WITH_STEAM

#if UE_BUILD_SHIPPING && PLATFORM_DESKTOP
// MovecadeKart.Build.cs turns Steam on for every desktop Shipping game; a shipped desktop store without it sells nothing
// and confirms nothing. (iOS and Android sell through their own backends: AppStoreBackend, GooglePlayBackend.)
#error "Shipping builds need the Steam Inventory backend (MOVECADE_WITH_STEAM=1)."
#endif

namespace MovecadeKart::Store
{
	TUniquePtr<IKartStoreBackend> MakeSteamBackend(int32 DevAppId)
	{
		return nullptr; // no Steamworks in this build: the subsystem uses the offline store
	}
}

#endif // MOVECADE_WITH_STEAM
