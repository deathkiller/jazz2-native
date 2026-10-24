#if defined(WITH_ANGELSCRIPT)

#include "ObjectHost.h"
#include "WorldCanvas.h"
#include "AnimationTables.h"
#include "EventTranslator.h"
#include "../LevelScriptLoader.h"
#include "../../ContentResolver.h"
#include "../../ILevelHandler.h"
#include "../../LevelHandler.h"
#include "../../Actors/Explosion.h"
#include "../../Actors/Player.h"
#include "../../Actors/Collectibles/CollectibleBase.h"
#include "../../Actors/Enemies/Dragon.h"
#include "../../Actors/Enemies/Monkey.h"
#include "../../Actors/Solid/GenericContainer.h"
#include "../../Actors/Weapons/ShotBase.h"
#include "../../Actors/Weapons/TNT.h"
#include "../../Events/EventMap.h"
#include "../../Tiles/TileMap.h"

#include <algorithm>
#include <cstring>


namespace Jazz2::Scripting::Legacy
{
	namespace
	{
		// Value of BEHAVIOR::DEFAULT, the behavior the object has in the game
		constexpr std::uint32_t DefaultBehavior = UINT32_MAX;

		// HANDLING::Player and HANDLING::Bullet of JJ2+
		constexpr std::uint32_t hPLAYERBULLET = 1;
		constexpr std::uint32_t hENEMYBULLET = 2;
		constexpr std::uint32_t hPARTICLE = 3;
		constexpr std::uint32_t hPICKUP = 5;
		constexpr std::uint32_t hSPECIAL = 8;
		constexpr std::uint32_t hIGNOREBULLET = 1;
		constexpr std::uint32_t hDESTROYBULLET = 2;

		// CREATOR::Type of JJ2+
		constexpr std::uint32_t cOBJECT = 0;
		constexpr std::uint32_t cPLAYER = 2;

		// BEHAVIOR::EXPLOSION2 of JJ2+, the end of a collected pickup
		constexpr std::uint32_t BehaviorExplosion2 = aCOUNT + 21;
		// BEHAVIOR::MONKEYBULLET of JJ2+, a banana thrown by a monkey
		constexpr std::uint32_t BehaviorMonkeyBullet = aCOUNT + 13;

		// The `playerHandling` the template of the objects of an event has before a script changes it, PICKUP for the
		// pickups of the original - ammo, coins, gems, food and the power-ups collected by touching them - PLAYERBULLET
		// for the bullets of the players, and ENEMY (0) for the rest, which the game handles on its own anyway (see
		// ScriptObjectHost::OnCollision())
		std::uint32_t GetDefaultPlayerHandling(std::uint8_t eventId)
		{
			if (eventId >= aPLAYERBULLET1 && eventId <= aPLAYERBULLETC3) {
				return hPLAYERBULLET;
			}
			switch (eventId) {
				case aREDGEMTEMP:
				case aGUN3AMMO3: case aGUN2AMMO3: case aGUN4AMMO3: case aGUN5AMMO3:
				case aGUN6AMMO3: case aGUN7AMMO3: case aGUN8AMMO3: case aGUN9AMMO3:
				case aSILVERCOIN: case aGOLDCOIN:
				case aGUN3AMMO15: case aGUN2AMMO15: case aGUN4AMMO15: case aGUN5AMMO15: case aGUN6AMMO15:
				case aGUNFASTFIRE:
				case aREDGEM: case aGREENGEM: case aBLUEGEM: case aPURPLEGEM: case aSUPERREDGEM:
				case aENERGYUP: case aFULLENERGY:
				case aFIRESHIELD: case aWATERSHIELD: case aLIGHTSHIELD: case aLASERSHIELD:
				case aFASTFEET: case aEXTRALIFE: case aINVINCIBILITY: case aEXTRATIME: case aFREEZER: case aSTOPWATCH:
				case aFLYCARROT:
				case aRECTREDGEM: case aRECTGREENGEM: case aRECTBLUEGEM:
					return hPICKUP;
				default:
					// Food, two runs of the events
					return ((eventId >= aAPPLE && eventId <= aSTRAWBERRY) || (eventId >= aLEMON && eventId <= aCHEESE) ? hPICKUP : 0);
			}
		}

		// The animations the templates of the objects have in JJ2+ before a script changes them, by the event - as measured
		// in JJ2+ over the levels of the original and of a few custom episodes, `mCOUNT` where there's none, an event that
		// isn't listed has none or its set wasn't loaded in any of them
		constexpr PresetAnimation PresetAnimations[] = {
			{ aPLAYERBULLET1, mAMMO, 17, mAMMO, 80 },
			{ aPLAYERBULLET2, mAMMO, 23, mAMMO, 4 },
			{ aPLAYERBULLET3, mAMMO, 27, mAMMO, 82 },
			{ aPLAYERBULLET4, mAMMO, 37, mAMMO, 5 },
			{ aPLAYERBULLET5, mAMMO, 45, mAMMO, 3 },
			{ aPLAYERBULLET6, mAMMO, 55, mCOUNT, 0 },
			{ aPLAYERBULLET8, mAMMO, 60, mAMMO, 80 },
			{ aPLAYERBULLET9, mAMMO, 75, mCOUNT, 0 },
			{ aPLAYERBULLETP1, mAMMO, 20, mAMMO, 80 },
			{ aPLAYERBULLETP2, mAMMO, 26, mAMMO, 4 },
			{ aPLAYERBULLETP3, mAMMO, 30, mAMMO, 82 },
			{ aPLAYERBULLETP4, mAMMO, 42, mAMMO, 5 },
			{ aPLAYERBULLETP5, mAMMO, 50, mAMMO, 3 },
			{ aPLAYERBULLETP6, mAMMO, 58, mCOUNT, 0 },
			{ aPLAYERBULLETP8, mAMMO, 63, mAMMO, 80 },
			{ aPLAYERBULLETP9, mAMMO, 75, mCOUNT, 0 },
			{ aPLAYERBULLETC1, mAMMO, 14, mCOUNT, 0 },
			{ aPLAYERBULLETC2, mAMMO, 8, mCOUNT, 0 },
			{ aPLAYERBULLETC3, mAMMO, 11, mCOUNT, 0 },
			{ aCATSMOKE, mCATERPIL, 6, mCOUNT, 0 },
			{ aREDGEMTEMP, mPICKUPS, 35, mPICKUPS, 86 },
			{ aUTERUSEL, mUTERUS, 6, mAMMO, 7 },
			{ aBIRD, mCOUNT, 0, mBIRD, 5 },
			{ aBUBBLE, mCOMMON, 1, mCOUNT, 0 },
			{ aGUN3AMMO3, mAMMO, 29, mPICKUPS, 86 },
			{ aGUN2AMMO3, mAMMO, 25, mPICKUPS, 86 },
			{ aGUN4AMMO3, mAMMO, 34, mPICKUPS, 86 },
			{ aGUN5AMMO3, mAMMO, 49, mPICKUPS, 86 },
			{ aGUN6AMMO3, mAMMO, 57, mPICKUPS, 86 },
			{ aGUN7AMMO3, mAMMO, 59, mPICKUPS, 86 },
			{ aGUN8AMMO3, mAMMO, 62, mPICKUPS, 86 },
			{ aGUN9AMMO3, mAMMO, 68, mPICKUPS, 86 },
			{ aTURTLESHELL, mTURTLE, 4, mCOUNT, 0 },
			{ aSWINGVINE, mVINE, 0, mCOUNT, 0 },
			{ aBOMB, mAMMO, 1, mAMMO, 81 },
			{ aSILVERCOIN, mPICKUPS, 84, mPICKUPS, 86 },
			{ aGOLDCOIN, mPICKUPS, 37, mPICKUPS, 86 },
			{ aGUNCRATE, mPICKUPS, 5, mPICKUPS, 5 },
			{ aCARROTCRATE, mPICKUPS, 5, mPICKUPS, 5 },
			{ a1UPCRATE, mPICKUPS, 5, mPICKUPS, 5 },
			{ aGEMBARREL, mPICKUPS, 3, mCOUNT, 0 },
			{ aCARROTBARREL, mPICKUPS, 3, mCOUNT, 0 },
			{ a1UPBARREL, mPICKUPS, 3, mCOUNT, 0 },
			{ aBOMBCRATE, mPICKUPS, 5, mPICKUPS, 5 },
			{ aGUN3AMMO15, mPICKUPS, 55, mAMMO, 71 },
			{ aGUN2AMMO15, mPICKUPS, 54, mAMMO, 71 },
			{ aGUN4AMMO15, mPICKUPS, 56, mAMMO, 71 },
			{ aGUN5AMMO15, mPICKUPS, 57, mAMMO, 71 },
			{ aGUN6AMMO15, mPICKUPS, 58, mAMMO, 71 },
			{ aTNT, mAMMO, 59, mAMMO, 77 },
			{ aAIRBOARDGENERATOR, mPICKUPS, 86, mCOUNT, 0 },
			{ aFROZENGREENSPRING, mSPRING, 5, mCOUNT, 0 },
			{ aGUNFASTFIRE, mPICKUPS, 29, mPICKUPS, 86 },
			{ aSPRINGCRATE, mPICKUPS, 5, mPICKUPS, 5 },
			{ aREDGEM, mPICKUPS, 35, mPICKUPS, 86 },
			{ aGREENGEM, mPICKUPS, 35, mPICKUPS, 86 },
			{ aBLUEGEM, mPICKUPS, 35, mPICKUPS, 86 },
			{ aPURPLEGEM, mPICKUPS, 35, mPICKUPS, 86 },
			{ aSUPERREDGEM, mPICKUPS, 34, mPICKUPS, 86 },
			{ aGUNBARREL, mPICKUPS, 3, mCOUNT, 0 },
			{ aGEMCRATE, mPICKUPS, 5, mPICKUPS, 5 },
			{ aMORPHMONITOR, mPICKUPS, 70, mAMMO, 71 },
			{ aENERGYUP, mPICKUPS, 21, mPICKUPS, 86 },
			{ aFULLENERGY, mPICKUPS, 82, mPICKUPS, 86 },
			{ aFIRESHIELD, mPICKUPS, 31, mAMMO, 71 },
			{ aWATERSHIELD, mPICKUPS, 10, mAMMO, 71 },
			{ aLIGHTSHIELD, mPICKUPS, 51, mAMMO, 71 },
			{ aFASTFEET, mPICKUPS, 33, mPICKUPS, 86 },
			{ aEXTRALIFE, mPICKUPS, 0, mPICKUPS, 86 },
			{ aENDOFLEVELPOST, mPICKUPS, 28, mCOUNT, 0 },
			{ aSAVEPOST, mPICKUPS, 14, mCOUNT, 0 },
			{ aBONUSLEVELPOST, mBONUS, 1, mCOUNT, 0 },
			{ aREDSPRING, mSPRING, 7, mCOUNT, 0 },
			{ aGREENSPRING, mSPRING, 5, mCOUNT, 0 },
			{ aBLUESPRING, mSPRING, 0, mCOUNT, 0 },
			{ aINVINCIBILITY, mPICKUPS, 72, mPICKUPS, 86 },
			{ aEXTRATIME, mPICKUPS, 87, mPICKUPS, 86 },
			{ aFREEZER, mPICKUPS, 42, mPICKUPS, 86 },
			{ aHREDSPRING, mSPRING, 8, mCOUNT, 0 },
			{ aHGREENSPRING, mSPRING, 6, mCOUNT, 0 },
			{ aHBLUESPRING, mSPRING, 1, mCOUNT, 0 },
			{ aBIRDMORPHMONITOR, mPICKUPS, 68, mAMMO, 71 },
			{ aTRIGGERCRATE, mPICKUPS, 52, mCOUNT, 0 },
			{ aFLYCARROT, mPICKUPS, 40, mPICKUPS, 86 },
			{ aRECTREDGEM, mPICKUPS, 22, mPICKUPS, 86 },
			{ aRECTGREENGEM, mPICKUPS, 22, mPICKUPS, 86 },
			{ aRECTBLUEGEM, mPICKUPS, 22, mPICKUPS, 86 },
			{ aTUFTURT, mTUFTURT, 0, mAMMO, 81 },
			{ aTUFBOSS, mTUFBOSS, 5, mAMMO, 80 },
			{ aLABRAT, mLABRAT, 2, mAMMO, 80 },
			{ aDRAGON, mDRAGON, 1, mAMMO, 80 },
			{ aLIZARD, mLIZARD, 4, mAMMO, 80 },
			{ aBUMBEE, mBUMBEE, 0, mAMMO, 80 },
			{ aRAPIER, mRAPIER, 2, mAMMO, 80 },
			{ aSPARK, mSPARK, 0, mAMMO, 80 },
			{ aBAT, mBAT, 1, mAMMO, 80 },
			{ aSUCKER, mSUCKER, 6, mAMMO, 80 },
			{ aCATERPILLAR, mCATERPIL, 3, mCOUNT, 0 },
			{ aCHESHIRE1, mCAT, 2, mAMMO, 81 },
			{ aCHESHIRE2, mCOUNT, 0, mAMMO, 81 },
			{ aHATTER, mHATTER, 4, mAMMO, 81 },
			{ aBILSYBOSS, mBILSBOSS, 4, mAMMO, 80 },
			{ aSKELETON, mSKELETON, 2, mAMMO, 81 },
			{ aDOGGYDOGG, mDOG, 1, mAMMO, 81 },
			{ aNORMTURTLE, mTURTLE, 7, mAMMO, 81 },
			{ aHELMUT, mHELMUT, 1, mAMMO, 80 },
			{ aDEMON, mDEMON, 2, mAMMO, 81 },
			{ aDRAGONFLY, mDRAGFLY, 0, mAMMO, 80 },
			{ aMONKEY, mMONKEY, 6, mAMMO, 80 },
			{ aFATCHK, mFATCHK, 1, mAMMO, 80 },
			{ aFENCER, mFENCER, 1, mAMMO, 80 },
			{ aFISH, mFISH, 1, mAMMO, 80 },
			{ aMOTH, mMOTH, 3, mCOUNT, 0 },
			{ aSTEAM, mSTEAM, 0, mCOUNT, 0 },
			{ aROCK, mROCK, 0, mCOUNT, 0 },
			{ aGUN1POWER, mPICKUPS, 60, mAMMO, 71 },
			{ aGUN2POWER, mPICKUPS, 61, mAMMO, 71 },
			{ aGUN3POWER, mPICKUPS, 62, mAMMO, 71 },
			{ aGUN4POWER, mPICKUPS, 63, mAMMO, 71 },
			{ aGUN5POWER, mPICKUPS, 64, mAMMO, 71 },
			{ aGUN6POWER, mPICKUPS, 65, mAMMO, 71 },
			{ aPINLEFTPADDLE, mPINBALL, 4, mCOUNT, 0 },
			{ aPINRIGHTPADDLE, mPINBALL, 5, mCOUNT, 0 },
			{ aPIN500BUMP, mPINBALL, 1, mCOUNT, 0 },
			{ aPINCARROTBUMP, mPINBALL, 3, mCOUNT, 0 },
			{ aAPPLE, mPICKUPS, 1, mPICKUPS, 86 },
			{ aBANANA, mPICKUPS, 2, mPICKUPS, 86 },
			{ aCHERRY, mPICKUPS, 16, mPICKUPS, 86 },
			{ aORANGE, mPICKUPS, 71, mPICKUPS, 86 },
			{ aPEAR, mPICKUPS, 74, mPICKUPS, 86 },
			{ aPRETZEL, mPICKUPS, 79, mPICKUPS, 86 },
			{ aSTRAWBERRY, mPICKUPS, 81, mPICKUPS, 86 },
			{ aQUEENBOSS, mQUEEN, 3, mCOUNT, 0 },
			{ aFLOATSUCKER, mSUCKER, 4, mAMMO, 80 },
			{ aLEMON, mPICKUPS, 48, mPICKUPS, 86 },
			{ aLIME, mPICKUPS, 50, mPICKUPS, 86 },
			{ aTHING, mPICKUPS, 89, mPICKUPS, 86 },
			{ aWMELON, mPICKUPS, 92, mPICKUPS, 86 },
			{ aPEACH, mPICKUPS, 73, mPICKUPS, 86 },
			{ aGRAPES, mPICKUPS, 38, mPICKUPS, 86 },
			{ aLETTUCE, mPICKUPS, 49, mPICKUPS, 86 },
			{ aEGGPLANT, mPICKUPS, 26, mPICKUPS, 86 },
			{ aCUCUMB, mPICKUPS, 23, mPICKUPS, 86 },
			{ aCOKE, mPICKUPS, 20, mPICKUPS, 86 },
			{ aPEPSI, mPICKUPS, 75, mPICKUPS, 86 },
			{ aMILK, mPICKUPS, 53, mPICKUPS, 86 },
			{ aPIE, mPICKUPS, 76, mPICKUPS, 86 },
			{ aCAKE, mPICKUPS, 12, mPICKUPS, 86 },
			{ aDONUT, mPICKUPS, 25, mPICKUPS, 86 },
			{ aCUPCAKE, mPICKUPS, 24, mPICKUPS, 86 },
			{ aCHIPS, mPICKUPS, 18, mPICKUPS, 86 },
			{ aCANDY1, mPICKUPS, 13, mPICKUPS, 86 },
			{ aCHOCBAR, mPICKUPS, 19, mPICKUPS, 86 },
			{ aICECREAM, mPICKUPS, 43, mPICKUPS, 86 },
			{ aBURGER, mPICKUPS, 11, mPICKUPS, 86 },
			{ aPIZZA, mPICKUPS, 77, mPICKUPS, 86 },
			{ aFRIES, mPICKUPS, 32, mPICKUPS, 86 },
			{ aCHICKLEG, mPICKUPS, 17, mPICKUPS, 86 },
			{ aSANDWICH, mPICKUPS, 80, mPICKUPS, 86 },
			{ aTACOBELL, mPICKUPS, 88, mPICKUPS, 86 },
			{ aWEENIE, mPICKUPS, 91, mPICKUPS, 86 },
			{ aHAM, mPICKUPS, 39, mPICKUPS, 86 },
			{ aCHEESE, mPICKUPS, 15, mPICKUPS, 86 },
			{ aFLOATLIZARD, mLIZARD, 2, mAMMO, 80 },
			{ aSTANDMONKEY, mMONKEY, 2, mAMMO, 80 },
			{ aDESTRUCTSCENERY, mDESTSCEN, 0, mCOUNT, 0 },
			{ aDESTRUCTSCENERYBOMB, mDESTSCEN, 0, mCOUNT, 0 },
			{ aCOLLAPSESCENERY, mDESTSCEN, 0, mCOUNT, 0 },
			{ aSTOMPSCENERY, mDESTSCEN, 0, mCOUNT, 0 },
			{ aGEMSTOMP, mDESTSCEN, 0, mCOUNT, 0 },
			{ aRAVEN, mRAVEN, 1, mAMMO, 80 },
			{ aTUBETURTLE, mTUBETURT, 0, mAMMO, 80 },
			{ aGEMRING, mPICKUPS, 35, mCOUNT, 0 },
			{ aROTSMALLTREE, mSMALTREE, 0, mCOUNT, 0 },
			{ aUTERUS, mUTERUS, 2, mCOUNT, 0 },
			{ aCRAB, mUTERUS, 7, mAMMO, 81 },
			{ aROCKTURT, mCOUNT, 0, mAMMO, 80 },
			{ aBUBBA, mBUBBA, 0, mAMMO, 80 },
			{ aDEVILDEVAN, mDEVILDEVAN, 8, mAMMO, 80 },
			{ aDEVANROBOT, mDEVAN, 1, mCOUNT, 0 },
			{ aROBOT, mROBOT, 3, mCOUNT, 0 },
			{ aCARROTUSPOLE, mCARROTPOLE, 0, mCOUNT, 0 },
			{ aDIAMONDUSPOLE, mDIAMPOLE, 0, mCOUNT, 0 },
			{ aFRUITPLATFORM, mFRUITPLAT, 0, mFRUITPLAT, 1 },
			{ aBOLLPLATFORM, mBOLLPLAT, 0, mBOLLPLAT, 1 },
			{ aGRASSPLATFORM, mGRASSPLAT, 0, mGRASSPLAT, 1 },
			{ aPINKPLATFORM, mPINKPLAT, 0, mPINKPLAT, 1 },
			{ aSONICPLATFORM, mSONICPLAT, 0, mSONICPLAT, 1 },
			{ aSPIKEPLATFORM, mSPIKEPLAT, 0, mSPIKEPLAT, 1 },
			{ aSPIKEBOLL, mSPIKEBOLL, 0, mSPIKEBOLL, 1 },
			{ aGENERATOR, mPICKUPS, 86, mCOUNT, 0 },
			{ aEVA, mEVA, 1, mCOUNT, 0 },
			{ aBUBBLER, mCOMMON, 1, mCOUNT, 0 },
			{ aTNTPOWER, mCOUNT, 0, mAMMO, 71 },
			{ aGUN8POWER, mPICKUPS, 66, mAMMO, 71 },
			{ aGUN9POWER, mPICKUPS, 67, mAMMO, 71 },
			{ aSPIKEBOLL3D, mSPIKEBOLL3D, 0, mSPIKEBOLL3D, 1 },
			{ aBEES, mCOUNT, 0, mAMMO, 80 },
			{ aCOPTER, mLIZARD, 3, mAMMO, 80 },
			{ aLASERSHIELD, mCOUNT, 0, mAMMO, 71 },
			{ aSTOPWATCH, mPICKUPS, 87, mPICKUPS, 86 },
			{ aBIGROCK, mBIGROCK, 0, mAMMO, 81 },
			{ aBIGBOX, mBIGBOX, 0, mAMMO, 81 },
			{ aSONICBOSS, mSONCSHIP, 3, mCOUNT, 0 },
			{ aBUTTERFLY, mBUTTERFLY, 0, mCOUNT, 0 },
			{ aBEEBOY, mBEEBOY, 0, mAMMO, 80 },
			{ aSNOW, mSNOW, 0, mCOUNT, 0 },
			{ aTWEEDLEBOSS, mCOUNT, 0, mAMMO, 80 },
			{ aAIRBOARD, mPICKUPS, 36, mPICKUPS, 86 },
			{ aFLAG, mFLAG, 7, mFLAG, 7 },
			{ aXNORMTURTLE, mXTURTLE, 7, mAMMO, 81 },
			{ aXLIZARD, mXLIZARD, 4, mAMMO, 80 },
			{ aXFLOATLIZARD, mXLIZARD, 2, mAMMO, 80 },
			{ aXBILSYBOSS, mXBILSY, 4, mAMMO, 80 },
			{ aZCAT, mZDOG, 1, mAMMO, 81 },
			{ aZGHOST, mCOUNT, 0, mAMMO, 80 },
		};
	}

	const PresetAnimation* FindPresetAnimation(std::uint8_t eventId)
	{
		const PresetAnimation* end = PresetAnimations + arraySize(PresetAnimations);
		const PresetAnimation* it = std::lower_bound(PresetAnimations, end, eventId,
			[](const PresetAnimation& entry, std::uint8_t id) { return entry.EventID < id; });
		return (it != end && it->EventID == eventId ? it : nullptr);
	}

	namespace
	{
		// LIGHT::Type of JJ2+
		constexpr std::uint8_t lightPOINT = 1;
		constexpr std::uint8_t lightPOINT2 = 2;
		constexpr std::uint8_t lightNORMAL = 3;
		constexpr std::uint8_t lightFLICKER = 4;
		constexpr std::uint8_t lightBRIGHT = 5;
		constexpr std::uint8_t lightLASER = 7;
		constexpr std::uint8_t lightRING = 8;
		constexpr std::uint8_t lightRING2 = 9;
		constexpr std::uint8_t lightPLAYER = 10;

		// Radius of half the light of a round light of a size, as measured in JJ2+
		float GetLightRadius(std::int32_t size)
		{
			return 4.2f * size + 1.0f;
		}

		// The round light of a size (LIGHT::NORMAL), fitted to the ones measured in JJ2+ - the small ones never light fully,
		// the others do up to about their radius, past which their light fades out in about 28 px
		void GetRoundLight(std::int32_t size, float& radiusNear, float& radiusFar, float& intensity)
		{
			switch (size) {
				case 1: radiusNear = 3.0f; radiusFar = 23.0f; intensity = 0.25f; break;
				case 2:
				case 3: radiusNear = 7.0f; radiusFar = 39.0f; intensity = 0.8f; break;
				case 4: radiusNear = 10.0f; radiusFar = 41.0f; intensity = 0.85f; break;
				case 5: radiusNear = 14.0f; radiusFar = 44.0f; intensity = 0.9f; break;
				default: {
					// A light is clamped to full, the intensity above it widens what's lit fully
					float radius = GetLightRadius(size);
					radiusNear = radius - 18.0f;
					radiusFar = radius + 28.0f;
					intensity = 2.0f;
					break;
				}
			}
		}

		// A number from 0 to 1, the same for the same seed
		float GetLightHash(std::uint32_t seed)
		{
			std::uint32_t h = seed * 0x9E3779B1u;
			h ^= h >> 15;
			h *= 0x2C1B3C6Du;
			h ^= h >> 12;
			return float(h & 0xFFFF) / 65535.0f;
		}

		// Smooth noise from 0 to 1 that changes about once per unit of time, the same for the same seed
		float GetLightNoise(std::uint32_t seed, float time)
		{
			auto hash = [seed](std::int32_t n) {
				std::uint32_t h = std::uint32_t(n) * 0x9E3779B1u ^ seed * 0x85EBCA77u;
				h ^= h >> 15;
				h *= 0x2C1B3C6Du;
				h ^= h >> 12;
				return float(h & 0xFFFF) / 65535.0f;
			};
			float whole = std::floor(time);
			float t = time - whole;
			t = t * t * (3.0f - 2.0f * t);
			float a = hash(std::int32_t(whole));
			float b = hash(std::int32_t(whole) + 1);
			return a + (b - a) * t;
		}

		LightEmitter& AddLight(SmallVectorImpl<LightEmitter>& lights, Vector2f pos, float radiusNear, float radiusFar,
			float intensity, float brightness = 0.0f, float ringRadius = 0.0f)
		{
			LightEmitter& light = lights.emplace_back();
			light.Pos = pos;
			light.Intensity = intensity;
			light.Brightness = brightness;
			light.RadiusNear = radiusNear;
			light.RadiusFar = radiusFar;
			light.RingRadius = ringRadius;
			return light;
		}
	}

	void EmitObjectLight(const jjOBJ& obj, Vector2f pos, std::int32_t gameTick, SmallVectorImpl<LightEmitter>& lights)
	{
		// As measured in JJ2+ - the ones not listed (LASERBEAM, a line) aren't emulated, a light of no size has none
		std::int32_t size = obj.light;
		float radiusNear, radiusFar, intensity;
		switch (obj.lightType) {
			case lightNORMAL:
				if (size > 0) {
					GetRoundLight(size, radiusNear, radiusFar, intensity);
					AddLight(lights, pos, radiusNear, radiusFar, intensity);
				}
				break;
			case lightBRIGHT:
				// The same light, but brighter than the colors are at full light (about 1.25 times in the middle)
				if (size > 0) {
					GetRoundLight(size, radiusNear, radiusFar, intensity);
					AddLight(lights, pos, radiusNear, radiusFar, intensity, 0.25f);
				}
				break;
			case lightPOINT:
				// A dim point, whatever the size is
				AddLight(lights, pos, 6.0f, 21.0f, 0.15f);
				break;
			case lightPOINT2:
				// A brighter point that pulses every 2 seconds
				AddLight(lights, pos, 4.0f, 18.0f, 0.61f + 0.32f * std::sin(gameTick * (fTwoPi / 140.0f)));
				break;
			case lightFLICKER:
				// Dimmer than the round light, at random
				if (size > 0) {
					GetRoundLight(size, radiusNear, radiusFar, intensity);
					AddLight(lights, pos, radiusNear, radiusFar, 0.1f + 0.35f * GetLightNoise(std::uint32_t(obj.objectID), gameTick / 5.0f));
				}
				break;
			case lightLASER: {
				// What JJ2+ illuminates an object with (an event's "illuminate" bit), whatever the size is: a cloud of soft spots
				// wandering up to ~50 px around the object, which never lights beyond full light - the swarm of an illuminated
				// pickup of the game (see Actors::Collectibles::CollectibleBase), only without the brightening
				constexpr std::int32_t SpotCount = 20;
				// The swarm turns in units of the 60 frames in a second, a tick is shorter
				constexpr float TickTimeMult = 60.0f / 70.0f;
				for (std::int32_t i = 0; i < SpotCount; i++) {
					std::uint32_t seed = std::uint32_t(obj.objectID) * 131u + std::uint32_t(i) * 7919u;
					float spotIntensity = 0.22f + 0.2f * GetLightHash(seed);
					float distance = 4.0f + 32.0f * GetLightHash(seed + 1u);
					float speed = -(0.04f + 0.08f * GetLightHash(seed + 2u));
					float phase = fTwoPi * GetLightHash(seed + 3u) + speed * TickTimeMult * float(gameTick);
					Vector2f spotPos(pos.X + std::cos(phase + std::cos(phase * 0.33f) * 0.33f) * distance,
						pos.Y + std::sin(phase + std::sin(phase) * 0.33f) * distance);
					AddLight(lights, spotPos, 0.0f, spotIntensity * 94.0f, spotIntensity);
				}
				break;
			}
			case lightRING:
				if (size > 0) {
					AddLight(lights, pos, 6.0f, 26.0f, 2.5f, 0.0f, 3.75f * size);
				}
				break;
			case lightRING2:
				// A wider ring around a dim disk
				if (size > 0) {
					float ringRadius = 3.75f * size;
					AddLight(lights, pos, 8.0f, 28.0f, 2.0f, 0.0f, ringRadius);
					AddLight(lights, pos, ringRadius * 0.6f, ringRadius + 8.0f, 0.4f);
				}
				break;
			case lightPLAYER:
				// The light around a player, dimmer for the small ones
				if (size > 0) {
					float radius = GetLightRadius(size);
					AddLight(lights, pos, radius - 2.0f, radius + 29.0f, std::clamp(0.75f + (size - 11) * 0.025f, 0.3f, 1.0f));
				}
				break;
		}
	}

	namespace
	{
		// What the template of a bullet has in JJ2+ before a script changes it, its animations are in PresetAnimations
		struct PresetBullet
		{
			std::uint8_t EventID;
			float XSpeed, YSpeed, XAcc, YAcc;
			std::uint8_t CounterEnd;
			std::uint8_t LightType;
			// How much the bullet hurts
			std::int32_t AnimSpeed;
		};

		// As measured in JJ2+, the bullets of the weapons, powered-up ones and the ones of the shields
		constexpr PresetBullet PresetBullets[] = {
			{ aPLAYERBULLET1, 6.0f, 0.0f, 0.125f, 0.0f, 35, lightPOINT2, 1 },
			{ aPLAYERBULLET2, 5.0f, 1.0f, 0.25f, 0.091552734375f, 105, lightPOINT2, 1 },
			{ aPLAYERBULLET3, 6.0f, 0.0f, 0.125f, 0.0f, 35, lightPOINT2, 2 },
			{ aPLAYERBULLET4, 2.0f, 0.0f, 0.25f, 0.00390625f, 140, lightPOINT2, 2 },
			{ aPLAYERBULLET5, 1.0f, 0.0f, 0.1875f, 0.0f, 40, lightPOINT2, 2 },
			{ aPLAYERBULLET6, 4.0f, 0.0f, 0.125f, 0.0f, 35, 0, 1 },
			{ aPLAYERBULLET8, 11.0f, 0.0f, 0.125f, 0.0f, 45, lightPOINT2, 2 },
			{ aPLAYERBULLET9, 4.0f, 0.0f, 0.0f, 0.0f, 35, lightPOINT2, 1 },
			{ aPLAYERBULLETP1, 7.0f, 0.0f, 0.1875f, 0.0f, 30, lightPOINT, 2 },
			{ aPLAYERBULLETP2, 5.0f, 1.0f, 0.25f, 0.091552734375f, 105, lightPOINT2, 2 },
			{ aPLAYERBULLETP3, 7.0f, 0.0f, 0.1875f, 0.0f, 30, lightPOINT2, 2 },
			{ aPLAYERBULLETP4, 1.5f, 0.0f, 0.25f, 0.00390625f, 140, lightPOINT2, 3 },
			{ aPLAYERBULLETP5, 1.5f, 0.0f, 0.1875f, 0.0f, 35, lightPOINT2, 2 },
			{ aPLAYERBULLETP6, 4.0f, 0.0f, 0.125f, 0.0f, 35, 0, 2 },
			{ aPLAYERBULLETP8, 11.0f, 0.0f, 0.25f, 0.0f, 70, lightPOINT, 3 },
			{ aPLAYERBULLETP9, 4.0f, 0.0f, 0.0f, 0.0f, 35, lightPOINT2, 3 },
			{ aPLAYERBULLETC1, 2.0f, 0.0f, 0.125f, 0.0f, 46, lightPOINT2, 3 },
			{ aPLAYERBULLETC2, 2.0f, 0.0f, 0.125f, 0.0f, 46, lightPOINT, 1 },
			{ aPLAYERBULLETC3, 2.0f, 0.0f, 0.125f, 0.0f, 46, lightPOINT, 3 }
		};

		// Gives the template of a bullet what it has in JJ2+, nothing for any other event
		void InitPresetBullet(jjOBJ& preset)
		{
			for (const PresetBullet& bullet : PresetBullets) {
				if (bullet.EventID == preset.eventID) {
					preset.xSpeed = bullet.XSpeed;
					preset.ySpeed = bullet.YSpeed;
					preset.xAcc = bullet.XAcc;
					preset.yAcc = bullet.YAcc;
					preset.counterEnd = bullet.CounterEnd;
					preset.lightType = bullet.LightType;
					preset.animSpeed = bullet.AnimSpeed;
					break;
				}
			}
		}

		// A projectile an enemy of the game shoots, as JJ2+ has it when a script sees it first - an object of the event
		// BULLET made by the enemy, which the scripts of JJ2+ tell apart by its behavior (to replace it by their own)
		struct EnemyProjectile
		{
			std::uint32_t Behavior;
			std::int8_t State;
			std::uint8_t CounterEnd;
			// Animation of `killAnim`, `mCOUNT` for none
			anim KillSet;
			std::uint8_t KillAnimation;
			// Speed (not along the direction) and acceleration the object has, a speed of 0 keeps the one of the object of the game
			float XSpeed, XAcc;
		};

		const EnemyProjectile* FindEnemyProjectile(Actors::ActorBase& actor)
		{
			// As measured in JJ2+, a banana flies already, the flame of a dragon is new
			static constexpr EnemyProjectile Banana = { BehaviorMonkeyBullet, sFLY, 35, mMONKEY, 1, 0.0f, 0.0f };
			static constexpr EnemyProjectile Fire = { aPLAYERBULLET6, sSTART, 35, mCOUNT, 0, 2.0f, 0.125f };
			if (runtime_cast<Actors::Enemies::Monkey::Banana>(&actor) != nullptr) {
				return &Banana;
			}
			if (runtime_cast<Actors::Enemies::Dragon::Fire>(&actor) != nullptr) {
				return &Fire;
			}
			return nullptr;
		}
	}

	bool DoesObjectTouch(LevelScriptLoader* levelScripts, const jjOBJ& obj, const AABBf& box)
	{
		// By the pixels of the frame the object shows, flipped as it's drawn (see ScriptObjectHost::DrawFrame())
		Recti frameRect, bounds;
		if (obj.curFrame == 0 || !levelScripts->GetAnimationTables().GetFrameBounds(obj.curFrame, frameRect, bounds)) {
			return false;
		}
		float left = (obj.direction < 0 ? float(-bounds.X - bounds.W) : float(bounds.X));
		AABBf objectBox(obj.xPos + left, obj.yPos + float(bounds.Y), obj.xPos + left + float(bounds.W), obj.yPos + float(bounds.Y + bounds.H));
		return objectBox.Overlaps(box);
	}

	ScriptObjectHost::ScriptObjectHost(LevelScriptLoader* levelScripts, jjOBJ* obj, Actors::ActorBase* actor)
		: _levelScripts(levelScripts), _obj(obj), _actor(actor), _timeMult(1.0f), _nextTick(-1), _drawn(true), _drawFrame(false),
			_ranOwnBehavior(false), _isRunningOwnBehavior(false), _hasFrameHitbox(false), _heldInPlace(false), _breaking(false),
			_ownsLights(actor->GetState(Actors::ActorState::Illuminated)), _boundsFrame(0)
	{
		_obj->AddRef();
		_obj->_host = this;
	}

	ScriptObjectHost::~ScriptObjectHost()
	{
		// The object is gone, or the script is, the view stays as the object left it
		_obj->_host = nullptr;
		_obj->Release();
	}

	void ScriptObjectHost::OnUpdate(Actors::ActorBase* actor, float timeMult)
	{
		// The behaviors of JJ2+ count in ticks, 70 of them in a second whatever the frame rate is - in a frame without a tick
		// the object stays as the last one left it, and drawn so (see LevelScriptLoader::OnLevelUpdate()), a few ticks are
		// caught up after a slow frame
		constexpr std::int32_t MaxTicksPerFrame = 4;
		std::int32_t currentTick = _levelScripts->GetGameTick();
		if (_nextTick < 0 || currentTick - _nextTick >= MaxTicksPerFrame) {
			_nextTick = currentTick;
		}
		if (_nextTick > currentTick) {
			return;
		}
		if (_breaking) {
			// The object of the script runs and draws on its own until the object of the game is gone (see OnPerish())
			_drawn = false;
			return;
		}
		while (_nextTick <= currentTick) {
			_nextTick++;
			if (!UpdateTick(actor)) {
				return;
			}
		}
		// Drawn as the last tick left it
		FinishTicks();
	}

	bool ScriptObjectHost::UpdateTick(Actors::ActorBase* actor)
	{
		// What the object of the game does in a tick, its time is in units of the 60 frames in a second
		_timeMult = 60.0f / 70.0f;
		_drawn = false;
		_drawFrame = false;
		_ranOwnBehavior = false;

		// The frame changes only when the behavior of the game runs (see RunOwnBehavior())
		PullFromActor(_levelScripts, *_obj, *actor, false);

		// A copy keeps the behavior alive while it runs, it may replace itself
		jjBEHAVIOR behavior = _obj->behavior;
		if (!CallScriptBehavior(_levelScripts, *_obj, behavior)) {
			// The script gave the object a behavior of the game again
			if (behavior.behaviorId == aUNKNOWN) {
				// BEHAVIOR::INACTIVE
				Delete();
				return false;
			}
			if (behavior.behaviorId == aEXPLOSION || behavior.behaviorId == BehaviorExplosion2) {
				// What ends a collected pickup and the like, as the game ends its own
				Actors::Explosion::Create(actor->_levelHandler, Vector3i(std::int32_t(_obj->xPos + PositionOffset),
					std::int32_t(_obj->yPos + PositionOffset), actor->_renderer.layer()),
					behavior.behaviorId == aEXPLOSION ? Actors::Explosion::Type::Small : Actors::Explosion::Type::Generator);
				Delete();
				return false;
			}
			RunOwnBehavior(true);
		}

		// The game doesn't count `age` itself, the behaviors do and scripts use it for anything
		UpdateSkin(*_obj);
		PushToActor(*_obj, *actor);
		UpdateFrameHitbox(*actor);
		return true;
	}

	void ScriptObjectHost::FinishTicks()
	{
		if (_drawn || _drawFrame) {
			// A frame of an animation a script gave the object, or one it chose, is drawn as it is, the object of the game
			// draws itself otherwise (its frame is the same then)
			bool ownFrame = (_obj->_skinAnim > 0 || _obj->curFrame != _obj->_synced.CurFrame || _obj->_synced.NativeAnim == 0);
			if (ownFrame && _obj->curFrame != 0) {
				DrawFrame(_levelScripts, *_obj);
				_drawn = false;
			} else {
				_drawn = true;
			}
		}
	}

	bool ScriptObjectHost::IsDrawn() const
	{
		return _drawn;
	}

	bool ScriptObjectHost::OnCollision(Actors::ActorBase* actor, Actors::ActorBase* other)
	{
		jjOBJ& obj = *_obj;
		auto* player = runtime_cast<Actors::Player>(other);
		auto* shot = (player == nullptr ? runtime_cast<Actors::Weapons::ShotBase>(other) : nullptr);
		if (player == nullptr && shot == nullptr) {
			return false;
		}

		// What the object is to players, the game handles a pickup as one, anything else as special
		std::uint32_t playerHandling = (obj._playerHandlingSet ? obj._playerHandling
			: (runtime_cast<Actors::Collectibles::CollectibleBase>(actor) != nullptr ? hPICKUP : hSPECIAL));

		if (obj._playerHandlingSet && playerHandling == hPARTICLE) {
			// A particle collides with nothing, players and bullets go through it whatever its `bulletHandling` is, as in JJ2+
			return true;
		}

		if (player != nullptr) {
			if (!obj._scriptedCollisions || (playerHandling != hPICKUP && playerHandling != hSPECIAL)) {
				return false;
			}

			jjPLAYER* p = _levelScripts->GetPlayerBackingStore(player);
			if (playerHandling == hPICKUP) {
				// Whatever the script does, the game doesn't collect it
				CallObjectHit(_levelScripts, obj, nullptr, p, 0);
				return true;
			}
			// Only an attack hits a special object, a sugar rush (0) or a special move (1), as JJ2+ tells them apart
			std::int32_t force;
			if (player->HasSugarRush()) {
				force = 0;
			} else if (player->GetSpecialMove() != Actors::Player::SpecialMoveType::None) {
				force = 1;
			} else {
				return false;
			}
			return CallObjectHit(_levelScripts, obj, nullptr, p, force);
		}

		if (obj._bulletHandlingSet) {
			if (obj._bulletHandling == hIGNOREBULLET) {
				return true;
			}
			if (obj._bulletHandling == hDESTROYBULLET) {
				// The bullet stops without harm
				shot->DecreaseHealth(INT32_MAX);
				return true;
			}
		}
		if (!obj._scriptedCollisions || playerHandling != hSPECIAL) {
			return false;
		}

		// The force of a bullet is its damage, as the `animSpeed` of a bullet in JJ2+
		jjOBJ* bullet = _levelScripts->GetObjectView(shot);
		Actors::Player* owner = shot->GetOwner();
		jjPLAYER* p = (owner != nullptr ? _levelScripts->GetPlayerBackingStore(owner) : nullptr);
		return CallObjectHit(_levelScripts, obj, bullet, p, shot->GetStrength());
	}

	ScriptSceneryObject::ScriptSceneryObject()
		: _phase(Phase::Whole)
	{
	}

	Task<bool> ScriptSceneryObject::OnActivatedAsync(const Actors::ActorActivationDetails& details)
	{
		SetState(Actors::ActorState::ForceDisableCollisions, true);
		SetState(Actors::ActorState::CanBeFrozen | Actors::ActorState::CollideWithTileset | Actors::ActorState::CollideWithOtherActors |
			Actors::ActorState::ApplyGravitation, false);
		UpdatePhase(true);
		async_return true;
	}

	void ScriptSceneryObject::OnUpdate(float timeMult)
	{
		// The tile breaks by itself, this is all the behavior of the game does here
		UpdatePhase(false);
	}

	bool ScriptSceneryObject::OnDraw(RenderQueue& renderQueue)
	{
		// The tile is the scenery
		return true;
	}

	void ScriptSceneryObject::UpdatePhase(bool activating)
	{
		auto* tileMap = _levelHandler->TileMap();
		bool destroyed = (tileMap != nullptr && tileMap->IsTileDestroyed(std::int32_t(_pos.X) / Tiles::TileSet::DefaultTileSize,
			std::int32_t(_pos.Y) / Tiles::TileSet::DefaultTileSize));
		if (!destroyed) {
			// Also a tile made whole again, by going back to a checkpoint
			_phase = Phase::Whole;
		} else if (_phase == Phase::Whole) {
			// Collapsing scenery collapses in this update, the next one finds it broken (a tile broken already isn't)
			_phase = (!activating && GetSpawnEventType() == EventType::SceneryCollapse ? Phase::Collapsing : Phase::Broken);
		} else {
			_phase = Phase::Broken;
		}
	}

	void ScriptObjectHost::OnPerish(Actors::ActorBase* actor, Actors::ActorBase* collider)
	{
		// As measured in JJ2+, a hit puts a crate in STATE::ACTION, and the behavior breaks it by BEHAVIOR::CRATE then
		if (_breaking || runtime_cast<Actors::Solid::GenericContainer>(actor) == nullptr ||
			(_obj->behavior.function == nullptr && _obj->behavior.object == nullptr)) {
			return;
		}
		_breaking = true;
		_drawn = false;
		_levelScripts->BreakObject(_obj->objectID);
	}

	bool ScriptObjectHost::OwnsDebris() const
	{
		return _breaking;
	}

	bool ScriptObjectHost::OwnsLights() const
	{
		return _ownsLights;
	}

	void ScriptObjectHost::OnEmitLights(Actors::ActorBase* actor, SmallVectorImpl<LightEmitter>& lights)
	{
		// The object of the game lights itself as it does in the game, unless it's illuminated
		if (_ownsLights || _obj->lightType != _obj->_synced.LightType) {
			EmitObjectLight(*_obj, actor->_pos, _levelScripts->GetGameTick(), lights);
		}
	}

	bool ScriptObjectHost::OnUpdateHitbox(Actors::ActorBase* actor)
	{
		AABBf hitbox;
		if (!GetFrameHitbox(*actor, hitbox)) {
			return false;
		}
		actor->AABBInner = hitbox;
		return true;
	}

	bool ScriptObjectHost::GetFrameHitbox(const Actors::ActorBase& actor, AABBf& hitbox)
	{
		if (_obj->_skinAnim <= 0 || _obj->curFrame == 0 || !actor.GetState(Actors::ActorState::IsSolidObject)) {
			return false;
		}
		if (!UpdateFrameBounds()) {
			return false;
		}

		// Flipped as the frame is drawn (see DrawFrame())
		const Recti& bounds = _pixelBounds;
		float left = (actor.IsFacingLeft() ? float(-bounds.X - bounds.W) : float(bounds.X));
		float x = actor._pos.X - PositionOffset;
		float y = actor._pos.Y - PositionOffset;
		hitbox = AABBf(x + left, y + float(bounds.Y), x + left + float(bounds.W), y + float(bounds.Y + bounds.H));
		return true;
	}

	bool ScriptObjectHost::UpdateFrameBounds()
	{
		if (_boundsFrame != _obj->curFrame) {
			_boundsFrame = _obj->curFrame;
			if (!_levelScripts->GetAnimationTables().GetFrameBounds(_boundsFrame, _frameRect, _pixelBounds)) {
				_frameRect = {};
				_pixelBounds = {};
			}
		}
		return (_pixelBounds.W > 0);
	}

	bool ScriptObjectHost::FindFrameGround(const Actors::ActorBase& actor, float& offset)
	{
		auto* tileMap = actor._levelHandler->TileMap();
		if (tileMap == nullptr || !UpdateFrameBounds()) {
			return false;
		}

		// As measured in JJ2+, the object goes down by 4 pixels at a time until the pixel right below the bottom of its
		// frame where it is (not the pixels of the frame) is in the ground, so the frame can reach up to 3 pixels into it
		// - or up out of the ground the same way
		constexpr std::int32_t Step = 4;
		constexpr std::int32_t MaxSteps = 32;
		Tiles::TileCollisionParams params = { Tiles::TileDestructType::None, true };
		float x = std::floor(actor._pos.X - PositionOffset);
		float bottom = std::floor(actor._pos.Y - PositionOffset) + float(_frameRect.Y + _frameRect.H);
		auto isMasked = [&](std::int32_t steps) {
			float y = bottom + float(steps * Step);
			// The edges of the rectangle are pixels of it, so it's a single pixel
			return !tileMap->IsTileEmpty(AABBf(x, y, x, y), params);
		};

		std::int32_t steps = 0;
		if (isMasked(0)) {
			while (steps > -MaxSteps && isMasked(steps - 1)) {
				steps--;
			}
		} else {
			while (steps < MaxSteps && !isMasked(steps)) {
				steps++;
			}
		}
		if (steps <= -MaxSteps || steps >= MaxSteps) {
			return false;
		}
		offset = float(steps * Step);
		return true;
	}

	void ScriptObjectHost::UpdateFrameHitbox(Actors::ActorBase& actor)
	{
		AABBf hitbox;
		bool hasFrameHitbox = GetFrameHitbox(actor, hitbox);
		if (hasFrameHitbox && !_hasFrameHitbox) {
			// The object of the game stood on the ground by a hitbox of its own, it's put on the ground by its frame as JJ2+
			// puts its crates on the ground when they start (see FindFrameGround()) - and it stays there, a frame that reaches
			// into a slope by a corner would slide down it
			float offset;
			bool onGround = FindFrameGround(actor, offset);
			if (onGround) {
				actor.MoveInstantly(Vector2f(0.0f, offset), Actors::MoveType::Relative | Actors::MoveType::Force);
				actor._speed = Vector2f::Zero;
				hitbox.T += offset;
				hitbox.B += offset;
				_obj->yPos = actor._pos.Y - PositionOffset;
				_obj->ySpeed = 0.0f;
				_obj->_synced.YPos = _obj->yPos;
				_obj->_synced.YSpeed = 0.0f;
				if (actor.GetState(Actors::ActorState::ApplyGravitation)) {
					actor.SetState(Actors::ActorState::ApplyGravitation, false);
					_heldInPlace = true;
				}
			}
		} else if (!hasFrameHitbox && _heldInPlace) {
			// The object of the game stands on the ground by its own hitbox again, unless it's gone already
			if (actor._health > 0 && !actor.GetState(Actors::ActorState::IsDestroyed)) {
				actor.SetState(Actors::ActorState::ApplyGravitation, true);
			}
			_heldInPlace = false;
		}
		_hasFrameHitbox = hasFrameHitbox;
		if (hasFrameHitbox) {
			actor.AABBInner = hitbox;
		}
	}

	void ScriptObjectHost::RunOwnBehavior(bool draw)
	{
		if (_isRunningOwnBehavior) {
			return;
		}

		_isRunningOwnBehavior = true;
		// What the behavior changed so far goes to the object first, what the object did comes back
		UpdateSkin(*_obj);
		PushToActor(*_obj, *_actor);
		UpdateFrameHitbox(*_actor);
		_actor->OnUpdate(_timeMult);
		PullFromActor(_levelScripts, *_obj, *_actor);
		// The behaviors of the game leave the start state on their first update, scenery sleeps until it breaks
		if (_obj->state == sSTART) {
			_obj->state = (runtime_cast<ScriptSceneryObject>(_actor) != nullptr ? sSLEEP : sSTILL);
			_obj->_synced.State = _obj->state;
		}
		_isRunningOwnBehavior = false;

		_ranOwnBehavior = true;
		if (draw) {
			_drawn = true;
		}
	}

	void ScriptObjectHost::Draw()
	{
		_drawFrame = true;
	}

	bool ScriptObjectHost::NeedsHost(const jjOBJ& obj)
	{
		// Also a behavior of the game that ends the object, which the object doesn't know itself
		std::uint32_t behaviorId = obj.behavior.behaviorId;
		bool ending = (obj.behavior.function == nullptr && obj.behavior.object == nullptr &&
			(behaviorId == aUNKNOWN || behaviorId == aEXPLOSION || behaviorId == BehaviorExplosion2));
		return (obj.behavior.function != nullptr || obj.behavior.object != nullptr || ending || obj._skinAnim > 0 ||
			obj._scriptedCollisions || obj._playerHandlingSet || obj._bulletHandlingSet || obj.lightType != obj._synced.LightType);
	}

	void ScriptObjectHost::UpdateSkin(jjOBJ& obj)
	{
		if (obj.curAnim != obj._synced.CurAnim) {
			// The animation of the object of the game shows its own frames again, any other one is shown in their place
			obj._skinAnim = (obj.curAnim > 0 && obj.curAnim != obj._synced.NativeAnim ? obj.curAnim : 0);
			obj._synced.CurAnim = obj.curAnim;
		}
	}

	void ScriptObjectHost::DrawFrame(LevelScriptLoader* levelScripts, const jjOBJ& obj)
	{
		if (ContentResolver::Get().IsHeadless()) {
			return;
		}
		std::int32_t cell = 0;
		auto* graphics = levelScripts->GetAnimationTables().ResolveFrame(obj.curFrame, cell);
		if (graphics == nullptr) {
			return;
		}
		// Frozen or just hit as the game draws the objects, in the sprite layer
		std::uint32_t mode = (obj.freeze != 0 ? spriteType_FROZEN : (obj.justHit != 0 ? spriteType_SINGLECOLOR : spriteType_NORMAL));
		ScriptWorldCanvas::SpriteDraw draw = { graphics, cell, obj.xPos, obj.yPos, obj.direction, 1.0f, 1.0f, 0, mode, 15,
			levelScripts->GetLayerDrawZ(4), 0 };
		levelScripts->GetWorldCanvas().AddSprite(draw);
	}

	void ScriptObjectHost::Delete()
	{
		DeleteActor(*_actor);
	}

	void ScriptObjectHost::Deactivate()
	{
		DeactivateActor(*_actor);
	}

	void ScriptObjectHost::PullFromActor(LevelScriptLoader* levelScripts, jjOBJ& obj, Actors::ActorBase& actor, bool withAnimation)
	{
		obj.xPos = actor._pos.X - PositionOffset;
		obj.yPos = GetRestingY(actor) - PositionOffset;
		obj.xSpeed = actor._speed.X;
		obj.ySpeed = actor._speed.Y;
		obj.direction = (actor.IsFacingLeft() ? -1 : 1);
		obj.energy = std::int8_t(std::clamp<std::int32_t>(actor._health, INT8_MIN, INT8_MAX));
		if (actor._health <= 0 || actor.GetState(Actors::ActorState::IsDestroyed)) {
			obj.state = sKILL;
		} else if (auto* scenery = runtime_cast<ScriptSceneryObject>(&actor)) {
			if (scenery->IsCollapsing()) {
				obj.state = sEXTRA;
			} else if (scenery->IsBroken()) {
				obj.state = sDONE;
			}
		}

		obj._synced.XPos = obj.xPos;
		obj._synced.YPos = obj.yPos;
		obj._synced.XSpeed = obj.xSpeed;
		obj._synced.YSpeed = obj.ySpeed;
		obj._synced.Direction = obj.direction;
		obj._synced.Energy = obj.energy;
		obj._synced.State = obj.state;

		if (withAnimation) {
			// The frame of the animation the object shows, or of the one a script gave it in its place, of the same position
			auto& tables = levelScripts->GetAnimationTables();
			std::uint32_t nativeAnim = tables.FindAnimationOfGraphics(actor._renderer.FrameSource);
			std::uint32_t shownAnim = (obj._skinAnim > 0 ? std::uint32_t(obj._skinAnim) : nativeAnim);
			std::uint32_t frameCount, firstFrame;
			if (shownAnim != 0 && tables.GetAnimationFrames(shownAnim, frameCount, firstFrame) && frameCount > 0) {
				std::uint32_t position = std::uint32_t(std::max<std::int32_t>(actor._renderer.CurrentFrame, 0)) % frameCount;
				obj.curAnim = std::int16_t(shownAnim);
				obj.frameID = std::int8_t(position);
				obj.curFrame = firstFrame + position;
			}
			obj._synced.NativeAnim = std::int32_t(nativeAnim);
			obj._synced.CurAnim = obj.curAnim;
			obj._synced.CurFrame = obj.curFrame;
		}
	}

	void ScriptObjectHost::PushToActor(jjOBJ& obj, Actors::ActorBase& actor)
	{
		jjOBJ::SyncedState& synced = obj._synced;

		if (obj.xPos != synced.XPos || obj.yPos != synced.YPos) {
			Vector2f pos(obj.xPos + PositionOffset, obj.yPos + PositionOffset);
			if (auto* collectible = runtime_cast<Actors::Collectibles::CollectibleBase>(&actor)) {
				if (collectible->_untouched) {
					// It floats around the new place
					float floatOffset = actor._pos.Y - collectible->_startingY;
					collectible->_startingY = pos.Y;
					pos.Y += floatOffset;
				}
			}
			actor.MoveInstantly(pos, Actors::MoveType::Absolute | Actors::MoveType::Force);
		}
		if (obj.xSpeed != synced.XSpeed || obj.ySpeed != synced.YSpeed) {
			actor._speed = Vector2f(obj.xSpeed, obj.ySpeed);
		}
		if (obj.direction != synced.Direction && obj.direction != 0) {
			actor.SetFacingLeft(obj.direction < 0);
		}
		if (obj.energy != synced.Energy) {
			if (obj.energy <= 0) {
				// No energy kills the object as the game does
				if (actor._health > 0) {
					actor.DecreaseHealth(INT32_MAX);
				}
			} else {
				actor.SetHealth(obj.energy);
			}
		}
		if (obj.state != synced.State) {
			if (obj.state == sKILL) {
				if (actor._health > 0) {
					actor.DecreaseHealth(INT32_MAX);
				}
			} else if (obj.state == sDEACTIVATE) {
				DeactivateActor(actor);
			}
		}

		synced.XPos = obj.xPos;
		synced.YPos = obj.yPos;
		synced.XSpeed = obj.xSpeed;
		synced.YSpeed = obj.ySpeed;
		synced.Direction = obj.direction;
		synced.Energy = obj.energy;
		synced.State = obj.state;
	}

	float ScriptObjectHost::GetRestingY(const Actors::ActorBase& actor)
	{
		// A pickup of the original floats only as it's drawn, the object stays where it is - scripts take the phase of
		// floating of their own from its position, which a floating collectible would turn into a jitter
		if (auto* collectible = runtime_cast<Actors::Collectibles::CollectibleBase>(&actor)) {
			if (collectible->_untouched) {
				return collectible->_startingY;
			}
		}
		return actor._pos.Y;
	}

	Vector2f ScriptObjectHost::GetSpawnPos(const Actors::ActorBase& actor)
	{
		return Vector2f(float(actor._spawnPos.X) - PositionOffset, float(actor._spawnPos.Y) - PositionOffset);
	}

	Vector2i ScriptObjectHost::GetOriginTile(const Actors::ActorBase& actor)
	{
		return actor._originTile;
	}

	void ScriptObjectHost::DeleteActor(Actors::ActorBase& actor)
	{
		// The event stays as it is, its object isn't spawned again
		actor.SetState(Actors::ActorState::IsDestroyed, true);
	}

	void ScriptObjectHost::DeactivateActor(Actors::ActorBase& actor)
	{
		// The event spawns its object again when it's seen again
		if (actor.GetState(Actors::ActorState::IsCreatedFromEventMap)) {
			if (auto* events = actor._levelHandler->EventMap()) {
				events->Deactivate(actor._originTile.X, actor._originTile.Y);
			}
		}
		actor.SetState(Actors::ActorState::IsDestroyed, true);
	}

	namespace
	{
		void LogBehaviorException(asIScriptContext* ctx)
		{
			std::int32_t column = 0; const char* sectionName = nullptr;
			std::int32_t line = ctx->GetExceptionLineNumber(&column, &sectionName);
			LOGE("{}:{}({}) ‡ An exception \"{}\" occurred in \"{}\". Please correct the code and try again.",
				sectionName, line, column, ctx->GetExceptionString(), ctx->GetExceptionFunction()->GetDeclaration());
		}
	}

	bool ScriptObjectHost::CallScriptBehavior(LevelScriptLoader* levelScripts, jjOBJ& obj, const jjBEHAVIOR& behavior, bool draw)
	{
		if (behavior.function == nullptr && behavior.object == nullptr) {
			return false;
		}

		asIScriptEngine* engine = levelScripts->GetEngine();
		asIScriptContext* ctx = engine->RequestContext();
		std::int32_t r = asSUCCESS;
		if (behavior.object != nullptr) {
			// The object may lose its behavior while it runs, which would free the instance
			asIScriptObject* instance = behavior.object;
			instance->AddRef();
			const LevelScriptLoader::BehaviorMethods& methods = levelScripts->GetBehaviorMethods(instance->GetObjectType());
			if (methods.OnBehave != nullptr) {
				ctx->Prepare(methods.OnBehave);
				ctx->SetObject(instance);
				ctx->SetArgObject(0, &obj);
				r = ctx->Execute();
			}
			// What relates to drawing the object, called every time the behavior is unless it's told not to draw
			if (draw && r != asEXECUTION_EXCEPTION) {
				if (asIScriptFunction* onDraw = methods.OnDraw) {
					ctx->Prepare(onDraw);
					ctx->SetObject(instance);
					ctx->SetArgObject(0, &obj);
					r = ctx->Execute();
				}
			}
			instance->Release();
		} else {
			ctx->Prepare(behavior.function);
			ctx->SetArgObject(0, &obj);
			r = ctx->Execute();
		}
		if (r == asEXECUTION_EXCEPTION) {
			LogBehaviorException(ctx);
		}
		engine->ReturnContext(ctx);
		return true;
	}

	bool ScriptObjectHost::CallObjectHit(LevelScriptLoader* levelScripts, jjOBJ& obj, jjOBJ* bullet, jjPLAYER* player, std::int32_t force)
	{
		asIScriptEngine* engine = levelScripts->GetEngine();
		asIScriptContext* ctx = engine->RequestContext();

		// The method of the behavior, if it has one, decides first
		if (asIScriptObject* instance = obj.behavior.object) {
			instance->AddRef();
			if (asIScriptFunction* onObjectHit = levelScripts->GetBehaviorMethods(instance->GetObjectType()).OnObjectHit) {
				ctx->Prepare(onObjectHit);
				ctx->SetObject(instance);
				ctx->SetArgObject(0, &obj);
				ctx->SetArgObject(1, bullet);
				ctx->SetArgObject(2, player);
				ctx->SetArgDWord(3, std::uint32_t(force));
				std::int32_t r = ctx->Execute();
				bool handled = (r == asEXECUTION_FINISHED && ctx->GetReturnByte() != 0);
				if (r == asEXECUTION_EXCEPTION) {
					LogBehaviorException(ctx);
				}
				if (handled) {
					instance->Release();
					engine->ReturnContext(ctx);
					return true;
				}
			}
			instance->Release();
		}

		// Then the global ones, kept by JJ2+ for older scripts
		bool called = false;
		if (asIScriptFunction* onObjectHit = levelScripts->GetGlobalObjectHit()) {
			ctx->Prepare(onObjectHit);
			ctx->SetArgObject(0, &obj);
			ctx->SetArgObject(1, bullet);
			ctx->SetArgObject(2, player);
			ctx->SetArgDWord(3, std::uint32_t(force));
			if (ctx->Execute() == asEXECUTION_EXCEPTION) {
				LogBehaviorException(ctx);
			}
			called = true;
		}
		engine->ReturnContext(ctx);
		return called;
	}

	void jjOBJ::CopyTemplate(const jjOBJ& preset)
	{
		xOrg = preset.xOrg;
		yOrg = preset.yOrg;
		xPos = preset.xPos;
		yPos = preset.yPos;
		xSpeed = preset.xSpeed;
		ySpeed = preset.ySpeed;
		xAcc = preset.xAcc;
		yAcc = preset.yAcc;
		counter = preset.counter;
		curFrame = preset.curFrame;
		age = preset.age;
		creator = preset.creator;
		curAnim = preset.curAnim;
		killAnim = preset.killAnim;
		freeze = preset.freeze;
		lightType = preset.lightType;
		frameID = preset.frameID;
		noHit = preset.noHit;
		energy = preset.energy;
		light = preset.light;
		objType = preset.objType;
		state = preset.state;
		points = preset.points;
		eventID = preset.eventID;
		direction = preset.direction;
		justHit = preset.justHit;
		oldState = preset.oldState;
		animSpeed = preset.animSpeed;
		special = preset.special;
		doesHurt = preset.doesHurt;
		counterEnd = preset.counterEnd;
		behavior = preset.behavior;

		std::memcpy(_vars, preset._vars, sizeof(_vars));
		_creatorID = preset._creatorID;
		_creatorType = preset._creatorType;
		_bulletHandling = preset._bulletHandling;
		_playerHandling = preset._playerHandling;
		_ricochetProp = preset._ricochetProp;
		_freezable = preset._freezable;
		_blastable = preset._blastable;
		_isTarget = preset._isTarget;
		_triggersTNT = preset._triggersTNT;
		_deactivates = preset._deactivates;
		_scriptedCollisions = preset._scriptedCollisions;
		_bulletHandlingSet = preset._bulletHandlingSet;
		_playerHandlingSet = preset._playerHandlingSet;

		// An animation a script gave the template is the one the object shows (not the one of the template, see
		// LevelScriptLoader::GetObjectPreset())
		_skinAnim = (preset.curAnim > 0 && preset.curAnim != preset._synced.CurAnim ? preset.curAnim : 0);
		_synced.LightType = preset._synced.LightType;
	}
}

namespace Jazz2::Scripting
{
	void LevelScriptLoader::OnActorAdded(const std::shared_ptr<Actors::ActorBase>& actor)
	{
		_lastObjectId = 0;

		// The bullets of the players are the objects of the events of their weapons, the powered-up ones 8 further, as in
		// JJ2+ - the players and effects are no objects of events
		std::uint8_t eventId;
		std::int32_t creatorId = -1;
		bool madeByObject = false;
		if (auto* shot = runtime_cast<Actors::Weapons::ShotBase>(actor.get())) {
			// There's no bullet of TNT, the ones of the 8th and the 9th weapon follow the 6th, there's none of a weapon JJ2+ doesn't have
			WeaponType weapon = shot->GetWeaponType();
			std::uint8_t bullet;
			if (weapon >= WeaponType::Blaster && weapon <= WeaponType::Toaster) {
				bullet = std::uint8_t(Legacy::aPLAYERBULLET1 + std::uint8_t(weapon));
			} else if (weapon == WeaponType::Pepper) {
				bullet = Legacy::aPLAYERBULLET8;
			} else if (weapon == WeaponType::Electro) {
				bullet = Legacy::aPLAYERBULLET9;
			} else {
				return;
			}
			eventId = std::uint8_t(bullet + (shot->IsUpgraded() ? Legacy::aPLAYERBULLETP1 - Legacy::aPLAYERBULLET1 : 0));
			if (Actors::Player* owner = shot->GetOwner()) {
				creatorId = owner->GetPlayerIndex();
			}
		} else if (runtime_cast<Actors::Weapons::TNT>(actor.get()) != nullptr) {
			eventId = Legacy::aTNT;
		} else if (Legacy::FindEnemyProjectile(*actor) != nullptr) {
			// The scripts of JJ2+ find them to replace them (see EnsureObjectView())
			eventId = Legacy::aBULLET;
			madeByObject = true;
		} else {
			EventType type = actor->GetSpawnEventType();
			if (type == EventType::Empty) {
				return;
			}
			eventId = std::uint8_t(GetEventTranslator().ToOriginal(type, actor->GetSpawnEventParams()) & 0xFF);
		}

		std::int32_t objectId = RegisterObject(actor, nullptr, eventId);
		_lastObjectId = objectId;
		if (objectId <= 0) {
			return;
		}
		if (creatorId >= 0) {
			_objectSlots[objectId].CreatorType = Legacy::cPLAYER;
			_objectSlots[objectId].CreatorID = std::uint16_t(creatorId);
		} else if (madeByObject) {
			_objectSlots[objectId].CreatorType = Legacy::cOBJECT;
		}

		// A template no script asked for is as the game has it
		Legacy::jjOBJ* preset = _objectPresets[eventId];
		if (preset == nullptr) {
			return;
		}

		// An animation a script gave the template is what the objects show (see jjOBJ::CopyTemplate())
		if (Legacy::ScriptObjectHost::NeedsHost(*preset) || (preset->curAnim > 0 && preset->curAnim != preset->_synced.CurAnim)) {
			if (Legacy::jjOBJ* obj = EnsureObjectView(objectId)) {
				HostObject(obj, actor.get());
			}
		} else if (preset->energy != preset->_synced.Energy) {
			// The energy a script gave the template
			actor->SetHealth(std::max<std::int32_t>(preset->energy, 1));
		}
	}

	std::shared_ptr<Actors::ActorBase> LevelScriptLoader::SpawnSceneryObject(const Actors::ActorActivationDetails& details)
	{
		// Every scenery event in view gets here once (see Events::EventMap::SpawnTileEvent()), most of them don't need an
		// object - the one of the tile that does is driven by the script as the other objects of events are (see OnActorAdded())
		auto* levelHandler = runtime_cast<LevelHandler>(details.LevelHandler);
		LevelScriptLoader* _this = (levelHandler != nullptr ? levelHandler->_scripts.get() : nullptr);
		if (_this == nullptr) {
			return nullptr;
		}
		std::uint8_t eventId = std::uint8_t(_this->GetEventTranslator().ToOriginal(details.Type, details.Params) & 0xFF);
		Legacy::jjOBJ* preset = _this->_objectPresets[eventId];
		if (preset == nullptr || !Legacy::ScriptObjectHost::NeedsHost(*preset)) {
			return nullptr;
		}

		std::shared_ptr<Actors::ActorBase> actor = std::make_shared<Legacy::ScriptSceneryObject>();
		actor->OnActivated(details);
		return actor;
	}

	namespace
	{
		// The events of scenery of JJ2+, a bit each in LevelScriptLoader::_sceneryObjectsMask
		constexpr std::uint8_t SceneryEvents[] = { Legacy::aDESTRUCTSCENERY, Legacy::aDESTRUCTSCENERYBOMB, Legacy::aCOLLAPSESCENERY, Legacy::aSTOMPSCENERY };
	}

	std::uint8_t LevelScriptLoader::GetSceneryObjectsMask() const
	{
		std::uint8_t mask = 0;
		for (std::size_t i = 0; i < arraySize(SceneryEvents); i++) {
			Legacy::jjOBJ* preset = _objectPresets[SceneryEvents[i]];
			if (preset != nullptr && Legacy::ScriptObjectHost::NeedsHost(*preset)) {
				mask |= std::uint8_t(1 << i);
			}
		}
		return mask;
	}

	void LevelScriptLoader::RefreshSceneryObjects()
	{
		// A template that came to need an object after the scenery in view was activated (in onLevelBegin(), when it already
		// is) - that scenery is activated again, which makes the objects. It costs nothing while the templates stay as they are.
		// A bit is never cleared, the objects made stay, so the scenery is never activated again where it has one.
		std::uint8_t mask = GetSceneryObjectsMask();
		std::uint8_t added = std::uint8_t(mask & ~_sceneryObjectsMask);
		_sceneryObjectsMask |= mask;
		Events::EventMap* events = _levelHandler->EventMap();
		if (added == 0 || events == nullptr) {
			return;
		}

		events->ForEachEvent([this, added](Events::EventMap::EventTile& tile, std::int32_t x, std::int32_t y) {
			if (tile.IsEventActive && (tile.Event == EventType::SceneryDestruct || tile.Event == EventType::SceneryDestructButtstomp ||
				tile.Event == EventType::SceneryDestructSpeed || tile.Event == EventType::SceneryCollapse)) {
				std::uint8_t eventId = std::uint8_t(GetEventTranslator().ToOriginal(tile.Event, tile.EventParams) & 0xFF);
				for (std::size_t i = 0; i < arraySize(SceneryEvents); i++) {
					if (SceneryEvents[i] == eventId && (added & (1 << i)) != 0) {
						tile.IsEventActive = false;
						break;
					}
				}
			}
			return true;
		});
	}

	const LevelScriptLoader::BehaviorMethods& LevelScriptLoader::GetBehaviorMethods(asITypeInfo* type)
	{
		auto it = _behaviorMethods.find(type);
		if (it == _behaviorMethods.end()) {
			BehaviorMethods methods;
			methods.OnBehave = type->GetMethodByDecl("void onBehave(jjOBJ@ obj)");
			methods.OnDraw = type->GetMethodByDecl("void onDraw(jjOBJ@ obj)");
			methods.OnObjectHit = type->GetMethodByDecl("bool onObjectHit(jjOBJ@, jjOBJ@, jjPLAYER@, int)");
			it = _behaviorMethods.emplace(type, methods).first;
		}
		return it->second;
	}

	Legacy::jjOBJ* LevelScriptLoader::GetObjectPreset(std::uint8_t eventId)
	{
		Legacy::jjOBJ*& preset = _objectPresets[eventId];
		if (preset == nullptr) {
			void* mem = asAllocMem(sizeof(Legacy::jjOBJ));
			preset = new(mem) Legacy::jjOBJ();
			preset->eventID = eventId;
			preset->behavior = Legacy::DefaultBehavior;
			preset->state = Legacy::sSTART;
			// Not as set by a script, it doesn't change how the objects are handled
			preset->_playerHandling = Legacy::GetDefaultPlayerHandling(eventId);
			// The animations of the objects in JJ2+, scripts take the frames of them to change them
			if (const Legacy::PresetAnimation* presetAnim = Legacy::FindPresetAnimation(eventId)) {
				auto& tables = GetAnimationTables();
				std::int32_t setID = Legacy::AnimationTables::GetSetID(presetAnim->Set, versionTSF);
				std::uint32_t firstAnim = (setID >= 0 ? tables.GetFirstAnim(setID) : 0);
				std::uint32_t frameCount, firstFrame;
				if (firstAnim != 0 && tables.GetAnimationFrames(firstAnim + presetAnim->Animation, frameCount, firstFrame)) {
					preset->curAnim = std::int16_t(firstAnim + presetAnim->Animation);
					preset->curFrame = firstFrame;
				}
				std::int32_t killSetID = Legacy::AnimationTables::GetSetID(presetAnim->KillSet, versionTSF);
				std::uint32_t killFirstAnim = (killSetID >= 0 ? tables.GetFirstAnim(killSetID) : 0);
				if (killFirstAnim != 0) {
					preset->killAnim = std::int16_t(killFirstAnim + presetAnim->KillAnimation);
				}
			}
			Legacy::InitPresetBullet(*preset);
			// What a script changes of the template is told apart by this, see OnActorAdded()
			preset->_synced.Energy = preset->energy;
			preset->_synced.CurAnim = preset->curAnim;
			preset->_synced.LightType = preset->lightType;
		}
		preset->AddRef();
		return preset;
	}

	Legacy::jjOBJ* LevelScriptLoader::GetObjectById(std::int32_t objectId)
	{
		Legacy::jjOBJ* obj = (objectId > 0 && objectId < std::int32_t(_objectSlots.size()) ? EnsureObjectView(objectId) : nullptr);
		if (obj == nullptr) {
			// An object ID nothing has gets an inactive object
			void* mem = asAllocMem(sizeof(Legacy::jjOBJ));
			obj = new(mem) Legacy::jjOBJ();
			obj->objectID = std::int16_t(objectId);
			return obj;
		}
		obj->AddRef();
		return obj;
	}

	std::int32_t LevelScriptLoader::RegisterObject(const std::shared_ptr<Actors::ActorBase>& actor, Legacy::jjOBJ* object, std::uint8_t eventId)
	{
		if (_objectSlots.empty()) {
			// Object ID 0 is never used
			_objectSlots.push_back({});
		}

		// The lowest object ID that's free, as JJ2+ reuses them - the one of a crate that broke stays its (see BreakObject())
		std::int32_t objectId = 0;
		for (std::int32_t i = 1; i < std::int32_t(_objectSlots.size()); i++) {
			if (_objectSlots[i].Actor.expired() && std::find(_brokenObjects.begin(), _brokenObjects.end(), i) == _brokenObjects.end()) {
				objectId = i;
				break;
			}
		}
		if (objectId == 0) {
			if (std::int32_t(_objectSlots.size()) >= jjObjectMax) {
				return 0;
			}
			objectId = std::int32_t(_objectSlots.size());
			_objectSlots.push_back({});
		}

		ObjectSlot& slot = _objectSlots[objectId];
		if (slot.Object != nullptr) {
			// The view of the object that had the ID before stays inactive for whoever holds it
			slot.Object->Release();
		}
		SetSlotActor(objectId, actor);
		slot.Object = object;
		slot.EventID = eventId;
		// An object of an event is made by the level
		slot.CreatorType = 1;
		slot.CreatorID = 0;
		slot.SpawnTick = GetGameTick();
		if (object != nullptr) {
			object->objectID = std::int16_t(objectId);
		}

		jjObjectCount = std::max<std::int32_t>(jjObjectCount, objectId + 1);
		return objectId;
	}

	Legacy::jjOBJ* LevelScriptLoader::EnsureObjectView(std::int32_t objectId)
	{
		ObjectSlot& slot = _objectSlots[objectId];
		if (slot.Object != nullptr) {
			return slot.Object;
		}

		std::shared_ptr<Actors::ActorBase> actor = slot.Actor.lock();
		if (actor == nullptr) {
			return nullptr;
		}

		// An object is built from its template, as in JJ2+
		void* mem = asAllocMem(sizeof(Legacy::jjOBJ));
		Legacy::jjOBJ* obj = new(mem) Legacy::jjOBJ();
		if (Legacy::jjOBJ* preset = _objectPresets[slot.EventID]) {
			obj->CopyTemplate(*preset);
		} else {
			obj->behavior = Legacy::DefaultBehavior;
			obj->state = Legacy::sSTART;
			obj->_playerHandling = Legacy::GetDefaultPlayerHandling(slot.EventID);
		}
		obj->eventID = slot.EventID;
		obj->objectID = std::int16_t(objectId);
		obj->_actor = actor;
		obj->_creatorType = slot.CreatorType;
		obj->_creatorID = slot.CreatorID;
		Vector2f spawnPos = Legacy::ScriptObjectHost::GetSpawnPos(*actor);
		obj->xOrg = spawnPos.X;
		obj->yOrg = spawnPos.Y;
		Legacy::ScriptObjectHost::PullFromActor(this, *obj, *actor);
		// An object the level illuminates has the light JJ2+ gives it (see ScriptObjectHost::OwnsLights())
		if (actor->GetState(Actors::ActorState::Illuminated) && obj->lightType == obj->_synced.LightType) {
			obj->lightType = Legacy::lightLASER;
			obj->light = 1;
			obj->_synced.LightType = obj->lightType;
		}

		UpdateEnemyProjectileView(slot, *obj, *actor, true);

		slot.Object = obj;
		return obj;
	}

	void LevelScriptLoader::SetSlotActor(std::int32_t objectId, const std::shared_ptr<Actors::ActorBase>& actor)
	{
		ObjectSlot& slot = _objectSlots[objectId];
		if (slot.ActorKey != nullptr) {
			auto it = _objectIdsByActor.find(slot.ActorKey);
			if (it != _objectIdsByActor.end() && it->second == objectId) {
				_objectIdsByActor.erase(it);
			}
		}
		slot.Actor = actor;
		slot.ActorKey = actor.get();
		if (actor != nullptr) {
			_objectIdsByActor[actor.get()] = objectId;
		}
	}

	Legacy::jjOBJ* LevelScriptLoader::GetObjectView(Actors::ActorBase* actor)
	{
		auto it = _objectIdsByActor.find(actor);
		if (it == _objectIdsByActor.end()) {
			return nullptr;
		}
		// The entry may be of an object that's gone, whose memory another one has now
		ObjectSlot& slot = _objectSlots[it->second];
		std::shared_ptr<Actors::ActorBase> current = slot.Actor.lock();
		if (current.get() != actor) {
			return nullptr;
		}
		return EnsureObjectView(it->second);
	}

	void LevelScriptLoader::HostObject(Legacy::jjOBJ* obj, Actors::ActorBase* actor)
	{
		if (obj->_host == nullptr) {
			actor->SetUpdateOverride(std::make_unique<Legacy::ScriptObjectHost>(this, obj, actor));
		}
	}

	void LevelScriptLoader::SyncObjectsFromActors()
	{
		BreakObjects();

		for (std::size_t i = 1; i < _objectSlots.size(); i++) {
			ObjectSlot& slot = _objectSlots[i];
			// A driven object and one a script controls entirely keep their views themselves
			if (slot.Object == nullptr || slot.Object->_host != nullptr || slot.Object->_wrapper != nullptr) {
				continue;
			}
			if (std::shared_ptr<Actors::ActorBase> actor = slot.Actor.lock()) {
				Legacy::ScriptObjectHost::PullFromActor(this, *slot.Object, *actor);
				UpdateEnemyProjectileView(slot, *slot.Object, *actor, false);
			}
		}
	}

	void LevelScriptLoader::UpdateEnemyProjectileView(const ObjectSlot& slot, Legacy::jjOBJ& obj, Actors::ActorBase& actor, bool created)
	{
		const Legacy::EnemyProjectile* projectile = Legacy::FindEnemyProjectile(actor);
		if (projectile == nullptr) {
			return;
		}

		if (created) {
			obj.behavior = projectile->Behavior;
			obj.counterEnd = projectile->CounterEnd;
			obj.animSpeed = 1;
			obj._playerHandling = Legacy::hENEMYBULLET;
			if (projectile->KillSet != Legacy::mCOUNT) {
				std::int32_t setID = Legacy::AnimationTables::GetSetID(projectile->KillSet, versionTSF);
				std::uint32_t firstAnim = (setID >= 0 ? GetAnimationTables().GetFirstAnim(setID) : 0);
				obj.killAnim = std::uint16_t(firstAnim != 0 ? firstAnim + projectile->KillAnimation : 0);
			}
		}
		if (obj.state == Legacy::sKILL) {
			return;
		}

		// A new one is as JJ2+ has it before its behavior runs first, in the tick it's made in or the next one - which is
		// when the scripts of JJ2+ replace it, a bullet made for it takes the speed of its creator (see
		// ScriptLegacyObject::RunBullet()) - and it flies after that
		if (GetGameTick() - slot.SpawnTick <= 1) {
			obj.state = projectile->State;
			if (projectile->XSpeed != 0.0f) {
				obj.xSpeed = projectile->XSpeed;
				obj.xAcc = projectile->XAcc;
			}
		} else {
			obj.state = Legacy::sFLY;
		}
		// Nothing of it goes to the object of the game
		obj._synced.XSpeed = obj.xSpeed;
		obj._synced.State = obj.state;
	}

	void LevelScriptLoader::SyncObjectsToActors()
	{
		for (std::size_t i = 1; i < _objectSlots.size(); i++) {
			ObjectSlot& slot = _objectSlots[i];
			if (slot.Object == nullptr || slot.Object->_wrapper != nullptr) {
				continue;
			}
			std::shared_ptr<Actors::ActorBase> actor = slot.Actor.lock();
			if (actor == nullptr) {
				continue;
			}

			Legacy::ScriptObjectHost::UpdateSkin(*slot.Object);
			Legacy::ScriptObjectHost::PushToActor(*slot.Object, *actor);
			// A script gave a running object a behavior or an animation of its own, or handles its collisions
			if (slot.Object->_host == nullptr && Legacy::ScriptObjectHost::NeedsHost(*slot.Object)) {
				HostObject(slot.Object, actor.get());
			}
		}
	}

	void LevelScriptLoader::ReleaseObjects()
	{
		for (std::size_t i = 1; i < _objectSlots.size(); i++) {
			ObjectSlot& slot = _objectSlots[i];
			if (slot.Object == nullptr) {
				continue;
			}
			// An object the script drives stops, the script goes away
			if (slot.Object->_host != nullptr) {
				if (std::shared_ptr<Actors::ActorBase> actor = slot.Actor.lock()) {
					if (actor->GetUpdateOverride() == static_cast<Actors::IActorUpdateOverride*>(slot.Object->_host)) {
						actor->SetUpdateOverride(nullptr);
					}
				}
			}
			// One the script controls entirely stays as long as the level does, its behavior mustn't hold the script
			if (slot.Object->_wrapper != nullptr) {
				slot.Object->behavior = Legacy::jjBEHAVIOR();
			}
			slot.Object->Release();
			slot.Object = nullptr;
		}
		_objectSlots.clear();
		_objectIdsByActor.clear();

		for (Legacy::jjOBJ*& preset : _objectPresets) {
			if (preset != nullptr) {
				preset->Release();
				preset = nullptr;
			}
		}
	}
}

#endif
