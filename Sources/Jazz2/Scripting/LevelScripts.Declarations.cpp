#if defined(WITH_ANGELSCRIPT)

#include "LevelScripts.h"
#include "RegisterArray.h"
#include "RegisterRef.h"
#include "RegisterString.h"
#include "ScriptActorWrapper.h"
#include "ScriptPlayerWrapper.h"

#include "../LevelHandler.h"
#include "../PreferencesCache.h"
#include "../Actors/ActorBase.h"

#include "../../nCine/Base/Random.h"

// TODO: No-op implementations for Legacy declarations
namespace Jazz2::Scripting
{
	void NoOp(const char* sourceName) {
		LOGE_X("%s", sourceName);
	}

#ifdef __GNUC__
#	define noop() NoOp(__PRETTY_FUNCTION__)
#elif _MSC_VER
#	define noop() NoOp(__FUNCTION__)
#else
#	define noop() NoOp(__func__))
#endif

	enum airjump {
		airjumpNONE,
		airjumpHELICOPTER,
		airjumpSPAZ
	};

	enum ambientLighting {
		ambientLighting_OPTIONAL,
		ambientLighting_BASIC,
		ambientLighting_COMPLETE
	};

	enum anim {
		mAMMO,
		mBAT,
		mBEEBOY,
		mBEES,
		mBIGBOX,
		mBIGROCK,
		mBIGTREE,
		mBILSBOSS,
		mBIRD,
		mBIRD3D,
		mBOLLPLAT,
		mBONUS,
		mBOSS,
		mBRIDGE,
		mBUBBA,
		mBUMBEE,
		mBUTTERFLY,
		mCARROTPOLE,
		mCAT,
		mCAT2,
		mCATERPIL,
		mCHUCK,
		mCOMMON,
		mCONTINUE,
		mDEMON,
		mDESTSCEN,
		mDEVAN,
		mDEVILDEVAN,
		mDIAMPOLE,
		mDOG,
		mDOOR,
		mDRAGFLY,
		mDRAGON,

		mEVA,
		mFACES,

		mFATCHK,
		mFENCER,
		mFISH,
		mFLAG,
		mFLARE,
		mFONT,
		mFROG,
		mFRUITPLAT,
		mGEMRING,
		mGLOVE,
		mGRASSPLAT,
		mHATTER,
		mHELMUT,

		mJAZZ,
		mJAZZ3D,

		mJUNGLEPOLE,
		mLABRAT,
		mLIZARD,
		mLORI,
		mLORI2,

		mMENU,
		mMENUFONT,

		mMONKEY,
		mMOTH,

		mPICKUPS,
		mPINBALL,
		mPINKPLAT,
		mPSYCHPOLE,
		mQUEEN,
		mRAPIER,
		mRAVEN,
		mROBOT,
		mROCK,
		mROCKTURT,

		mSKELETON,
		mSMALTREE,
		mSNOW,
		mSONCSHIP,
		mSONICPLAT,
		mSPARK,
		mSPAZ,
		mSPAZ2,
		mSPAZ3D,
		mSPIKEBOLL,
		mSPIKEBOLL3D,
		mSPIKEPLAT,
		mSPRING,
		mSTEAM,

		mSUCKER,
		mTUBETURT,
		mTUFBOSS,
		mTUFTURT,
		mTURTLE,
		mTWEEDLE,
		mUTERUS,
		mVINE,
		mWARP10,
		mWARP100,
		mWARP20,
		mWARP50,

		mWITCH,
		mXBILSY,
		mXLIZARD,
		mXTURTLE,
		mZDOG,
		mZSPARK,
		mZZAMMO,
		mZZBETA,
		mZZCOMMON,
		mZZCONTINUE,

		mZZFONT,
		mZZMENUFONT,
		mZZREPLACEMENTS,
		mZZRETICLES,
		mZZSCENERY,
		mZZWARP
	};

	enum dir {
		dirRIGHT,
		dirLEFT,
		dirUP,
		dirCURRENT
	};

	enum gameState {
		gameSTOPPED,
		gameSTARTED,
		gamePAUSED,
		gamePREGAME,
		gameOVERTIME
	};

	enum gameConnection {
		gameLOCAL,
		gameINTERNET,
		gameLAN_TCP
	};

	enum GM_ {
		GM_SP,
		GM_COOP,
		GM_BATTLE,
		GM_CTF,
		GM_TREASURE,
		GM_RACE
	};

	enum groundjump {
		groundjumpNONE,
		groundjumpREGULARJUMP,
		groundjumpJAZZ,
		groundjumpSPAZ,
		groundjumpLORI
	};

	enum object {
		aPLAYERBULLET1,
		aPLAYERBULLET2,
		aPLAYERBULLET3,
		aPLAYERBULLET4,
		aPLAYERBULLET5,
		aPLAYERBULLET6,
		aPLAYERBULLET8,
		aPLAYERBULLET9,
		aPLAYERBULLETP1,
		aPLAYERBULLETP2,
		aPLAYERBULLETP3,
		aPLAYERBULLETP4,
		aPLAYERBULLETP5,
		aPLAYERBULLETP6,
		aPLAYERBULLETP8,
		aPLAYERBULLETP9,
		aPLAYERBULLETC1,
		aPLAYERBULLETC2,
		aPLAYERBULLETC3,
		aBULLET,
		aCATSMOKE,
		aSHARD,
		aEXPLOSION,
		aBOUNCEONCE,
		aREDGEMTEMP,
		aPLAYERLASER,
		aUTERUSEL,
		aBIRD,
		aBUBBLE,
		aGUN3AMMO3,
		aGUN2AMMO3,
		aGUN4AMMO3,
		aGUN5AMMO3,
		aGUN6AMMO3,
		aGUN7AMMO3,
		aGUN8AMMO3,
		aGUN9AMMO3,
		aTURTLESHELL,
		aSWINGVINE,
		aBOMB,
		aSILVERCOIN,
		aGOLDCOIN,
		aGUNCRATE,
		aCARROTCRATE,
		a1UPCRATE,
		aGEMBARREL,
		aCARROTBARREL,
		a1UPBARREL,
		aBOMBCRATE,
		aGUN3AMMO15,
		aGUN2AMMO15,
		aGUN4AMMO15,
		aGUN5AMMO15,
		aGUN6AMMO15,
		aTNT,
		aAIRBOARDGENERATOR,
		aFROZENGREENSPRING,
		aGUNFASTFIRE,
		aSPRINGCRATE,
		aREDGEM,
		aGREENGEM,
		aBLUEGEM,
		aPURPLEGEM,
		aSUPERREDGEM,
		aBIRDCAGE,
		aGUNBARREL,
		aGEMCRATE,
		aMORPHMONITOR,
		aENERGYUP,
		aFULLENERGY,
		aFIRESHIELD,
		aWATERSHIELD,
		aLIGHTSHIELD,
		aFASTFEET,
		aEXTRALIFE,
		aENDOFLEVELPOST,
		aSAVEPOST,
		aBONUSLEVELPOST,
		aREDSPRING,
		aGREENSPRING,
		aBLUESPRING,
		aINVINCIBILITY,
		aEXTRATIME,
		aFREEZER,
		aHREDSPRING,
		aHGREENSPRING,
		aHBLUESPRING,
		aBIRDMORPHMONITOR,
		aTRIGGERCRATE,
		aFLYCARROT,
		aRECTREDGEM,
		aRECTGREENGEM,
		aRECTBLUEGEM,
		aTUFTURT,
		aTUFBOSS,
		aLABRAT,
		aDRAGON,
		aLIZARD,
		aBUMBEE,
		aRAPIER,
		aSPARK,
		aBAT,
		aSUCKER,
		aCATERPILLAR,
		aCHESHIRE1,
		aCHESHIRE2,
		aHATTER,
		aBILSYBOSS,
		aSKELETON,
		aDOGGYDOGG,
		aNORMTURTLE,
		aHELMUT,
		aDEMON,
		aDRAGONFLY,
		aMONKEY,
		aFATCHK,
		aFENCER,
		aFISH,
		aMOTH,
		aSTEAM,
		aROCK,
		aGUN1POWER,
		aGUN2POWER,
		aGUN3POWER,
		aGUN4POWER,
		aGUN5POWER,
		aGUN6POWER,
		aPINLEFTPADDLE,
		aPINRIGHTPADDLE,
		aPIN500BUMP,
		aPINCARROTBUMP,
		aAPPLE,
		aBANANA,
		aCHERRY,
		aORANGE,
		aPEAR,
		aPRETZEL,
		aSTRAWBERRY,
		aSTEADYLIGHT,
		aPULZELIGHT,
		aFLICKERLIGHT,
		aQUEENBOSS,
		aFLOATSUCKER,
		aBRIDGE,
		aLEMON,
		aLIME,
		aTHING,
		aWMELON,
		aPEACH,
		aGRAPES,
		aLETTUCE,
		aEGGPLANT,
		aCUCUMB,
		aCOKE,
		aPEPSI,
		aMILK,
		aPIE,
		aCAKE,
		aDONUT,
		aCUPCAKE,
		aCHIPS,
		aCANDY1,
		aCHOCBAR,
		aICECREAM,
		aBURGER,
		aPIZZA,
		aFRIES,
		aCHICKLEG,
		aSANDWICH,
		aTACOBELL,
		aWEENIE,
		aHAM,
		aCHEESE,
		aFLOATLIZARD,
		aSTANDMONKEY,
		aDESTRUCTSCENERY,
		aDESTRUCTSCENERYBOMB,
		aCOLLAPSESCENERY,
		aSTOMPSCENERY,
		aGEMSTOMP,
		aRAVEN,
		aTUBETURTLE,
		aGEMRING,
		aROTSMALLTREE,
		aAMBIENTSOUND,
		aUTERUS,
		aCRAB,
		aWITCH,
		aROCKTURT,
		aBUBBA,
		aDEVILDEVAN,
		aDEVANROBOT,
		aROBOT,
		aCARROTUSPOLE,
		aPSYCHPOLE,
		aDIAMONDUSPOLE,
		aFRUITPLATFORM,
		aBOLLPLATFORM,
		aGRASSPLATFORM,
		aPINKPLATFORM,
		aSONICPLATFORM,
		aSPIKEPLATFORM,
		aSPIKEBOLL,
		aGENERATOR,
		aEVA,
		aBUBBLER,
		aTNTPOWER,
		aGUN8POWER,
		aGUN9POWER,
		aSPIKEBOLL3D,
		aSPRINGCORD,
		aBEES,
		aCOPTER,
		aLASERSHIELD,
		aSTOPWATCH,
		aJUNGLEPOLE,
		aBIGROCK,
		aBIGBOX,
		aTRIGGERSCENERY,
		aSONICBOSS,
		aBUTTERFLY,
		aBEEBOY,
		aSNOW,
		aTWEEDLEBOSS,
		aAIRBOARD,
		aFLAG,
		aXNORMTURTLE,
		aXLIZARD,
		aXFLOATLIZARD,
		aXBILSYBOSS,
		aZCAT,
		aZGHOST,

		areaONEWAY,
		areaHURT,
		areaVINE,
		areaHOOK,
		areaSLIDE,
		areaHPOLE,
		areaVPOLE,
		areaFLYOFF,
		areaRICOCHET,
		areaBELTRIGHT,
		areaBELTLEFT,
		areaBELTACCRIGHT,
		areaBELTACCLEFT,
		areaSTOPENEMY,
		areaWINDLEFT,
		areaWINDRIGHT,
		areaEOL,
		areaWARPEOL,
		areaENDMORPH,
		areaFLOATUP,
		areaROCKTRIGGER,
		areaDIMLIGHT,
		areaSETLIGHT,
		areaLIMITXSCROLL,
		areaRESETLIGHT,
		areaWARPSECRET,
		areaECHO,
		areaBOSSTRIGGER,
		areaJAZZLEVELSTART,
		areaSPAZLEVELSTART,
		areaMPLEVELSTART,
		areaLORILEVELSTART,
		areaWARP,
		areaWARPTARGET,
		areaAREAID,
		areaNOFIREZONE,
		areaTRIGGERZONE,

		aSUCKERTUBE,
		aTEXT,
		aWATERLEVEL,
		aMORPHFROG,
		aWATERBLOCK
	};

	enum particle {
		particleNONE,
		particlePIXEL,
		particleFIRE,
		particleSMOKE,
		particleICETRAIL,
		particleSPARK,
		particleSCORE,
		particleSNOW,
		particleRAIN,
		particleFLOWER,
		particleLEAF,
		particleSTAR,
		particleTILE
	};

	enum playerAnim {
		mJAZZ_AIRBOARD,
		mJAZZ_AIRBOARDTURN,
		mJAZZ_BUTTSTOMPLAND,
		mJAZZ_CORPSE,
		mJAZZ_DIE,
		mJAZZ_DIVE,
		mJAZZ_DIVEFIREQUIT,
		mJAZZ_DIVEFIRERIGHT,
		mJAZZ_DIVEUP,
		mJAZZ_EARBRACHIATE,
		mJAZZ_ENDOFLEVEL,
		mJAZZ_FALL,
		mJAZZ_FALLBUTTSTOMP,
		mJAZZ_FALLLAND,
		mJAZZ_FIRE,
		mJAZZ_FIREUP,
		mJAZZ_FIREUPQUIT,
		mJAZZ_FROG,
		mJAZZ_HANGFIREQUIT,
		mJAZZ_HANGFIREREST,
		mJAZZ_HANGFIREUP,
		mJAZZ_HANGIDLE1,
		mJAZZ_HANGIDLE2,
		mJAZZ_HANGINGFIREQUIT,
		mJAZZ_HANGINGFIRERIGHT,
		mJAZZ_HELICOPTER,
		mJAZZ_HELICOPTERFIREQUIT,
		mJAZZ_HELICOPTERFIRERIGHT,
		mJAZZ_HPOLE,
		mJAZZ_HURT,
		mJAZZ_IDLE1,
		mJAZZ_IDLE2,
		mJAZZ_IDLE3,
		mJAZZ_IDLE4,
		mJAZZ_IDLE5,
		mJAZZ_JUMPFIREQUIT,
		mJAZZ_JUMPFIRERIGHT,
		mJAZZ_JUMPING1,
		mJAZZ_JUMPING2,
		mJAZZ_JUMPING3,
		mJAZZ_LEDGEWIGGLE,
		mJAZZ_LIFT,
		mJAZZ_LIFTJUMP,
		mJAZZ_LIFTLAND,
		mJAZZ_LOOKUP,
		mJAZZ_LOOPY,
		mJAZZ_PUSH,
		mJAZZ_QUIT,
		mJAZZ_REV1,
		mJAZZ_REV2,
		mJAZZ_REV3,
		mJAZZ_RIGHTFALL,
		mJAZZ_RIGHTJUMP,
		mJAZZ_ROLLING,
		mJAZZ_RUN1,
		mJAZZ_RUN2,
		mJAZZ_RUN3,
		mJAZZ_SKID1,
		mJAZZ_SKID2,
		mJAZZ_SKID3,
		mJAZZ_SPRING,
		mJAZZ_STAND,
		mJAZZ_STATIONARYJUMP,
		mJAZZ_STATIONARYJUMPEND,
		mJAZZ_STATIONARYJUMPSTART,
		mJAZZ_STONED,
		mJAZZ_SWIMDOWN,
		mJAZZ_SWIMRIGHT,
		mJAZZ_SWIMTURN1,
		mJAZZ_SWIMTURN2,
		mJAZZ_SWIMUP,
		mJAZZ_SWINGINGVINE,
		mJAZZ_TELEPORT,
		mJAZZ_TELEPORTFALL,
		mJAZZ_TELEPORTFALLING,
		mJAZZ_TELEPORTFALLTELEPORT,
		mJAZZ_TELEPORTSTAND,
		mJAZZ_VPOLE
	};
	
	enum spriteType {
		spriteType_NORMAL,
		spriteType_TRANSLUCENT,
		spriteType_TINTED,
		spriteType_GEM,
		spriteType_INVISIBLE,
		spriteType_SINGLECOLOR,
		spriteType_RESIZED,
		spriteType_NEONGLOW,
		spriteType_FROZEN,
		spriteType_PLAYER,
		spriteType_PALSHIFT,
		spriteType_SHADOW,
		spriteType_SINGLEHUE,
		spriteType_BRIGHTNESS,
		spriteType_TRANSLUCENTCOLOR,
		spriteType_TRANSLUCENTPLAYER,
		spriteType_TRANSLUCENTPALSHIFT,
		spriteType_TRANSLUCENTSINGLEHUE,
		spriteType_ALPHAMAP,
		spriteType_MENUPLAYER,
		spriteType_BLENDNORMAL,
		spriteType_BLENDDARKEN,
		spriteType_BLENDLIGHTEN,
		spriteType_BLENDHUE,
		spriteType_BLENDSATURATION,
		spriteType_BLENDCOLOR,
		spriteType_BLENDLUMINANCE,
		spriteType_BLENDMULTIPLY,
		spriteType_BLENDSCREEN,
		spriteType_BLENDDISSOLVE,
		spriteType_BLENDOVERLAY,
		spriteType_BLENDHARDLIGHT,
		spriteType_BLENDSOFTLIGHT,
		spriteType_BLENDDIFFERENCE,
		spriteType_BLENDDODGE,
		spriteType_BLENDBURN,
		spriteType_BLENDEXCLUSION,
		spriteType_TRANSLUCENTTILE,
		spriteType_CHROMAKEY,
		spriteType_MAPPING,
		spriteType_TRANSLUCENTMAPPING
	};

	enum sound {
		sAMMO_BLUB1,
		sAMMO_BLUB2,
		sAMMO_BMP1,
		sAMMO_BMP2,
		sAMMO_BMP3,
		sAMMO_BMP4,
		sAMMO_BMP5,
		sAMMO_BMP6,
		sAMMO_BOEM1,
		sAMMO_BUL1,
		sAMMO_BULFL1,
		sAMMO_BULFL2,
		sAMMO_BULFL3,
		sAMMO_FIREGUN1A,
		sAMMO_FIREGUN2A,
		sAMMO_FUMP,
		sAMMO_GUN1,
		sAMMO_GUN2,
		sAMMO_GUN3PLOP,
		sAMMO_GUNFLP,
		sAMMO_GUNFLP1,
		sAMMO_GUNFLP2,
		sAMMO_GUNFLP3,
		sAMMO_GUNFLP4,
		sAMMO_GUNFLPL,
		sAMMO_GUNJAZZ,
		sAMMO_GUNVELOCITY,
		sAMMO_ICEGUN,
		sAMMO_ICEGUN2,
		sAMMO_ICEGUNPU,
		sAMMO_ICEPU1,
		sAMMO_ICEPU2,
		sAMMO_ICEPU3,
		sAMMO_ICEPU4,
		sAMMO_LASER,
		sAMMO_LASER2,
		sAMMO_LASER3,
		sAMMO_LAZRAYS,
		sAMMO_MISSILE,
		sAMMO_SPZBL1,
		sAMMO_SPZBL2,
		sAMMO_SPZBL3,
		sBAT_BATFLY1,
		sBILSBOSS_BILLAPPEAR,
		sBILSBOSS_FINGERSNAP,
		sBILSBOSS_FIRE,
		sBILSBOSS_FIRESTART,
		sBILSBOSS_SCARY3,
		sBILSBOSS_THUNDER,
		sBILSBOSS_ZIP,
		sBONUS_BONUS1,
		sBONUS_BONUSBLUB,
		sBUBBA_BUBBABOUNCE1,
		sBUBBA_BUBBABOUNCE2,
		sBUBBA_BUBBAEXPLO,
		sBUBBA_FROG2,
		sBUBBA_FROG3,
		sBUBBA_FROG4,
		sBUBBA_FROG5,
		sBUBBA_SNEEZE2,
		sBUBBA_TORNADOATTACK2,
		sBUMBEE_BEELOOP,
		sCATERPIL_RIDOE,
		sCOMMON_AIRBOARD,
		sCOMMON_AIRBTURN,
		sCOMMON_AIRBTURN2,
		sCOMMON_BASE1,
		sCOMMON_BELL_FIRE,
		sCOMMON_BELL_FIRE2,
		sCOMMON_BENZIN1,
		sCOMMON_BIRDFLY,
		sCOMMON_BIRDFLY2,
		sCOMMON_BLOKPLOP,
		sCOMMON_BLUB1,
		sCOMMON_BUBBLGN1,
		sCOMMON_BURN,
		sCOMMON_BURNIN,
		sCOMMON_CANSPS,
		sCOMMON_CLOCK,
		sCOMMON_COIN,
		sCOMMON_COLLAPS,
		sCOMMON_CUP,
		sCOMMON_DAMPED1,
		sCOMMON_DOWN,
		sCOMMON_DOWNFL2,
		sCOMMON_DRINKSPAZZ1,
		sCOMMON_DRINKSPAZZ2,
		sCOMMON_DRINKSPAZZ3,
		sCOMMON_DRINKSPAZZ4,
		sCOMMON_EAT1,
		sCOMMON_EAT2,
		sCOMMON_EAT3,
		sCOMMON_EAT4,
		sCOMMON_ELECTRIC1,
		sCOMMON_ELECTRIC2,
		sCOMMON_ELECTRICHIT,
		sCOMMON_EXPL_TNT,
		sCOMMON_EXPSM1,
		sCOMMON_FLAMER,
		sCOMMON_FLAP,
		sCOMMON_FOEW1,
		sCOMMON_FOEW2,
		sCOMMON_FOEW3,
		sCOMMON_FOEW4,
		sCOMMON_FOEW5,
		sCOMMON_GEMSMSH1,
		sCOMMON_GLASS2,
		sCOMMON_GUNSM1,
		sCOMMON_HARP1,
		sCOMMON_HEAD,
		sCOMMON_HELI1,
		sCOMMON_HIBELL,
		sCOMMON_HOLYFLUT,
		sCOMMON_HORN1,
		sCOMMON_ICECRUSH,
		sCOMMON_IMPACT1,
		sCOMMON_IMPACT2,
		sCOMMON_IMPACT3,
		sCOMMON_IMPACT4,
		sCOMMON_IMPACT5,
		sCOMMON_IMPACT6,
		sCOMMON_IMPACT7,
		sCOMMON_IMPACT8,
		sCOMMON_IMPACT9,
		sCOMMON_ITEMTRE,
		sCOMMON_JUMP,
		sCOMMON_JUMP2,
		sCOMMON_LAND,
		sCOMMON_LAND1,
		sCOMMON_LAND2,
		sCOMMON_LANDCAN1,
		sCOMMON_LANDCAN2,
		sCOMMON_LANDPOP,
		sCOMMON_LOADJAZZ,
		sCOMMON_LOADSPAZ,
		sCOMMON_METALHIT,
		sCOMMON_MONITOR,
		sCOMMON_NOCOIN,
		sCOMMON_PICKUP1,
		sCOMMON_PICKUPW1,
		sCOMMON_PISTOL1,
		sCOMMON_PLOOP1,
		sCOMMON_PLOP1,
		sCOMMON_PLOP2,
		sCOMMON_PLOP3,
		sCOMMON_PLOP4,
		sCOMMON_PLOPKORK,
		sCOMMON_PREEXPL1,
		sCOMMON_PREHELI,
		sCOMMON_REVUP,
		sCOMMON_RINGGUN,
		sCOMMON_RINGGUN2,
		sCOMMON_SHIELD1,
		sCOMMON_SHIELD4,
		sCOMMON_SHIELD_ELEC,
		sCOMMON_SHLDOF3,
		sCOMMON_SLIP,
		sCOMMON_SMASH,
		sCOMMON_SPLAT1,
		sCOMMON_SPLAT2,
		sCOMMON_SPLAT3,
		sCOMMON_SPLAT4,
		sCOMMON_SPLUT,
		sCOMMON_SPRING1,
		sCOMMON_STEAM,
		sCOMMON_STEP,
		sCOMMON_STRETCH,
		sCOMMON_SWISH1,
		sCOMMON_SWISH2,
		sCOMMON_SWISH3,
		sCOMMON_SWISH4,
		sCOMMON_SWISH5,
		sCOMMON_SWISH6,
		sCOMMON_SWISH7,
		sCOMMON_SWISH8,
		sCOMMON_TELPORT1,
		sCOMMON_TELPORT2,
		sCOMMON_UP,
		sCOMMON_WATER,
		sCOMMON_WOOD1,
		sDEMON_RUN,
		sDEVILDEVAN_DRAGONFIRE,
		sDEVILDEVAN_FLAP,
		sDEVILDEVAN_FROG4,
		sDEVILDEVAN_JUMPUP,
		sDEVILDEVAN_LAUGH,
		sDEVILDEVAN_PHASER2,
		sDEVILDEVAN_STRECH2,
		sDEVILDEVAN_STRECHTAIL,
		sDEVILDEVAN_STRETCH1,
		sDEVILDEVAN_STRETCH3,
		sDEVILDEVAN_VANISH1,
		sDEVILDEVAN_WHISTLEDESCENDING2,
		sDEVILDEVAN_WINGSOUT,
		sDOG_AGRESSIV,
		sDOG_SNIF1,
		sDOG_WAF1,
		sDOG_WAF2,
		sDOG_WAF3,
		sDRAGFLY_BEELOOP,
		sENDING_OHTHANK,
		sENDTUNEJAZZ_TUNE,
		sENDTUNELORI_CAKE,
		sENDTUNESPAZ_TUNE,
		sEPICLOGO_EPIC1,
		sEPICLOGO_EPIC2,
		sEVA_KISS1,
		sEVA_KISS2,
		sEVA_KISS3,
		sEVA_KISS4,
		sFAN_FAN,
		sFATCHK_HIT1,
		sFATCHK_HIT2,
		sFATCHK_HIT3,
		sFENCER_FENCE1,
		sFROG_FROG,
		sFROG_FROG1,
		sFROG_FROG2,
		sFROG_FROG3,
		sFROG_FROG4,
		sFROG_FROG5,
		sFROG_JAZZ2FROG,
		sFROG_TONG,
		sGLOVE_HIT,
		sHATTER_CUP,
		sHATTER_HAT,
		sHATTER_PTOEI,
		sHATTER_SPLIN,
		sHATTER_SPLOUT,
		sINTRO_BLOW,
		sINTRO_BOEM1,
		sINTRO_BOEM2,
		sINTRO_BRAKE,
		sINTRO_END,
		sINTRO_GRAB,
		sINTRO_GREN1,
		sINTRO_GREN2,
		sINTRO_GREN3,
		sINTRO_GUNM0,
		sINTRO_GUNM1,
		sINTRO_GUNM2,
		sINTRO_HELI,
		sINTRO_HITSPAZ,
		sINTRO_HITTURT,
		sINTRO_IFEEL,
		sINTRO_INHALE,
		sINTRO_INSECT,
		sINTRO_KATROL,
		sINTRO_LAND,
		sINTRO_MONSTER,
		sINTRO_MONSTER2,
		sINTRO_ROCK,
		sINTRO_ROPE1,
		sINTRO_ROPE2,
		sINTRO_RUN,
		sINTRO_SHOT1,
		sINTRO_SHOTGRN,
		sINTRO_SKI,
		sINTRO_STRING,
		sINTRO_SWISH1,
		sINTRO_SWISH2,
		sINTRO_SWISH3,
		sINTRO_SWISH4,
		sINTRO_UHTURT,
		sINTRO_UP1,
		sINTRO_UP2,
		sINTRO_WIND_01,
		sJAZZSOUNDS_BALANCE,
		sJAZZSOUNDS_HEY1,
		sJAZZSOUNDS_HEY2,
		sJAZZSOUNDS_HEY3,
		sJAZZSOUNDS_HEY4,
		sJAZZSOUNDS_IDLE,
		sJAZZSOUNDS_JAZZV1,
		sJAZZSOUNDS_JAZZV2,
		sJAZZSOUNDS_JAZZV3,
		sJAZZSOUNDS_JAZZV4,
		sJAZZSOUNDS_JUMMY,
		sJAZZSOUNDS_PFOE,
		sLABRAT_BITE,
		sLABRAT_EYE2,
		sLABRAT_EYE3,
		sLABRAT_MOUSE1,
		sLABRAT_MOUSE2,
		sLABRAT_MOUSE3,
		sLIZARD_LIZ1,
		sLIZARD_LIZ2,
		sLIZARD_LIZ4,
		sLIZARD_LIZ6,
		sLORISOUNDS_DIE1,
		sLORISOUNDS_HURT0,
		sLORISOUNDS_HURT1,
		sLORISOUNDS_HURT2,
		sLORISOUNDS_HURT3,
		sLORISOUNDS_HURT4,
		sLORISOUNDS_HURT5,
		sLORISOUNDS_HURT6,
		sLORISOUNDS_HURT7,
		sLORISOUNDS_LORI1,
		sLORISOUNDS_LORI2,
		sLORISOUNDS_LORIBOOM,
		sLORISOUNDS_LORIFALL,
		sLORISOUNDS_LORIJUMP,
		sLORISOUNDS_LORIJUMP2,
		sLORISOUNDS_LORIJUMP3,
		sLORISOUNDS_LORIJUMP4,
		sLORISOUNDS_TOUCH,
		sLORISOUNDS_WEHOO,
		sMENUSOUNDS_SELECT0,
		sMENUSOUNDS_SELECT1,
		sMENUSOUNDS_SELECT2,
		sMENUSOUNDS_SELECT3,
		sMENUSOUNDS_SELECT4,
		sMENUSOUNDS_SELECT5,
		sMENUSOUNDS_SELECT6,
		sMENUSOUNDS_TYPE,
		sMENUSOUNDS_TYPEENTER,
		sMONKEY_SPLUT,
		sMONKEY_THROW,
		sMOTH_FLAPMOTH,
		sORANGE_BOEML,
		sORANGE_BOEMR,
		sORANGE_BUBBELSL,
		sORANGE_BUBBELSR,
		sORANGE_GLAS1L,
		sORANGE_GLAS1R,
		sORANGE_GLAS2L,
		sORANGE_GLAS2R,
		sORANGE_MERGE,
		sORANGE_SWEEP0L,
		sORANGE_SWEEP0R,
		sORANGE_SWEEP1L,
		sORANGE_SWEEP1R,
		sORANGE_SWEEP2L,
		sORANGE_SWEEP2R,
		sP2_CRUNCH,
		sP2_FART,
		sP2_FOEW1,
		sP2_FOEW4,
		sP2_FOEW5,
		sP2_FROG1,
		sP2_FROG2,
		sP2_FROG3,
		sP2_FROG4,
		sP2_FROG5,
		sP2_KISS4,
		sP2_OPEN,
		sP2_PINCH1,
		sP2_PINCH2,
		sP2_PLOPSEQ1,
		sP2_PLOPSEQ2,
		sP2_PLOPSEQ3,
		sP2_PLOPSEQ4,
		sP2_POEP,
		sP2_PTOEI,
		sP2_SPLOUT,
		sP2_SPLUT,
		sP2_THROW,
		sP2_TONG,
		sPICKUPS_BOING_CHECK,
		sPICKUPS_HELI2,
		sPICKUPS_STRETCH1A,
		sPINBALL_BELL,
		sPINBALL_FLIP1,
		sPINBALL_FLIP2,
		sPINBALL_FLIP3,
		sPINBALL_FLIP4,
		sQUEEN_LADYUP,
		sQUEEN_SCREAM,
		sRAPIER_GOSTDIE,
		sRAPIER_GOSTLOOP,
		sRAPIER_GOSTOOOH,
		sRAPIER_GOSTRIP,
		sRAPIER_HITCHAR,
		sROBOT_BIG1,
		sROBOT_BIG2,
		sROBOT_CAN1,
		sROBOT_CAN2,
		sROBOT_HYDRO,
		sROBOT_HYDRO2,
		sROBOT_HYDROFIL,
		sROBOT_HYDROPUF,
		sROBOT_IDLE1,
		sROBOT_IDLE2,
		sROBOT_JMPCAN1,
		sROBOT_JMPCAN10,
		sROBOT_JMPCAN2,
		sROBOT_JMPCAN3,
		sROBOT_JMPCAN4,
		sROBOT_JMPCAN5,
		sROBOT_JMPCAN6,
		sROBOT_JMPCAN7,
		sROBOT_JMPCAN8,
		sROBOT_JMPCAN9,
		sROBOT_METAL1,
		sROBOT_METAL2,
		sROBOT_METAL3,
		sROBOT_METAL4,
		sROBOT_METAL5,
		sROBOT_OPEN,
		sROBOT_OUT,
		sROBOT_POEP,
		sROBOT_POLE,
		sROBOT_SHOOT,
		sROBOT_STEP1,
		sROBOT_STEP2,
		sROBOT_STEP3,
		sROCK_ROCK1,
		sRUSH_RUSH,
		sSCIENCE_PLOPKAOS,
		sSKELETON_BONE1,
		sSKELETON_BONE2,
		sSKELETON_BONE3,
		sSKELETON_BONE5,
		sSKELETON_BONE6,
		sSKELETON_BONE7,
		sSMALTREE_FALL,
		sSMALTREE_GROUND,
		sSMALTREE_HEAD,
		sSONCSHIP_METAL1,
		sSONCSHIP_MISSILE2,
		sSONCSHIP_SCRAPE,
		sSONCSHIP_SHIPLOOP,
		sSONCSHIP_TARGETLOCK,
		sSPAZSOUNDS_AUTSCH1,
		sSPAZSOUNDS_AUTSCH2,
		sSPAZSOUNDS_BIRDSIT,
		sSPAZSOUNDS_BURP,
		sSPAZSOUNDS_CHIRP,
		sSPAZSOUNDS_EATBIRD,
		sSPAZSOUNDS_HAHAHA,
		sSPAZSOUNDS_HAHAHA2,
		sSPAZSOUNDS_HAPPY,
		sSPAZSOUNDS_HIHI,
		sSPAZSOUNDS_HOHOHO1,
		sSPAZSOUNDS_HOOO,
		sSPAZSOUNDS_KARATE7,
		sSPAZSOUNDS_KARATE8,
		sSPAZSOUNDS_OHOH,
		sSPAZSOUNDS_OOOH,
		sSPAZSOUNDS_WOOHOO,
		sSPAZSOUNDS_YAHOO,
		sSPAZSOUNDS_YAHOO2,
		sSPRING_BOING_DOWN,
		sSPRING_SPRING1,
		sSTEAM_STEAM,
		sSTONED_STONED,
		sSUCKER_FART,
		sSUCKER_PINCH1,
		sSUCKER_PINCH2,
		sSUCKER_PINCH3,
		sSUCKER_PLOPSEQ1,
		sSUCKER_PLOPSEQ2,
		sSUCKER_PLOPSEQ3,
		sSUCKER_PLOPSEQ4,
		sSUCKER_UP,
		sTUFBOSS_CATCH,
		sTUFBOSS_RELEASE,
		sTUFBOSS_SWING,
		sTURTLE_BITE3,
		sTURTLE_HIDE,
		sTURTLE_HITSHELL,
		sTURTLE_IDLE1,
		sTURTLE_IDLE2,
		sTURTLE_NECK,
		sTURTLE_SPK1TURT,
		sTURTLE_SPK2TURT,
		sTURTLE_SPK3TURT,
		sTURTLE_SPK4TURT,
		sTURTLE_TURN,
		sUTERUS_CRABCLOSE,
		sUTERUS_CRABOPEN2,
		sUTERUS_SCISSORS1,
		sUTERUS_SCISSORS2,
		sUTERUS_SCISSORS3,
		sUTERUS_SCISSORS4,
		sUTERUS_SCISSORS5,
		sUTERUS_SCISSORS6,
		sUTERUS_SCISSORS7,
		sUTERUS_SCISSORS8,
		sUTERUS_SCREAM1,
		sUTERUS_STEP1,
		sUTERUS_STEP2,
		sWIND_WIND2A,
		sWITCH_LAUGH,
		sWITCH_MAGIC,
		sXBILSY_BILLAPPEAR,
		sXBILSY_FINGERSNAP,
		sXBILSY_FIRE,
		sXBILSY_FIRESTART,
		sXBILSY_SCARY3,
		sXBILSY_THUNDER,
		sXBILSY_ZIP,
		sXLIZARD_LIZ1,
		sXLIZARD_LIZ2,
		sXLIZARD_LIZ4,
		sXLIZARD_LIZ6,
		sXTURTLE_BITE3,
		sXTURTLE_HIDE,
		sXTURTLE_HITSHELL,
		sXTURTLE_IDLE1,
		sXTURTLE_IDLE2,
		sXTURTLE_NECK,
		sXTURTLE_SPK1TURT,
		sXTURTLE_SPK2TURT,
		sXTURTLE_SPK3TURT,
		sXTURTLE_SPK4TURT,
		sXTURTLE_TURN,
		sZDOG_AGRESSIV,
		sZDOG_SNIF1,
		sZDOG_WAF1,
		sZDOG_WAF2,
		sZDOG_WAF3
	};

	enum state {
		sSTART,
		sSLEEP,
		sWAKE,
		sKILL,
		sDEACTIVATE,
		sWALK,
		sJUMP,
		sFIRE,
		sFLY,
		sBOUNCE,
		sEXPLODE,
		sROCKETFLY,
		sSTILL,
		sFLOAT,
		sHIT,
		sSPRING,
		sACTION,
		sDONE,
		sPUSH,
		sFALL,
		sFLOATFALL,
		sCIRCLE,
		sATTACK,
		sFREEZE,
		sFADEIN,
		sFADEOUT,
		sHIDE,
		sTURN,
		sIDLE,
		sEXTRA,
		sSTOP,
		sWAIT,
		sLAND,
		sDELAYEDSTART,
		sROTATE,
		sDUCK
	};

	enum tbgMode {
		tbgModeWARPHORIZON,
		tbgModeTUNNEL,
		tbgModeMENU,
		tbgModeTILEMENU,
		tbgModeWAVE,
		tbgModeCYLINDER,
		tbgModeREFLECTION
	};

	
	struct TtextAppearance {
		enum align_ {
			align_DEFAULT,
			align_LEFT,
			align_CENTER,
			align_RIGHT
		};

		enum ch_ {
			ch_HIDE,
			ch_DISPLAY,
			ch_SPECIAL
		};

		int xAmp;
		int yAmp;
		int spacing;
		bool monospace;
		bool skipInitialHash;

		ch_ at;
		ch_ caret;
		ch_ hash;
		ch_ newline;
		ch_ pipe;
		ch_ section;
		ch_ tilde;
		align_ align;

		static TtextAppearance constructor() { noop(); return { }; }
		static TtextAppearance constructorMode(uint32_t mode) { noop(); return { }; }

		TtextAppearance& operator=(uint32_t other) { noop(); return *this; }
	};

	struct TgameCanvas {

		void DrawPixel(int32_t xPixel, int32_t yPixel, uint8_t color, uint32_t mode, uint8_t param) { noop(); }
		void DrawRectangle(int32_t xPixel, int32_t yPixel, int32_t width, int32_t height, uint8_t color, uint32_t mode, uint8_t param) { noop(); }
		void DrawSprite(int32_t xPixel, int32_t yPixel, int32_t setID, uint8_t animation, uint8_t frame, int8_t direction, uint32_t mode, uint8_t param) { noop(); }
		void DrawCurFrameSprite(int32_t xPixel, int32_t yPixel, uint32_t sprite, int8_t direction, uint32_t mode, uint8_t param) { noop(); }
		void DrawResizedSprite(int32_t xPixel, int32_t yPixel, int32_t setID, uint8_t animation, uint8_t frame, float xScale, float yScale, uint32_t mode, uint8_t param) { noop(); }
		void DrawResizedCurFrameSprite(int32_t xPixel, int32_t yPixel, uint32_t sprite, float xScale, float yScale, uint32_t mode, uint8_t param) { noop(); }
		void DrawTransformedSprite(int32_t xPixel, int32_t yPixel, int32_t setID, uint8_t animation, uint8_t frame, int32_t angle, float xScale, float yScale, uint32_t mode, uint8_t param) { noop(); }
		void DrawTransformedCurFrameSprite(int32_t xPixel, int32_t yPixel, uint32_t sprite, int32_t angle, float xScale, float yScale, uint32_t mode, uint8_t param) { noop(); }
		void DrawSwingingVine(int32_t xPixel, int32_t yPixel, uint32_t sprite, int32_t length, int32_t curvature, uint32_t mode, uint8_t param) { noop(); }

		void ExternalDrawTile(int32_t xPixel, int32_t yPixel, uint16_t tile, uint32_t tileQuadrant) { noop(); }
		void DrawTextBasicSize(int32_t xPixel, int32_t yPixel, const String& text, uint32_t size, uint32_t mode, uint8_t param) { noop(); }
		void DrawTextExtSize(int32_t xPixel, int32_t yPixel, const String& text, uint32_t size, const TtextAppearance& appearance, uint8_t param1, uint32_t mode, uint8_t param) { noop(); }

	};

	struct TpaletteEntry {
		uint8_t red;
		uint8_t green;
		uint8_t blue;

		static TpaletteEntry constructor() { noop(); return { }; }
		static TpaletteEntry constructorRGB(uint8_t red, uint8_t green, uint8_t blue) { noop(); return { red, green, blue }; }

		uint8_t getHue() { noop(); return 0; }
		uint8_t getSat() { noop(); return 0; }
		uint8_t getLight() { noop(); return 0; }

		void swizzle(uint32_t redc, uint32_t greenc, uint32_t bluec) {
			noop();

			uint8_t r = red;
			uint8_t g = green;
			uint8_t b = blue;

			switch (redc) {
				case 1: red = g; break;
				case 2: red = b; break;
			}
			switch (greenc) {
				case 0: green = r; break;
				case 2: green = b; break;
			}
			switch (bluec) {
				case 0: blue = r; break;
				case 1: blue = g; break;
			}
		}
		void setHSL(int hue, uint8_t sat, uint8_t light) { noop(); }

		TpaletteEntry& operator=(const TpaletteEntry& other) { noop(); *this = other; return *this; }
		bool operator==(const TpaletteEntry& other) { noop(); return (red == other.red && green == other.green && blue == other.blue); }
	};

	// TODO
	/*class Tpalette {

	};*/

	enum waterInteraction_ {
		waterInteraction_POSITIONBASED,
		waterInteraction_SWIM,
		waterInteraction_LOWGRAVITY
	};

	enum ws {
		wsNORMAL,
		wsMISSILE,
		wsPOPCORN,
		wsCAPPED,
	};

	enum wsp {
		wspNORMAL,
		wspNORMALORDIRECTIONANDAIM,
		wspDIRECTIONANDAIM,
		wspDOUBLEORTRIPLE,
		wspDOUBLE,
		wspTRIPLE,
		wspREFLECTSFASTFIRE,
		wspNORMALORBBGUN,
		wspBBGUN
	};

	constexpr int FLAG_HFLIPPED_TILE = 0x1000;
	constexpr int FLAG_VFLIPPED_TILE = 0x2000;
	constexpr int FLAG_ANIMATED_TILE = 0x4000;

	float get_sinTable(uint32_t angle) { noop(); return 0.0f; };
	float get_cosTable(uint32_t angle) { noop(); return 0.0f; };
	uint32_t RandWord32() { noop(); return 0; }
	uint64_t unixTimeSec() { noop(); return 0; }
	uint64_t unixTimeMs() { noop(); return 0; }

	//int32_t gameTicks = 0;
	int32_t LevelScripts::jjGameTicks()
	{
		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		//return (int32_t)_this->_levelHandler->_elapsedFrames;
		return _this->_onLevelUpdateLastFrame;
	}

	uint32_t gameTicksSpentWhileActive = 0;
	int32_t renderFrame = 0;

	int32_t GetFPS() { noop(); return 0; }

	bool versionTSF = true;

	bool isAdmin() { noop(); return false; }

	bool isServer = false;

	int32_t GetDifficulty() { noop(); return 0; }
	void SetDifficulty(int32_t value) { noop(); }

	int32_t DifficultyForNextLevel = 0;
	int32_t DifficultyAtLevelStart = 0;

	String getLevelFileName() { noop(); return ""; }
	String getCurrLevelName() { noop(); return ""; }
	void setCurrLevelName(const String& in) { noop(); }
	String getCurrMusic() { noop(); return ""; }
	String getCurrTileset() { noop(); return ""; }

	uint32_t numberOfTiles = 0;

	String getHelpString(uint32_t index) { noop(); return ""; }
	void setHelpString(uint32_t index, const String& in) { noop(); }

	int32_t get_gameState() { noop(); return 0; }

	int32_t gameMode = 0;
	int32_t customMode = 0;
	int32_t partyMode = 0;

	int32_t numPlayers = 0;
	int32_t localPlayers = 0;

	// TODO

	void LevelScripts::jjAlert(const String& text, bool sendToAll, uint32_t size)
	{
		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		_this->_levelHandler->ShowLevelText(text);
	}
	void jjPrint(const String& text, bool timestamp) { LOGW_X("%s", text.data()); }
	void jjDebug(const String& text, bool timestamp) { LOGV_X("%s", text.data()); }
	void jjChat(const String& text, bool teamchat) { LOGW_X("%s", text.data()); }
	void jjConsole(const String& text, bool sendToAll) { LOGW_X("%s", text.data()); }
	void jjSpy(const String& text) { LOGV_X("%s", text.data()); }

	// TODO

	bool parLowDetail = false;
	int32_t colorDepth = 0;
	int32_t checkedMaxSubVideoWidth = 0;
	int32_t checkedMaxSubVideoHeight = 0;
	int32_t realVideoW = 0;
	int32_t realVideoH = 0;
	int32_t subVideoW = 0;
	int32_t subVideoH = 0;
	int32_t getBorderWidth() { noop(); return 0; }
	int32_t getBorderHeight() { noop(); return 0; }
	bool getSplitscreenType() { noop(); return false; }
	void setSplitscreenType() { noop(); }

	// TODO

	int32_t maxScore = 0;

	int32_t get_teamScore(int32_t color) { noop(); return 0; }
	int32_t GetMaxHealth() { noop(); return 0; }
	int32_t GetStartHealth() { noop(); return 0; }

	// TODO

	float get_layerXOffset(uint8_t id) { noop(); return 0; }
	void set_layerXOffset(uint8_t id, float value) { noop(); }
	float get_layerYOffset(uint8_t id) { noop(); return 0; }
	void set_layerYOffset(uint8_t id, float value) { noop(); }
	int get_layerWidth(uint8_t id) { noop(); return 0; }
	int get_layerRealWidth(uint8_t id) { noop(); return 0; }
	int get_layerRoundedWidth(uint8_t id) { noop(); return 0; }
	int get_layerHeight(uint8_t id) { noop(); return 0; }
	float get_layerXSpeed(uint8_t id) { noop(); return 0; }
	void set_layerXSpeed(uint8_t id, float value) { noop(); }
	float get_layerYSpeed(uint8_t id) { noop(); return 0; }
	void set_layerYSpeed(uint8_t id, float value) { noop(); }
	float get_layerXAutoSpeed(uint8_t id) { noop(); return 0; }
	void set_layerXAutoSpeed(uint8_t id, float value) { noop(); }
	float get_layerYAutoSpeed(uint8_t id) { noop(); return 0; }
	void set_layerYAutoSpeed(uint8_t id, float value) { noop(); }
	bool get_layerHasTiles(uint8_t id) { noop(); return 0; }
	void set_layerHasTiles(uint8_t id, bool value) { noop(); }
	bool get_layerTileHeight(uint8_t id) { noop(); return 0; }
	void set_layerTileHeight(uint8_t id, bool value) { noop(); }
	bool get_layerTileWidth(uint8_t id) { noop(); return 0; }
	void set_layerTileWidth(uint8_t id, bool value) { noop(); }
	bool get_layerLimitVisibleRegion(uint8_t id) { noop(); return 0; }
	void set_layerLimitVisibleRegion(uint8_t id, bool value) { noop(); }

	void setLayerXSpeedSeamlessly(uint8_t id, float newspeed, bool newSpeedIsAnAutoSpeed) { noop(); }
	void setLayerYSpeedSeamlessly(uint8_t id, float newspeed, bool newSpeedIsAnAutoSpeed) { noop(); }

	// TODO

	bool snowing = false;
	bool snowingOutdoors = false;
	uint8_t snowingIntensity = 0;
	int32_t snowingType = 0;

	bool getTrigger(uint8_t id) { noop(); return false; }
	void setTrigger(uint8_t id, bool value) { noop(); }
	bool switchTrigger(uint8_t id) { noop(); return false; }

	bool isNumberedASFunctionEnabled(uint8_t id) { noop(); return false; }
	void setNumberedASFunctionEnabled(uint8_t id, bool value) { noop(); }
	void reenableAllNumberedASFunctions() { noop(); }

	int32_t waterLightMode = 0;
	int32_t waterInteraction = 0;

	float getWaterLevel() { noop(); return 0; }
	float getWaterLevel2() { noop(); return 0; }
	float setWaterLevel(float value, bool instant) { noop(); return 0; }
	float get_waterChangeSpeed() { noop(); return 0; }
	void set_waterChangeSpeed(float value) { noop(); }
	int32_t get_waterLayer() { noop(); return 0; }
	void set_waterLayer(int32_t value) { noop(); }
	void setWaterGradient(uint8_t red1, uint8_t green1, uint8_t blue1, uint8_t red2, uint8_t green2, uint8_t blue2) { noop(); }
	// TODO: void setWaterGradientFromColors(jjPALCOLOR color1, jjPALCOLOR color2)
	void setWaterGradientToTBG() { noop(); }
	void resetWaterGradient() { noop(); }

	void triggerRock(uint8_t id) { noop(); }

	void cycleTo(const String& filename, bool warp, bool fast) { noop(); }
	void LevelScripts::jjNxt(bool warp, bool fast)
	{
		ExitType exitType = (warp ? ExitType::Warp : ExitType::Normal);
		if (fast) {
			exitType |= ExitType::FastTransition;
		}

		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		_this->_levelHandler->BeginLevelChange(exitType, { });
	}

	bool getEnabledTeam(uint8_t team) { noop(); return false; }

	uint8_t ChatKey = 0;

	bool getKeyDown(uint8_t key) { noop(); return false; }
	int32_t getCursorX() { noop(); return 0; }
	int32_t getCursorY() { noop(); return 0; }

	bool LoadNewMusicFile(const String& filename, bool forceReload, bool temporary) { noop(); return false; }
	void MusicStop() { noop(); }
	void MusicPlay() { noop(); }
	void ModMusicPause() { noop(); }
	void ModMusicResume() { noop(); }

	void playSample(float xPixel, float yPixel, int32_t sample, int32_t volume, int32_t frequency) { noop(); }
	int32_t playLoopedSample(float xPixel, float yPixel, int32_t sample, int32_t volume, int32_t frequency) { noop(); return 0; }
	void playPrioritySample(int32_t sample) { noop(); }
	bool isSampleLoaded(int32_t sample) { noop(); return false; }
	bool loadSample(int32_t sample, const String& filename) { noop(); return false; }

	bool soundEnabled = false;
	bool soundFXActive = false;
	bool musicActive = false;
	int32_t soundFXVolume = false;
	int32_t musicVolume = false;
	int32_t levelEcho = 0;

	bool warpsTransmuteCoins = false;
	bool delayGeneratedCrateOrigins = false;

	bool getUseLayer8Speeds() { noop(); return false; }
	void setUseLayer8Speeds(bool value) { noop(); }

	bool g_levelHasFood = false;

	// TODO

	int32_t GetEvent(uint16_t tx, uint16_t ty) { noop(); return 0; }
	int32_t GetEventParamWrapper(uint16_t tx, uint16_t ty, int32_t offset, int32_t length) { noop(); return 0; }
	void SetEventByte(uint16_t tx, uint16_t ty, uint8_t newEventId) { noop(); }
	void SetEventParam(uint16_t tx, uint16_t ty, int8_t offset, int8_t length, int32_t newValue) { noop(); }
	int8_t GetTileType(uint16_t tile) { noop(); return 0; }
	void SetTileType(uint16_t tile, uint16_t value) { noop(); }

	int32_t enforceAmbientLighting = 0;

	// TODO
}

// Without namespace for shorter log messages
static void asScript(String& msg)
{
	LOGI_X("%s", msg.data());
}

static float asFractionf(float v)
{
	float intPart;
	return modff(v, &intPart);
}

static int asRandom()
{
	return Random().Next();
}

static int asRandom(int max)
{
	return Random().Fast(0, max);
}

static float asRandom(float min, float max)
{
	return Random().FastFloat(min, max);
}

namespace Jazz2::Scripting
{
	void LevelScripts::OnLevelBegin()
	{
		asIScriptFunction* func = _module->GetFunctionByDecl("void OnLevelBegin()");
		if (func == nullptr) {
			return;
		}

		asIScriptContext* ctx = _engine->RequestContext();

		ctx->Prepare(func);
		int r = ctx->Execute();
		if (r == asEXECUTION_EXCEPTION) {
			LOGE_X("An exception \"%s\" occurred in \"%s\". Please correct the code and try again.", ctx->GetExceptionString(), ctx->GetExceptionFunction()->GetDeclaration());
		}

		_engine->ReturnContext(ctx);
	}

	void LevelScripts::OnLevelUpdate(float timeMult)
	{
		switch (_scriptContextType) {
			case ScriptContextType::Legacy: {
				if (_onLevelUpdate == nullptr) {
					_onLevelUpdateLastFrame = (int32_t)_levelHandler->_elapsedFrames;
					return;
				}

				// Legacy context requires fixed frame count per second
				asIScriptContext* ctx = _engine->RequestContext();

				int32_t currentFrame = (int32_t)_levelHandler->_elapsedFrames;
				while (_onLevelUpdateLastFrame <= currentFrame) {
					ctx->Prepare(_onLevelUpdate);
					int r = ctx->Execute();
					if (r == asEXECUTION_EXCEPTION) {
						LOGE_X("An exception \"%s\" occurred in \"%s\". Please correct the code and try again.", ctx->GetExceptionString(), ctx->GetExceptionFunction()->GetDeclaration());
						// Don't call the method again if an exception occurs
						_onLevelUpdate = nullptr;
					}
					_onLevelUpdateLastFrame++;
				}

				_engine->ReturnContext(ctx);
				break;
			}
			case ScriptContextType::Standard: {
				_onLevelUpdateLastFrame = (int32_t)_levelHandler->_elapsedFrames;
				if (_onLevelUpdate == nullptr) {
					return;
				}

				// Standard context supports floating frame rate
				asIScriptContext* ctx = _engine->RequestContext();
				ctx->Prepare(_onLevelUpdate);
				ctx->SetArgFloat(0, timeMult);
				int r = ctx->Execute();
				if (r == asEXECUTION_EXCEPTION) {
					LOGE_X("An exception \"%s\" occurred in \"%s\". Please correct the code and try again.", ctx->GetExceptionString(), ctx->GetExceptionFunction()->GetDeclaration());
					// Don't call the method again if an exception occurs
					_onLevelUpdate = nullptr;
				}

				_engine->ReturnContext(ctx);
				break;
			}
		}
	}

	void LevelScripts::OnLevelCallback(Actors::ActorBase* initiator, uint8_t* eventParams)
	{
		char funcName[64];
		asIScriptFunction* func;

		// If known player is the initiator, try to call specific variant of the function
		if (auto player = dynamic_cast<Actors::Player*>(initiator)) {
			formatString(funcName, sizeof(funcName), "void onFunction%i(Player@, uint8)", eventParams[0]);
			func = _module->GetFunctionByDecl(funcName);
			if (func != nullptr) {
				asIScriptContext* ctx = _engine->RequestContext();

				void* mem = asAllocMem(sizeof(ScriptPlayerWrapper));
				ScriptPlayerWrapper* playerWrapper = new(mem) ScriptPlayerWrapper(this, player);

				ctx->Prepare(func);
				ctx->SetArgObject(0, playerWrapper);
				ctx->SetArgByte(1, eventParams[1]);
				int r = ctx->Execute();
				if (r == asEXECUTION_EXCEPTION) {
					LOGE_X("An exception \"%s\" occurred in \"%s\". Please correct the code and try again.", ctx->GetExceptionString(), ctx->GetExceptionFunction()->GetDeclaration());
				}

				_engine->ReturnContext(ctx);

				playerWrapper->Release();
				return;
			}
		}

		// Try to call parameter-less variant
		formatString(funcName, sizeof(funcName), "void onFunction%i()", eventParams[0]);
		func = _module->GetFunctionByDecl(funcName);
		if (func != nullptr) {
			asIScriptContext* ctx = _engine->RequestContext();

			ctx->Prepare(func);
			int r = ctx->Execute();
			if (r == asEXECUTION_EXCEPTION) {
				LOGE_X("An exception \"%s\" occurred in \"%s\". Please correct the code and try again.", ctx->GetExceptionString(), ctx->GetExceptionFunction()->GetDeclaration());
			}

			_engine->ReturnContext(ctx);
			return;
		}

		LOGW_X("Callback function \"%s\" was not found in the script. Please correct the code and try again.", funcName);
	}

	void LevelScripts::RegisterBuiltInFunctions(asIScriptEngine* engine)
	{
		RegisterArray(engine);
		RegisterRef(engine);
		RegisterString(engine);

		// Math functions
		int r;
		r = engine->RegisterGlobalFunction("float cos(float)", asFUNCTIONPR(cosf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("float sin(float)", asFUNCTIONPR(sinf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("float tan(float)", asFUNCTIONPR(tanf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);

		r = engine->RegisterGlobalFunction("float acos(float)", asFUNCTIONPR(acosf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("float asin(float)", asFUNCTIONPR(asinf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("float atan(float)", asFUNCTIONPR(atanf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("float atan2(float, float)", asFUNCTIONPR(atan2f, (float, float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);

		r = engine->RegisterGlobalFunction("float cosh(float)", asFUNCTIONPR(coshf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("float sinh(float)", asFUNCTIONPR(sinhf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("float tanh(float)", asFUNCTIONPR(tanhf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);

		r = engine->RegisterGlobalFunction("float log(float)", asFUNCTIONPR(logf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("float log10(float)", asFUNCTIONPR(log10f, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);

		r = engine->RegisterGlobalFunction("float pow(float, float)", asFUNCTIONPR(powf, (float, float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("float sqrt(float)", asFUNCTIONPR(sqrtf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);

		r = engine->RegisterGlobalFunction("float ceil(float)", asFUNCTIONPR(ceilf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("float abs(float)", asFUNCTIONPR(fabsf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("float floor(float)", asFUNCTIONPR(floorf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("float fraction(float)", asFUNCTIONPR(asFractionf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);
	}

	void LevelScripts::RegisterLegacyFunctions(asIScriptEngine* engine)
	{
		// All declaration were provided by JJ2+ team
		engine->RegisterGlobalFunction("float jjSin(uint angle)", asFUNCTION(get_sinTable), asCALL_CDECL);
		engine->RegisterGlobalFunction("float jjCos(uint angle)", asFUNCTION(get_cosTable), asCALL_CDECL);
		engine->RegisterGlobalFunction("uint jjRandom()", asFUNCTION(RandWord32), asCALL_CDECL);
		engine->RegisterGlobalFunction("uint64 jjUnixTimeSec()", asFUNCTION(unixTimeSec), asCALL_CDECL);
		engine->RegisterGlobalFunction("uint64 jjUnixTimeMs()", asFUNCTION(unixTimeMs), asCALL_CDECL);

		//engine->RegisterGlobalFunction("bool jjIsValidCheat(const string &in text)", asFUNCTION(stringIsCheat), asCALL_CDECL);
		//engine->RegisterGlobalProperty("const bool jjDebugF10", &F10Debug);

		//engine->RegisterGlobalFunction("bool jjRegexIsValid(const string &in expression)", asFUNCTION(regexIsValid), asCALL_CDECL);
		//engine->RegisterGlobalFunction("bool jjRegexMatch(const string &in text, const string &in expression, bool ignoreCase = false)", asFUNCTION(regexMatch), asCALL_CDECL);
		//engine->RegisterGlobalFunction("bool jjRegexMatch(const string &in text, const string &in expression, array<string> &out results, bool ignoreCase = false)", asFUNCTION(regexMatchWithResults), asCALL_CDECL);
		//engine->RegisterGlobalFunction("bool jjRegexSearch(const string &in text, const string &in expression, bool ignoreCase = false)", asFUNCTION(regexSearch), asCALL_CDECL);
		//engine->RegisterGlobalFunction("bool jjRegexSearch(const string &in text, const string &in expression, array<string> &out results, bool ignoreCase = false)", asFUNCTION(regexSearchWithResults), asCALL_CDECL);
		//engine->RegisterGlobalFunction("string jjRegexReplace(const string &in text, const string &in expression, const string &in replacement, bool ignoreCase= false)", asFUNCTION(regexReplace), asCALL_CDECL);

		// TODO
		//engine->RegisterGlobalProperty("const int jjGameTicks", &gameTicks);
		engine->RegisterGlobalFunction("int get_jjGameTicks() property", asFUNCTION(jjGameTicks), asCALL_CDECL);
		engine->RegisterGlobalProperty("const uint jjActiveGameTicks", &gameTicksSpentWhileActive);
		engine->RegisterGlobalProperty("const int jjRenderFrame", &renderFrame);
		engine->RegisterGlobalFunction("int get_jjFPS() property", asFUNCTION(GetFPS), asCALL_CDECL);
		engine->RegisterGlobalProperty("const bool jjIsTSF", &versionTSF);
		engine->RegisterGlobalFunction("bool get_jjIsAdmin() property", asFUNCTION(isAdmin), asCALL_CDECL);
		engine->RegisterGlobalProperty("const bool jjIsServer", &isServer);
		engine->RegisterGlobalFunction("int get_jjDifficulty() property", asFUNCTION(GetDifficulty), asCALL_CDECL);
		engine->RegisterGlobalFunction("void set_jjDifficulty(int) property", asFUNCTION(SetDifficulty), asCALL_CDECL);
		engine->RegisterGlobalProperty("int jjDifficultyNext", &DifficultyForNextLevel);
		engine->RegisterGlobalProperty("const int jjDifficultyOrig", &DifficultyAtLevelStart);

		engine->RegisterGlobalFunction("string get_jjLevelFileName() property", asFUNCTION(getLevelFileName), asCALL_CDECL);
		engine->RegisterGlobalFunction("string get_jjLevelName() property", asFUNCTION(getCurrLevelName), asCALL_CDECL);
		engine->RegisterGlobalFunction("void set_jjLevelName(const string &in) property", asFUNCTION(setCurrLevelName), asCALL_CDECL);
		engine->RegisterGlobalFunction("string get_jjMusicFileName() property", asFUNCTION(getCurrMusic), asCALL_CDECL);
		engine->RegisterGlobalFunction("string get_jjTilesetFileName() property", asFUNCTION(getCurrTileset), asCALL_CDECL);
		engine->RegisterGlobalProperty("const uint jjTileCount", &numberOfTiles);

		engine->RegisterGlobalFunction("string get_jjHelpStrings(uint) property", asFUNCTION(getHelpString), asCALL_CDECL);
		engine->RegisterGlobalFunction("void set_jjHelpStrings(uint, const string &in) property", asFUNCTION(setHelpString), asCALL_CDECL);

		engine->SetDefaultNamespace("GAME");
		engine->RegisterEnum("State");
		engine->RegisterEnumValue("State", "STOPPED", gameSTOPPED);
		engine->RegisterEnumValue("State", "STARTED", gameSTARTED);
		engine->RegisterEnumValue("State", "PAUSED", gamePAUSED);
		engine->RegisterEnumValue("State", "PREGAME", gamePREGAME);
		engine->RegisterEnumValue("State", "OVERTIME", gameOVERTIME);
		engine->RegisterEnum("Mode");
		engine->RegisterEnumValue("Mode", "SP", GM_SP);
		engine->RegisterEnumValue("Mode", "COOP", GM_COOP);
		engine->RegisterEnumValue("Mode", "BATTLE", GM_BATTLE);
		engine->RegisterEnumValue("Mode", "CTF", GM_CTF);
		engine->RegisterEnumValue("Mode", "TREASURE", GM_TREASURE);
		engine->RegisterEnumValue("Mode", "RACE", GM_RACE);
		engine->RegisterEnum("Custom");
		engine->RegisterEnumValue("Custom", "NOCUSTOM", 0);
		engine->RegisterEnumValue("Custom", "RT", 1);
		engine->RegisterEnumValue("Custom", "LRS", 2);
		engine->RegisterEnumValue("Custom", "XLRS", 3);
		engine->RegisterEnumValue("Custom", "PEST", 4);
		engine->RegisterEnumValue("Custom", "TB", 5);
		engine->RegisterEnumValue("Custom", "JB", 6);
		engine->RegisterEnumValue("Custom", "DCTF", 7);
		engine->RegisterEnumValue("Custom", "FR", 8);
		engine->RegisterEnumValue("Custom", "TLRS", 9);
		engine->RegisterEnumValue("Custom", "DOM", 10);
		engine->RegisterEnumValue("Custom", "HEAD", 11);
		engine->RegisterEnum("Connection");
		engine->RegisterEnumValue("Connection", "LOCAL", gameLOCAL);
		engine->RegisterEnumValue("Connection", "ONLINE", gameINTERNET);
		engine->RegisterEnumValue("Connection", "LAN", gameLAN_TCP);
		engine->SetDefaultNamespace("");
		engine->RegisterGlobalFunction("GAME::State get_jjGameState() property", asFUNCTION(get_gameState), asCALL_CDECL);
		engine->RegisterGlobalProperty("const GAME::Mode jjGameMode", &gameMode);
		engine->RegisterGlobalProperty("const GAME::Custom jjGameCustom", &customMode);
		engine->RegisterGlobalProperty("const GAME::Connection jjGameConnection", &partyMode);

		// TODO
		engine->RegisterObjectType("jjPLAYER", /*sizeof(Tplayer)*/0, asOBJ_REF | asOBJ_NOCOUNT);
		engine->RegisterGlobalProperty("const int jjPlayerCount", &numPlayers);
		engine->RegisterGlobalProperty("const int jjLocalPlayerCount", &localPlayers);
		//setASplayer(0);
		/*engine->RegisterGlobalProperty("jjPLAYER @jjP", &ASplayer);
		engine->RegisterGlobalProperty("jjPLAYER @p", &ASplayer); // Deprecated
		engine->RegisterGlobalFunction("jjPLAYER@ get_jjPlayers(uint8) property", asFUNCTION(getPlayer), asCALL_CDECL);
		engine->RegisterGlobalFunction("jjPLAYER@ get_jjLocalPlayers(uint8) property", asFUNCTION(getLocalPlayer), asCALL_CDECL);*/

		engine->SetDefaultNamespace("WEAPON");
		engine->RegisterEnum("Weapon");
		engine->RegisterEnumValue("Weapon", "BLASTER", 1);
		engine->RegisterEnumValue("Weapon", "BOUNCER", 2);
		engine->RegisterEnumValue("Weapon", "ICE", 3);
		engine->RegisterEnumValue("Weapon", "SEEKER", 4);
		engine->RegisterEnumValue("Weapon", "RF", 5);
		engine->RegisterEnumValue("Weapon", "TOASTER", 6);
		engine->RegisterEnumValue("Weapon", "TNT", 7);
		engine->RegisterEnumValue("Weapon", "GUN8", 8);
		engine->RegisterEnumValue("Weapon", "GUN9", 9);
		engine->RegisterEnumValue("Weapon", "CURRENT", 0);
		engine->RegisterEnum("Style");
		engine->RegisterEnumValue("Style", "NORMAL", wsNORMAL);
		engine->RegisterEnumValue("Style", "MISSILE", wsMISSILE);
		engine->RegisterEnumValue("Style", "POPCORN", wsPOPCORN);
		engine->RegisterEnumValue("Style", "CAPPED", wsCAPPED);
		engine->RegisterEnumValue("Style", "TUNA", wsCAPPED); // Deprecated
		engine->SetDefaultNamespace("SPREAD");
		engine->RegisterEnum("Spread");
		engine->RegisterEnumValue("Spread", "NORMAL", wspNORMAL);
		engine->RegisterEnumValue("Spread", "ICE", wspNORMALORDIRECTIONANDAIM);
		engine->RegisterEnumValue("Spread", "ICEPU", wspDIRECTIONANDAIM);
		engine->RegisterEnumValue("Spread", "RF", wspDOUBLEORTRIPLE);
		engine->RegisterEnumValue("Spread", "RFNORMAL", wspDOUBLE);
		engine->RegisterEnumValue("Spread", "RFPU", wspTRIPLE);
		engine->RegisterEnumValue("Spread", "TOASTER", wspREFLECTSFASTFIRE);
		engine->RegisterEnumValue("Spread", "GUN8", wspNORMALORBBGUN);
		engine->RegisterEnumValue("Spread", "PEPPERSPRAY", wspBBGUN);
		engine->SetDefaultNamespace("GEM");
		engine->RegisterEnum("Color");
		engine->RegisterEnumValue("Color", "RED", 1);
		engine->RegisterEnumValue("Color", "GREEN", 2);
		engine->RegisterEnumValue("Color", "BLUE", 3);
		engine->RegisterEnumValue("Color", "PURPLE", 4);
		engine->SetDefaultNamespace("SHIELD");
		engine->RegisterEnum("Shield");
		engine->RegisterEnumValue("Shield", "NONE", 0);
		engine->RegisterEnumValue("Shield", "FIRE", 1);
		engine->RegisterEnumValue("Shield", "BUBBLE", 2);
		engine->RegisterEnumValue("Shield", "WATER", 2);
		engine->RegisterEnumValue("Shield", "LIGHTNING", 3);
		engine->RegisterEnumValue("Shield", "PLASMA", 3);
		engine->RegisterEnumValue("Shield", "LASER", 4);

		engine->SetDefaultNamespace("");

		// TODO
		/*engine->RegisterObjectProperty("jjPLAYER", "int score", asOFFSET(Tplayer, score));
		engine->RegisterObjectProperty("jjPLAYER", "int scoreDisplayed", asOFFSET(Tplayer, lastScoreDisplay));
		engine->RegisterObjectMethod("jjPLAYER", "int setScore(int score)", asFUNCTION(set_playerScore), asCALL_CDECL_OBJLAST);
		REGISTER_FLOAT_PROPERTY("jjPLAYER", "xPos", Tplayer, xPos);
		REGISTER_FLOAT_PROPERTY("jjPLAYER", "yPos", Tplayer, yPos);
		REGISTER_FLOAT_PROPERTY("jjPLAYER", "xAcc", Tplayer, xAcc);
		REGISTER_FLOAT_PROPERTY("jjPLAYER", "yAcc", Tplayer, yAcc);
		REGISTER_FLOAT_PROPERTY("jjPLAYER", "xOrg", Tplayer, xStart);
		REGISTER_FLOAT_PROPERTY("jjPLAYER", "yOrg", Tplayer, yStart);
		engine->RegisterObjectMethod("jjPLAYER", "float get_xSpeed() const property", asFUNCTION(get_xSpeedP), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "float set_xSpeed(float) property", asFUNCTION(set_xSpeedP), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "float get_ySpeed() const property", asFUNCTION(get_ySpeedP), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "float set_ySpeed(float) property", asFUNCTION(set_ySpeedP), asCALL_CDECL_OBJLAST);
		REGISTER_FLOAT_PROPERTY("jjPLAYER", "jumpStrength", Tplayer, jumpSpeed);
		engine->RegisterObjectProperty("jjPLAYER", "int8 frozen", asOFFSET(Tplayer, freeze));
		engine->RegisterObjectMethod("jjPLAYER", "void freeze(bool frozen = true)", asFUNCTION(freezePlayer), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "int get_currTile() const property", asFUNCTION(get_currTile), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "bool startSugarRush(int time = 1400)", asFUNCTION(giveSugarRush), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "int8 get_health() const property", asFUNCTION(get_health), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "int8 set_health(int8) property", asFUNCTION(set_health), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectProperty("jjPLAYER", "const int warpID", asOFFSET(Tplayer, warpArea));
		engine->RegisterObjectProperty("jjPLAYER", "int fastfire", asOFFSET(Tplayer, fireSpeed));
		engine->RegisterObjectMethod("jjPLAYER", "uint8 get_currWeapon() const property", asFUNCTION(getCurrWeapon), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "uint8 set_currWeapon(uint8) property", asFUNCTION(setCurrWeapon), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectProperty("jjPLAYER", "int lives", asOFFSET(Tplayer, extraLives));
		engine->RegisterObjectProperty("jjPLAYER", "int invincibility", asOFFSET(Tplayer, invincibility));
		engine->RegisterObjectProperty("jjPLAYER", "int blink", asOFFSET(Tplayer, flicker));
		engine->RegisterObjectMethod("jjPLAYER", "int extendInvincibility(int duration)", asFUNCTION(addToInvincibilityDuration), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectProperty("jjPLAYER", "int food", asOFFSET(Tplayer, food));
		engine->RegisterObjectProperty("jjPLAYER", "int coins", asOFFSET(Tplayer, coins));
		engine->RegisterObjectMethod("jjPLAYER", "bool testForCoins(int numberOfCoins)", asFUNCTION(testForCoins), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "int get_gems(GEM::Color) const property", asFUNCTION(get_gems), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "int set_gems(GEM::Color, int) property", asFUNCTION(set_gems), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "bool testForGems(int numberOfGems, GEM::Color type)", asFUNCTION(testForGems), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectProperty("jjPLAYER", "int shieldType", asOFFSET(Tplayer, shieldType));
		engine->RegisterObjectProperty("jjPLAYER", "int shieldTime", asOFFSET(Tplayer, shieldTime));
		engine->RegisterObjectProperty("jjPLAYER", "int ballTime", asOFFSET(Tplayer, rolling));
		engine->RegisterObjectProperty("jjPLAYER", "int boss", asOFFSET(Tplayer, bossNumber));
		engine->RegisterObjectProperty("jjPLAYER", "bool bossActivated", asOFFSET(Tplayer, bossActive));
		engine->RegisterObjectProperty("jjPLAYER", "int8 direction", asOFFSET(Tplayer, direction));
		engine->RegisterObjectProperty("jjPLAYER", "int platform", asOFFSET(Tplayer, platform));
		engine->RegisterObjectProperty("jjPLAYER", "const int flag", asOFFSET(Tplayer, flag));
		engine->RegisterObjectProperty("jjPLAYER", "const int clientID", asOFFSET(Tplayer, clientID));
		engine->RegisterObjectProperty("jjPLAYER", "const int8 playerID", asOFFSET(Tplayer, playerID));
		engine->RegisterObjectProperty("jjPLAYER", "const int localPlayerID", asOFFSET(Tplayer, localPlayerID));
		engine->RegisterObjectProperty("jjPLAYER", "const bool teamRed", asOFFSET(Tplayer, team));
		engine->RegisterObjectProperty("jjPLAYER", "bool running", asOFFSET(Tplayer, run));
		engine->RegisterObjectProperty("jjPLAYER", "bool alreadyDoubleJumped", asOFFSET(Tplayer, specialJump)); // Deprecated
		engine->RegisterObjectProperty("jjPLAYER", "int doubleJumpCount", asOFFSET(Tplayer, specialJump));
		engine->RegisterObjectMethod("jjPLAYER", "int get_stoned() const property", asFUNCTION(get_stoned), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "int set_stoned(int) property", asFUNCTION(set_stoned), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectProperty("jjPLAYER", "int buttstomp", asOFFSET(Tplayer, downAttack));
		engine->RegisterObjectProperty("jjPLAYER", "int helicopter", asOFFSET(Tplayer, helicopter));
		engine->RegisterObjectProperty("jjPLAYER", "int helicopterElapsed", asOFFSET(Tplayer, helicopterTotal));
		engine->RegisterObjectProperty("jjPLAYER", "int specialMove", asOFFSET(Tplayer, specialMove));
		engine->RegisterObjectProperty("jjPLAYER", "int idle", asOFFSET(Tplayer, idleTime));
		engine->RegisterObjectMethod("jjPLAYER", "void suckerTube(int xSpeed, int ySpeed, bool center, bool noclip = false, bool trigSample = false)", asMETHOD(Tplayer, suckerTube), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "void poleSpin(float xSpeed, float ySpeed, uint delay = 70)", asMETHOD(Tplayer, externalPole), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "void spring(float xSpeed, float ySpeed, bool keepZeroSpeeds, bool sample)", asMETHOD(Tplayer, externalSpring), asCALL_THISCALL);

		engine->RegisterObjectProperty("jjPLAYER", "const bool isLocal", asOFFSET(Tplayer, controls));
		engine->RegisterObjectProperty("jjPLAYER", "const bool isActive", asOFFSET(Tplayer, state));
		engine->RegisterObjectMethod("jjPLAYER", "bool get_isConnecting() const property", asFUNCTION(get_isConnecting), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "bool get_isIdle() const property", asFUNCTION(get_isIdle), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "bool get_isOut() const property", asFUNCTION(get_isOut), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "bool get_isSpectating() const property", asFUNCTION(get_isSpectating), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "bool get_isInGame() const property", asFUNCTION(get_isInGame), asCALL_CDECL_OBJFIRST);

		engine->RegisterObjectMethod("jjPLAYER", "string get_name() const property", asFUNCTION(getName), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "string get_nameUnformatted() const property", asFUNCTION(getNameBleached), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "bool setName(const string &in name)", asFUNCTION(setName), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "int8 get_light() const property", asFUNCTION(getLight), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "int8 set_light(int8) property", asFUNCTION(setLight), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "uint32 get_fur() const property", asFUNCTION(getFur32), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "uint32 set_fur(uint32) property", asFUNCTION(setFur32), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "void furGet(uint8 &out a, uint8 &out b, uint8 &out c, uint8 &out d) const", asFUNCTION(getFur), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "void furSet(uint8 a, uint8 b, uint8 c, uint8 d)", asFUNCTION(setFur), asCALL_CDECL_OBJFIRST);

		engine->RegisterObjectMethod("jjPLAYER", "bool get_noFire() const property", asFUNCTION(get_Nofire), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "bool set_noFire(bool) property", asFUNCTION(set_Nofire), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "bool get_antiGrav() const property", asFUNCTION(get_Antigrav), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "bool set_antiGrav(bool) property", asFUNCTION(set_Antigrav), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "bool get_invisibility() const property", asFUNCTION(get_Invisibility), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "bool set_invisibility(bool) property", asFUNCTION(set_Invisibility), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "bool get_noclipMode() const property", asFUNCTION(get_Noclip), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "bool set_noclipMode(bool) property", asFUNCTION(set_Noclip), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "uint8 get_lighting() const property", asFUNCTION(get_lighting), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "uint8 set_lighting(uint8) property", asFUNCTION(set_lighting), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "uint8 resetLight()", asFUNCTION(reset_lighting), asCALL_CDECL_OBJLAST);

		engine->RegisterObjectMethod("jjPLAYER", "bool get_keyLeft() const property", asFUNCTION(get_playerKeyLeftPressed), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "bool get_keyRight() const property", asFUNCTION(get_playerKeyRightPressed), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "bool get_keyUp() const property", asFUNCTION(get_playerKeyUpPressed), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "bool get_keyDown() const property", asFUNCTION(get_playerKeyDownPressed), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "bool get_keyFire() const property", asFUNCTION(get_playerKeyFirePressed), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "bool get_keySelect() const property", asFUNCTION(get_playerKeySelectPressed), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "bool get_keyJump() const property", asFUNCTION(get_playerKeyJumpPressed), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "bool get_keyRun() const property", asFUNCTION(get_playerKeyRunPressed), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "bool set_keyLeft(bool) property", asFUNCTION(set_playerKeyLeftPressed), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "bool set_keyRight(bool) property", asFUNCTION(set_playerKeyRightPressed), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "bool set_keyUp(bool) property", asFUNCTION(set_playerKeyUpPressed), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "bool set_keyDown(bool) property", asFUNCTION(set_playerKeyDownPressed), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "bool set_keyFire(bool) property", asFUNCTION(set_playerKeyFirePressed), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "bool set_keySelect(bool) property", asFUNCTION(set_playerKeySelectPressed), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "bool set_keyJump(bool) property", asFUNCTION(set_playerKeyJumpPressed), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "bool set_keyRun(bool) property", asFUNCTION(set_playerKeyRunPressed), asCALL_CDECL_OBJFIRST);

		engine->RegisterObjectMethod("jjPLAYER", "bool get_powerup(uint8) const property", asFUNCTION(getPowerup), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "bool set_powerup(uint8, bool) property", asFUNCTION(setPowerup), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "int get_ammo(uint8) const property", asFUNCTION(getAmmo), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "int set_ammo(uint8, int) property", asFUNCTION(setAmmo), asCALL_CDECL_OBJLAST);

		engine->RegisterObjectMethod("jjPLAYER", "bool offsetPosition(int xPixels, int yPixels)", asFUNCTION(offsetPosition), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "bool warpToTile(int xTile, int yTile, bool fast = false)", asFUNCTION(warpToTile), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "bool warpToID(uint8 warpID, bool fast = false)", asFUNCTION(warpToID), asCALL_CDECL_OBJFIRST);*/

		engine->SetDefaultNamespace("CHAR");
		engine->RegisterEnum("Char");
		engine->RegisterEnumValue("Char", "JAZZ", mJAZZ);
		engine->RegisterEnumValue("Char", "SPAZ", mSPAZ);
		engine->RegisterEnumValue("Char", "LORI", mLORI);

		engine->RegisterEnumValue("Char", "BIRD", mBIRD);
		engine->RegisterEnumValue("Char", "BIRD2", mCHUCK);
		engine->RegisterEnumValue("Char", "FROG", mFROG);
		engine->SetDefaultNamespace("");

		// TODO
		/*engine->RegisterObjectMethod("jjPLAYER", "CHAR::Char morph(bool rabbitsOnly = false, bool morphEffect = true)", asFUNCTION(morph), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "CHAR::Char morphTo(CHAR::Char charNew, bool morphEffect = true)", asFUNCTION(morphTo), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "CHAR::Char revertMorph(bool morphEffect = true)", asFUNCTION(revertMorph), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "CHAR::Char get_charCurr() const property", asFUNCTION(getCharCurr), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectProperty("jjPLAYER", "CHAR::Char charOrig", asOFFSET(Tplayer, charOrig));*/

		engine->SetDefaultNamespace("TEAM");
		engine->RegisterEnum("Color");
		engine->RegisterEnumValue("Color", "NEUTRAL", -1);
		engine->RegisterEnumValue("Color", "BLUE", 0);
		engine->RegisterEnumValue("Color", "RED", 1);
		engine->RegisterEnumValue("Color", "GREEN", 2);
		engine->RegisterEnumValue("Color", "YELLOW", 3);
		engine->SetDefaultNamespace("");

		engine->SetDefaultNamespace("CHAT");
		engine->RegisterEnum("Type");
		engine->RegisterEnumValue("Type", "NORMAL", 0);
		engine->RegisterEnumValue("Type", "TEAMCHAT", 1);
		engine->RegisterEnumValue("Type", "WHISPER", 2);
		engine->RegisterEnumValue("Type", "ME", 3);
		engine->SetDefaultNamespace("");

		// TODO
		/*engine->RegisterObjectProperty("jjPLAYER", "const TEAM::Color team", asOFFSET(Tplayer, team));

		engine->RegisterObjectMethod("jjPLAYER", "void kill()", asFUNCTION(killLocalPlayer), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "bool hurt(int8 damage = 1, bool forceHurt = false, jjPLAYER@ attacker = null)", asFUNCTION(HitPlayerVariableDamage), asCALL_CDECL_OBJFIRST);

		engine->RegisterGlobalFunction("array<jjPLAYER@>@ jjPlayersWithClientID(int clientID)", asFUNCTION(getPlayersWithClientID), asCALL_CDECL);*/

		engine->SetDefaultNamespace("TIMER");
		engine->RegisterEnum("State");
		engine->RegisterEnumValue("State", "STOPPED", 0);
		engine->RegisterEnumValue("State", "STARTED", 1);
		engine->RegisterEnumValue("State", "PAUSED", 2);
		engine->SetDefaultNamespace("STRING");
		engine->RegisterEnum("Mode");
		engine->RegisterEnumValue("Mode", "NORMAL", 0);
		engine->RegisterEnumValue("Mode", "DARK", 1);
		engine->RegisterEnumValue("Mode", "RIGHTALIGN", 2);
		engine->RegisterEnumValue("Mode", "BOUNCE", 3);
		engine->RegisterEnumValue("Mode", "SPIN", 4);
		engine->RegisterEnumValue("Mode", "PALSHIFT", 5);
		engine->RegisterEnum("Size");
		engine->RegisterEnumValue("Size", "SMALL", 1);
		engine->RegisterEnumValue("Size", "MEDIUM", 0);
		engine->RegisterEnumValue("Size", "LARGE", 2);
		engine->SetDefaultNamespace("");

		engine->RegisterGlobalFunction("void jjAlert(const ::string &in text, bool sendToAll = false, STRING::Size size = STRING::SMALL)", asFUNCTION(jjAlert), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjPrint(const ::string &in text, bool timestamp = false)", asFUNCTION(jjPrint), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjDebug(const ::string &in text, bool timestamp = false)", asFUNCTION(jjDebug), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjChat(const ::string &in text, bool teamchat = false)", asFUNCTION(jjChat), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjConsole(const ::string &in text, bool sendToAll = false)", asFUNCTION(jjConsole), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjSpy(const ::string &in text)", asFUNCTION(jjSpy), asCALL_CDECL);
		
		// TODO
		/*engine->RegisterObjectMethod("jjPLAYER", "TIMER::State get_timerState() const property", asFUNCTION(get_playerTimerState), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "bool get_timerPersists() const property", asFUNCTION(get_playerTimerEffectPersists), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "bool set_timerPersists(bool) property", asFUNCTION(set_playerTimerEffectPersists), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "TIMER::State timerStart(int ticks, bool startPaused = false)", asFUNCTION(startPlayerTimer), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "TIMER::State timerPause()", asFUNCTION(pausePlayerTimer), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "TIMER::State timerResume()", asFUNCTION(resumePlayerTimer), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "TIMER::State timerStop()", asFUNCTION(stopPlayerTimer), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "int get_timerTime() const property", asFUNCTION(get_playerTimerTime), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "int set_timerTime(int) property", asFUNCTION(set_playerTimerTime), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "void timerFunction(const string functionName)", asFUNCTION(setPlayerTimerFunction), asCALL_CDECL_OBJLAST);
		engine->RegisterFuncdef("void jjVOIDFUNC()");
		engine->RegisterObjectMethod("jjPLAYER", "void timerFunction(jjVOIDFUNC@ function)", asFUNCTION(setPlayerTimerFunctionPointer), asCALL_CDECL_OBJLAST);
		engine->RegisterFuncdef("void jjVOIDFUNCPLAYER(jjPLAYER@)");
		engine->RegisterObjectMethod("jjPLAYER", "void timerFunction(jjVOIDFUNCPLAYER@ function)", asFUNCTION(setPlayerTimerFunctionPointer), asCALL_CDECL_OBJLAST);

		engine->RegisterObjectMethod("jjPLAYER", "bool activateBoss(bool activate = true)", asFUNCTION(activateBoss), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "bool limitXScroll(uint16 left, uint16 width)", asFUNCTION(limitXScroll), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "void cameraFreeze(float xPixel, float yPixel, bool centered, bool instant)", asFUNCTION(cameraFreezeFF), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "void cameraFreeze(bool xUnfreeze,  float yPixel, bool centered, bool instant)", asFUNCTION(cameraFreezeBF), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "void cameraFreeze(float xPixel, bool yUnfreeze,  bool centered, bool instant)", asFUNCTION(cameraFreezeFB), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "void cameraFreeze(bool xUnfreeze,  bool yUnfreeze,  bool centered, bool instant)", asFUNCTION(cameraFreezeBB), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "void cameraUnfreeze(bool instant = true)", asFUNCTION(cameraUnfreeze), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "void showText(string &in text, STRING::Size size = STRING::SMALL)", asFUNCTION(showTextStr), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "void showText(uint textID, uint offset, STRING::Size size = STRING::SMALL)", asFUNCTION(showTextHstr), asCALL_CDECL_OBJFIRST);

		engine->SetDefaultNamespace("FLIGHT");
		engine->RegisterEnum("Mode");
		engine->RegisterEnumValue("Mode", "NONE", 0);
		engine->RegisterEnumValue("Mode", "FLYCARROT", 1);
		engine->RegisterEnumValue("Mode", "AIRBOARD", -1);
		engine->SetDefaultNamespace("");
		engine->RegisterObjectMethod("jjPLAYER", "FLIGHT::Mode get_fly() const property", asFUNCTION(get_playerFly), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "FLIGHT::Mode set_fly(FLIGHT::Mode) property", asFUNCTION(set_playerFly), asCALL_CDECL_OBJFIRST);*/

		engine->SetDefaultNamespace("DIRECTION");
		engine->RegisterEnum("Dir");
		engine->RegisterEnumValue("Dir", "RIGHT", dirRIGHT);
		engine->RegisterEnumValue("Dir", "LEFT", dirLEFT);
		engine->RegisterEnumValue("Dir", "UP", dirUP);
		engine->RegisterEnumValue("Dir", "CURRENT", dirCURRENT);
		engine->SetDefaultNamespace("");

		// TODO
		/*engine->RegisterObjectMethod("jjPLAYER", "int fireBullet(uint8 gun = 0, bool depleteAmmo = true, bool requireAmmo = true, DIRECTION::Dir direction = DIRECTION::CURRENT)", asFUNCTION(fireBulletDirection), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "int fireBullet(uint8 gun, bool depleteAmmo, bool requireAmmo, float angle)", asFUNCTION(fireBulletAngle), asCALL_CDECL_OBJFIRST);

		engine->RegisterObjectProperty("jjPLAYER", "const int subscreenX", asOFFSET(Tplayer, xOrgWin));
		engine->RegisterObjectProperty("jjPLAYER", "const int subscreenY", asOFFSET(Tplayer, yOrgWin));
		engine->RegisterObjectMethod("jjPLAYER", "float get_cameraX() const property", AS_OBJ_FLOAT_GETTER(Tplayer, viewStartX), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "float get_cameraY() const property", AS_OBJ_FLOAT_GETTER(Tplayer, viewStartY), asCALL_CDECL_OBJLAST);

		engine->RegisterObjectMethod("jjPLAYER", "int get_deaths() const property", asFUNCTION(get_deaths), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "bool get_isJailed() const property", asFUNCTION(get_isJailed), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "bool get_isZombie() const property", asFUNCTION(get_isZombie), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "int get_lrsLives() const property", asFUNCTION(get_lrsLives), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "int get_roasts() const property", asFUNCTION(get_roasts), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "int get_laps() const property", asFUNCTION(get_laps), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "int get_lapTimeCurrent() const property", asFUNCTION(get_lapTime), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "int get_lapTimes(uint) const property", asFUNCTION(get_lapTimes), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "int get_lapTimeBest() const property", asFUNCTION(get_lapTimeBest), asCALL_CDECL_OBJLAST);

		engine->RegisterObjectMethod("jjPLAYER", "bool get_isAdmin() const property", asFUNCTION(get_isAdmin), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjPLAYER", "bool hasPrivilege(const string &in privilege, uint moduleID = ::jjScriptModuleID) const", asFUNCTION(playerHasPrivilege), asCALL_CDECL_OBJLAST);*/

		engine->RegisterGlobalProperty("const bool jjLowDetail", &parLowDetail);
		engine->RegisterGlobalProperty("const int jjColorDepth", &colorDepth);
		engine->RegisterGlobalProperty("const int jjResolutionMaxWidth", &checkedMaxSubVideoWidth);
		engine->RegisterGlobalProperty("const int jjResolutionMaxHeight", &checkedMaxSubVideoHeight);
		engine->RegisterGlobalProperty("const int jjResolutionWidth", &realVideoW);
		engine->RegisterGlobalProperty("const int jjResolutionHeight", &realVideoH);
		engine->RegisterGlobalProperty("const int jjSubscreenWidth", &subVideoW);
		engine->RegisterGlobalProperty("const int jjSubscreenHeight", &subVideoH);
		engine->RegisterGlobalFunction("int get_jjBorderWidth() property", asFUNCTION(getBorderWidth), asCALL_CDECL);
		engine->RegisterGlobalFunction("int get_jjBorderHeight() property", asFUNCTION(getBorderHeight), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool get_jjVerticalSplitscreen() property", asFUNCTION(getSplitscreenType), asCALL_CDECL);
		engine->RegisterGlobalFunction("void set_jjVerticalSplitscreen(bool) property", asFUNCTION(setSplitscreenType), asCALL_CDECL);

		// TODO
		/*engine->RegisterGlobalProperty("const bool jjAllowsFireball", &checkedFireball);
		engine->RegisterGlobalProperty("const bool jjAllowsMouseAim", &checkedAllowMouseAim);
		engine->RegisterGlobalProperty("const bool jjAllowsReady", &checkedAllowReady);
		engine->RegisterGlobalProperty("const bool jjAllowsWalljump", &checkedAllowWalljump);
		engine->RegisterGlobalProperty("const bool jjAlwaysRunning", &alwaysRunning);
		engine->RegisterGlobalProperty("const bool jjAutoWeaponChange", &weaponChange);
		engine->RegisterGlobalProperty("const bool jjFriendlyFire", &checkedFriendlyFire);
		engine->RegisterGlobalProperty("const bool jjMouseAim", &mouseAim);
		engine->RegisterGlobalProperty("const bool jjNoBlink", &checkedNoBlink);
		engine->RegisterGlobalProperty("const bool jjNoMovement", &checkedNoMovement);
		engine->RegisterGlobalProperty("const bool jjQuirks", &testQuirksMode);
		engine->RegisterGlobalProperty("const bool jjShowMaxHealth", &showEmptyHearts);
		engine->RegisterGlobalProperty("const bool jjStrongPowerups", &checkedStrongPowerups);*/

		engine->RegisterGlobalProperty("const int jjMaxScore", &maxScore);
		engine->RegisterGlobalFunction("int get_jjTeamScore(TEAM::Color) property", asFUNCTION(get_teamScore), asCALL_CDECL);
		engine->RegisterGlobalFunction("int get_jjMaxHealth() property", asFUNCTION(GetMaxHealth), asCALL_CDECL);
		engine->RegisterGlobalFunction("int get_jjStartHealth() property", asFUNCTION(GetStartHealth), asCALL_CDECL);

		/*engine->RegisterGlobalProperty("const bool jjDoZombiesAlreadyExist", &doZombiesAlreadyExist);
		engine->RegisterGlobalFunction("jjPLAYER@ get_jjBottomFeeder() property", asFUNCTION(get_bottomFeeder), asCALL_CDECL);
		engine->RegisterGlobalFunction("jjPLAYER@ get_jjTokenOwner() property", asFUNCTION(get_tokenOwner), asCALL_CDECL);*/

		engine->RegisterGlobalFunction("float get_jjLayerXOffset(uint8) property", asFUNCTION(get_layerXOffset), asCALL_CDECL);
		engine->RegisterGlobalFunction("void set_jjLayerXOffset(uint8, float) property", asFUNCTION(set_layerXOffset), asCALL_CDECL);
		engine->RegisterGlobalFunction("float get_jjLayerYOffset(uint8) property", asFUNCTION(get_layerYOffset), asCALL_CDECL);
		engine->RegisterGlobalFunction("void set_jjLayerYOffset(uint8, float) property", asFUNCTION(set_layerYOffset), asCALL_CDECL);
		engine->RegisterGlobalFunction("int get_jjLayerWidth(uint8) property", asFUNCTION(get_layerWidth), asCALL_CDECL);
		engine->RegisterGlobalFunction("int get_jjLayerWidthReal(uint8) property", asFUNCTION(get_layerRealWidth), asCALL_CDECL);
		engine->RegisterGlobalFunction("int get_jjLayerWidthRounded(uint8) property", asFUNCTION(get_layerRoundedWidth), asCALL_CDECL);
		engine->RegisterGlobalFunction("int get_jjLayerHeight(uint8) property", asFUNCTION(get_layerHeight), asCALL_CDECL);
		engine->RegisterGlobalFunction("float get_jjLayerXSpeed(uint8) property", asFUNCTION(get_layerXSpeed), asCALL_CDECL);
		engine->RegisterGlobalFunction("void set_jjLayerXSpeed(uint8, float) property", asFUNCTION(set_layerXSpeed), asCALL_CDECL);
		engine->RegisterGlobalFunction("float get_jjLayerYSpeed(uint8) property", asFUNCTION(get_layerYSpeed), asCALL_CDECL);
		engine->RegisterGlobalFunction("void set_jjLayerYSpeed(uint8, float) property", asFUNCTION(set_layerYSpeed), asCALL_CDECL);
		engine->RegisterGlobalFunction("float get_jjLayerXAutoSpeed(uint8) property", asFUNCTION(get_layerXAutoSpeed), asCALL_CDECL);
		engine->RegisterGlobalFunction("void set_jjLayerXAutoSpeed(uint8, float) property", asFUNCTION(set_layerXAutoSpeed), asCALL_CDECL);
		engine->RegisterGlobalFunction("float get_jjLayerYAutoSpeed(uint8) property", asFUNCTION(get_layerYAutoSpeed), asCALL_CDECL);
		engine->RegisterGlobalFunction("void set_jjLayerYAutoSpeed(uint8, float) property", asFUNCTION(set_layerYAutoSpeed), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool get_jjLayerHasTiles(uint8) property", asFUNCTION(get_layerHasTiles), asCALL_CDECL);
		engine->RegisterGlobalFunction("void set_jjLayerHasTiles(uint8, bool) property", asFUNCTION(set_layerHasTiles), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool get_jjLayerTileHeight(uint8) property", asFUNCTION(get_layerTileHeight), asCALL_CDECL);
		engine->RegisterGlobalFunction("void set_jjLayerTileHeight(uint8, bool) property", asFUNCTION(set_layerTileHeight), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool get_jjLayerTileWidth(uint8) property", asFUNCTION(get_layerTileWidth), asCALL_CDECL);
		engine->RegisterGlobalFunction("void set_jjLayerTileWidth(uint8, bool) property", asFUNCTION(set_layerTileWidth), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool get_jjLayerLimitVisibleRegion(uint8) property", asFUNCTION(get_layerLimitVisibleRegion), asCALL_CDECL);
		engine->RegisterGlobalFunction("void set_jjLayerLimitVisibleRegion(uint8, bool) property", asFUNCTION(set_layerLimitVisibleRegion), asCALL_CDECL);

		engine->RegisterGlobalFunction("void jjSetLayerXSpeed(uint8 layerID, float newspeed, bool newSpeedIsAnAutoSpeed)", asFUNCTION(setLayerXSpeedSeamlessly), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjSetLayerYSpeed(uint8 layerID, float newspeed, bool newSpeedIsAnAutoSpeed)", asFUNCTION(setLayerYSpeedSeamlessly), asCALL_CDECL);

		engine->RegisterObjectType("jjPALCOLOR", sizeof(TpaletteEntry), asOBJ_VALUE | asOBJ_POD | asGetTypeTraits<TpaletteEntry>());
		engine->RegisterObjectBehaviour("jjPALCOLOR", asBEHAVE_CONSTRUCT, "void f()", asFUNCTION(TpaletteEntry::constructor), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectBehaviour("jjPALCOLOR", asBEHAVE_CONSTRUCT, "void f(uint8 red, uint8 green, uint8 blue)", asFUNCTION(TpaletteEntry::constructorRGB), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectProperty("jjPALCOLOR", "uint8 red", asOFFSET(TpaletteEntry, red));
		engine->RegisterObjectProperty("jjPALCOLOR", "uint8 green", asOFFSET(TpaletteEntry, green));
		engine->RegisterObjectProperty("jjPALCOLOR", "uint8 blue", asOFFSET(TpaletteEntry, blue));
		engine->RegisterObjectMethod("jjPALCOLOR", "jjPALCOLOR& opAssign(const jjPALCOLOR &in)", asMETHODPR(TpaletteEntry, operator=, (const TpaletteEntry&), TpaletteEntry&), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPALCOLOR", "bool opEquals(const jjPALCOLOR &in) const", asMETHOD(TpaletteEntry, operator==), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPALCOLOR", "uint8 getHue() const", asMETHOD(TpaletteEntry, getHue), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPALCOLOR", "uint8 getSat() const", asMETHOD(TpaletteEntry, getSat), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPALCOLOR", "uint8 getLight() const", asMETHOD(TpaletteEntry, getLight), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPALCOLOR", "void setHSL(int hue, uint8 sat, uint8 light)", asMETHOD(TpaletteEntry, setHSL), asCALL_THISCALL);
		engine->SetDefaultNamespace("COLOR");
		engine->RegisterEnum("Component");
		engine->RegisterEnumValue("Component", "RED", 0);
		engine->RegisterEnumValue("Component", "GREEN", 1);
		engine->RegisterEnumValue("Component", "BLUE", 2);
		engine->SetDefaultNamespace("");
		engine->RegisterObjectMethod("jjPALCOLOR", "void swizzle(COLOR::Component red, COLOR::Component green, COLOR::Component blue)", asMETHOD(TpaletteEntry, swizzle), asCALL_THISCALL);
		// TODO
		/*engine->RegisterObjectType("jjPAL", sizeof(Tpalette), asOBJ_REF);
		engine->RegisterObjectBehaviour("jjPAL", asBEHAVE_FACTORY, "jjPAL@ f()", asFUNCTION(Tpalette::factory), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjPAL", asBEHAVE_ADDREF, "void f()", asMETHOD(Tpalette, addRef), asCALL_THISCALL);
		engine->RegisterObjectBehaviour("jjPAL", asBEHAVE_RELEASE, "void f()", asMETHOD(Tpalette, release), asCALL_THISCALL);
		engine->RegisterGlobalProperty("jjPAL jjPalette", &GeneralGlobals->palette);
		engine->RegisterGlobalProperty("const jjPAL jjBackupPalette", &GeneralGlobals->tilesetPalette);
		engine->RegisterObjectMethod("jjPAL", "jjPAL& opAssign(const jjPAL &in)", asMETHOD(Tpalette, operator=), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPAL", "bool opEquals(const jjPAL &in) const", asMETHOD(Tpalette, operator==), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPAL", "jjPALCOLOR& get_color(uint8)", asMETHOD(Tpalette, getColor), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPAL", "const jjPALCOLOR& get_color(uint8) const", asMETHOD(Tpalette, getConstColor), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPAL", "jjPALCOLOR& set_color(uint8, const jjPALCOLOR &in)", asMETHOD(Tpalette, setColorEntry), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPAL", "void reset()", asMETHOD(Tpalette, reset), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPAL", "void apply() const", asMETHOD(Tpalette, apply), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPAL", "bool load(string &in filename)", asMETHOD(Tpalette, load), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPAL", "void fill(uint8 red, uint8 green, uint8 blue, uint8 start = 1, uint8 length = 254, float opacity = 1.0)", asMETHOD(Tpalette, tintfill), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPAL", "void fill(uint8 red, uint8 green, uint8 blue, float opacity)", asMETHOD(Tpalette, fill), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPAL", "void fill(jjPALCOLOR color, uint8 start = 1, uint8 length = 254, float opacity = 1.0)", asMETHOD(Tpalette, tintfillFromColor), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPAL", "void fill(jjPALCOLOR color, float opacity)", asMETHOD(Tpalette, fillFromColor), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPAL", "void gradient(uint8 red1, uint8 green1, uint8 blue1, uint8 red2, uint8 green2, uint8 blue2, uint8 start = 176, uint8 length = 32, float opacity = 1.0, bool inclusive = false)", asMETHOD(Tpalette, gradient), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPAL", "void gradient(jjPALCOLOR color1, jjPALCOLOR color2, uint8 start = 176, uint8 length = 32, float opacity = 1.0, bool inclusive = false)", asMETHOD(Tpalette, gradientFromColor), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPAL", "void copyFrom(uint8 start, uint8 length, uint8 start2, const jjPAL &in source, float opacity = 1.0)", asMETHOD(Tpalette, copyFrom), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPAL", "uint8 findNearestColor(jjPALCOLOR color) const", asMETHOD(Tpalette, findNearest), asCALL_THISCALL);*/

		engine->SetDefaultNamespace("SPRITE");
		engine->RegisterEnum("Mode");
		engine->RegisterEnumValue("Mode", "NORMAL", spriteType_NORMAL);
		engine->RegisterEnumValue("Mode", "TRANSLUCENT", spriteType_TRANSLUCENT);
		engine->RegisterEnumValue("Mode", "TINTED", spriteType_TINTED);
		engine->RegisterEnumValue("Mode", "GEM", spriteType_GEM);
		engine->RegisterEnumValue("Mode", "INVISIBLE", spriteType_INVISIBLE);
		engine->RegisterEnumValue("Mode", "SINGLECOLOR", spriteType_SINGLECOLOR);
		engine->RegisterEnumValue("Mode", "RESIZED", spriteType_RESIZED);
		engine->RegisterEnumValue("Mode", "NEONGLOW", spriteType_NEONGLOW);
		engine->RegisterEnumValue("Mode", "FROZEN", spriteType_FROZEN);
		engine->RegisterEnumValue("Mode", "PLAYER", spriteType_PLAYER);
		engine->RegisterEnumValue("Mode", "PALSHIFT", spriteType_PALSHIFT);
		engine->RegisterEnumValue("Mode", "SHADOW", spriteType_SHADOW);
		engine->RegisterEnumValue("Mode", "SINGLEHUE", spriteType_SINGLEHUE);
		engine->RegisterEnumValue("Mode", "BRIGHTNESS", spriteType_BRIGHTNESS);
		engine->RegisterEnumValue("Mode", "TRANSLUCENTCOLOR", spriteType_TRANSLUCENTCOLOR);
		engine->RegisterEnumValue("Mode", "TRANSLUCENTPLAYER", spriteType_TRANSLUCENTPLAYER);
		engine->RegisterEnumValue("Mode", "TRANSLUCENTPALSHIFT", spriteType_TRANSLUCENTPALSHIFT);
		engine->RegisterEnumValue("Mode", "TRANSLUCENTSINGLEHUE", spriteType_TRANSLUCENTSINGLEHUE);
		engine->RegisterEnumValue("Mode", "ALPHAMAP", spriteType_ALPHAMAP);
		engine->RegisterEnumValue("Mode", "MENUPLAYER", spriteType_MENUPLAYER);
		engine->RegisterEnumValue("Mode", "BLEND_NORMAL", spriteType_BLENDNORMAL);
		engine->RegisterEnumValue("Mode", "BLEND_DARKEN", spriteType_BLENDDARKEN);
		engine->RegisterEnumValue("Mode", "BLEND_LIGHTEN", spriteType_BLENDLIGHTEN);
		engine->RegisterEnumValue("Mode", "BLEND_HUE", spriteType_BLENDHUE);
		engine->RegisterEnumValue("Mode", "BLEND_SATURATION", spriteType_BLENDSATURATION);
		engine->RegisterEnumValue("Mode", "BLEND_COLOR", spriteType_BLENDCOLOR);
		engine->RegisterEnumValue("Mode", "BLEND_LUMINANCE", spriteType_BLENDLUMINANCE);
		engine->RegisterEnumValue("Mode", "BLEND_MULTIPLY", spriteType_BLENDMULTIPLY);
		engine->RegisterEnumValue("Mode", "BLEND_SCREEN", spriteType_BLENDSCREEN);
		engine->RegisterEnumValue("Mode", "BLEND_DISSOLVE", spriteType_BLENDDISSOLVE);
		engine->RegisterEnumValue("Mode", "BLEND_OVERLAY", spriteType_BLENDOVERLAY);
		engine->RegisterEnumValue("Mode", "BLEND_HARDLIGHT", spriteType_BLENDHARDLIGHT);
		engine->RegisterEnumValue("Mode", "BLEND_SOFTLIGHT", spriteType_BLENDSOFTLIGHT);
		engine->RegisterEnumValue("Mode", "BLEND_DIFFERENCE", spriteType_BLENDDIFFERENCE);
		engine->RegisterEnumValue("Mode", "BLEND_DODGE", spriteType_BLENDDODGE);
		engine->RegisterEnumValue("Mode", "BLEND_BURN", spriteType_BLENDBURN);
		engine->RegisterEnumValue("Mode", "BLEND_EXCLUSION", spriteType_BLENDEXCLUSION);
		engine->RegisterEnumValue("Mode", "TRANSLUCENTTILE", spriteType_TRANSLUCENTTILE);
		engine->RegisterEnumValue("Mode", "CHROMAKEY", spriteType_CHROMAKEY);
		engine->RegisterEnumValue("Mode", "MAPPING", spriteType_MAPPING);
		engine->RegisterEnumValue("Mode", "TRANSLUCENTMAPPING", spriteType_TRANSLUCENTMAPPING);
		engine->RegisterEnum("Direction");
		engine->RegisterEnumValue("Direction", "FLIPNONE", 0x00);
		engine->RegisterEnumValue("Direction", "FLIPH", 0xFF - 0x100);
		engine->RegisterEnumValue("Direction", "FLIPV", 0x40);
		engine->RegisterEnumValue("Direction", "FLIPHV", 0xBF - 0x100);
		engine->SetDefaultNamespace("TILE");
		engine->RegisterEnum("Quadrant");
		engine->RegisterEnumValue("Quadrant", "TOPLEFT", 0);
		engine->RegisterEnumValue("Quadrant", "TOPRIGHT", 1);
		engine->RegisterEnumValue("Quadrant", "BOTTOMLEFT", 2);
		engine->RegisterEnumValue("Quadrant", "BOTTOMRIGHT", 3);
		engine->RegisterEnumValue("Quadrant", "ALLQUADRANTS", 4);
		engine->RegisterEnum("Flags");
		engine->RegisterEnumValue("Flags", "RAWRANGE", FLAG_HFLIPPED_TILE - 1);
		engine->RegisterEnumValue("Flags", "HFLIPPED", FLAG_HFLIPPED_TILE);
		engine->RegisterEnumValue("Flags", "VFLIPPED", FLAG_VFLIPPED_TILE);
		engine->RegisterEnumValue("Flags", "ANIMATED", FLAG_ANIMATED_TILE);
		engine->SetDefaultNamespace("");

		// TODO
		/*engine->RegisterObjectMethod("jjPLAYER", "SPRITE::Mode get_spriteMode() const property", asFUNCTION(getSpriteMode), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "SPRITE::Mode set_spriteMode(SPRITE::Mode) property", asFUNCTION(setSpriteMode), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "uint8 get_spriteParam() const property", asFUNCTION(getSpriteParam), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "uint8 set_spriteParam(uint8) property", asFUNCTION(setSpriteParam), asCALL_CDECL_OBJFIRST);

		engine->RegisterGlobalFunction("void jjSpriteModeSetMapping(uint8 index, const array<uint8> &in indexMapping, const jjPAL &in rgbMapping)", asFUNCTION(setSpriteModeMapping), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjSpriteModeSetMapping(uint8 index, const array<uint8> &in indexMapping)", asFUNCTION(setSpriteModeMappingDynamic), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool jjSpriteModeIsMappingUsed(uint8 index)", asFUNCTION(isSpriteModeMappingUsed), asCALL_CDECL);
		engine->RegisterGlobalFunction("int jjSpriteModeFirstFreeMapping()", asFUNCTION(firstFreeSpriteModeMapping), asCALL_CDECL);
		engine->RegisterGlobalFunction("array<uint8>@ jjSpriteModeGetIndexMapping(uint8 index)", asFUNCTION(getSpriteModeIndexMapping), asCALL_CDECL);
		engine->RegisterGlobalFunction("jjPAL@ jjSpriteModeGetColorMapping(uint8 index)", asFUNCTION(getSpriteModeColorMapping), asCALL_CDECL);*/

		engine->RegisterObjectType("jjTEXTAPPEARANCE", sizeof(TtextAppearance), asOBJ_VALUE | asOBJ_POD);
		engine->RegisterObjectBehaviour("jjTEXTAPPEARANCE", asBEHAVE_CONSTRUCT, "void f()", asFUNCTION(TtextAppearance::constructor), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectBehaviour("jjTEXTAPPEARANCE", asBEHAVE_CONSTRUCT, "void f(STRING::Mode mode)", asFUNCTION(TtextAppearance::constructorMode), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjTEXTAPPEARANCE", "jjTEXTAPPEARANCE& opAssign(STRING::Mode)", asMETHODPR(TtextAppearance, operator=, (uint32_t), TtextAppearance&), asCALL_THISCALL);
		engine->RegisterObjectProperty("jjTEXTAPPEARANCE", "int xAmp", asOFFSET(TtextAppearance, xAmp));
		engine->RegisterObjectProperty("jjTEXTAPPEARANCE", "int yAmp", asOFFSET(TtextAppearance, yAmp));
		engine->RegisterObjectProperty("jjTEXTAPPEARANCE", "int spacing", asOFFSET(TtextAppearance, spacing));
		engine->RegisterObjectProperty("jjTEXTAPPEARANCE", "bool monospace", asOFFSET(TtextAppearance, monospace));
		engine->RegisterObjectProperty("jjTEXTAPPEARANCE", "bool skipInitialHash", asOFFSET(TtextAppearance, skipInitialHash));
		engine->SetDefaultNamespace("STRING");
		engine->RegisterEnum("SignTreatment");
		engine->RegisterEnumValue("SignTreatment", "HIDESIGN", TtextAppearance::ch_HIDE);
		engine->RegisterEnumValue("SignTreatment", "DISPLAYSIGN", TtextAppearance::ch_DISPLAY);
		engine->RegisterEnumValue("SignTreatment", "SPECIALSIGN", TtextAppearance::ch_SPECIAL);
		engine->RegisterEnum("Alignment");
		engine->RegisterEnumValue("Alignment", "DEFAULT", TtextAppearance::align_DEFAULT);
		engine->RegisterEnumValue("Alignment", "LEFT", TtextAppearance::align_LEFT);
		engine->RegisterEnumValue("Alignment", "CENTER", TtextAppearance::align_CENTER);
		engine->RegisterEnumValue("Alignment", "RIGHT", TtextAppearance::align_RIGHT);
		engine->SetDefaultNamespace("");
		engine->RegisterObjectProperty("jjTEXTAPPEARANCE", "STRING::SignTreatment at", asOFFSET(TtextAppearance, at));
		engine->RegisterObjectProperty("jjTEXTAPPEARANCE", "STRING::SignTreatment caret", asOFFSET(TtextAppearance, caret));
		engine->RegisterObjectProperty("jjTEXTAPPEARANCE", "STRING::SignTreatment hash", asOFFSET(TtextAppearance, hash));
		engine->RegisterObjectProperty("jjTEXTAPPEARANCE", "STRING::SignTreatment newline", asOFFSET(TtextAppearance, newline));
		engine->RegisterObjectProperty("jjTEXTAPPEARANCE", "STRING::SignTreatment pipe", asOFFSET(TtextAppearance, pipe));
		engine->RegisterObjectProperty("jjTEXTAPPEARANCE", "STRING::SignTreatment section", asOFFSET(TtextAppearance, section));
		engine->RegisterObjectProperty("jjTEXTAPPEARANCE", "STRING::SignTreatment tilde", asOFFSET(TtextAppearance, tilde));
		engine->RegisterObjectProperty("jjTEXTAPPEARANCE", "STRING::Alignment align", asOFFSET(TtextAppearance, align));

		engine->RegisterObjectType("jjCANVAS", sizeof(TgameCanvas), asOBJ_REF | asOBJ_NOCOUNT);
		engine->RegisterObjectMethod("jjCANVAS", "void drawPixel(int xPixel, int yPixel, uint8 color, SPRITE::Mode mode = SPRITE::NORMAL, uint8 param = 0)", asMETHOD(TgameCanvas, DrawPixel), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjCANVAS", "void drawRectangle(int xPixel, int yPixel, int width, int height, uint8 color, SPRITE::Mode mode = SPRITE::NORMAL, uint8 param = 0)", asMETHOD(TgameCanvas, DrawRectangle), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjCANVAS", "void drawSprite(int xPixel, int yPixel, int setID, uint8 animation, uint8 frame, int8 direction = 0, SPRITE::Mode mode = SPRITE::NORMAL, uint8 param = 0)", asMETHOD(TgameCanvas, DrawSprite), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjCANVAS", "void drawSpriteFromCurFrame(int xPixel, int yPixel, uint sprite, int8 direction = 0, SPRITE::Mode mode = SPRITE::NORMAL, uint8 param = 0)", asMETHOD(TgameCanvas, DrawCurFrameSprite), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjCANVAS", "void drawResizedSprite(int xPixel, int yPixel, int setID, uint8 animation, uint8 frame, float xScale, float yScale, SPRITE::Mode mode = SPRITE::NORMAL, uint8 param = 0)", asMETHOD(TgameCanvas, DrawResizedSprite), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjCANVAS", "void drawResizedSpriteFromCurFrame(int xPixel, int yPixel, uint sprite, float xScale, float yScale, SPRITE::Mode mode = SPRITE::NORMAL, uint8 param = 0)", asMETHOD(TgameCanvas, DrawResizedCurFrameSprite), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjCANVAS", "void drawRotatedSprite(int xPixel, int yPixel, int setID, uint8 animation, uint8 frame, int angle, float xScale = 1, float yScale = 1, SPRITE::Mode mode = SPRITE::NORMAL, uint8 param = 0)", asMETHOD(TgameCanvas, DrawTransformedSprite), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjCANVAS", "void drawRotatedSpriteFromCurFrame(int xPixel, int yPixel, uint sprite, int angle, float xScale = 1, float yScale = 1, SPRITE::Mode mode = SPRITE::NORMAL, uint8 param = 0)", asMETHOD(TgameCanvas, DrawTransformedCurFrameSprite), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjCANVAS", "void drawSwingingVineSpriteFromCurFrame(int xPixel, int yPixel, uint sprite, int length, int curvature, SPRITE::Mode mode = SPRITE::NORMAL, uint8 param = 0)", asMETHOD(TgameCanvas, DrawSwingingVine), asCALL_THISCALL);

		engine->RegisterObjectMethod("jjCANVAS", "void drawTile(int xPixel, int yPixel, uint16 tile, TILE::Quadrant tileQuadrant = TILE::ALLQUADRANTS)", asMETHOD(TgameCanvas, ExternalDrawTile), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjCANVAS", "void drawString(int xPixel, int yPixel, const ::string &in text, STRING::Size size = STRING::SMALL, STRING::Mode mode = STRING::NORMAL, uint8 param = 0)", asMETHOD(TgameCanvas, DrawTextBasicSize), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjCANVAS", "void drawString(int xPixel, int yPixel, const ::string &in text, STRING::Size size, const jjTEXTAPPEARANCE &in appearance, uint8 param1 = 0, SPRITE::Mode spriteMode = SPRITE::PALSHIFT, uint8 param2 = 0)", asMETHOD(TgameCanvas, DrawTextExtSize), asCALL_THISCALL);

		// TODO
		/*engine->RegisterGlobalFunction("void jjDrawPixel(float xPixel, float yPixel, uint8 color, SPRITE::Mode mode = SPRITE::NORMAL, uint8 param = 0, int8 layerZ = 4, uint8 layerXY = 4, int8 playerID = -1)", asFUNCTION(externalAddPixel), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjDrawRectangle(float xPixel, float yPixel, int width, int height, uint8 color, SPRITE::Mode mode = SPRITE::NORMAL, uint8 param = 0, int8 layerZ = 4, uint8 layerXY = 4, int8 playerID = -1)", asFUNCTION(externalAddRectangle), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjDrawSprite(float xPixel, float yPixel, int setID, uint8 animation, uint8 frame, int8 direction = 0, SPRITE::Mode mode = SPRITE::NORMAL, uint8 param = 0, int8 layerZ = 4, uint8 layerXY = 4, int8 playerID = -1)", asFUNCTION(externalAddSprite), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjDrawSpriteFromCurFrame(float xPixel, float yPixel, uint sprite, int8 direction = 0, SPRITE::Mode mode = SPRITE::NORMAL, uint8 param = 0, int8 layerZ = 4, uint8 layerXY = 4, int8 playerID = -1)", asFUNCTION(externalAddCurFrameSprite), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjDrawResizedSprite(float xPixel, float yPixel, int setID, uint8 animation, uint8 frame, float xScale, float yScale, SPRITE::Mode mode = SPRITE::NORMAL, uint8 param = 0, int8 layerZ = 4, uint8 layerXY = 4, int8 playerID = -1)", asFUNCTION(externalAddResizedSprite), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjDrawResizedSpriteFromCurFrame(float xPixel, float yPixel, uint sprite, float xScale, float yScale, SPRITE::Mode mode = SPRITE::NORMAL, uint8 param = 0, int8 layerZ = 4, uint8 layerXY = 4, int8 playerID = -1)", asFUNCTION(externalAddResizedCurFrameSprite), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjDrawRotatedSprite(float xPixel, float yPixel, int setID, uint8 animation, uint8 frame, int angle, float xScale = 1, float yScale = 1, SPRITE::Mode mode = SPRITE::NORMAL, uint8 param = 0, int8 layerZ = 4, uint8 layerXY = 4, int8 playerID = -1)", asFUNCTION(externalAddTransformedSprite), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjDrawRotatedSpriteFromCurFrame(float xPixel, float yPixel, uint sprite, int angle, float xScale = 1, float yScale = 1, SPRITE::Mode mode = SPRITE::NORMAL, uint8 param = 0, int8 layerZ = 4, uint8 layerXY = 4, int8 playerID = -1)", asFUNCTION(externalAddTransformedCurFrameSprite), asCALL_CDECL);

		engine->RegisterGlobalFunction("void jjDrawSwingingVineSpriteFromCurFrame(float xPixel, float yPixel, uint sprite, int length, int curvature, SPRITE::Mode mode = SPRITE::NORMAL, uint8 param = 0, int8 layerZ = 4, uint8 layerXY = 4, int8 playerID = -1)", asFUNCTION(externalAddSwingingVineSprite), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjDrawTile(float xPixel, float yPixel, uint16 tile, TILE::Quadrant tileQuadrant = TILE::ALLQUADRANTS, int8 layerZ = 4, uint8 layerXY = 4, int8 playerID = -1)", asFUNCTION(externalAddTileSprite), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjDrawString(float xPixel, float yPixel, const ::string &in text, STRING::Size size = STRING::SMALL, STRING::Mode mode = STRING::NORMAL, uint8 param = 0, int8 layerZ = 4, uint8 layerXY = 4, int8 playerID = -1)", asFUNCTION(externalAddTextSpriteSize), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjDrawString(float xPixel, float yPixel, const ::string &in text, STRING::Size size, const jjTEXTAPPEARANCE &in appearance, uint8 param1 = 0, SPRITE::Mode spriteMode = SPRITE::PALSHIFT, uint8 param2 = 0, int8 layerZ = 4, uint8 layerXY = 4, int8 playerID = -1)", asFUNCTION(externalAddTextSpriteExtSize), asCALL_CDECL);
		engine->RegisterGlobalFunction("int jjGetStringWidth(const ::string &in text, STRING::Size size, const jjTEXTAPPEARANCE &in style)", asFUNCTION(externalGetTextWidthSize), asCALL_CDECL);

		engine->SetDefaultNamespace("TEXTURE");
		engine->RegisterEnum("Texture");
		engine->RegisterEnumValue("Texture", "FROMTILES", 0);
		engine->RegisterEnumValue("Texture", "LAYER8", 0);
		engine->RegisterEnumValue("Texture", "NORMAL", 1);
		engine->RegisterEnumValue("Texture", "PSYCH", 2);
		engine->RegisterEnumValue("Texture", "MEDIVO", 3);
		engine->RegisterEnumValue("Texture", "DIAMONDUSBETA", 4);
		engine->RegisterEnumValue("Texture", "WISETYNESS", 5);
		engine->RegisterEnumValue("Texture", "BLADE", 6);
		engine->RegisterEnumValue("Texture", "MEZ02", 7);
		engine->RegisterEnumValue("Texture", "WINDSTORMFORTRESS", 8);
		engine->RegisterEnumValue("Texture", "RANEFORUSV", 9);
		engine->RegisterEnumValue("Texture", "CORRUPTEDSANCTUARY", 10);
		engine->RegisterEnumValue("Texture", "XARGON", 11);
		engine->RegisterEnumValue("Texture", "ICTUBELECTRIC", 12);
		engine->RegisterEnumValue("Texture", "WTF", 13);
		engine->RegisterEnumValue("Texture", "MUCKAMOKNIGHT", 14);
		engine->RegisterEnumValue("Texture", "DESOLATION", 15);
		engine->RegisterEnumValue("Texture", "CUSTOM", ~0);
		engine->RegisterEnum("Style");
		engine->RegisterEnumValue("Style", "WARPHORIZON", tbgModeWARPHORIZON);
		engine->RegisterEnumValue("Style", "TUNNEL", tbgModeTUNNEL);
		engine->RegisterEnumValue("Style", "MENU", tbgModeMENU);
		engine->RegisterEnumValue("Style", "TILEMENU", tbgModeTILEMENU);
		engine->RegisterEnumValue("Style", "WAVE", tbgModeWAVE);
		engine->RegisterEnumValue("Style", "CYLINDER", tbgModeCYLINDER);
		engine->RegisterEnumValue("Style", "REFLECTION", tbgModeREFLECTION);
		engine->SetDefaultNamespace("");

		// TODO
		/*engine->RegisterGlobalFunction("void jjSetDarknessColor(jjPALCOLOR color = jjPALCOLOR())", asFUNCTION(setDarknessColors), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjSetFadeColors(uint8 red, uint8 green, uint8 blue)", asFUNCTION(setFadeColors), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjSetFadeColors(uint8 paletteColorID = 207)", asFUNCTION(setFadeColorsToTBG), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjSetFadeColors(jjPALCOLOR color)", asFUNCTION(BackgroundLayerFunction<void(TpaletteEntry)>::invoke<&Tlayer::SetFadeColor>), asCALL_CDECL);
		engine->RegisterGlobalFunction("jjPALCOLOR jjGetFadeColors()", asFUNCTION(BackgroundLayerFunction<TpaletteEntry()>::invoke<&Tlayer::GetFadeColor>), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjUpdateTexturedBG()", asFUNCTION(BackgroundLayerFunction<void()>::invoke<&Tlayer::GetTextureFromTiles>), asCALL_CDECL); //deprecated
		engine->RegisterGlobalFunction("TEXTURE::Texture set_jjTexturedBGTexture(TEXTURE::Texture) property", asFUNCTION(BackgroundLayerFunction<unsigned(unsigned)>::invoke<&Tlayer::SetTexture>), asCALL_CDECL);
		engine->RegisterGlobalFunction("TEXTURE::Texture get_jjTexturedBGTexture() property", asFUNCTION(BackgroundLayerFunction<unsigned()>::invoke<&Tlayer::GetTexture>), asCALL_CDECL);
		engine->RegisterGlobalFunction("TEXTURE::Style set_jjTexturedBGStyle(TEXTURE::Style) property", asFUNCTION(BackgroundLayerFunction<unsigned(unsigned)>::invoke<&Tlayer::SetTextureMode>), asCALL_CDECL);
		engine->RegisterGlobalFunction("TEXTURE::Style get_jjTexturedBGStyle() property", asFUNCTION(BackgroundLayerFunction<unsigned()>::invoke<&Tlayer::GetTextureMode>), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool set_jjTexturedBGUsed(bool) property", asFUNCTION(BackgroundLayerFunction<unsigned(unsigned)>::invoke<&Tlayer::SetTextureSurface>), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool get_jjTexturedBGUsed() property", asFUNCTION(BackgroundLayerFunction<unsigned()>::invoke<&Tlayer::GetTextureSurface>), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool set_jjTexturedBGStars(bool) property", asFUNCTION(BackgroundLayerFunction<bool(bool)>::invoke<&Tlayer::SetStars>), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool get_jjTexturedBGStars() property", asFUNCTION(BackgroundLayerFunction<bool()>::invoke<&Tlayer::GetStars>), asCALL_CDECL);
		engine->RegisterGlobalProperty("float jjTexturedBGFadePositionX", &(BackgroundLayer.WARPHORIZON.FadePosition[0]));
		engine->RegisterGlobalProperty("float jjTexturedBGFadePositionY", &(BackgroundLayer.WARPHORIZON.FadePosition[1]));*/

		engine->SetDefaultNamespace("SNOWING");
		engine->RegisterEnum("Type");
		engine->RegisterEnumValue("Type", "SNOW", 0);
		engine->RegisterEnumValue("Type", "FLOWER", 1);
		engine->RegisterEnumValue("Type", "RAIN", 2);
		engine->RegisterEnumValue("Type", "LEAF", 3);
		engine->SetDefaultNamespace("");
		engine->RegisterGlobalProperty("bool jjIsSnowing", &snowing);
		engine->RegisterGlobalProperty("bool jjIsSnowingOutdoorsOnly", &snowingOutdoors);
		engine->RegisterGlobalProperty("uint8 jjSnowingIntensity", &snowingIntensity);
		engine->RegisterGlobalProperty("SNOWING::Type jjSnowingType", &snowingType);

		engine->RegisterGlobalFunction("bool get_jjTriggers(uint8) property", asFUNCTION(getTrigger), asCALL_CDECL);
		engine->RegisterGlobalFunction("void set_jjTriggers(uint8, bool) property", asFUNCTION(setTrigger), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool jjSwitchTrigger(uint8 id)", asFUNCTION(switchTrigger), asCALL_CDECL);

		engine->RegisterGlobalFunction("bool get_jjEnabledASFunctions(uint8) property", asFUNCTION(isNumberedASFunctionEnabled), asCALL_CDECL);
		engine->RegisterGlobalFunction("void set_jjEnabledASFunctions(uint8, bool) property", asFUNCTION(setNumberedASFunctionEnabled), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjEnableEachASFunction()", asFUNCTION(reenableAllNumberedASFunctions), asCALL_CDECL);

		engine->SetDefaultNamespace("WATERLIGHT");
		engine->RegisterEnum("wl");
		engine->RegisterEnumValue("wl", "NONE", 0);
		engine->RegisterEnumValue("wl", "GLOBAL", 1);
		engine->RegisterEnumValue("wl", "LAGUNICUS", 3);
		engine->SetDefaultNamespace("WATERINTERACTION");
		engine->RegisterEnum("WaterInteraction");
		engine->RegisterEnumValue("WaterInteraction", "POSITIONBASED", waterInteraction_POSITIONBASED);
		engine->RegisterEnumValue("WaterInteraction", "SWIM", waterInteraction_SWIM);
		engine->RegisterEnumValue("WaterInteraction", "LOWGRAVITY", waterInteraction_LOWGRAVITY);
		engine->SetDefaultNamespace("");
		engine->RegisterGlobalProperty("WATERLIGHT::wl jjWaterLighting", &waterLightMode);
		engine->RegisterGlobalProperty("WATERINTERACTION::WaterInteraction jjWaterInteraction", &waterInteraction);
		engine->RegisterGlobalFunction("float get_jjWaterLevel() property", asFUNCTION(getWaterLevel), asCALL_CDECL);
		engine->RegisterGlobalFunction("float get_jjWaterTarget() property", asFUNCTION(getWaterLevel2), asCALL_CDECL);
		engine->RegisterGlobalFunction("float jjSetWaterLevel(float yPixel, bool instant)", asFUNCTION(setWaterLevel), asCALL_CDECL);
		engine->RegisterGlobalFunction("float get_jjWaterChangeSpeed() property", asFUNCTION(get_waterChangeSpeed), asCALL_CDECL);
		engine->RegisterGlobalFunction("void set_jjWaterChangeSpeed(float) property", asFUNCTION(set_waterChangeSpeed), asCALL_CDECL);
		engine->RegisterGlobalFunction("int get_jjWaterLayer() property", asFUNCTION(get_waterLayer), asCALL_CDECL);
		engine->RegisterGlobalFunction("void set_jjWaterLayer(int) property", asFUNCTION(set_waterLayer), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjSetWaterGradient(uint8 red1, uint8 green1, uint8 blue1, uint8 red2, uint8 green2, uint8 blue2)", asFUNCTION(setWaterGradient), asCALL_CDECL);
		// TODO
		//engine->RegisterGlobalFunction("void jjSetWaterGradient(jjPALCOLOR color1, jjPALCOLOR color2)", asFUNCTION(setWaterGradientFromColors), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjSetWaterGradient()", asFUNCTION(setWaterGradientToTBG), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjResetWaterGradient()", asFUNCTION(resetWaterGradient), asCALL_CDECL);

		engine->RegisterGlobalFunction("void jjTriggerRock(uint8 id)", asFUNCTION(triggerRock), asCALL_CDECL);

		engine->RegisterGlobalFunction("void jjNxt(const string &in filename, bool warp = false, bool fast = false)", asFUNCTION(cycleTo), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjNxt(bool warp = false, bool fast = false)", asFUNCTION(jjNxt), asCALL_CDECL);

		engine->RegisterGlobalFunction("bool get_jjEnabledTeams(uint8) property", asFUNCTION(getEnabledTeam), asCALL_CDECL);

		engine->RegisterGlobalProperty("uint8 jjKeyChat", &ChatKey);
		engine->RegisterGlobalFunction("bool get_jjKey(uint8) property", asFUNCTION(getKeyDown), asCALL_CDECL);
		engine->RegisterGlobalFunction("int get_jjMouseX() property", asFUNCTION(getCursorX), asCALL_CDECL);
		engine->RegisterGlobalFunction("int get_jjMouseY() property", asFUNCTION(getCursorY), asCALL_CDECL);

		engine->RegisterGlobalFunction("bool jjMusicLoad(string &in filename, bool forceReload = false, bool temporary = false)", asFUNCTION(LoadNewMusicFile), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjMusicStop()", asFUNCTION(MusicStop), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjMusicPlay()", asFUNCTION(MusicPlay), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjMusicPause()", asFUNCTION(ModMusicPause), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjMusicResume()", asFUNCTION(ModMusicResume), asCALL_CDECL);

		engine->SetDefaultNamespace("SOUND");
		engine->RegisterEnum("Sample");
		engine->SetDefaultNamespace("");
		engine->RegisterGlobalFunction("void jjSample(float xPixel, float yPixel, SOUND::Sample sample, int volume = 63, int frequency = 0)", asFUNCTION(playSample), asCALL_CDECL);
		engine->RegisterGlobalFunction("int jjSampleLooped(float xPixel, float yPixel, SOUND::Sample sample, int channel, int volume = 63, int frequency = 0)", asFUNCTION(playLoopedSample), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjSamplePriority(SOUND::Sample sample)", asFUNCTION(playPrioritySample), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool jjSampleIsLoaded(SOUND::Sample sample)", asFUNCTION(isSampleLoaded), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool jjSampleLoad(SOUND::Sample sample, string& in filename)", asFUNCTION(loadSample), asCALL_CDECL);

		engine->RegisterGlobalProperty("const bool jjSoundEnabled", &soundEnabled);
		engine->RegisterGlobalProperty("const bool jjSoundFXActive", &soundFXActive);
		engine->RegisterGlobalProperty("const bool jjMusicActive", &musicActive);
		engine->RegisterGlobalProperty("const int jjSoundFXVolume", &soundFXVolume);
		engine->RegisterGlobalProperty("const int jjMusicVolume", &musicVolume);
		engine->RegisterGlobalProperty("int jjEcho", &levelEcho);

		engine->RegisterGlobalProperty("bool jjWarpsTransmuteCoins", &warpsTransmuteCoins);
		engine->RegisterGlobalProperty("bool jjDelayGeneratedCrateOrigins", &delayGeneratedCrateOrigins);
		engine->RegisterGlobalFunction("bool get_jjUseLayer8Speeds() property", asFUNCTION(getUseLayer8Speeds), asCALL_CDECL);
		engine->RegisterGlobalFunction("void set_jjUseLayer8Speeds(bool) property", asFUNCTION(setUseLayer8Speeds), asCALL_CDECL);

		engine->RegisterGlobalProperty("bool jjSugarRushAllowed", &g_levelHasFood);
		engine->RegisterGlobalProperty("bool jjSugarRushesAllowed", &g_levelHasFood);

		// TODO
		/*engine->RegisterObjectType("jjWEAPON", sizeof(tWeaponProfile), asOBJ_REF | asOBJ_NOCOUNT);
		engine->RegisterGlobalFunction("jjWEAPON@ get_jjWeapons(int) property", asFUNCTION(getWeaponProfile), asCALL_CDECL);
		engine->RegisterObjectProperty("jjWEAPON", "bool infinite", asOFFSET(tWeaponProfile, infinite));
		engine->RegisterObjectProperty("jjWEAPON", "bool replenishes", asOFFSET(tWeaponProfile, replenishes));
		engine->RegisterObjectProperty("jjWEAPON", "bool replacedByShield", asOFFSET(tWeaponProfile, shield));
		engine->RegisterObjectProperty("jjWEAPON", "bool replacedByBubbles", asOFFSET(tWeaponProfile, bubbles));
		engine->RegisterObjectProperty("jjWEAPON", "bool comesFromGunCrates", asOFFSET(tWeaponProfile, crates));
		engine->RegisterObjectProperty("jjWEAPON", "bool gradualAim", asOFFSET(tWeaponProfile, gradual));
		engine->RegisterObjectProperty("jjWEAPON", "int multiplier", asOFFSET(tWeaponProfile, multiplier));
		engine->RegisterObjectProperty("jjWEAPON", "int maximum", asOFFSET(tWeaponProfile, maximum));
		engine->RegisterObjectProperty("jjWEAPON", "int gemsLost", asOFFSET(tWeaponProfile, gemsLost));
		engine->RegisterObjectProperty("jjWEAPON", "int gemsLostPowerup", asOFFSET(tWeaponProfile, gemsLostPowerup));
		engine->RegisterObjectProperty("jjWEAPON", "int8 style", asOFFSET(tWeaponProfile, style));
		engine->RegisterObjectProperty("jjWEAPON", "SPREAD::Spread spread", asOFFSET(tWeaponProfile, spread));
		engine->RegisterObjectProperty("jjWEAPON", "bool defaultSample", asOFFSET(tWeaponProfile, sample));
		engine->RegisterObjectProperty("jjWEAPON", "bool allowed", asOFFSET(tWeaponProfile, appearsInLevel));
		engine->RegisterObjectProperty("jjWEAPON", "bool allowedPowerup", asOFFSET(tWeaponProfile, powerupAppearsInLevel));
		engine->RegisterObjectProperty("jjWEAPON", "bool comesFromBirds", asOFFSET(tWeaponProfile, canBeShotByBirds));
		engine->RegisterObjectProperty("jjWEAPON", "bool comesFromBirdsPowerup", asOFFSET(tWeaponProfile, powerupCanBeShotByBirds));

		engine->SetDefaultNamespace("AIR");
		engine->RegisterEnum("Jump");
		engine->RegisterEnumValue("Jump", "NONE", airjumpNONE);
		engine->RegisterEnumValue("Jump", "HELICOPTER", airjumpHELICOPTER);
		engine->RegisterEnumValue("Jump", "DOUBLEJUMP", airjumpSPAZ);
		engine->SetDefaultNamespace("GROUND");
		engine->RegisterEnum("Jump");
		engine->RegisterEnumValue("Jump", "CROUCH", groundjumpNONE);
		engine->RegisterEnumValue("Jump", "JUMP", groundjumpREGULARJUMP);
		engine->RegisterEnumValue("Jump", "JAZZ", groundjumpJAZZ);
		engine->RegisterEnumValue("Jump", "SPAZ", groundjumpSPAZ);
		engine->RegisterEnumValue("Jump", "LORI", groundjumpLORI);
		engine->SetDefaultNamespace("");
		engine->RegisterObjectType("jjCHARACTER", sizeof(tCharacterProfile), asOBJ_REF | asOBJ_NOCOUNT);
		engine->RegisterGlobalFunction("jjCHARACTER@ get_jjCharacters(CHAR::Char) property", asFUNCTION(getCharacterProfile), asCALL_CDECL);
		engine->RegisterObjectProperty("jjCHARACTER", "AIR::Jump airJump", asOFFSET(tCharacterProfile, airJump));
		engine->RegisterObjectProperty("jjCHARACTER", "GROUND::Jump groundJump", asOFFSET(tCharacterProfile, groundJump));
		engine->RegisterObjectProperty("jjCHARACTER", "int doubleJumpCountMax", asOFFSET(tCharacterProfile, doubleJumpCountMax));
		REGISTER_FLOAT_PROPERTY("jjCHARACTER", "doubleJumpXSpeed", tCharacterProfile, doubleJumpXSpeed);
		REGISTER_FLOAT_PROPERTY("jjCHARACTER", "doubleJumpYSpeed", tCharacterProfile, doubleJumpYSpeed);
		engine->RegisterObjectProperty("jjCHARACTER", "int helicopterDurationMax", asOFFSET(tCharacterProfile, helicopterDurationMax));
		REGISTER_FLOAT_PROPERTY("jjCHARACTER", "helicopterXSpeed", tCharacterProfile, helicopterXSpeed);
		REGISTER_FLOAT_PROPERTY("jjCHARACTER", "helicopterYSpeed", tCharacterProfile, helicopterYSpeed);
		engine->RegisterObjectProperty("jjCHARACTER", "bool canHurt", asOFFSET(tCharacterProfile, specialMovesDoDamage));
		engine->RegisterObjectProperty("jjCHARACTER", "bool canRun", asOFFSET(tCharacterProfile, canRun));
		engine->RegisterObjectProperty("jjCHARACTER", "bool morphBoxCycle", asOFFSET(tCharacterProfile, availableToMorphBoxes));*/

		engine->SetDefaultNamespace("CREATOR");
		engine->RegisterEnum("Type");
		engine->RegisterEnumValue("Type", "OBJECT", 0);
		engine->RegisterEnumValue("Type", "LEVEL", 1);
		engine->RegisterEnumValue("Type", "PLAYER", 2);
		engine->SetDefaultNamespace("AREA");
		engine->RegisterEnum("Area");
		engine->SetDefaultNamespace("OBJECT");
		engine->RegisterEnum("Object");
		engine->SetDefaultNamespace("ANIM");
		engine->RegisterEnum("Set");
		engine->SetDefaultNamespace("");

		engine->RegisterGlobalFunction("int jjEventGet(uint16 xTile, uint16 yTile)", asFUNCTION(GetEvent), asCALL_CDECL);
		engine->RegisterGlobalFunction("int jjParameterGet(uint16 xTile, uint16 yTile, int offset, int length)", asFUNCTION(GetEventParamWrapper), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjEventSet(uint16 xTile, uint16 yTile, uint8 newEventID)", asFUNCTION(SetEventByte), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjParameterSet(uint16 xTile, uint16 yTile, int8 offset, int8 length, int newValue)", asFUNCTION(SetEventParam), asCALL_CDECL);
		engine->RegisterGlobalFunction("uint8 get_jjTileType(uint16) property", asFUNCTION(GetTileType), asCALL_CDECL);
		engine->RegisterGlobalFunction("void set_jjTileType(uint16,uint8) property", asFUNCTION(SetTileType), asCALL_CDECL);

		engine->SetDefaultNamespace("LIGHT");
		engine->RegisterEnum("Enforce");
		engine->RegisterEnumValue("Enforce", "OPTIONAL", ambientLighting_OPTIONAL);
		engine->RegisterEnumValue("Enforce", "BASIC", ambientLighting_BASIC);
		engine->RegisterEnumValue("Enforce", "COMPLETE", ambientLighting_COMPLETE);

		engine->SetDefaultNamespace("");
		engine->RegisterGlobalProperty("LIGHT::Enforce jjEnforceLighting", &enforceAmbientLighting);

		engine->SetDefaultNamespace("STATE");
		engine->RegisterEnum("State");
		engine->SetDefaultNamespace("BEHAVIOR");
		engine->RegisterEnum("Behavior");
		engine->SetDefaultNamespace("LIGHT");
		engine->RegisterEnum("Type");
		engine->RegisterEnumValue("Type", "NONE", 0);
		engine->RegisterEnumValue("Type", "NORMAL", 3);
		engine->RegisterEnumValue("Type", "POINT", 1);
		engine->RegisterEnumValue("Type", "POINT2", 2);
		engine->RegisterEnumValue("Type", "FLICKER", 4);
		engine->RegisterEnumValue("Type", "BRIGHT", 5);
		engine->RegisterEnumValue("Type", "LASERBEAM", 6);
		engine->RegisterEnumValue("Type", "LASER", 7);
		engine->RegisterEnumValue("Type", "RING", 8);
		engine->RegisterEnumValue("Type", "RING2", 9);
		engine->RegisterEnumValue("Type", "PLAYER", 10);

		engine->SetDefaultNamespace("HANDLING");
		engine->RegisterEnum("Bullet");
		engine->RegisterEnumValue("Bullet", "HURTBYBULLET", 0);
		engine->RegisterEnumValue("Bullet", "IGNOREBULLET", 1);
		engine->RegisterEnumValue("Bullet", "DESTROYBULLET", 2);
		engine->RegisterEnumValue("Bullet", "DETECTBULLET", 3);
		engine->RegisterEnum("Player");
		engine->RegisterEnumValue("Player", "ENEMY", 0);
		engine->RegisterEnumValue("Player", "PLAYERBULLET", 1);
		engine->RegisterEnumValue("Player", "ENEMYBULLET", 2);
		engine->RegisterEnumValue("Player", "PARTICLE", 3);
		engine->RegisterEnumValue("Player", "EXPLOSION", 4);
		engine->RegisterEnumValue("Player", "PICKUP", 5);
		engine->RegisterEnumValue("Player", "DELAYEDPICKUP", 6);
		engine->RegisterEnumValue("Player", "HURT", 7);
		engine->RegisterEnumValue("Player", "SPECIAL", 8);
		engine->RegisterEnumValue("Player", "DYING", 9);
		engine->RegisterEnumValue("Player", "SPECIALDONE", 10);
		engine->RegisterEnumValue("Player", "SELFCOLLISION", 11);
		engine->SetDefaultNamespace("");

		// TODO
		/*engine->RegisterObjectType("jjOBJ", sizeof(Omonster), asOBJ_REF | asOBJ_NOCOUNT);
		engine->RegisterGlobalFunction("jjOBJ @get_jjObjects(int) property", asFUNCTION(getObject), asCALL_CDECL);
		engine->RegisterGlobalFunction("jjOBJ @get_jjObjectPresets(uint8) property", asFUNCTION(getLoadObject), asCALL_CDECL);
		engine->RegisterGlobalProperty("const int jjObjectCount", numObjects);
		engine->RegisterGlobalProperty("const int jjObjectMax", maxObjects);
		engine->RegisterObjectMethod("jjOBJ", "bool get_isActive() const property", asFUNCTION(isActive), asCALL_CDECL_OBJLAST);

		engine->RegisterObjectMethod("jjPLAYER", "LIGHT::Type get_lightType() const property", asFUNCTION(getLightType), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "LIGHT::Type set_lightType(LIGHT::Type) property", asFUNCTION(setLightType), asCALL_CDECL_OBJFIRST);

		engine->RegisterObjectMethod("jjPLAYER", "bool doesCollide(const jjOBJ@ object, bool always = false) const", asFUNCTION(doesCollidePlayer), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "int getObjectHitForce(const jjOBJ@ target = null) const", asFUNCTION(getDownAttack), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjPLAYER", "bool objectHit(jjOBJ@ target, int force, HANDLING::Player playerHandling)", asFUNCTION(playerHitObject), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjOBJ", "void objectHit(jjOBJ@ target, HANDLING::Player playerHandling)", asFUNCTION(CheckBulletAgainstObject), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjOBJ", "void blast(int maxDistance, bool blastObjects)", asFUNCTION(jjOBJDoBlastBase), asCALL_CDECL_OBJFIRST);

		engine->RegisterObjectMethod("jjPLAYER", "bool isEnemy(const jjPLAYER &in victim) const", asFUNCTION(playerIsEnemyOfPlayer), asCALL_CDECL_OBJFIRST);

		engine->RegisterObjectProperty("jjPLAYER", "const ANIM::Set setID", asOFFSET(Tplayer, charCurr));
		engine->RegisterObjectProperty("jjPLAYER", "const uint16 curAnim", asOFFSET(Tplayer, curAnim));
		engine->RegisterObjectProperty("jjPLAYER", "const uint curFrame", asOFFSET(Tplayer, curFrame));
		engine->RegisterObjectProperty("jjPLAYER", "const uint8 frameID", asOFFSET(Tplayer, frameID));

		engine->RegisterFuncdef("void jjVOIDFUNCOBJ(jjOBJ@)");
		engine->RegisterObjectType("jjBEHAVIOR", sizeof(AiProcPtr), asOBJ_VALUE | asOBJ_APP_CLASS_CDA);
		engine->RegisterObjectBehaviour("jjBEHAVIOR", asBEHAVE_CONSTRUCT, "void f()", asFUNCTION(behaviorDefaultConstructor), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectBehaviour("jjBEHAVIOR", asBEHAVE_CONSTRUCT, "void f(const BEHAVIOR::Behavior &in behavior)", asFUNCTION(behaviorCopyConstructor), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectBehaviour("jjBEHAVIOR", asBEHAVE_DESTRUCT, "void f()", asFUNCTION(behaviorDestructor), asCALL_CDECL_OBJLAST);

		engine->RegisterInterface("jjBEHAVIORINTERFACE");
		engine->RegisterInterfaceMethod("jjBEHAVIORINTERFACE", jjBehaviorDelegateMethodDeclarations[JJBDMOnBehave]);

		engine->RegisterObjectMethod("jjBEHAVIOR", "jjBEHAVIOR& opAssign(const jjBEHAVIOR &in)", asMETHODPR(jjBEHAVIOR, operator=, (const jjBEHAVIOR&), jjBEHAVIOR&), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjBEHAVIOR", "jjBEHAVIOR& opAssign(BEHAVIOR::Behavior)", asMETHODPR(jjBEHAVIOR, operator=, (AiProcPtr), jjBEHAVIOR&), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjBEHAVIOR", "jjBEHAVIOR& opAssign(jjVOIDFUNCOBJ@)", asMETHODPR(jjBEHAVIOR, operator=, (asIScriptFunction*), jjBEHAVIOR&), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjBEHAVIOR", "jjBEHAVIOR& opAssign(jjBEHAVIORINTERFACE@)", asMETHODPR(jjBEHAVIOR, operator=, (asIScriptObject*), jjBEHAVIOR&), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjBEHAVIOR", "bool opEquals(const jjBEHAVIOR &in) const", asMETHODPR(jjBEHAVIOR, operator==, (const jjBEHAVIOR&) const, bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjBEHAVIOR", "bool opEquals(BEHAVIOR::Behavior) const", asMETHODPR(jjBEHAVIOR, operator==, (AiProcPtr) const, bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjBEHAVIOR", "bool opEquals(const jjVOIDFUNCOBJ@) const", asMETHODPR(jjBEHAVIOR, operator==, (const asIScriptFunction*) const, bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjBEHAVIOR", "BEHAVIOR::Behavior opConv() const", asMETHOD(jjBEHAVIOR, operator AiProcPtr), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjBEHAVIOR", "jjVOIDFUNCOBJ@ opCast() const", asMETHOD(jjBEHAVIOR, operator asIScriptFunction*), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjBEHAVIOR", "jjBEHAVIORINTERFACE@ opCast() const", asMETHOD(jjBEHAVIOR, operator asIScriptObject*), asCALL_THISCALL);

		engine->RegisterObjectProperty("jjOBJ", "jjBEHAVIOR behavior", asOFFSET(TgameObj, behavior));

		engine->RegisterObjectMethod("jjOBJ", "void behave(BEHAVIOR::Behavior behavior = BEHAVIOR::DEFAULT, bool draw = true)", asFUNCTION(jjOBJBehave), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjOBJ", "void behave(jjBEHAVIOR behavior, bool draw = true)", asFUNCTION(jjOBJBehave), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjOBJ", "void behave(jjVOIDFUNCOBJ@ behavior, bool draw = true)", asFUNCTION(jjOBJBehave), asCALL_CDECL_OBJFIRST);

		engine->RegisterGlobalFunction("int jjAddObject(uint8 eventID, float xPixel, float yPixel, uint16 creatorID = 0, CREATOR::Type creatorType = CREATOR::OBJECT, BEHAVIOR::Behavior behavior = BEHAVIOR::DEFAULT)", asFUNCTION(addObject), asCALL_CDECL);
		engine->RegisterGlobalFunction("int jjAddObject(uint8 eventID, float xPixel, float xPixel, uint16 creatorID, CREATOR::Type creatorType, jjVOIDFUNCOBJ@ behavior)", asFUNCTION(addObjectUsingASFunc), asCALL_CDECL);

		REGISTER_FLOAT_PROPERTY("jjOBJ", "xOrg", TgameObj, xOrg);
		REGISTER_FLOAT_PROPERTY("jjOBJ", "yOrg", TgameObj, yOrg);
		REGISTER_FLOAT_PROPERTY("jjOBJ", "xPos", TgameObj, xPos);
		REGISTER_FLOAT_PROPERTY("jjOBJ", "yPos", TgameObj, yPos);
		REGISTER_FLOAT_PROPERTY("jjOBJ", "xSpeed", TgameObj, xSpeed);
		REGISTER_FLOAT_PROPERTY("jjOBJ", "ySpeed", TgameObj, ySpeed);
		REGISTER_FLOAT_PROPERTY("jjOBJ", "xAcc", TgameObj, xAcc);
		REGISTER_FLOAT_PROPERTY("jjOBJ", "yAcc", TgameObj, yAcc);
		engine->RegisterObjectProperty("jjOBJ", "int counter", asOFFSET(TgameObj, counter));
		engine->RegisterObjectProperty("jjOBJ", "uint curFrame", asOFFSET(TgameObj, curFrame));
		engine->RegisterObjectMethod("jjOBJ", "uint determineCurFrame(bool change = true)", asFUNCTION(doCurFrame), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectProperty("jjOBJ", "int age", asOFFSET(TgameObj, age));
		engine->RegisterObjectProperty("jjOBJ", "int creator", asOFFSET(TgameObj, creator)); // Deprecated
		engine->RegisterObjectMethod("jjOBJ", "uint16 get_creatorID() const property", asFUNCTION(getCreatorID), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjOBJ", "uint16 set_creatorID(uint16) property", asFUNCTION(setCreatorID), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjOBJ", "CREATOR::Type get_creatorType() const property", asFUNCTION(getCreatorType), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjOBJ", "CREATOR::Type set_creatorType(CREATOR::Type) property", asFUNCTION(setCreatorType), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectProperty("jjOBJ", "int16 curAnim", asOFFSET(TgameObj, curAnim));
		engine->RegisterObjectMethod("jjOBJ", "int16 determineCurAnim(uint8 setID, uint8 animation, bool change = true)", asFUNCTION(doCurAnim), asCALL_CDECL_OBJFIRST);

		engine->RegisterObjectProperty("jjOBJ", "int16 killAnim", asOFFSET(TgameObj, killAnim));
		engine->RegisterObjectProperty("jjOBJ", "uint8 freeze", asOFFSET(TgameObj, freeze));
		engine->RegisterObjectProperty("jjOBJ", "uint8 lightType", asOFFSET(TgameObj, lightType));
		engine->RegisterObjectProperty("jjOBJ", "int8 frameID", asOFFSET(TgameObj, frameID));
		engine->RegisterObjectProperty("jjOBJ", "int8 noHit", asOFFSET(TgameObj, noHit)); // Deprecated
		engine->RegisterObjectMethod("jjOBJ", "HANDLING::Bullet get_bulletHandling() const", asFUNCTION(getBulletHandling), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjOBJ", "HANDLING::Bullet set_bulletHandling(HANDLING::Bullet)", asFUNCTION(setBulletHandling), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjOBJ", "bool get_causesRicochet() const property", asFUNCTION(get_ricochet), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjOBJ", "bool set_causesRicochet(bool) property", asFUNCTION(set_ricochet), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjOBJ", "bool get_isFreezable() const property", asFUNCTION(get_freezable), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjOBJ", "bool set_isFreezable(bool) property", asFUNCTION(set_freezable), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjOBJ", "bool get_isBlastable() const property", asFUNCTION(get_blastable), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjOBJ", "bool set_isBlastable(bool) property", asFUNCTION(set_blastable), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectProperty("jjOBJ", "int8 energy", asOFFSET(TgameObj, energy));
		engine->RegisterObjectProperty("jjOBJ", "int8 light", asOFFSET(TgameObj, light));
		engine->RegisterObjectProperty("jjOBJ", "uint8 objType", asOFFSET(TgameObj, objType)); // Deprecated
		engine->RegisterObjectMethod("jjOBJ", "HANDLING::Player get_playerHandling() const property", asFUNCTION(getPlayerHandling), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjOBJ", "HANDLING::Player set_playerHandling(HANDLING::Player) property", asFUNCTION(setPlayerHandling), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjOBJ", "bool get_isTarget() const property", asFUNCTION(get_target), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjOBJ", "bool set_isTarget(bool) property", asFUNCTION(set_target), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjOBJ", "bool get_triggersTNT() const property", asFUNCTION(get_trigtnt), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjOBJ", "bool set_triggersTNT(bool) property", asFUNCTION(set_trigtnt), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjOBJ", "bool get_deactivates() const property", asFUNCTION(get_deactivates), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjOBJ", "bool set_deactivates(bool) property", asFUNCTION(set_deactivates), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjOBJ", "bool get_scriptedCollisions() const property", asFUNCTION(get_ascoll), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjOBJ", "bool set_scriptedCollisions(bool) property", asFUNCTION(set_ascoll), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectProperty("jjOBJ", "int8 state", asOFFSET(TgameObj, state));
		engine->RegisterObjectProperty("jjOBJ", "uint16 points", asOFFSET(TgameObj, points));
		engine->RegisterObjectProperty("jjOBJ", "uint8 eventID", asOFFSET(TgameObj, load));
		engine->RegisterObjectProperty("jjOBJ", "int8 direction", asOFFSET(TgameObj, direction));
		engine->RegisterObjectProperty("jjOBJ", "uint8 justHit", asOFFSET(TgameObj, justHit));
		engine->RegisterObjectProperty("jjOBJ", "int8 oldState", asOFFSET(TgameObj, oldState));
		engine->RegisterObjectProperty("jjOBJ", "int animSpeed", asOFFSET(Omonster, animSpeed));
		engine->RegisterObjectProperty("jjOBJ", "int special", asOFFSET(Omonster, special));
		engine->RegisterObjectMethod("jjOBJ", "int get_var(uint8) const property", asFUNCTION(get_VarX), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjOBJ", "int set_var(uint8, int) property", asFUNCTION(set_VarX), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectProperty("jjOBJ", "uint8 doesHurt", asOFFSET(Omonster, channel));
		engine->RegisterObjectProperty("jjOBJ", "uint8 counterEnd", asOFFSET(Omonster, boss));
		engine->RegisterObjectProperty("jjOBJ", "const int16 objectID", asOFFSET(Omonster, objectID));

		engine->RegisterGlobalFunction("void jjDeleteObject(int objectID)", asFUNCTION(MyDeleteObject), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjKillObject(int objectID)", asFUNCTION(KillObject), asCALL_CDECL);
		engine->RegisterGlobalProperty("const bool jjDeactivatingBecauseOfDeath", &GeneralGlobals->reInitializingObjects);

		engine->RegisterObjectMethod("jjOBJ", "int draw()", asFUNCTION(jjOBJDrawGameObject), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjOBJ", "int beSolid(bool shouldCheckForStompingLocalPlayers = false)", asFUNCTION(jjOBJDoSolidObject), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjOBJ", "void bePlatform(float xOld, float yOld, int width = 0, int height = 0)", asFUNCTION(jjOBJBePlatform), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjOBJ", "void clearPlatform()", asFUNCTION(jjOBJClearPlayerPlatform), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjOBJ", "void putOnGround(bool precise = false)", asFUNCTION(jjOBJPutObjectOnGround), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjOBJ", "bool ricochet()", asFUNCTION(ricochet), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjOBJ", "int unfreeze(int style)", asFUNCTION(jjOBJcUNFREEZE), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjOBJ", "void delete()", asFUNCTION(jjOBJMyDeleteObject), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjOBJ", "void deactivate()", asFUNCTION(jjOBJcDEACTIVATE), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjOBJ", "void pathMovement()", asFUNCTION(jjOBJPathMovement), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjOBJ", "int fireBullet(uint8 eventID) const", asFUNCTION(jjOBJAddObjectAsBullet), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjOBJ", "void particlePixelExplosion(int style) const", asFUNCTION(jjOBJParticlePixelExplosion), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjOBJ", "void grantPickup(jjPLAYER@ player, int frequency) const", asFUNCTION(AddExtraPickup), asCALL_CDECL_OBJFIRST);

		engine->RegisterObjectMethod("jjOBJ", "int findNearestPlayer(int maxDistance) const", asFUNCTION(jjOBJfindNearestPlayer), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjOBJ", "int findNearestPlayer(int maxDistance, int &out foundDistance) const", asFUNCTION(jjOBJfindNearestPlayerDistance), asCALL_CDECL_OBJFIRST);

		engine->RegisterObjectMethod("jjOBJ", "bool doesCollide(const jjOBJ@ object, bool always = false) const", asFUNCTION(doesCollide), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjOBJ", "bool doesCollide(const jjPLAYER@ player, bool always = false) const", asFUNCTION(doesCollidePlayer2), asCALL_CDECL_OBJFIRST);

		engine->RegisterGlobalFunction("void jjAddParticleTileExplosion(uint16 xTile, uint16 yTile, uint16 tile, bool collapseSceneryStyle)", asFUNCTION(ExternalAddParticleTile), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjAddParticlePixelExplosion(float xPixel, float yPixel, int curFrame, int direction, int mode)", asFUNCTION(addParticlePixelExplosion), asCALL_CDECL);

		engine->SetDefaultNamespace("PARTICLE");
		engine->RegisterEnum("Type");
		engine->RegisterEnumValue("Type", "INACTIVE", particleNONE);
		engine->RegisterEnumValue("Type", "PIXEL", particlePIXEL);
		engine->RegisterEnumValue("Type", "FIRE", particleFIRE);
		engine->RegisterEnumValue("Type", "SMOKE", particleSMOKE);
		engine->RegisterEnumValue("Type", "ICETRAIL", particleICETRAIL);
		engine->RegisterEnumValue("Type", "SPARK", particleSPARK);
		engine->RegisterEnumValue("Type", "STRING", particleSCORE);
		engine->RegisterEnumValue("Type", "SNOW", particleSNOW);
		engine->RegisterEnumValue("Type", "RAIN", particleRAIN);
		engine->RegisterEnumValue("Type", "FLOWER", particleFLOWER);
		engine->RegisterEnumValue("Type", "LEAF", particleLEAF);
		engine->RegisterEnumValue("Type", "STAR", particleSTAR);
		engine->RegisterEnumValue("Type", "TILE", particleTILE);
		engine->SetDefaultNamespace("");

		engine->RegisterObjectType("jjPARTICLE", sizeof(Tparticle), asOBJ_REF | asOBJ_NOCOUNT);
		engine->RegisterGlobalFunction("jjPARTICLE @get_jjParticles(int) property", asFUNCTION(getParticle), asCALL_CDECL);
		engine->RegisterGlobalFunction("jjPARTICLE @jjAddParticle(PARTICLE::Type type)", asFUNCTION(AddParticle), asCALL_CDECL);
		REGISTER_FLOAT_PROPERTY("jjPARTICLE", "xPos", Tparticle, xPos);
		REGISTER_FLOAT_PROPERTY("jjPARTICLE", "yPos", Tparticle, yPos);
		REGISTER_FLOAT_PROPERTY("jjPARTICLE", "xSpeed", Tparticle, xSpeed);
		REGISTER_FLOAT_PROPERTY("jjPARTICLE", "ySpeed", Tparticle, ySpeed);
		engine->RegisterObjectProperty("jjPARTICLE", "uint8 type", asOFFSET(Tparticle, particleType));
		engine->RegisterObjectProperty("jjPARTICLE", "bool isActive", asOFFSET(Tparticle, active));
		engine->RegisterObjectType("jjPARTICLEPIXEL", 9, asOBJ_VALUE | asOBJ_POD); // Private/deprecated
		engine->RegisterObjectProperty("jjPARTICLEPIXEL", "uint8 size", -2);
		engine->RegisterObjectMethod("jjPARTICLEPIXEL", "uint8 get_color(int) const property", asMETHOD(TparticlePIXEL, get_color), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPARTICLEPIXEL", "uint8 set_color(int, uint8) property", asMETHOD(TparticlePIXEL, set_color), asCALL_THISCALL);
		engine->RegisterObjectProperty("jjPARTICLE", "jjPARTICLEPIXEL pixel", asOFFSET(Tparticle, GENERIC));
		engine->RegisterObjectType("jjPARTICLEFIRE", 9, asOBJ_VALUE | asOBJ_POD);  // Private/deprecated
		engine->RegisterObjectProperty("jjPARTICLEFIRE", "uint8 size", -2);
		engine->RegisterObjectProperty("jjPARTICLEFIRE", "uint8 color", 0);
		engine->RegisterObjectProperty("jjPARTICLEFIRE", "uint8 colorStop", 1);
		engine->RegisterObjectProperty("jjPARTICLEFIRE", "int8 colorDelta", 2);
		engine->RegisterObjectProperty("jjPARTICLE", "jjPARTICLEFIRE fire", asOFFSET(Tparticle, GENERIC));
		engine->RegisterObjectType("jjPARTICLESMOKE", 9, asOBJ_VALUE | asOBJ_POD);  // Private/deprecated
		engine->RegisterObjectProperty("jjPARTICLESMOKE", "uint8 countdown", 0);
		engine->RegisterObjectProperty("jjPARTICLE", "jjPARTICLESMOKE smoke", asOFFSET(Tparticle, GENERIC));
		engine->RegisterObjectType("jjPARTICLEICETRAIL", 9, asOBJ_VALUE | asOBJ_POD);
		engine->RegisterObjectProperty("jjPARTICLEICETRAIL", "uint8 color", 0);
		engine->RegisterObjectProperty("jjPARTICLEICETRAIL", "uint8 colorStop", 1);
		engine->RegisterObjectProperty("jjPARTICLEICETRAIL", "int8 colorDelta", 2);
		engine->RegisterObjectProperty("jjPARTICLE", "jjPARTICLEICETRAIL icetrail", asOFFSET(Tparticle, GENERIC));
		engine->RegisterObjectType("jjPARTICLESPARK", 9, asOBJ_VALUE | asOBJ_POD);  // Private/deprecated
		engine->RegisterObjectProperty("jjPARTICLESPARK", "uint8 color", 0);
		engine->RegisterObjectProperty("jjPARTICLESPARK", "uint8 colorStop", 1);
		engine->RegisterObjectProperty("jjPARTICLESPARK", "int8 colorDelta", 2);
		engine->RegisterObjectProperty("jjPARTICLE", "jjPARTICLESPARK spark", asOFFSET(Tparticle, GENERIC));
		engine->RegisterObjectType("jjPARTICLESTRING", 9, asOBJ_VALUE | asOBJ_POD); // Private/deprecated
		engine->RegisterObjectMethod("jjPARTICLESTRING", "::string get_text() const property", asMETHOD(TparticleSCORE, get_text), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPARTICLESTRING", "void set_text(::string) property", asMETHOD(TparticleSCORE, set_text), asCALL_THISCALL);

		engine->RegisterObjectProperty("jjPARTICLE", "jjPARTICLESTRING string", asOFFSET(Tparticle, GENERIC));
		engine->RegisterObjectType("jjPARTICLESNOW", 9, asOBJ_VALUE | asOBJ_POD); // Private/deprecated
		engine->RegisterObjectProperty("jjPARTICLESNOW", "uint8 frame", 0);
		engine->RegisterObjectProperty("jjPARTICLESNOW", "uint8 countup", 1);
		engine->RegisterObjectProperty("jjPARTICLESNOW", "uint8 countdown", 2);
		engine->RegisterObjectProperty("jjPARTICLESNOW", "uint16 frameBase", -7);
		engine->RegisterObjectProperty("jjPARTICLE", "jjPARTICLESNOW snow", asOFFSET(Tparticle, GENERIC));
		engine->RegisterObjectType("jjPARTICLERAIN", 9, asOBJ_VALUE | asOBJ_POD); // Private/deprecated
		engine->RegisterObjectProperty("jjPARTICLERAIN", "uint8 frame", 0);
		engine->RegisterObjectProperty("jjPARTICLERAIN", "uint16 frameBase", -7);
		engine->RegisterObjectProperty("jjPARTICLE", "jjPARTICLERAIN rain", asOFFSET(Tparticle, GENERIC));
		engine->RegisterObjectType("jjPARTICLELEAF", 9, asOBJ_VALUE | asOBJ_POD); // Private/deprecated
		engine->RegisterObjectProperty("jjPARTICLELEAF", "uint8 frame", 0);
		engine->RegisterObjectProperty("jjPARTICLELEAF", "uint8 countup", 1);
		engine->RegisterObjectProperty("jjPARTICLELEAF", "bool noclip", 2);
		engine->RegisterObjectProperty("jjPARTICLELEAF", "uint8 height", 3);
		engine->RegisterObjectProperty("jjPARTICLELEAF", "uint16 frameBase", -7);
		engine->RegisterObjectProperty("jjPARTICLE", "jjPARTICLELEAF leaf", asOFFSET(Tparticle, GENERIC));
		engine->RegisterObjectType("jjPARTICLEFLOWER", 9, asOBJ_VALUE | asOBJ_POD); // Private/deprecated
		engine->RegisterObjectProperty("jjPARTICLEFLOWER", "uint8 size", -2);
		engine->RegisterObjectProperty("jjPARTICLEFLOWER", "uint8 color", 0);
		engine->RegisterObjectProperty("jjPARTICLEFLOWER", "uint8 angle", 1);
		engine->RegisterObjectProperty("jjPARTICLEFLOWER", "int8 angularSpeed", 2);
		engine->RegisterObjectProperty("jjPARTICLEFLOWER", "uint8 petals", 3);
		engine->RegisterObjectProperty("jjPARTICLE", "jjPARTICLEFLOWER flower", asOFFSET(Tparticle, GENERIC));
		engine->RegisterObjectType("jjPARTICLESTAR", 9, asOBJ_VALUE | asOBJ_POD); // Private/deprecated
		engine->RegisterObjectProperty("jjPARTICLESTAR", "uint8 size", -2);
		engine->RegisterObjectProperty("jjPARTICLESTAR", "uint8 color", 0);
		engine->RegisterObjectProperty("jjPARTICLESTAR", "uint8 angle", 1);
		engine->RegisterObjectProperty("jjPARTICLESTAR", "int8 angularSpeed", 2);
		engine->RegisterObjectProperty("jjPARTICLESTAR", "uint8 frame", 3);
		engine->RegisterObjectProperty("jjPARTICLESTAR", "uint8 colorChangeCounter", 4);
		engine->RegisterObjectProperty("jjPARTICLESTAR", "uint8 colorChangeInterval", 5);
		engine->RegisterObjectProperty("jjPARTICLE", "jjPARTICLESTAR star", asOFFSET(Tparticle, GENERIC));
		engine->RegisterObjectType("jjPARTICLETILE", 9, asOBJ_VALUE | asOBJ_POD); // Private/deprecated
		engine->RegisterObjectProperty("jjPARTICLETILE", "uint8 quadrant", 0);

		engine->RegisterObjectMethod("jjPARTICLETILE", "uint16 get_tileID() const property", asMETHOD(TparticleTILE, get_AStile), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPARTICLETILE", "uint16 set_tileID(uint16) property", asMETHOD(TparticleTILE, set_AStile), asCALL_THISCALL);
		engine->RegisterObjectProperty("jjPARTICLE", "jjPARTICLETILE tile", asOFFSET(Tparticle, GENERIC));

		engine->RegisterObjectType("jjCONTROLPOINT", sizeof(_controlPoint), asOBJ_REF | asOBJ_NOCOUNT);
		engine->RegisterGlobalFunction("const jjCONTROLPOINT@ get_jjControlPoints(int) property", asFUNCTION(getControlPoint), asCALL_CDECL);
		engine->RegisterObjectProperty("jjCONTROLPOINT", "const string name", asOFFSET(_controlPoint, name));
		engine->RegisterObjectProperty("jjCONTROLPOINT", "const int xTile", asOFFSET(_controlPoint, xTile));
		engine->RegisterObjectProperty("jjCONTROLPOINT", "const int yTile", asOFFSET(_controlPoint, yTile));
		engine->RegisterObjectProperty("jjCONTROLPOINT", "const int direction", asOFFSET(_controlPoint, direction));
		engine->RegisterObjectProperty("jjCONTROLPOINT", "const TEAM::Color controlTeam", asOFFSET(_controlPoint, controlTeam));
		engine->RegisterObjectMethod("jjCONTROLPOINT", "float get_xPos() const property", AS_OBJ_FLOAT_GETTER(_controlPoint, pos.x), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjCONTROLPOINT", "float get_yPos() const property", AS_OBJ_FLOAT_GETTER(_controlPoint, pos.y), asCALL_CDECL_OBJLAST);

		engine->RegisterObjectType("jjSTREAM", sizeof(jjSTREAM), asOBJ_REF);
		engine->RegisterObjectBehaviour("jjSTREAM", asBEHAVE_FACTORY, "jjSTREAM@ f()", asFUNCTION(jjSTREAM::Constructor_jjSTREAM), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjSTREAM", asBEHAVE_FACTORY, "jjSTREAM@ f(const ::string &in filename)", asFUNCTION(jjSTREAM::ConstructorFile_jjSTREAM), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjSTREAM", asBEHAVE_ADDREF, "void f()", asMETHOD(jjSTREAM, addRef), asCALL_THISCALL);
		engine->RegisterObjectBehaviour("jjSTREAM", asBEHAVE_RELEASE, "void f()", asMETHOD(jjSTREAM, release), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "jjSTREAM& opAssign(const jjSTREAM &in)", asMETHODPR(jjSTREAM, operator=, (const jjSTREAM & other), jjSTREAM&), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "uint getSize() const", asMETHOD(jjSTREAM, size), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool isEmpty() const", asMETHOD(jjSTREAM, isEmpty), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool save(const ::string &in filename) const", asMETHOD(jjSTREAM, save), asCALL_THISCALL);

		engine->RegisterObjectMethod("jjSTREAM", "void clear()", asMETHOD(jjSTREAM, clear), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool discard(uint count)", asMETHOD(jjSTREAM, discard), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool write(const ::string &in value)", asMETHODPR(jjSTREAM, write, (const std::string&), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool write(const jjSTREAM &in value)", asMETHODPR(jjSTREAM, write, (const jjSTREAM&), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool get(::string &out value, uint count = 1)", asMETHODPR(jjSTREAM, get, (std::string&, unsigned), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool get(jjSTREAM &out value, uint count = 1)", asMETHODPR(jjSTREAM, get, (jjSTREAM&, unsigned), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool getLine(::string &out value, const ::string &in delim = '\\n')", asMETHOD(jjSTREAM, getLine), asCALL_THISCALL);

		engine->RegisterObjectMethod("jjSTREAM", "bool push(bool value)", asMETHODPR(jjSTREAM, push, (bool), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool push(uint8 value)", asMETHODPR(jjSTREAM, push, (unsigned char), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool push(int8 value)", asMETHODPR(jjSTREAM, push, (signed char), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool push(uint16 value)", asMETHODPR(jjSTREAM, push, (unsigned short), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool push(int16 value)", asMETHODPR(jjSTREAM, push, (short), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool push(uint32 value)", asMETHODPR(jjSTREAM, push, (unsigned), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool push(int32 value)", asMETHODPR(jjSTREAM, push, (int), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool push(uint64 value)", asMETHODPR(jjSTREAM, push, (unsigned long long), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool push(int64 value)", asMETHODPR(jjSTREAM, push, (long long), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool push(float value)", asMETHODPR(jjSTREAM, push, (float), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool push(double value)", asMETHODPR(jjSTREAM, push, (double), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool push(const ::string &in value)", asMETHODPR(jjSTREAM, push, (const std::string&), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool push(const jjSTREAM &in value)", asMETHODPR(jjSTREAM, push, (const jjSTREAM&), bool), asCALL_THISCALL);

		engine->RegisterObjectMethod("jjSTREAM", "bool pop(bool &out value)", asMETHODPR(jjSTREAM, pop, (bool&), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool pop(uint8 &out value)", asMETHODPR(jjSTREAM, pop, (unsigned char&), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool pop(int8 &out value)", asMETHODPR(jjSTREAM, pop, (signed char&), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool pop(uint16 &out value)", asMETHODPR(jjSTREAM, pop, (unsigned short&), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool pop(int16 &out value)", asMETHODPR(jjSTREAM, pop, (short&), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool pop(uint32 &out value)", asMETHODPR(jjSTREAM, pop, (unsigned&), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool pop(int32 &out value)", asMETHODPR(jjSTREAM, pop, (int&), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool pop(uint64 &out value)", asMETHODPR(jjSTREAM, pop, (unsigned long long&), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool pop(int64 &out value)", asMETHODPR(jjSTREAM, pop, (long long&), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool pop(float &out value)", asMETHODPR(jjSTREAM, pop, (float&), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool pop(double &out value)", asMETHODPR(jjSTREAM, pop, (double&), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool pop(::string &out value)", asMETHODPR(jjSTREAM, pop, (std::string&), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool pop(jjSTREAM &out value)", asMETHODPR(jjSTREAM, pop, (jjSTREAM&), bool), asCALL_THISCALL);

		engine->RegisterGlobalFunction("bool jjSendPacket(const jjSTREAM &in packet, int toClientID = 0, uint toScriptModuleID = ::jjScriptModuleID)", asFUNCTION(sendASPacket), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool jjTakeScreenshot(const string &in filename = '')", asFUNCTION(requestScreenshot), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool jjZlibCompress(const jjSTREAM &in input, jjSTREAM &out output)", asFUNCTION(streamCompress), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool jjZlibUncompress(const jjSTREAM &in input, jjSTREAM &out output, uint size)", asFUNCTION(streamUncompress), asCALL_CDECL);
		engine->RegisterGlobalFunction("uint jjCRC32(const jjSTREAM &in input, uint crc = 0)", asFUNCTION(streamCRC32), asCALL_CDECL);

		engine->RegisterObjectType("jjRNG", sizeof(jjRNG), asOBJ_REF);
		engine->RegisterObjectBehaviour("jjRNG", asBEHAVE_FACTORY, "jjRNG@ f(uint64 seed = 5489)", asFUNCTION(jjRNG::factory), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjRNG", asBEHAVE_ADDREF, "void f()", asMETHOD(jjRNG, addRef), asCALL_THISCALL);
		engine->RegisterObjectBehaviour("jjRNG", asBEHAVE_RELEASE, "void f()", asMETHOD(jjRNG, release), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjRNG", "uint64 opCall()", asMETHOD(jjRNG, operator()), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjRNG", "jjRNG& opAssign(const jjRNG &in)", asMETHOD(jjRNG, operator=), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjRNG", "bool opEquals(const jjRNG &in) const", asMETHOD(jjRNG, operator==), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjRNG", "void seed(uint64 value = 5489)", asMETHOD(jjRNG, seed), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjRNG", "void discard(uint64 count = 1)", asMETHOD(jjRNG, discard), asCALL_THISCALL);

		engine->RegisterInterface("jjPUBLICINTERFACE");
		engine->RegisterInterfaceMethod("jjPUBLICINTERFACE", "string getVersion() const");
		engine->RegisterGlobalFunction("jjPUBLICINTERFACE@ jjGetPublicInterface(const string &in moduleName)", asFUNCTION(getPublicInterface), asCALL_CDECL);

		engine->RegisterObjectType("jjANIMFRAME", sizeof(Tframe), asOBJ_REF | asOBJ_NOCOUNT);
		engine->RegisterGlobalFunction("jjANIMFRAME @get_jjAnimFrames(uint)", asFUNCTION(getFrame), asCALL_CDECL);
		engine->RegisterObjectProperty("jjANIMFRAME", "int16 hotSpotX", asOFFSET(Tframe, hotSpotX));
		engine->RegisterObjectProperty("jjANIMFRAME", "int16 hotSpotY", asOFFSET(Tframe, hotSpotY));
		engine->RegisterObjectProperty("jjANIMFRAME", "int16 coldSpotX", asOFFSET(Tframe, coldSpotX));
		engine->RegisterObjectProperty("jjANIMFRAME", "int16 coldSpotY", asOFFSET(Tframe, coldSpotY));
		engine->RegisterObjectProperty("jjANIMFRAME", "int16 gunSpotX", asOFFSET(Tframe, gunSpotX));
		engine->RegisterObjectProperty("jjANIMFRAME", "int16 gunSpotY", asOFFSET(Tframe, gunSpotY));
		engine->RegisterObjectProperty("jjANIMFRAME", "const uint16 width", asOFFSET(Tframe, width));
		engine->RegisterObjectProperty("jjANIMFRAME", "const uint16 height", asOFFSET(Tframe, height));
		engine->RegisterObjectMethod("jjANIMFRAME", "jjANIMFRAME& opAssign(const jjANIMFRAME &in)", asMETHOD(Tframe, operator=), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjANIMFRAME", "bool get_transparent() const property", asMETHOD(Tframe, getImageTransparency), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjANIMFRAME", "bool set_transparent(bool) property", asMETHOD(Tframe, setImageTransparency), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjANIMFRAME", "bool doesCollide(int xPos, int yPos, int direction, const jjANIMFRAME@ frame2, int xPos2, int yPos2, int direction2, bool always = false) const", asFUNCTION(doesCollideBase), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectType("jjANIMATION", sizeof(_AnimData), asOBJ_REF | asOBJ_NOCOUNT);
		engine->RegisterGlobalFunction("jjANIMATION @get_jjAnimations(uint) property", asFUNCTION(getAnim), asCALL_CDECL);
		engine->RegisterObjectProperty("jjANIMATION", "uint16 frameCount", asOFFSET(_AnimData, numFrames));
		engine->RegisterObjectProperty("jjANIMATION", "int16 fps", asOFFSET(_AnimData, fps));
		engine->RegisterObjectMethod("jjANIMATION", "uint get_firstFrame() const property", asFUNCTION(getAnimFirstFrame), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjANIMATION", "uint set_firstFrame(uint) property", asFUNCTION(setAnimFirstFrame), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjANIMATION", "uint opImplConv() const", asFUNCTION(getAnimFirstFrame), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjANIMATION", "jjANIMATION& opAssign(const jjANIMATION &in)", asMETHOD(_AnimData, operator=), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjANIMATION", "bool save(const ::string &in filename, const jjPAL &in palette = jjPalette) const", asFUNCTION(exportAnimationToGif), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjANIMATION", "bool load(const ::string &in filename, int hotSpotX, int hotSpotY, int coldSpotYOffset = 0, int firstFrameToOverwrite = -1)", asFUNCTION(importAnimationFromGif), asCALL_CDECL_OBJFIRST);

		engine->RegisterObjectType("jjANIMSET", sizeof(int), asOBJ_REF | asOBJ_NOCOUNT);
		engine->RegisterGlobalFunction("jjANIMSET @get_jjAnimSets(uint) property", asFUNCTION(getAnimSet), asCALL_CDECL);
		engine->RegisterObjectProperty("jjANIMSET", "uint firstAnim", 0);
		engine->RegisterObjectMethod("jjANIMSET", "uint opImplConv() const", asFUNCTION(convertAnimSetToUint), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjANIMSET", "jjANIMSET @load(uint fileSetID = 2048, const string &in filename = '', int firstAnimToOverwrite = -1, int firstFrameToOverwrite = -1)", asFUNCTION(loadAnimSet), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjANIMSET", "jjANIMSET @allocate(const array<uint> &in frameCounts)", asFUNCTION(loadEmptyAnimSet), asCALL_CDECL_OBJFIRST);

		engine->RegisterObjectMethod("jjCANVAS", "void drawString(int xPixel, int yPixel, const ::string &in text, const jjANIMATION &in animation, STRING::Mode mode = STRING::NORMAL, uint8 param = 0)", asMETHOD(TgameCanvas, DrawTextBasic), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjCANVAS", "void drawString(int xPixel, int yPixel, const ::string &in text, const jjANIMATION &in animation, const jjTEXTAPPEARANCE &in appearance, uint8 param1 = 0, SPRITE::Mode spriteMode = SPRITE::PALSHIFT, uint8 param2 = 0)", asMETHOD(TgameCanvas, DrawTextExt), asCALL_THISCALL);
		engine->RegisterGlobalFunction("void jjDrawString(float xPixel, float yPixel, const ::string &in text, const jjANIMATION &in animation, STRING::Mode mode = STRING::NORMAL, uint8 param = 0, int8 layerZ = 4, uint8 layerXY = 4, int8 playerID = -1)", asFUNCTION(externalAddTextSprite), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjDrawString(float xPixel, float yPixel, const ::string &in text, const jjANIMATION &in animation, const jjTEXTAPPEARANCE &in appearance, uint8 param1 = 0, SPRITE::Mode spriteMode = SPRITE::PALSHIFT, uint8 param2 = 0, int8 layerZ = 4, uint8 layerXY = 4, int8 playerID = -1)", asFUNCTION(externalAddTextSpriteExt), asCALL_CDECL);
		engine->RegisterGlobalFunction("int jjGetStringWidth(const ::string &in text, const jjANIMATION &in animation, const jjTEXTAPPEARANCE &in style)", asFUNCTION(externalGetTextWidth), asCALL_CDECL);

		engine->RegisterObjectType("jjLAYER", sizeof(Tlayer), asOBJ_REF);

		engine->RegisterObjectType("jjPIXELMAP", sizeof(PixelMap), asOBJ_REF);
		engine->RegisterObjectBehaviour("jjPIXELMAP", asBEHAVE_FACTORY, "jjPIXELMAP@ f(uint16 tileID = 0)", asFUNCTION(PixelMap::factoryTile), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjPIXELMAP", asBEHAVE_FACTORY, "jjPIXELMAP@ f(uint width, uint height)", asFUNCTION(PixelMap::factorySize), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjPIXELMAP", asBEHAVE_FACTORY, "jjPIXELMAP@ f(const jjANIMFRAME@ animFrame)", asFUNCTION(PixelMap::factoryFrame), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjPIXELMAP", asBEHAVE_FACTORY, "jjPIXELMAP@ f(uint left, uint top, uint width, uint height, uint layer = 4)", asFUNCTION(PixelMap::factoryLayer), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjPIXELMAP", asBEHAVE_FACTORY, "jjPIXELMAP@ f(uint left, uint top, uint width, uint height, const jjLAYER &in layer)", asFUNCTION(PixelMap::factoryLayerObject), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjPIXELMAP", asBEHAVE_FACTORY, "jjPIXELMAP@ f(TEXTURE::Texture texture)", asFUNCTION(PixelMap::factoryTexture), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjPIXELMAP", asBEHAVE_FACTORY, "jjPIXELMAP@ f(const ::string &in filename, const jjPAL &in palette = jjPalette, uint8 threshold = 1)", asFUNCTION(PixelMap::factoryFilename), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjPIXELMAP", asBEHAVE_ADDREF, "void f()", asMETHOD(PixelMap, addRef), asCALL_THISCALL);
		engine->RegisterObjectBehaviour("jjPIXELMAP", asBEHAVE_RELEASE, "void f()", asMETHOD(PixelMap, release), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPIXELMAP", "uint8& opIndex(uint, uint)", asMETHOD(PixelMap, findPixel), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPIXELMAP", "const uint8& opIndex(uint, uint) const", asMETHOD(PixelMap, findPixel), asCALL_THISCALL);
		engine->RegisterObjectProperty("jjPIXELMAP", "const uint width", asOFFSET(PixelMap, width));
		engine->RegisterObjectProperty("jjPIXELMAP", "const uint height", asOFFSET(PixelMap, height));
		engine->RegisterObjectMethod("jjPIXELMAP", "bool save(uint16 tileID, bool hFlip = false) const", asMETHOD(PixelMap, saveToTile), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPIXELMAP", "bool save(jjANIMFRAME@ frame) const", asMETHOD(PixelMap, saveToFrame), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPIXELMAP", "bool save(const ::string &in filename, const jjPAL &in palette = jjPalette) const", asMETHOD(PixelMap, saveToFile), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPIXELMAP", "bool makeTexture(jjLAYER@ layer = null)", asMETHOD(PixelMap, saveToTexture), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPIXELMAP", "jjPIXELMAP& crop(uint, uint, uint, uint)", asMETHOD(PixelMap, crop), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPIXELMAP", "jjPIXELMAP& addBorders(int, int, int, int, uint8 = 0)", asMETHOD(PixelMap, addBorders), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPIXELMAP", "jjPIXELMAP& flip(SPRITE::Direction)", asMETHOD(PixelMap, flip), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPIXELMAP", "jjPIXELMAP& rotate()", asMETHOD(PixelMap, rotate), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPIXELMAP", "jjPIXELMAP& recolor(const array<uint8> &in)", asMETHOD(PixelMap, recolor), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPIXELMAP", "jjPIXELMAP& resize(uint, uint)", asMETHOD(PixelMap, resize), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPIXELMAP", "jjPIXELMAP& trim(uint &out, uint &out, uint &out, uint &out, uint8 = 0)", asMETHOD(PixelMap, trim), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPIXELMAP", "jjPIXELMAP& trim(uint8 = 0)", asMETHOD(PixelMap, trimBasic), asCALL_THISCALL);

		engine->RegisterObjectMethod("jjANIMSET", "jjANIMSET @load(const jjPIXELMAP &in, uint frameWidth, uint frameHeight, uint frameSpacingX = 0, uint frameSpacingY = 0, uint startX = 0, uint startY = 0, const array<int> &in coldSpotYOffsets = array<int>(), int firstAnimToOverwrite = -1, int firstFrameToOverwrite = -1)", asFUNCTION(importSpriteSheetToAnimSet), asCALL_CDECL_OBJFIRST);

		engine->RegisterObjectType("jjMASKMAP", sizeof(MaskMap), asOBJ_REF);
		engine->RegisterObjectBehaviour("jjMASKMAP", asBEHAVE_FACTORY, "jjMASKMAP@ f(bool filled = false)", asFUNCTION(MaskMap::factoryBool), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjMASKMAP", asBEHAVE_FACTORY, "jjMASKMAP@ f(uint16 tileID)", asFUNCTION(MaskMap::factoryTile), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjMASKMAP", asBEHAVE_ADDREF, "void f()", asMETHOD(MaskMap, addRef), asCALL_THISCALL);
		engine->RegisterObjectBehaviour("jjMASKMAP", asBEHAVE_RELEASE, "void f()", asMETHOD(MaskMap, release), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjMASKMAP", "bool& opIndex(uint, uint)", asMETHOD(MaskMap, findPixel), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjMASKMAP", "const bool& opIndex(uint, uint) const", asMETHOD(MaskMap, findPixel), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjMASKMAP", "bool save(uint16 tileID, bool hFlip = false) const", asMETHOD(MaskMap, saveToTile), asCALL_THISCALL);

		engine->RegisterObjectBehaviour("jjLAYER", asBEHAVE_FACTORY, "jjLAYER@ f(uint layerWidth, uint layerHeight)", asFUNCTION(Tlayer::SizeConstructor), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjLAYER", asBEHAVE_FACTORY, "jjLAYER@ f(const jjLAYER &in layer)", asFUNCTION(Tlayer::CopyConstructor), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjLAYER", asBEHAVE_ADDREF, "void f()", asMETHOD(Tlayer, AddRef), asCALL_THISCALL);
		engine->RegisterObjectBehaviour("jjLAYER", asBEHAVE_RELEASE, "void f()", asMETHOD(Tlayer, Release), asCALL_THISCALL);
		engine->RegisterGlobalFunction("jjLAYER @get_jjLayers(int)", asFUNCTION(getDefaultLayer), asCALL_CDECL);
		engine->RegisterObjectProperty("jjLAYER", "const int width", asOFFSET(Tlayer, Width));
		engine->RegisterObjectProperty("jjLAYER", "const int widthReal", asOFFSET(Tlayer, RealWidth));
		engine->RegisterObjectProperty("jjLAYER", "const int widthRounded", asOFFSET(Tlayer, RoundedWidth));
		engine->RegisterObjectProperty("jjLAYER", "const int height", asOFFSET(Tlayer, Height));
		REGISTER_FLOAT_PROPERTY("jjLAYER", "xSpeed", Tlayer, SpeedX);
		REGISTER_FLOAT_PROPERTY("jjLAYER", "ySpeed", Tlayer, SpeedY);
		REGISTER_FLOAT_PROPERTY("jjLAYER", "xAutoSpeed", Tlayer, AutoSpeedX);
		REGISTER_FLOAT_PROPERTY("jjLAYER", "yAutoSpeed", Tlayer, AutoSpeedY);
		REGISTER_FLOAT_PROPERTY("jjLAYER", "xOffset", Tlayer, XOffset);
		REGISTER_FLOAT_PROPERTY("jjLAYER", "yOffset", Tlayer, YOffset);
		REGISTER_FLOAT_PROPERTY("jjLAYER", "xInnerSpeed", Tlayer, InnerSpeedX);
		REGISTER_FLOAT_PROPERTY("jjLAYER", "yInnerSpeed", Tlayer, InnerSpeedY);
		REGISTER_FLOAT_PROPERTY("jjLAYER", "xInnerAutoSpeed", Tlayer, InnerAutoSpeedX);
		REGISTER_FLOAT_PROPERTY("jjLAYER", "yInnerAutoSpeed", Tlayer, InnerAutoSpeedY);
		engine->RegisterObjectMethod("jjLAYER", "SPRITE::Mode get_spriteMode() const property", asMETHOD(Tlayer, GetSpriteMode), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYER", "SPRITE::Mode set_spriteMode(SPRITE::Mode) property", asMETHOD(Tlayer, SetSpriteMode), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYER", "uint8 get_spriteParam() const property", asMETHOD(Tlayer, GetSpriteParam), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYER", "uint8 set_spriteParam(uint8) property", asMETHOD(Tlayer, SetSpriteParam), asCALL_THISCALL);

		engine->RegisterObjectMethod("jjLAYER", "void setXSpeed(float newspeed, bool newSpeedIsAnAutoSpeed)", asMETHOD(Tlayer, SetXSpeedSeamlessly), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYER", "void setYSpeed(float newspeed, bool newSpeedIsAnAutoSpeed)", asMETHOD(Tlayer, SetYSpeedSeamlessly), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYER", "float getXPosition(const jjPLAYER &in play) const", asMETHOD(Tlayer, GetXPositionExternal), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYER", "float getYPosition(const jjPLAYER &in play) const", asMETHOD(Tlayer, GetYPositionExternal), asCALL_THISCALL);

		engine->RegisterObjectType("jjLAYERWARPHORIZON", 0, asOBJ_REF | asOBJ_NOHANDLE);
		engine->RegisterObjectProperty("jjLAYERWARPHORIZON", "float fadePositionX", asOFFSET(Tlayer, WARPHORIZON.FadePosition[0]));
		engine->RegisterObjectProperty("jjLAYERWARPHORIZON", "float fadePositionY", asOFFSET(Tlayer, WARPHORIZON.FadePosition[1]));
		engine->RegisterObjectMethod("jjLAYERWARPHORIZON", "jjPALCOLOR getFadeColor() const", asMETHOD(Tlayer, GetFadeColor), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYERWARPHORIZON", "void setFadeColor(jjPALCOLOR)", asMETHOD(Tlayer, SetFadeColor), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYERWARPHORIZON", "bool get_stars() const property", asMETHOD(Tlayer, GetStars), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYERWARPHORIZON", "bool set_stars(bool) property", asMETHOD(Tlayer, SetStars), asCALL_THISCALL);
		engine->RegisterObjectProperty("jjLAYERWARPHORIZON", "bool fade", asOFFSET(Tlayer, WARPHORIZON.Fade));
		engine->RegisterObjectProperty("jjLAYER", "jjLAYERWARPHORIZON warpHorizon", asOFFSET(Tlayer, WARPHORIZON));
		engine->RegisterObjectType("jjLAYERTUNNEL", 0, asOBJ_REF | asOBJ_NOHANDLE);
		engine->RegisterObjectProperty("jjLAYERTUNNEL", "float fadePositionX", asOFFSET(Tlayer, TUNNEL.FadePosition[0]));
		engine->RegisterObjectProperty("jjLAYERTUNNEL", "float fadePositionY", asOFFSET(Tlayer, TUNNEL.FadePosition[1]));
		engine->RegisterObjectMethod("jjLAYERTUNNEL", "jjPALCOLOR getFadeColor() const", asMETHOD(Tlayer, GetFadeColor), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYERTUNNEL", "void setFadeColor(jjPALCOLOR)", asMETHOD(Tlayer, SetFadeColor), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYERTUNNEL", "bool get_spiral() const property", asMETHOD(Tlayer, GetStars), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYERTUNNEL", "bool set_spiral(bool) property", asMETHOD(Tlayer, SetStars), asCALL_THISCALL);
		engine->RegisterObjectProperty("jjLAYERTUNNEL", "bool fade", asOFFSET(Tlayer, TUNNEL.Fade));
		engine->RegisterObjectProperty("jjLAYER", "jjLAYERTUNNEL tunnel", asOFFSET(Tlayer, TUNNEL));
		engine->RegisterObjectType("jjLAYERMENU", 0, asOBJ_REF | asOBJ_NOHANDLE);
		engine->RegisterObjectProperty("jjLAYERMENU", "float pivotX", asOFFSET(Tlayer, MENU.Pivot[0]));
		engine->RegisterObjectProperty("jjLAYERMENU", "float pivotY", asOFFSET(Tlayer, MENU.Pivot[1]));
		engine->RegisterObjectMethod("jjLAYERMENU", "uint8 get_palrow16() const property", asMETHOD(Tlayer, GetFadeComponent<0>), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYERMENU", "uint8 get_palrow32() const property", asMETHOD(Tlayer, GetFadeComponent<1>), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYERMENU", "uint8 get_palrow256() const property", asMETHOD(Tlayer, GetFadeComponent<2>), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYERMENU", "uint8 set_palrow16(uint8) property", asMETHOD(Tlayer, SetFadeComponent<0>), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYERMENU", "uint8 set_palrow32(uint8) property", asMETHOD(Tlayer, SetFadeComponent<1>), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYERMENU", "uint8 set_palrow256(uint8) property", asMETHOD(Tlayer, SetFadeComponent<2>), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYERMENU", "bool get_lightToDark() const property", asMETHOD(Tlayer, GetStars), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYERMENU", "bool set_lightToDark(bool) property", asMETHOD(Tlayer, SetStars), asCALL_THISCALL);
		engine->RegisterObjectProperty("jjLAYER", "jjLAYERMENU menu", asOFFSET(Tlayer, MENU));
		engine->RegisterObjectType("jjLAYERTILEMENU", 0, asOBJ_REF | asOBJ_NOHANDLE);
		engine->RegisterObjectProperty("jjLAYERTILEMENU", "float pivotX", asOFFSET(Tlayer, TILEMENU.Pivot[0]));
		engine->RegisterObjectProperty("jjLAYERTILEMENU", "float pivotY", asOFFSET(Tlayer, TILEMENU.Pivot[1]));
		engine->RegisterObjectMethod("jjLAYERTILEMENU", "bool get_fullSize() const property", asMETHOD(Tlayer, GetStars), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYERTILEMENU", "bool set_fullSize(bool) property", asMETHOD(Tlayer, SetStars), asCALL_THISCALL);
		engine->RegisterObjectProperty("jjLAYER", "jjLAYERTILEMENU tileMenu", asOFFSET(Tlayer, TILEMENU));
		engine->RegisterObjectType("jjLAYERWAVE", 0, asOBJ_REF | asOBJ_NOHANDLE);
		engine->RegisterObjectProperty("jjLAYERWAVE", "float amplitudeX", asOFFSET(Tlayer, WAVE.Amplitude[0]));
		engine->RegisterObjectProperty("jjLAYERWAVE", "float amplitudeY", asOFFSET(Tlayer, WAVE.Amplitude[1]));
		engine->RegisterObjectMethod("jjLAYERWAVE", "uint8 get_wavelengthX() const property", asMETHOD(Tlayer, GetFadeComponent<0>), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYERWAVE", "uint8 get_wavelengthY() const property", asMETHOD(Tlayer, GetFadeComponent<1>), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYERWAVE", "int8 get_waveSpeed() const property", asMETHOD(Tlayer, GetFadeComponent<2>), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYERWAVE", "uint8 set_wavelengthX(uint8) property", asMETHOD(Tlayer, SetFadeComponent<0>), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYERWAVE", "uint8 set_wavelengthY(uint8) property", asMETHOD(Tlayer, SetFadeComponent<1>), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYERWAVE", "int8 set_waveSpeed(int8) property", asMETHOD(Tlayer, SetFadeComponent<2>), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYERWAVE", "bool get_distortionAngle() const property", asMETHOD(Tlayer, GetStars), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYERWAVE", "bool set_distortionAngle(bool) property", asMETHOD(Tlayer, SetStars), asCALL_THISCALL);
		engine->RegisterObjectProperty("jjLAYER", "jjLAYERWAVE wave", asOFFSET(Tlayer, WAVE));
		engine->RegisterObjectType("jjLAYERCYLINDER", 0, asOBJ_REF | asOBJ_NOHANDLE);
		engine->RegisterObjectProperty("jjLAYERCYLINDER", "float fadePositionX", asOFFSET(Tlayer, CYLINDER.FadePosition[0]));
		engine->RegisterObjectProperty("jjLAYERCYLINDER", "float fadePositionY", asOFFSET(Tlayer, CYLINDER.FadePosition[1]));
		engine->RegisterObjectMethod("jjLAYERCYLINDER", "jjPALCOLOR getFadeColor() const", asMETHOD(Tlayer, GetFadeColor), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYERCYLINDER", "void setFadeColor(jjPALCOLOR)", asMETHOD(Tlayer, SetFadeColor), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYERCYLINDER", "bool get_halfSize() const property", asMETHOD(Tlayer, GetStars), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYERCYLINDER", "bool set_halfSize(bool) property", asMETHOD(Tlayer, SetStars), asCALL_THISCALL);
		engine->RegisterObjectProperty("jjLAYERCYLINDER", "bool fade", asOFFSET(Tlayer, CYLINDER.Fade));
		engine->RegisterObjectProperty("jjLAYER", "jjLAYERCYLINDER cylinder", asOFFSET(Tlayer, CYLINDER));
		engine->RegisterObjectType("jjLAYERREFLECTION", 0, asOBJ_REF | asOBJ_NOHANDLE);
		engine->RegisterObjectProperty("jjLAYERREFLECTION", "float fadePositionX", asOFFSET(Tlayer, REFLECTION.FadePositionX));
		engine->RegisterObjectProperty("jjLAYERREFLECTION", "float top", asOFFSET(Tlayer, REFLECTION.Top));
		engine->RegisterObjectMethod("jjLAYERREFLECTION", "uint8 get_tintOpacity() const property", asMETHOD(Tlayer, GetFadeComponent<0>), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYERREFLECTION", "uint8 get_distance() const property", asMETHOD(Tlayer, GetFadeComponent<1>), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYERREFLECTION", "uint8 get_distortion() const property", asMETHOD(Tlayer, GetFadeComponent<2>), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYERREFLECTION", "uint8 set_tintOpacity(uint8) property", asMETHOD(Tlayer, SetFadeComponent<0>), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYERREFLECTION", "uint8 set_distance(uint8) property", asMETHOD(Tlayer, SetFadeComponent<1>), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYERREFLECTION", "uint8 set_distortion(uint8) property", asMETHOD(Tlayer, SetFadeComponent<2>), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYERREFLECTION", "bool get_blur() const property", asMETHOD(Tlayer, GetStars), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYERREFLECTION", "bool set_blur(bool) property", asMETHOD(Tlayer, SetStars), asCALL_THISCALL);
		engine->RegisterObjectProperty("jjLAYERREFLECTION", "uint8 tintColor", asOFFSET(Tlayer, REFLECTION.OverlayColor));
		engine->RegisterObjectProperty("jjLAYER", "jjLAYERREFLECTION reflection", asOFFSET(Tlayer, REFLECTION));

		engine->RegisterObjectMethod("jjLAYER", "TEXTURE::Style get_textureStyle() const property", asMETHOD(Tlayer, GetTextureMode), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYER", "void set_textureStyle(TEXTURE::Style) property", asMETHOD(Tlayer, SetTextureMode), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYER", "TEXTURE::Texture get_texture() const property", asMETHOD(Tlayer, GetTexture), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYER", "void set_texture(TEXTURE::Texture) property", asMETHOD(Tlayer, SetTexture), asCALL_THISCALL);

		engine->RegisterObjectProperty("jjLAYER", "int rotationAngle", asOFFSET(Tlayer, RotationAngle));
		engine->RegisterObjectProperty("jjLAYER", "int rotationRadiusMultiplier", asOFFSET(Tlayer, RotationRadiusMultiplier));
		engine->RegisterObjectProperty("jjLAYER", "bool tileHeight", asOFFSET(Tlayer, IsHeightTiled));
		engine->RegisterObjectProperty("jjLAYER", "bool tileWidth", asOFFSET(Tlayer, IsWidthTiled));
		engine->RegisterObjectProperty("jjLAYER", "bool limitVisibleRegion", asOFFSET(Tlayer, LimitVisibleRegion));
		engine->RegisterObjectProperty("jjLAYER", "const bool hasTileMap", asOFFSET(Tlayer, IsUsable));
		engine->RegisterObjectProperty("jjLAYER", "bool hasTiles", asOFFSET(Tlayer, IsUsed));
		engine->RegisterGlobalFunction("array<jjLAYER@>@ jjLayerOrderGet()", asFUNCTION(getLayerOrderExternal), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool jjLayerOrderSet(const array<jjLAYER@> &in order)", asFUNCTION(setLayerOrderExternal), asCALL_CDECL);
		engine->RegisterGlobalFunction("array<jjLAYER@>@ jjLayersFromLevel(const string &in filename, const array<uint> &in layerIDs, int tileIDAdjustmentFactor = 0)", asFUNCTION(layersFromLevel), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool jjTilesFromTileset(const string &in filename, uint firstTileID, uint tileCount, const array<uint8>@ paletteColorMapping = null)", asFUNCTION(tilesFromTileset), asCALL_CDECL);

		engine->SetDefaultNamespace("LAYERSPEEDMODEL");
		engine->RegisterEnum("LayerSpeedModel");
		engine->RegisterEnumValue("LayerSpeedModel", "NORMAL", (int)Tlayer::SpeedMode::Normal);
		engine->RegisterEnumValue("LayerSpeedModel", "LAYER8", (int)Tlayer::SpeedMode::Layer8);
		engine->RegisterEnumValue("LayerSpeedModel", "BOTHSPEEDS", (int)Tlayer::SpeedMode::BothRelativeAndAutoSpeeds);
		engine->RegisterEnumValue("LayerSpeedModel", "FROMSTART", (int)Tlayer::SpeedMode::BothSpeedsButStuckToTopLeftCorner);
		engine->RegisterEnumValue("LayerSpeedModel", "FITLEVEL", (int)Tlayer::SpeedMode::FitToLevelAndWindowSize);
		engine->RegisterEnumValue("LayerSpeedModel", "SPEEDMULTIPLIERS", (int)Tlayer::SpeedMode::SpeedsAsPercentagesOfWindowSize);
		engine->SetDefaultNamespace("");
		engine->RegisterObjectProperty("jjLAYER", "LAYERSPEEDMODEL::LayerSpeedModel xSpeedModel", asOFFSET(Tlayer, SpeedModeX));
		engine->RegisterObjectProperty("jjLAYER", "LAYERSPEEDMODEL::LayerSpeedModel ySpeedModel", asOFFSET(Tlayer, SpeedModeY));

		engine->SetDefaultNamespace("SURFACE");
		engine->RegisterEnum("Surface");
		engine->RegisterEnumValue("Surface", "UNTEXTURED", Tlayer::TextureSurfaceStyles::Untextured);
		engine->RegisterEnumValue("Surface", "LEGACY", Tlayer::TextureSurfaceStyles::Legacy);
		engine->RegisterEnumValue("Surface", "FULLSCREEN", Tlayer::TextureSurfaceStyles::FullScreen);
		engine->RegisterEnumValue("Surface", "INNERWINDOW", Tlayer::TextureSurfaceStyles::InnerWindow);
		engine->RegisterEnumValue("Surface", "INNERLAYER", Tlayer::TextureSurfaceStyles::InnerLayer);
		engine->SetDefaultNamespace("");
		engine->RegisterObjectMethod("jjLAYER", "SURFACE::Surface get_textureSurface() const", asMETHOD(Tlayer, GetTextureSurface), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjLAYER", "void set_textureSurface(SURFACE::Surface)", asMETHOD(Tlayer, SetTextureSurface), asCALL_THISCALL);

		engine->RegisterObjectType("jjTILE", sizeof(jjTILE), asOBJ_REF);
		engine->RegisterObjectBehaviour("jjTILE", asBEHAVE_ADDREF, "void f()", asMETHOD(jjTILE, addRef), asCALL_THISCALL);
		engine->RegisterObjectBehaviour("jjTILE", asBEHAVE_RELEASE, "void f()", asMETHOD(jjTILE, release), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjTILE", "array<uint16>@ getFrames() const", asMETHOD(jjTILE, getFrames), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjTILE", "bool setFrames(const array<uint16> &in frames, bool pingPong = false, uint16 wait = 0, uint16 randomWait = 0, uint16 pingPongWait = 0)", asMETHOD(jjTILE, setFrames), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjTILE", "uint8 get_fps() const property", asMETHOD(jjTILE, getFPS), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjTILE", "void set_fps(uint8) property", asMETHOD(jjTILE, setFPS), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjTILE", "uint16 get_tileID() const property", asMETHOD(jjTILE, getTileID), asCALL_THISCALL);

		engine->RegisterGlobalFunction("const jjTILE@ get_jjTiles(uint16) property", asFUNCTION(jjTILE::getTile), asCALL_CDECL);
		engine->RegisterGlobalFunction("jjTILE@ get_jjAnimatedTiles(uint16) property", asFUNCTION(jjTILE::getAnimatedTile), asCALL_CDECL);

		engine->RegisterGlobalFunction("uint16 jjGetStaticTile(uint16 tileID)", asFUNCTION(getStaticTile), asCALL_CDECL);
		engine->RegisterGlobalFunction("uint16 jjTileGet(uint8 layer, int xTile, int yTile)", asFUNCTION(getTileAt), asCALL_CDECL);
		engine->RegisterGlobalFunction("uint16 jjTileSet(uint8 layer, int xTile, int yTile, uint16 newTile)", asFUNCTION(setTileAt), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjGenerateSettableTileArea(uint8 layer, int xTile, int yTile, int width, int height)", asFUNCTION(generateSettableTileArea), asCALL_CDECL);
		engine->RegisterObjectMethod("jjLAYER", "uint16 tileGet(int xTile, int yTile) const", asFUNCTION(getTileAtInLayer), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYER", "uint16 tileSet(int xTile, int yTile, uint16 newTile)", asFUNCTION(setTileAtInLayer), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYER", "void generateSettableTileArea(int xTile, int yTile, int width, int height)", asFUNCTION(generateSettableTileAreaInLayer), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjLAYER", "void generateSettableTileArea()", asFUNCTION(generateSettableLayer), asCALL_CDECL_OBJFIRST);

		engine->RegisterGlobalFunction("bool jjMaskedPixel(int xPixel,int yPixel)", asFUNCTION(checkPixel), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool jjMaskedPixel(int xPixel,int yPixel,uint8 layer)", asFUNCTION(checkPixelLayer), asCALL_CDECL);
		engine->RegisterObjectMethod("jjLAYER", "bool maskedPixel(int xPixel,int yPixel) const", asMETHOD(Tlayer, CheckPixel), asCALL_THISCALL);
		engine->RegisterGlobalFunction("bool jjMaskedHLine(int xPixel,int lineLength,int yPixel)", asFUNCTION(MyCheckHLine), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool jjMaskedHLine(int xPixel,int lineLength,int yPixel,uint8 layer)", asFUNCTION(checkHLineLayer), asCALL_CDECL);
		engine->RegisterObjectMethod("jjLAYER", "bool maskedHLine(int xPixel,int lineLength,int yPixel) const", asMETHOD(Tlayer, CheckHLine), asCALL_THISCALL);
		engine->RegisterGlobalFunction("bool jjMaskedVLine(int xPixel,int yPixel,int lineLength)", asFUNCTION(BoolCheckVLine), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool jjMaskedVLine(int xPixel,int yPixel,int lineLength,uint8 layer)", asFUNCTION(boolCheckVLineLayer), asCALL_CDECL);
		engine->RegisterObjectMethod("jjLAYER", "bool maskedVLine(int xPixel,int yPixel,int lineLength) const", asMETHOD(Tlayer, CheckVLineBool), asCALL_THISCALL);
		engine->RegisterGlobalFunction("int jjMaskedTopVLine(int xPixel,int yPixel,int lineLength)", asFUNCTION(MyCheckVLine), asCALL_CDECL);
		engine->RegisterGlobalFunction("int jjMaskedTopVLine(int xPixel,int yPixel,int lineLength,uint8 layer)", asFUNCTION(checkVLineLayer), asCALL_CDECL);
		engine->RegisterObjectMethod("jjLAYER", "int maskedTopVLine(int xPixel,int yPixel,int lineLength) const", asMETHOD(Tlayer, CheckVLine), asCALL_THISCALL);
		engine->RegisterGlobalProperty("uint8 jjEventAtLastMaskedPixel", tileAttr);

		engine->RegisterGlobalFunction("void jjSetModPosition(int order, int row, bool reset)", asFUNCTION(setModPosition), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjSlideModChannelVolume(int channel, float volume, int milliseconds)", asFUNCTION(slideModChannelVolume), asCALL_CDECL);
		engine->RegisterGlobalFunction("int jjGetModOrder()", asFUNCTION(getModOrder), asCALL_CDECL);
		engine->RegisterGlobalFunction("int jjGetModRow()", asFUNCTION(getModRow), asCALL_CDECL);
		engine->RegisterGlobalFunction("int jjGetModTempo()", asFUNCTION(getModTempo), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjSetModTempo(uint8 tempo)", asFUNCTION(setModTempo), asCALL_CDECL);
		engine->RegisterGlobalFunction("int jjGetModSpeed()", asFUNCTION(getModSpeed), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjSetModSpeed(uint8 speed)", asFUNCTION(setModSpeed), asCALL_CDECL);

		engine->RegisterObjectType("jjPLAYERDRAW", sizeof(DrawPlayerElements), asOBJ_REF | asOBJ_NOCOUNT);
		engine->RegisterObjectProperty("jjPLAYERDRAW", "bool name", asOFFSET(DrawPlayerElements, Name));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "bool sprite", asOFFSET(DrawPlayerElements, Sprite));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "bool sugarRush", asOFFSET(DrawPlayerElements, SugarRush));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "bool gunFlash", asOFFSET(DrawPlayerElements, Flare));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "bool invincibility", asOFFSET(DrawPlayerElements, Invincibility));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "bool trail", asOFFSET(DrawPlayerElements, Trail));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "bool morphingExplosions", asOFFSET(DrawPlayerElements, Morph));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "bool airboardBouncingMotion", asOFFSET(DrawPlayerElements, AirboardOffset));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "bool airboardPuff", asOFFSET(DrawPlayerElements, AirboardPuff));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "SPRITE::Mode spriteMode", asOFFSET(DrawPlayerElements, SpriteMode));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "uint8 spriteParam", asOFFSET(DrawPlayerElements, SpriteParam));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "LIGHT::Type lightType", asOFFSET(DrawPlayerElements, LightType));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "int8 light", asOFFSET(DrawPlayerElements, LightIntensity));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "int layer", asOFFSET(DrawPlayerElements, Layer));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "uint curFrame", asOFFSET(DrawPlayerElements, CurFrame));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "int angle", asOFFSET(DrawPlayerElements, Angle));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "float xOffset", asOFFSET(DrawPlayerElements, XOffset));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "float yOffset", asOFFSET(DrawPlayerElements, YOffset));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "float xScale", asOFFSET(DrawPlayerElements, XScale));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "float yScale", asOFFSET(DrawPlayerElements, YScale));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "TEAM::Color flag", asOFFSET(DrawPlayerElements, FlagTeam));
		engine->RegisterObjectMethod("jjPLAYERDRAW", "bool get_shield(SHIELD::Shield) const property", asMETHOD(DrawPlayerElements, getShield1Index), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYERDRAW", "bool set_shield(SHIELD::Shield, bool) property", asMETHOD(DrawPlayerElements, setShield1Index), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYERDRAW", "jjPLAYER@ get_player() const property", asMETHOD(DrawPlayerElements, getPlayer), asCALL_THISCALL);*/

		engine->SetDefaultNamespace("STATE");
		engine->RegisterEnumValue("State", "START", sSTART);
		engine->RegisterEnumValue("State", "SLEEP", sSLEEP);
		engine->RegisterEnumValue("State", "WAKE", sWAKE);
		engine->RegisterEnumValue("State", "KILL", sKILL);
		engine->RegisterEnumValue("State", "DEACTIVATE", sDEACTIVATE);
		engine->RegisterEnumValue("State", "WALK", sWALK);
		engine->RegisterEnumValue("State", "JUMP", sJUMP);
		engine->RegisterEnumValue("State", "FIRE", sFIRE);
		engine->RegisterEnumValue("State", "FLY", sFLY);
		engine->RegisterEnumValue("State", "BOUNCE", sBOUNCE);
		engine->RegisterEnumValue("State", "EXPLODE", sEXPLODE);
		engine->RegisterEnumValue("State", "ROCKETFLY", sROCKETFLY);
		engine->RegisterEnumValue("State", "STILL", sSTILL);
		engine->RegisterEnumValue("State", "FLOAT", sFLOAT);
		engine->RegisterEnumValue("State", "HIT", sHIT);
		engine->RegisterEnumValue("State", "SPRING", sSPRING);
		engine->RegisterEnumValue("State", "ACTION", sACTION);
		engine->RegisterEnumValue("State", "DONE", sDONE);
		engine->RegisterEnumValue("State", "PUSH", sPUSH);
		engine->RegisterEnumValue("State", "FALL", sFALL);
		engine->RegisterEnumValue("State", "FLOATFALL", sFLOATFALL);
		engine->RegisterEnumValue("State", "CIRCLE", sCIRCLE);
		engine->RegisterEnumValue("State", "ATTACK", sATTACK);
		engine->RegisterEnumValue("State", "FREEZE", sFREEZE);
		engine->RegisterEnumValue("State", "FADEIN", sFADEIN);
		engine->RegisterEnumValue("State", "FADEOUT", sFADEOUT);
		engine->RegisterEnumValue("State", "HIDE", sHIDE);
		engine->RegisterEnumValue("State", "TURN", sTURN);
		engine->RegisterEnumValue("State", "IDLE", sIDLE);
		engine->RegisterEnumValue("State", "EXTRA", sEXTRA);
		engine->RegisterEnumValue("State", "STOP", sSTOP);
		engine->RegisterEnumValue("State", "WAIT", sWAIT);
		engine->RegisterEnumValue("State", "LAND", sLAND);
		engine->RegisterEnumValue("State", "DELAYEDSTART", sDELAYEDSTART);
		engine->RegisterEnumValue("State", "ROTATE", sROTATE);
		engine->RegisterEnumValue("State", "DUCK", sDUCK);

		engine->SetDefaultNamespace("SOUND");
		engine->RegisterEnumValue("Sample", "AMMO_BLUB1", sAMMO_BLUB1);
		engine->RegisterEnumValue("Sample", "AMMO_BLUB2", sAMMO_BLUB2);
		engine->RegisterEnumValue("Sample", "AMMO_BMP1", sAMMO_BMP1);
		engine->RegisterEnumValue("Sample", "AMMO_BMP2", sAMMO_BMP2);
		engine->RegisterEnumValue("Sample", "AMMO_BMP3", sAMMO_BMP3);
		engine->RegisterEnumValue("Sample", "AMMO_BMP4", sAMMO_BMP4);
		engine->RegisterEnumValue("Sample", "AMMO_BMP5", sAMMO_BMP5);
		engine->RegisterEnumValue("Sample", "AMMO_BMP6", sAMMO_BMP6);
		engine->RegisterEnumValue("Sample", "AMMO_BOEM1", sAMMO_BOEM1);
		engine->RegisterEnumValue("Sample", "AMMO_BUL1", sAMMO_BUL1);
		engine->RegisterEnumValue("Sample", "AMMO_BULFL1", sAMMO_BULFL1);
		engine->RegisterEnumValue("Sample", "AMMO_BULFL2", sAMMO_BULFL2);
		engine->RegisterEnumValue("Sample", "AMMO_BULFL3", sAMMO_BULFL3);
		engine->RegisterEnumValue("Sample", "AMMO_FIREGUN1A", sAMMO_FIREGUN1A);
		engine->RegisterEnumValue("Sample", "AMMO_FIREGUN2A", sAMMO_FIREGUN2A);
		engine->RegisterEnumValue("Sample", "AMMO_FUMP", sAMMO_FUMP);
		engine->RegisterEnumValue("Sample", "AMMO_GUN1", sAMMO_GUN1);
		engine->RegisterEnumValue("Sample", "AMMO_GUN2", sAMMO_GUN2);
		engine->RegisterEnumValue("Sample", "AMMO_GUN3PLOP", sAMMO_GUN3PLOP);
		engine->RegisterEnumValue("Sample", "AMMO_GUNFLP", sAMMO_GUNFLP);
		engine->RegisterEnumValue("Sample", "AMMO_GUNFLP1", sAMMO_GUNFLP1);
		engine->RegisterEnumValue("Sample", "AMMO_GUNFLP2", sAMMO_GUNFLP2);
		engine->RegisterEnumValue("Sample", "AMMO_GUNFLP3", sAMMO_GUNFLP3);
		engine->RegisterEnumValue("Sample", "AMMO_GUNFLP4", sAMMO_GUNFLP4);
		engine->RegisterEnumValue("Sample", "AMMO_GUNFLPL", sAMMO_GUNFLPL);
		engine->RegisterEnumValue("Sample", "AMMO_GUNJAZZ", sAMMO_GUNJAZZ);
		engine->RegisterEnumValue("Sample", "AMMO_GUNVELOCITY", sAMMO_GUNVELOCITY);
		engine->RegisterEnumValue("Sample", "AMMO_ICEGUN", sAMMO_ICEGUN);
		engine->RegisterEnumValue("Sample", "AMMO_ICEGUN2", sAMMO_ICEGUN2);
		engine->RegisterEnumValue("Sample", "AMMO_ICEGUNPU", sAMMO_ICEGUNPU);
		engine->RegisterEnumValue("Sample", "AMMO_ICEPU1", sAMMO_ICEPU1);
		engine->RegisterEnumValue("Sample", "AMMO_ICEPU2", sAMMO_ICEPU2);
		engine->RegisterEnumValue("Sample", "AMMO_ICEPU3", sAMMO_ICEPU3);
		engine->RegisterEnumValue("Sample", "AMMO_ICEPU4", sAMMO_ICEPU4);
		engine->RegisterEnumValue("Sample", "AMMO_LASER", sAMMO_LASER);
		engine->RegisterEnumValue("Sample", "AMMO_LASER2", sAMMO_LASER2);
		engine->RegisterEnumValue("Sample", "AMMO_LASER3", sAMMO_LASER3);
		engine->RegisterEnumValue("Sample", "AMMO_LAZRAYS", sAMMO_LAZRAYS);
		engine->RegisterEnumValue("Sample", "AMMO_MISSILE", sAMMO_MISSILE);
		engine->RegisterEnumValue("Sample", "AMMO_SPZBL1", sAMMO_SPZBL1);
		engine->RegisterEnumValue("Sample", "AMMO_SPZBL2", sAMMO_SPZBL2);
		engine->RegisterEnumValue("Sample", "AMMO_SPZBL3", sAMMO_SPZBL3);
		engine->RegisterEnumValue("Sample", "BAT_BATFLY1", sBAT_BATFLY1);
		engine->RegisterEnumValue("Sample", "BILSBOSS_BILLAPPEAR", sBILSBOSS_BILLAPPEAR);
		engine->RegisterEnumValue("Sample", "BILSBOSS_FINGERSNAP", sBILSBOSS_FINGERSNAP);
		engine->RegisterEnumValue("Sample", "BILSBOSS_FIRE", sBILSBOSS_FIRE);
		engine->RegisterEnumValue("Sample", "BILSBOSS_FIRESTART", sBILSBOSS_FIRESTART);
		engine->RegisterEnumValue("Sample", "BILSBOSS_SCARY3", sBILSBOSS_SCARY3);
		engine->RegisterEnumValue("Sample", "BILSBOSS_THUNDER", sBILSBOSS_THUNDER);
		engine->RegisterEnumValue("Sample", "BILSBOSS_ZIP", sBILSBOSS_ZIP);
		engine->RegisterEnumValue("Sample", "BONUS_BONUS1", sBONUS_BONUS1);
		engine->RegisterEnumValue("Sample", "BONUS_BONUSBLUB", sBONUS_BONUSBLUB);
		engine->RegisterEnumValue("Sample", "BUBBA_BUBBABOUNCE1", sBUBBA_BUBBABOUNCE1);
		engine->RegisterEnumValue("Sample", "BUBBA_BUBBABOUNCE2", sBUBBA_BUBBABOUNCE2);
		engine->RegisterEnumValue("Sample", "BUBBA_BUBBAEXPLO", sBUBBA_BUBBAEXPLO);
		engine->RegisterEnumValue("Sample", "BUBBA_FROG2", sBUBBA_FROG2);
		engine->RegisterEnumValue("Sample", "BUBBA_FROG3", sBUBBA_FROG3);
		engine->RegisterEnumValue("Sample", "BUBBA_FROG4", sBUBBA_FROG4);
		engine->RegisterEnumValue("Sample", "BUBBA_FROG5", sBUBBA_FROG5);
		engine->RegisterEnumValue("Sample", "BUBBA_SNEEZE2", sBUBBA_SNEEZE2);
		engine->RegisterEnumValue("Sample", "BUBBA_TORNADOATTACK2", sBUBBA_TORNADOATTACK2);
		engine->RegisterEnumValue("Sample", "BUMBEE_BEELOOP", sBUMBEE_BEELOOP);
		engine->RegisterEnumValue("Sample", "CATERPIL_RIDOE", sCATERPIL_RIDOE);
		engine->RegisterEnumValue("Sample", "COMMON_AIRBOARD", sCOMMON_AIRBOARD);
		engine->RegisterEnumValue("Sample", "COMMON_AIRBTURN", sCOMMON_AIRBTURN);
		engine->RegisterEnumValue("Sample", "COMMON_AIRBTURN2", sCOMMON_AIRBTURN2);
		engine->RegisterEnumValue("Sample", "COMMON_BASE1", sCOMMON_BASE1);
		engine->RegisterEnumValue("Sample", "COMMON_BELL_FIRE", sCOMMON_BELL_FIRE);
		engine->RegisterEnumValue("Sample", "COMMON_BELL_FIRE2", sCOMMON_BELL_FIRE2);
		engine->RegisterEnumValue("Sample", "COMMON_BENZIN1", sCOMMON_BENZIN1);
		engine->RegisterEnumValue("Sample", "COMMON_BIRDFLY", sCOMMON_BIRDFLY);
		engine->RegisterEnumValue("Sample", "COMMON_BIRDFLY2", sCOMMON_BIRDFLY2);
		engine->RegisterEnumValue("Sample", "COMMON_BLOKPLOP", sCOMMON_BLOKPLOP);
		engine->RegisterEnumValue("Sample", "COMMON_BLUB1", sCOMMON_BLUB1);
		engine->RegisterEnumValue("Sample", "COMMON_BUBBLGN1", sCOMMON_BUBBLGN1);
		engine->RegisterEnumValue("Sample", "COMMON_BURN", sCOMMON_BURN);
		engine->RegisterEnumValue("Sample", "COMMON_BURNIN", sCOMMON_BURNIN);
		engine->RegisterEnumValue("Sample", "COMMON_CANSPS", sCOMMON_CANSPS);
		engine->RegisterEnumValue("Sample", "COMMON_CLOCK", sCOMMON_CLOCK);
		engine->RegisterEnumValue("Sample", "COMMON_COIN", sCOMMON_COIN);
		engine->RegisterEnumValue("Sample", "COMMON_COLLAPS", sCOMMON_COLLAPS);
		engine->RegisterEnumValue("Sample", "COMMON_CUP", sCOMMON_CUP);
		engine->RegisterEnumValue("Sample", "COMMON_DAMPED1", sCOMMON_DAMPED1);
		engine->RegisterEnumValue("Sample", "COMMON_DOWN", sCOMMON_DOWN);
		engine->RegisterEnumValue("Sample", "COMMON_DOWNFL2", sCOMMON_DOWNFL2);
		engine->RegisterEnumValue("Sample", "COMMON_DRINKSPAZZ1", sCOMMON_DRINKSPAZZ1);
		engine->RegisterEnumValue("Sample", "COMMON_DRINKSPAZZ2", sCOMMON_DRINKSPAZZ2);
		engine->RegisterEnumValue("Sample", "COMMON_DRINKSPAZZ3", sCOMMON_DRINKSPAZZ3);
		engine->RegisterEnumValue("Sample", "COMMON_DRINKSPAZZ4", sCOMMON_DRINKSPAZZ4);
		engine->RegisterEnumValue("Sample", "COMMON_EAT1", sCOMMON_EAT1);
		engine->RegisterEnumValue("Sample", "COMMON_EAT2", sCOMMON_EAT2);
		engine->RegisterEnumValue("Sample", "COMMON_EAT3", sCOMMON_EAT3);
		engine->RegisterEnumValue("Sample", "COMMON_EAT4", sCOMMON_EAT4);
		engine->RegisterEnumValue("Sample", "COMMON_ELECTRIC1", sCOMMON_ELECTRIC1);
		engine->RegisterEnumValue("Sample", "COMMON_ELECTRIC2", sCOMMON_ELECTRIC2);
		engine->RegisterEnumValue("Sample", "COMMON_ELECTRICHIT", sCOMMON_ELECTRICHIT);
		engine->RegisterEnumValue("Sample", "COMMON_EXPL_TNT", sCOMMON_EXPL_TNT);
		engine->RegisterEnumValue("Sample", "COMMON_EXPSM1", sCOMMON_EXPSM1);
		engine->RegisterEnumValue("Sample", "COMMON_FLAMER", sCOMMON_FLAMER);
		engine->RegisterEnumValue("Sample", "COMMON_FLAP", sCOMMON_FLAP);
		engine->RegisterEnumValue("Sample", "COMMON_FOEW1", sCOMMON_FOEW1);
		engine->RegisterEnumValue("Sample", "COMMON_FOEW2", sCOMMON_FOEW2);
		engine->RegisterEnumValue("Sample", "COMMON_FOEW3", sCOMMON_FOEW3);
		engine->RegisterEnumValue("Sample", "COMMON_FOEW4", sCOMMON_FOEW4);
		engine->RegisterEnumValue("Sample", "COMMON_FOEW5", sCOMMON_FOEW5);
		engine->RegisterEnumValue("Sample", "COMMON_GEMSMSH1", sCOMMON_GEMSMSH1);
		engine->RegisterEnumValue("Sample", "COMMON_GLASS2", sCOMMON_GLASS2);
		engine->RegisterEnumValue("Sample", "COMMON_GUNSM1", sCOMMON_GUNSM1);
		engine->RegisterEnumValue("Sample", "COMMON_HARP1", sCOMMON_HARP1);
		engine->RegisterEnumValue("Sample", "COMMON_HEAD", sCOMMON_HEAD);
		engine->RegisterEnumValue("Sample", "COMMON_HELI1", sCOMMON_HELI1);
		engine->RegisterEnumValue("Sample", "COMMON_HIBELL", sCOMMON_HIBELL);
		engine->RegisterEnumValue("Sample", "COMMON_HOLYFLUT", sCOMMON_HOLYFLUT);
		engine->RegisterEnumValue("Sample", "COMMON_HORN1", sCOMMON_HORN1);
		engine->RegisterEnumValue("Sample", "COMMON_ICECRUSH", sCOMMON_ICECRUSH);
		engine->RegisterEnumValue("Sample", "COMMON_IMPACT1", sCOMMON_IMPACT1);
		engine->RegisterEnumValue("Sample", "COMMON_IMPACT2", sCOMMON_IMPACT2);
		engine->RegisterEnumValue("Sample", "COMMON_IMPACT3", sCOMMON_IMPACT3);
		engine->RegisterEnumValue("Sample", "COMMON_IMPACT4", sCOMMON_IMPACT4);
		engine->RegisterEnumValue("Sample", "COMMON_IMPACT5", sCOMMON_IMPACT5);
		engine->RegisterEnumValue("Sample", "COMMON_IMPACT6", sCOMMON_IMPACT6);
		engine->RegisterEnumValue("Sample", "COMMON_IMPACT7", sCOMMON_IMPACT7);
		engine->RegisterEnumValue("Sample", "COMMON_IMPACT8", sCOMMON_IMPACT8);
		engine->RegisterEnumValue("Sample", "COMMON_IMPACT9", sCOMMON_IMPACT9);
		engine->RegisterEnumValue("Sample", "COMMON_ITEMTRE", sCOMMON_ITEMTRE);
		engine->RegisterEnumValue("Sample", "COMMON_JUMP", sCOMMON_JUMP);
		engine->RegisterEnumValue("Sample", "COMMON_JUMP2", sCOMMON_JUMP2);
		engine->RegisterEnumValue("Sample", "COMMON_LAND", sCOMMON_LAND);
		engine->RegisterEnumValue("Sample", "COMMON_LAND1", sCOMMON_LAND1);
		engine->RegisterEnumValue("Sample", "COMMON_LAND2", sCOMMON_LAND2);
		engine->RegisterEnumValue("Sample", "COMMON_LANDCAN1", sCOMMON_LANDCAN1);
		engine->RegisterEnumValue("Sample", "COMMON_LANDCAN2", sCOMMON_LANDCAN2);
		engine->RegisterEnumValue("Sample", "COMMON_LANDPOP", sCOMMON_LANDPOP);
		engine->RegisterEnumValue("Sample", "COMMON_LOADJAZZ", sCOMMON_LOADJAZZ);
		engine->RegisterEnumValue("Sample", "COMMON_LOADSPAZ", sCOMMON_LOADSPAZ);
		engine->RegisterEnumValue("Sample", "COMMON_METALHIT", sCOMMON_METALHIT);
		engine->RegisterEnumValue("Sample", "COMMON_MONITOR", sCOMMON_MONITOR);
		engine->RegisterEnumValue("Sample", "COMMON_NOCOIN", sCOMMON_NOCOIN);
		engine->RegisterEnumValue("Sample", "COMMON_PICKUP1", sCOMMON_PICKUP1);
		engine->RegisterEnumValue("Sample", "COMMON_PICKUPW1", sCOMMON_PICKUPW1);
		engine->RegisterEnumValue("Sample", "COMMON_PISTOL1", sCOMMON_PISTOL1);
		engine->RegisterEnumValue("Sample", "COMMON_PLOOP1", sCOMMON_PLOOP1);
		engine->RegisterEnumValue("Sample", "COMMON_PLOP1", sCOMMON_PLOP1);
		engine->RegisterEnumValue("Sample", "COMMON_PLOP2", sCOMMON_PLOP2);
		engine->RegisterEnumValue("Sample", "COMMON_PLOP3", sCOMMON_PLOP3);
		engine->RegisterEnumValue("Sample", "COMMON_PLOP4", sCOMMON_PLOP4);
		engine->RegisterEnumValue("Sample", "COMMON_PLOPKORK", sCOMMON_PLOPKORK);
		engine->RegisterEnumValue("Sample", "COMMON_PREEXPL1", sCOMMON_PREEXPL1);
		engine->RegisterEnumValue("Sample", "COMMON_PREHELI", sCOMMON_PREHELI);
		engine->RegisterEnumValue("Sample", "COMMON_REVUP", sCOMMON_REVUP);
		engine->RegisterEnumValue("Sample", "COMMON_RINGGUN", sCOMMON_RINGGUN);
		engine->RegisterEnumValue("Sample", "COMMON_RINGGUN2", sCOMMON_RINGGUN2);
		engine->RegisterEnumValue("Sample", "COMMON_SHIELD1", sCOMMON_SHIELD1);
		engine->RegisterEnumValue("Sample", "COMMON_SHIELD4", sCOMMON_SHIELD4);
		engine->RegisterEnumValue("Sample", "COMMON_SHIELD_ELEC", sCOMMON_SHIELD_ELEC);
		engine->RegisterEnumValue("Sample", "COMMON_SHLDOF3", sCOMMON_SHLDOF3);
		engine->RegisterEnumValue("Sample", "COMMON_SLIP", sCOMMON_SLIP);
		engine->RegisterEnumValue("Sample", "COMMON_SMASH", sCOMMON_SMASH);
		engine->RegisterEnumValue("Sample", "COMMON_SPLAT1", sCOMMON_SPLAT1);
		engine->RegisterEnumValue("Sample", "COMMON_SPLAT2", sCOMMON_SPLAT2);
		engine->RegisterEnumValue("Sample", "COMMON_SPLAT3", sCOMMON_SPLAT3);
		engine->RegisterEnumValue("Sample", "COMMON_SPLAT4", sCOMMON_SPLAT4);
		engine->RegisterEnumValue("Sample", "COMMON_SPLUT", sCOMMON_SPLUT);
		engine->RegisterEnumValue("Sample", "COMMON_SPRING1", sCOMMON_SPRING1);
		engine->RegisterEnumValue("Sample", "COMMON_STEAM", sCOMMON_STEAM);
		engine->RegisterEnumValue("Sample", "COMMON_STEP", sCOMMON_STEP);
		engine->RegisterEnumValue("Sample", "COMMON_STRETCH", sCOMMON_STRETCH);
		engine->RegisterEnumValue("Sample", "COMMON_SWISH1", sCOMMON_SWISH1);
		engine->RegisterEnumValue("Sample", "COMMON_SWISH2", sCOMMON_SWISH2);
		engine->RegisterEnumValue("Sample", "COMMON_SWISH3", sCOMMON_SWISH3);
		engine->RegisterEnumValue("Sample", "COMMON_SWISH4", sCOMMON_SWISH4);
		engine->RegisterEnumValue("Sample", "COMMON_SWISH5", sCOMMON_SWISH5);
		engine->RegisterEnumValue("Sample", "COMMON_SWISH6", sCOMMON_SWISH6);
		engine->RegisterEnumValue("Sample", "COMMON_SWISH7", sCOMMON_SWISH7);
		engine->RegisterEnumValue("Sample", "COMMON_SWISH8", sCOMMON_SWISH8);
		engine->RegisterEnumValue("Sample", "COMMON_TELPORT1", sCOMMON_TELPORT1);
		engine->RegisterEnumValue("Sample", "COMMON_TELPORT2", sCOMMON_TELPORT2);
		engine->RegisterEnumValue("Sample", "COMMON_UP", sCOMMON_UP);
		engine->RegisterEnumValue("Sample", "COMMON_WATER", sCOMMON_WATER);
		engine->RegisterEnumValue("Sample", "COMMON_WOOD1", sCOMMON_WOOD1);
		engine->RegisterEnumValue("Sample", "DEMON_RUN", sDEMON_RUN);
		engine->RegisterEnumValue("Sample", "DEVILDEVAN_DRAGONFIRE", sDEVILDEVAN_DRAGONFIRE);
		engine->RegisterEnumValue("Sample", "DEVILDEVAN_FLAP", sDEVILDEVAN_FLAP);
		engine->RegisterEnumValue("Sample", "DEVILDEVAN_FROG4", sDEVILDEVAN_FROG4);
		engine->RegisterEnumValue("Sample", "DEVILDEVAN_JUMPUP", sDEVILDEVAN_JUMPUP);
		engine->RegisterEnumValue("Sample", "DEVILDEVAN_LAUGH", sDEVILDEVAN_LAUGH);
		engine->RegisterEnumValue("Sample", "DEVILDEVAN_PHASER2", sDEVILDEVAN_PHASER2);
		engine->RegisterEnumValue("Sample", "DEVILDEVAN_STRECH2", sDEVILDEVAN_STRECH2);
		engine->RegisterEnumValue("Sample", "DEVILDEVAN_STRECHTAIL", sDEVILDEVAN_STRECHTAIL);
		engine->RegisterEnumValue("Sample", "DEVILDEVAN_STRETCH1", sDEVILDEVAN_STRETCH1);
		engine->RegisterEnumValue("Sample", "DEVILDEVAN_STRETCH3", sDEVILDEVAN_STRETCH3);
		engine->RegisterEnumValue("Sample", "DEVILDEVAN_VANISH1", sDEVILDEVAN_VANISH1);
		engine->RegisterEnumValue("Sample", "DEVILDEVAN_WHISTLEDESCENDING2", sDEVILDEVAN_WHISTLEDESCENDING2);
		engine->RegisterEnumValue("Sample", "DEVILDEVAN_WINGSOUT", sDEVILDEVAN_WINGSOUT);
		engine->RegisterEnumValue("Sample", "DOG_AGRESSIV", sDOG_AGRESSIV);
		engine->RegisterEnumValue("Sample", "DOG_SNIF1", sDOG_SNIF1);
		engine->RegisterEnumValue("Sample", "DOG_WAF1", sDOG_WAF1);
		engine->RegisterEnumValue("Sample", "DOG_WAF2", sDOG_WAF2);
		engine->RegisterEnumValue("Sample", "DOG_WAF3", sDOG_WAF3);
		engine->RegisterEnumValue("Sample", "DRAGFLY_BEELOOP", sDRAGFLY_BEELOOP);
		engine->RegisterEnumValue("Sample", "ENDING_OHTHANK", sENDING_OHTHANK);
		engine->RegisterEnumValue("Sample", "ENDTUNEJAZZ_TUNE", sENDTUNEJAZZ_TUNE);
		engine->RegisterEnumValue("Sample", "ENDTUNELORI_CAKE", sENDTUNELORI_CAKE);
		engine->RegisterEnumValue("Sample", "ENDTUNESPAZ_TUNE", sENDTUNESPAZ_TUNE);
		engine->RegisterEnumValue("Sample", "EPICLOGO_EPIC1", sEPICLOGO_EPIC1);
		engine->RegisterEnumValue("Sample", "EPICLOGO_EPIC2", sEPICLOGO_EPIC2);
		engine->RegisterEnumValue("Sample", "EVA_KISS1", sEVA_KISS1);
		engine->RegisterEnumValue("Sample", "EVA_KISS2", sEVA_KISS2);
		engine->RegisterEnumValue("Sample", "EVA_KISS3", sEVA_KISS3);
		engine->RegisterEnumValue("Sample", "EVA_KISS4", sEVA_KISS4);
		engine->RegisterEnumValue("Sample", "FAN_FAN", sFAN_FAN);
		engine->RegisterEnumValue("Sample", "FATCHK_HIT1", sFATCHK_HIT1);
		engine->RegisterEnumValue("Sample", "FATCHK_HIT2", sFATCHK_HIT2);
		engine->RegisterEnumValue("Sample", "FATCHK_HIT3", sFATCHK_HIT3);
		engine->RegisterEnumValue("Sample", "FENCER_FENCE1", sFENCER_FENCE1);
		engine->RegisterEnumValue("Sample", "FROG_FROG", sFROG_FROG);
		engine->RegisterEnumValue("Sample", "FROG_FROG1", sFROG_FROG1);
		engine->RegisterEnumValue("Sample", "FROG_FROG2", sFROG_FROG2);
		engine->RegisterEnumValue("Sample", "FROG_FROG3", sFROG_FROG3);
		engine->RegisterEnumValue("Sample", "FROG_FROG4", sFROG_FROG4);
		engine->RegisterEnumValue("Sample", "FROG_FROG5", sFROG_FROG5);
		engine->RegisterEnumValue("Sample", "FROG_JAZZ2FROG", sFROG_JAZZ2FROG);
		engine->RegisterEnumValue("Sample", "FROG_TONG", sFROG_TONG);
		engine->RegisterEnumValue("Sample", "GLOVE_HIT", sGLOVE_HIT);
		engine->RegisterEnumValue("Sample", "HATTER_CUP", sHATTER_CUP);
		engine->RegisterEnumValue("Sample", "HATTER_HAT", sHATTER_HAT);
		engine->RegisterEnumValue("Sample", "HATTER_PTOEI", sHATTER_PTOEI);
		engine->RegisterEnumValue("Sample", "HATTER_SPLIN", sHATTER_SPLIN);
		engine->RegisterEnumValue("Sample", "HATTER_SPLOUT", sHATTER_SPLOUT);
		engine->RegisterEnumValue("Sample", "INTRO_BLOW", sINTRO_BLOW);
		engine->RegisterEnumValue("Sample", "INTRO_BOEM1", sINTRO_BOEM1);
		engine->RegisterEnumValue("Sample", "INTRO_BOEM2", sINTRO_BOEM2);
		engine->RegisterEnumValue("Sample", "INTRO_BRAKE", sINTRO_BRAKE);
		engine->RegisterEnumValue("Sample", "INTRO_END", sINTRO_END);
		engine->RegisterEnumValue("Sample", "INTRO_GRAB", sINTRO_GRAB);
		engine->RegisterEnumValue("Sample", "INTRO_GREN1", sINTRO_GREN1);
		engine->RegisterEnumValue("Sample", "INTRO_GREN2", sINTRO_GREN2);
		engine->RegisterEnumValue("Sample", "INTRO_GREN3", sINTRO_GREN3);
		engine->RegisterEnumValue("Sample", "INTRO_GUNM0", sINTRO_GUNM0);
		engine->RegisterEnumValue("Sample", "INTRO_GUNM1", sINTRO_GUNM1);
		engine->RegisterEnumValue("Sample", "INTRO_GUNM2", sINTRO_GUNM2);
		engine->RegisterEnumValue("Sample", "INTRO_HELI", sINTRO_HELI);
		engine->RegisterEnumValue("Sample", "INTRO_HITSPAZ", sINTRO_HITSPAZ);
		engine->RegisterEnumValue("Sample", "INTRO_HITTURT", sINTRO_HITTURT);
		engine->RegisterEnumValue("Sample", "INTRO_IFEEL", sINTRO_IFEEL);
		engine->RegisterEnumValue("Sample", "INTRO_INHALE", sINTRO_INHALE);
		engine->RegisterEnumValue("Sample", "INTRO_INSECT", sINTRO_INSECT);
		engine->RegisterEnumValue("Sample", "INTRO_KATROL", sINTRO_KATROL);
		engine->RegisterEnumValue("Sample", "INTRO_LAND", sINTRO_LAND);
		engine->RegisterEnumValue("Sample", "INTRO_MONSTER", sINTRO_MONSTER);
		engine->RegisterEnumValue("Sample", "INTRO_MONSTER2", sINTRO_MONSTER2);
		engine->RegisterEnumValue("Sample", "INTRO_ROCK", sINTRO_ROCK);
		engine->RegisterEnumValue("Sample", "INTRO_ROPE1", sINTRO_ROPE1);
		engine->RegisterEnumValue("Sample", "INTRO_ROPE2", sINTRO_ROPE2);
		engine->RegisterEnumValue("Sample", "INTRO_RUN", sINTRO_RUN);
		engine->RegisterEnumValue("Sample", "INTRO_SHOT1", sINTRO_SHOT1);
		engine->RegisterEnumValue("Sample", "INTRO_SHOTGRN", sINTRO_SHOTGRN);
		engine->RegisterEnumValue("Sample", "INTRO_SKI", sINTRO_SKI);
		engine->RegisterEnumValue("Sample", "INTRO_STRING", sINTRO_STRING);
		engine->RegisterEnumValue("Sample", "INTRO_SWISH1", sINTRO_SWISH1);
		engine->RegisterEnumValue("Sample", "INTRO_SWISH2", sINTRO_SWISH2);
		engine->RegisterEnumValue("Sample", "INTRO_SWISH3", sINTRO_SWISH3);
		engine->RegisterEnumValue("Sample", "INTRO_SWISH4", sINTRO_SWISH4);
		engine->RegisterEnumValue("Sample", "INTRO_UHTURT", sINTRO_UHTURT);
		engine->RegisterEnumValue("Sample", "INTRO_UP1", sINTRO_UP1);
		engine->RegisterEnumValue("Sample", "INTRO_UP2", sINTRO_UP2);
		engine->RegisterEnumValue("Sample", "INTRO_WIND_01", sINTRO_WIND_01);
		engine->RegisterEnumValue("Sample", "JAZZSOUNDS_BALANCE", sJAZZSOUNDS_BALANCE);
		engine->RegisterEnumValue("Sample", "JAZZSOUNDS_HEY1", sJAZZSOUNDS_HEY1);
		engine->RegisterEnumValue("Sample", "JAZZSOUNDS_HEY2", sJAZZSOUNDS_HEY2);
		engine->RegisterEnumValue("Sample", "JAZZSOUNDS_HEY3", sJAZZSOUNDS_HEY3);
		engine->RegisterEnumValue("Sample", "JAZZSOUNDS_HEY4", sJAZZSOUNDS_HEY4);
		engine->RegisterEnumValue("Sample", "JAZZSOUNDS_IDLE", sJAZZSOUNDS_IDLE);
		engine->RegisterEnumValue("Sample", "JAZZSOUNDS_JAZZV1", sJAZZSOUNDS_JAZZV1);
		engine->RegisterEnumValue("Sample", "JAZZSOUNDS_JAZZV2", sJAZZSOUNDS_JAZZV2);
		engine->RegisterEnumValue("Sample", "JAZZSOUNDS_JAZZV3", sJAZZSOUNDS_JAZZV3);
		engine->RegisterEnumValue("Sample", "JAZZSOUNDS_JAZZV4", sJAZZSOUNDS_JAZZV4);
		engine->RegisterEnumValue("Sample", "JAZZSOUNDS_JUMMY", sJAZZSOUNDS_JUMMY);
		engine->RegisterEnumValue("Sample", "JAZZSOUNDS_PFOE", sJAZZSOUNDS_PFOE);
		engine->RegisterEnumValue("Sample", "LABRAT_BITE", sLABRAT_BITE);
		engine->RegisterEnumValue("Sample", "LABRAT_EYE2", sLABRAT_EYE2);
		engine->RegisterEnumValue("Sample", "LABRAT_EYE3", sLABRAT_EYE3);
		engine->RegisterEnumValue("Sample", "LABRAT_MOUSE1", sLABRAT_MOUSE1);
		engine->RegisterEnumValue("Sample", "LABRAT_MOUSE2", sLABRAT_MOUSE2);
		engine->RegisterEnumValue("Sample", "LABRAT_MOUSE3", sLABRAT_MOUSE3);
		engine->RegisterEnumValue("Sample", "LIZARD_LIZ1", sLIZARD_LIZ1);
		engine->RegisterEnumValue("Sample", "LIZARD_LIZ2", sLIZARD_LIZ2);
		engine->RegisterEnumValue("Sample", "LIZARD_LIZ4", sLIZARD_LIZ4);
		engine->RegisterEnumValue("Sample", "LIZARD_LIZ6", sLIZARD_LIZ6);
		engine->RegisterEnumValue("Sample", "LORISOUNDS_DIE1", sLORISOUNDS_DIE1);
		engine->RegisterEnumValue("Sample", "LORISOUNDS_HURT0", sLORISOUNDS_HURT0);
		engine->RegisterEnumValue("Sample", "LORISOUNDS_HURT1", sLORISOUNDS_HURT1);
		engine->RegisterEnumValue("Sample", "LORISOUNDS_HURT2", sLORISOUNDS_HURT2);
		engine->RegisterEnumValue("Sample", "LORISOUNDS_HURT3", sLORISOUNDS_HURT3);
		engine->RegisterEnumValue("Sample", "LORISOUNDS_HURT4", sLORISOUNDS_HURT4);
		engine->RegisterEnumValue("Sample", "LORISOUNDS_HURT5", sLORISOUNDS_HURT5);
		engine->RegisterEnumValue("Sample", "LORISOUNDS_HURT6", sLORISOUNDS_HURT6);
		engine->RegisterEnumValue("Sample", "LORISOUNDS_HURT7", sLORISOUNDS_HURT7);
		engine->RegisterEnumValue("Sample", "LORISOUNDS_LORI1", sLORISOUNDS_LORI1);
		engine->RegisterEnumValue("Sample", "LORISOUNDS_LORI2", sLORISOUNDS_LORI2);
		engine->RegisterEnumValue("Sample", "LORISOUNDS_LORIBOOM", sLORISOUNDS_LORIBOOM);
		engine->RegisterEnumValue("Sample", "LORISOUNDS_LORIFALL", sLORISOUNDS_LORIFALL);
		engine->RegisterEnumValue("Sample", "LORISOUNDS_LORIJUMP", sLORISOUNDS_LORIJUMP);
		engine->RegisterEnumValue("Sample", "LORISOUNDS_LORIJUMP2", sLORISOUNDS_LORIJUMP2);
		engine->RegisterEnumValue("Sample", "LORISOUNDS_LORIJUMP3", sLORISOUNDS_LORIJUMP3);
		engine->RegisterEnumValue("Sample", "LORISOUNDS_LORIJUMP4", sLORISOUNDS_LORIJUMP4);
		engine->RegisterEnumValue("Sample", "LORISOUNDS_TOUCH", sLORISOUNDS_TOUCH);
		engine->RegisterEnumValue("Sample", "LORISOUNDS_WEHOO", sLORISOUNDS_WEHOO);
		engine->RegisterEnumValue("Sample", "MENUSOUNDS_SELECT0", sMENUSOUNDS_SELECT0);
		engine->RegisterEnumValue("Sample", "MENUSOUNDS_SELECT1", sMENUSOUNDS_SELECT1);
		engine->RegisterEnumValue("Sample", "MENUSOUNDS_SELECT2", sMENUSOUNDS_SELECT2);
		engine->RegisterEnumValue("Sample", "MENUSOUNDS_SELECT3", sMENUSOUNDS_SELECT3);
		engine->RegisterEnumValue("Sample", "MENUSOUNDS_SELECT4", sMENUSOUNDS_SELECT4);
		engine->RegisterEnumValue("Sample", "MENUSOUNDS_SELECT5", sMENUSOUNDS_SELECT5);
		engine->RegisterEnumValue("Sample", "MENUSOUNDS_SELECT6", sMENUSOUNDS_SELECT6);
		engine->RegisterEnumValue("Sample", "MENUSOUNDS_TYPE", sMENUSOUNDS_TYPE);
		engine->RegisterEnumValue("Sample", "MENUSOUNDS_TYPEENTER", sMENUSOUNDS_TYPEENTER);
		engine->RegisterEnumValue("Sample", "MONKEY_SPLUT", sMONKEY_SPLUT);
		engine->RegisterEnumValue("Sample", "MONKEY_THROW", sMONKEY_THROW);
		engine->RegisterEnumValue("Sample", "MOTH_FLAPMOTH", sMOTH_FLAPMOTH);
		engine->RegisterEnumValue("Sample", "ORANGE_BOEML", sORANGE_BOEML);
		engine->RegisterEnumValue("Sample", "ORANGE_BOEMR", sORANGE_BOEMR);
		engine->RegisterEnumValue("Sample", "ORANGE_BUBBELSL", sORANGE_BUBBELSL);
		engine->RegisterEnumValue("Sample", "ORANGE_BUBBELSR", sORANGE_BUBBELSR);
		engine->RegisterEnumValue("Sample", "ORANGE_GLAS1L", sORANGE_GLAS1L);
		engine->RegisterEnumValue("Sample", "ORANGE_GLAS1R", sORANGE_GLAS1R);
		engine->RegisterEnumValue("Sample", "ORANGE_GLAS2L", sORANGE_GLAS2L);
		engine->RegisterEnumValue("Sample", "ORANGE_GLAS2R", sORANGE_GLAS2R);
		engine->RegisterEnumValue("Sample", "ORANGE_MERGE", sORANGE_MERGE);
		engine->RegisterEnumValue("Sample", "ORANGE_SWEEP0L", sORANGE_SWEEP0L);
		engine->RegisterEnumValue("Sample", "ORANGE_SWEEP0R", sORANGE_SWEEP0R);
		engine->RegisterEnumValue("Sample", "ORANGE_SWEEP1L", sORANGE_SWEEP1L);
		engine->RegisterEnumValue("Sample", "ORANGE_SWEEP1R", sORANGE_SWEEP1R);
		engine->RegisterEnumValue("Sample", "ORANGE_SWEEP2L", sORANGE_SWEEP2L);
		engine->RegisterEnumValue("Sample", "ORANGE_SWEEP2R", sORANGE_SWEEP2R);
		engine->RegisterEnumValue("Sample", "P2_CRUNCH", sP2_CRUNCH);
		engine->RegisterEnumValue("Sample", "P2_FART", sP2_FART);
		engine->RegisterEnumValue("Sample", "P2_FOEW1", sP2_FOEW1);
		engine->RegisterEnumValue("Sample", "P2_FOEW4", sP2_FOEW4);
		engine->RegisterEnumValue("Sample", "P2_FOEW5", sP2_FOEW5);
		engine->RegisterEnumValue("Sample", "P2_FROG1", sP2_FROG1);
		engine->RegisterEnumValue("Sample", "P2_FROG2", sP2_FROG2);
		engine->RegisterEnumValue("Sample", "P2_FROG3", sP2_FROG3);
		engine->RegisterEnumValue("Sample", "P2_FROG4", sP2_FROG4);
		engine->RegisterEnumValue("Sample", "P2_FROG5", sP2_FROG5);
		engine->RegisterEnumValue("Sample", "P2_KISS4", sP2_KISS4);
		engine->RegisterEnumValue("Sample", "P2_OPEN", sP2_OPEN);
		engine->RegisterEnumValue("Sample", "P2_PINCH1", sP2_PINCH1);
		engine->RegisterEnumValue("Sample", "P2_PINCH2", sP2_PINCH2);
		engine->RegisterEnumValue("Sample", "P2_PLOPSEQ1", sP2_PLOPSEQ1);
		engine->RegisterEnumValue("Sample", "P2_PLOPSEQ2", sP2_PLOPSEQ2);
		engine->RegisterEnumValue("Sample", "P2_PLOPSEQ3", sP2_PLOPSEQ3);
		engine->RegisterEnumValue("Sample", "P2_PLOPSEQ4", sP2_PLOPSEQ4);
		engine->RegisterEnumValue("Sample", "P2_POEP", sP2_POEP);
		engine->RegisterEnumValue("Sample", "P2_PTOEI", sP2_PTOEI);
		engine->RegisterEnumValue("Sample", "P2_SPLOUT", sP2_SPLOUT);
		engine->RegisterEnumValue("Sample", "P2_SPLUT", sP2_SPLUT);
		engine->RegisterEnumValue("Sample", "P2_THROW", sP2_THROW);
		engine->RegisterEnumValue("Sample", "P2_TONG", sP2_TONG);
		engine->RegisterEnumValue("Sample", "PICKUPS_BOING_CHECK", sPICKUPS_BOING_CHECK);
		engine->RegisterEnumValue("Sample", "PICKUPS_HELI2", sPICKUPS_HELI2);
		engine->RegisterEnumValue("Sample", "PICKUPS_STRETCH1A", sPICKUPS_STRETCH1A);
		engine->RegisterEnumValue("Sample", "PINBALL_BELL", sPINBALL_BELL);
		engine->RegisterEnumValue("Sample", "PINBALL_FLIP1", sPINBALL_FLIP1);
		engine->RegisterEnumValue("Sample", "PINBALL_FLIP2", sPINBALL_FLIP2);
		engine->RegisterEnumValue("Sample", "PINBALL_FLIP3", sPINBALL_FLIP3);
		engine->RegisterEnumValue("Sample", "PINBALL_FLIP4", sPINBALL_FLIP4);
		engine->RegisterEnumValue("Sample", "QUEEN_LADYUP", sQUEEN_LADYUP);
		engine->RegisterEnumValue("Sample", "QUEEN_SCREAM", sQUEEN_SCREAM);
		engine->RegisterEnumValue("Sample", "RAPIER_GOSTDIE", sRAPIER_GOSTDIE);
		engine->RegisterEnumValue("Sample", "RAPIER_GOSTLOOP", sRAPIER_GOSTLOOP);
		engine->RegisterEnumValue("Sample", "RAPIER_GOSTOOOH", sRAPIER_GOSTOOOH);
		engine->RegisterEnumValue("Sample", "RAPIER_GOSTRIP", sRAPIER_GOSTRIP);
		engine->RegisterEnumValue("Sample", "RAPIER_HITCHAR", sRAPIER_HITCHAR);
		engine->RegisterEnumValue("Sample", "ROBOT_BIG1", sROBOT_BIG1);
		engine->RegisterEnumValue("Sample", "ROBOT_BIG2", sROBOT_BIG2);
		engine->RegisterEnumValue("Sample", "ROBOT_CAN1", sROBOT_CAN1);
		engine->RegisterEnumValue("Sample", "ROBOT_CAN2", sROBOT_CAN2);
		engine->RegisterEnumValue("Sample", "ROBOT_HYDRO", sROBOT_HYDRO);
		engine->RegisterEnumValue("Sample", "ROBOT_HYDRO2", sROBOT_HYDRO2);
		engine->RegisterEnumValue("Sample", "ROBOT_HYDROFIL", sROBOT_HYDROFIL);
		engine->RegisterEnumValue("Sample", "ROBOT_HYDROPUF", sROBOT_HYDROPUF);
		engine->RegisterEnumValue("Sample", "ROBOT_IDLE1", sROBOT_IDLE1);
		engine->RegisterEnumValue("Sample", "ROBOT_IDLE2", sROBOT_IDLE2);
		engine->RegisterEnumValue("Sample", "ROBOT_JMPCAN1", sROBOT_JMPCAN1);
		engine->RegisterEnumValue("Sample", "ROBOT_JMPCAN10", sROBOT_JMPCAN10);
		engine->RegisterEnumValue("Sample", "ROBOT_JMPCAN2", sROBOT_JMPCAN2);
		engine->RegisterEnumValue("Sample", "ROBOT_JMPCAN3", sROBOT_JMPCAN3);
		engine->RegisterEnumValue("Sample", "ROBOT_JMPCAN4", sROBOT_JMPCAN4);
		engine->RegisterEnumValue("Sample", "ROBOT_JMPCAN5", sROBOT_JMPCAN5);
		engine->RegisterEnumValue("Sample", "ROBOT_JMPCAN6", sROBOT_JMPCAN6);
		engine->RegisterEnumValue("Sample", "ROBOT_JMPCAN7", sROBOT_JMPCAN7);
		engine->RegisterEnumValue("Sample", "ROBOT_JMPCAN8", sROBOT_JMPCAN8);
		engine->RegisterEnumValue("Sample", "ROBOT_JMPCAN9", sROBOT_JMPCAN9);
		engine->RegisterEnumValue("Sample", "ROBOT_METAL1", sROBOT_METAL1);
		engine->RegisterEnumValue("Sample", "ROBOT_METAL2", sROBOT_METAL2);
		engine->RegisterEnumValue("Sample", "ROBOT_METAL3", sROBOT_METAL3);
		engine->RegisterEnumValue("Sample", "ROBOT_METAL4", sROBOT_METAL4);
		engine->RegisterEnumValue("Sample", "ROBOT_METAL5", sROBOT_METAL5);
		engine->RegisterEnumValue("Sample", "ROBOT_OPEN", sROBOT_OPEN);
		engine->RegisterEnumValue("Sample", "ROBOT_OUT", sROBOT_OUT);
		engine->RegisterEnumValue("Sample", "ROBOT_POEP", sROBOT_POEP);
		engine->RegisterEnumValue("Sample", "ROBOT_POLE", sROBOT_POLE);
		engine->RegisterEnumValue("Sample", "ROBOT_SHOOT", sROBOT_SHOOT);
		engine->RegisterEnumValue("Sample", "ROBOT_STEP1", sROBOT_STEP1);
		engine->RegisterEnumValue("Sample", "ROBOT_STEP2", sROBOT_STEP2);
		engine->RegisterEnumValue("Sample", "ROBOT_STEP3", sROBOT_STEP3);
		engine->RegisterEnumValue("Sample", "ROCK_ROCK1", sROCK_ROCK1);
		engine->RegisterEnumValue("Sample", "RUSH_RUSH", sRUSH_RUSH);
		engine->RegisterEnumValue("Sample", "SCIENCE_PLOPKAOS", sSCIENCE_PLOPKAOS);
		engine->RegisterEnumValue("Sample", "SKELETON_BONE1", sSKELETON_BONE1);
		engine->RegisterEnumValue("Sample", "SKELETON_BONE2", sSKELETON_BONE2);
		engine->RegisterEnumValue("Sample", "SKELETON_BONE3", sSKELETON_BONE3);
		engine->RegisterEnumValue("Sample", "SKELETON_BONE5", sSKELETON_BONE5);
		engine->RegisterEnumValue("Sample", "SKELETON_BONE6", sSKELETON_BONE6);
		engine->RegisterEnumValue("Sample", "SKELETON_BONE7", sSKELETON_BONE7);
		engine->RegisterEnumValue("Sample", "SMALTREE_FALL", sSMALTREE_FALL);
		engine->RegisterEnumValue("Sample", "SMALTREE_GROUND", sSMALTREE_GROUND);
		engine->RegisterEnumValue("Sample", "SMALTREE_HEAD", sSMALTREE_HEAD);
		engine->RegisterEnumValue("Sample", "SONCSHIP_METAL1", sSONCSHIP_METAL1);
		engine->RegisterEnumValue("Sample", "SONCSHIP_MISSILE2", sSONCSHIP_MISSILE2);
		engine->RegisterEnumValue("Sample", "SONCSHIP_SCRAPE", sSONCSHIP_SCRAPE);
		engine->RegisterEnumValue("Sample", "SONCSHIP_SHIPLOOP", sSONCSHIP_SHIPLOOP);
		engine->RegisterEnumValue("Sample", "SONCSHIP_TARGETLOCK", sSONCSHIP_TARGETLOCK);
		engine->RegisterEnumValue("Sample", "SONICSHIP_METAL1", sSONCSHIP_METAL1); // Private/deprecated
		engine->RegisterEnumValue("Sample", "SONICSHIP_MISSILE2", sSONCSHIP_MISSILE2); // Private/deprecated
		engine->RegisterEnumValue("Sample", "SONICSHIP_SCRAPE", sSONCSHIP_SCRAPE); // Private/deprecated
		engine->RegisterEnumValue("Sample", "SONICSHIP_SHIPLOOP", sSONCSHIP_SHIPLOOP); // Private/deprecated
		engine->RegisterEnumValue("Sample", "SONICSHIP_TARGETLOCK", sSONCSHIP_TARGETLOCK); // Private/deprecated
		engine->RegisterEnumValue("Sample", "SPAZSOUNDS_AUTSCH1", sSPAZSOUNDS_AUTSCH1);
		engine->RegisterEnumValue("Sample", "SPAZSOUNDS_AUTSCH2", sSPAZSOUNDS_AUTSCH2);
		engine->RegisterEnumValue("Sample", "SPAZSOUNDS_BIRDSIT", sSPAZSOUNDS_BIRDSIT);
		engine->RegisterEnumValue("Sample", "SPAZSOUNDS_BURP", sSPAZSOUNDS_BURP);
		engine->RegisterEnumValue("Sample", "SPAZSOUNDS_CHIRP", sSPAZSOUNDS_CHIRP);
		engine->RegisterEnumValue("Sample", "SPAZSOUNDS_EATBIRD", sSPAZSOUNDS_EATBIRD);
		engine->RegisterEnumValue("Sample", "SPAZSOUNDS_HAHAHA", sSPAZSOUNDS_HAHAHA);
		engine->RegisterEnumValue("Sample", "SPAZSOUNDS_HAHAHA2", sSPAZSOUNDS_HAHAHA2);
		engine->RegisterEnumValue("Sample", "SPAZSOUNDS_HAPPY", sSPAZSOUNDS_HAPPY);
		engine->RegisterEnumValue("Sample", "SPAZSOUNDS_HIHI", sSPAZSOUNDS_HIHI);
		engine->RegisterEnumValue("Sample", "SPAZSOUNDS_HOHOHO1", sSPAZSOUNDS_HOHOHO1);
		engine->RegisterEnumValue("Sample", "SPAZSOUNDS_HOOO", sSPAZSOUNDS_HOOO);
		engine->RegisterEnumValue("Sample", "SPAZSOUNDS_KARATE7", sSPAZSOUNDS_KARATE7);
		engine->RegisterEnumValue("Sample", "SPAZSOUNDS_KARATE8", sSPAZSOUNDS_KARATE8);
		engine->RegisterEnumValue("Sample", "SPAZSOUNDS_OHOH", sSPAZSOUNDS_OHOH);
		engine->RegisterEnumValue("Sample", "SPAZSOUNDS_OOOH", sSPAZSOUNDS_OOOH);
		engine->RegisterEnumValue("Sample", "SPAZSOUNDS_WOOHOO", sSPAZSOUNDS_WOOHOO);
		engine->RegisterEnumValue("Sample", "SPAZSOUNDS_YAHOO", sSPAZSOUNDS_YAHOO);
		engine->RegisterEnumValue("Sample", "SPAZSOUNDS_YAHOO2", sSPAZSOUNDS_YAHOO2);
		engine->RegisterEnumValue("Sample", "SPRING_BOING_DOWN", sSPRING_BOING_DOWN);
		engine->RegisterEnumValue("Sample", "SPRING_SPRING1", sSPRING_SPRING1);
		engine->RegisterEnumValue("Sample", "STEAM_STEAM", sSTEAM_STEAM);
		engine->RegisterEnumValue("Sample", "STONED_STONED", sSTONED_STONED);
		engine->RegisterEnumValue("Sample", "SUCKER_FART", sSUCKER_FART);
		engine->RegisterEnumValue("Sample", "SUCKER_PINCH1", sSUCKER_PINCH1);
		engine->RegisterEnumValue("Sample", "SUCKER_PINCH2", sSUCKER_PINCH2);
		engine->RegisterEnumValue("Sample", "SUCKER_PINCH3", sSUCKER_PINCH3);
		engine->RegisterEnumValue("Sample", "SUCKER_PLOPSEQ1", sSUCKER_PLOPSEQ1);
		engine->RegisterEnumValue("Sample", "SUCKER_PLOPSEQ2", sSUCKER_PLOPSEQ2);
		engine->RegisterEnumValue("Sample", "SUCKER_PLOPSEQ3", sSUCKER_PLOPSEQ3);
		engine->RegisterEnumValue("Sample", "SUCKER_PLOPSEQ4", sSUCKER_PLOPSEQ4);
		engine->RegisterEnumValue("Sample", "SUCKER_UP", sSUCKER_UP);
		engine->RegisterEnumValue("Sample", "TUFBOSS_CATCH", sTUFBOSS_CATCH);
		engine->RegisterEnumValue("Sample", "TUFBOSS_RELEASE", sTUFBOSS_RELEASE);
		engine->RegisterEnumValue("Sample", "TUFBOSS_SWING", sTUFBOSS_SWING);
		engine->RegisterEnumValue("Sample", "TURTLE_BITE3", sTURTLE_BITE3);
		engine->RegisterEnumValue("Sample", "TURTLE_HIDE", sTURTLE_HIDE);
		engine->RegisterEnumValue("Sample", "TURTLE_HITSHELL", sTURTLE_HITSHELL);
		engine->RegisterEnumValue("Sample", "TURTLE_IDLE1", sTURTLE_IDLE1);
		engine->RegisterEnumValue("Sample", "TURTLE_IDLE2", sTURTLE_IDLE2);
		engine->RegisterEnumValue("Sample", "TURTLE_NECK", sTURTLE_NECK);
		engine->RegisterEnumValue("Sample", "TURTLE_SPK1TURT", sTURTLE_SPK1TURT);
		engine->RegisterEnumValue("Sample", "TURTLE_SPK2TURT", sTURTLE_SPK2TURT);
		engine->RegisterEnumValue("Sample", "TURTLE_SPK3TURT", sTURTLE_SPK3TURT);
		engine->RegisterEnumValue("Sample", "TURTLE_SPK4TURT", sTURTLE_SPK4TURT);
		engine->RegisterEnumValue("Sample", "TURTLE_TURN", sTURTLE_TURN);
		engine->RegisterEnumValue("Sample", "UTERUS_CRABCLOSE", sUTERUS_CRABCLOSE);
		engine->RegisterEnumValue("Sample", "UTERUS_CRABOPEN2", sUTERUS_CRABOPEN2);
		engine->RegisterEnumValue("Sample", "UTERUS_SCISSORS1", sUTERUS_SCISSORS1);
		engine->RegisterEnumValue("Sample", "UTERUS_SCISSORS2", sUTERUS_SCISSORS2);
		engine->RegisterEnumValue("Sample", "UTERUS_SCISSORS3", sUTERUS_SCISSORS3);
		engine->RegisterEnumValue("Sample", "UTERUS_SCISSORS4", sUTERUS_SCISSORS4);
		engine->RegisterEnumValue("Sample", "UTERUS_SCISSORS5", sUTERUS_SCISSORS5);
		engine->RegisterEnumValue("Sample", "UTERUS_SCISSORS6", sUTERUS_SCISSORS6);
		engine->RegisterEnumValue("Sample", "UTERUS_SCISSORS7", sUTERUS_SCISSORS7);
		engine->RegisterEnumValue("Sample", "UTERUS_SCISSORS8", sUTERUS_SCISSORS8);
		engine->RegisterEnumValue("Sample", "UTERUS_SCREAM1", sUTERUS_SCREAM1);
		engine->RegisterEnumValue("Sample", "UTERUS_STEP1", sUTERUS_STEP1);
		engine->RegisterEnumValue("Sample", "UTERUS_STEP2", sUTERUS_STEP2);
		engine->RegisterEnumValue("Sample", "WIND_WIND2A", sWIND_WIND2A);
		engine->RegisterEnumValue("Sample", "WITCH_LAUGH", sWITCH_LAUGH);
		engine->RegisterEnumValue("Sample", "WITCH_MAGIC", sWITCH_MAGIC);
		engine->RegisterEnumValue("Sample", "XBILSY_BILLAPPEAR", sXBILSY_BILLAPPEAR);
		engine->RegisterEnumValue("Sample", "XBILSY_FINGERSNAP", sXBILSY_FINGERSNAP);
		engine->RegisterEnumValue("Sample", "XBILSY_FIRE", sXBILSY_FIRE);
		engine->RegisterEnumValue("Sample", "XBILSY_FIRESTART", sXBILSY_FIRESTART);
		engine->RegisterEnumValue("Sample", "XBILSY_SCARY3", sXBILSY_SCARY3);
		engine->RegisterEnumValue("Sample", "XBILSY_THUNDER", sXBILSY_THUNDER);
		engine->RegisterEnumValue("Sample", "XBILSY_ZIP", sXBILSY_ZIP);
		engine->RegisterEnumValue("Sample", "XLIZARD_LIZ1", sXLIZARD_LIZ1);
		engine->RegisterEnumValue("Sample", "XLIZARD_LIZ2", sXLIZARD_LIZ2);
		engine->RegisterEnumValue("Sample", "XLIZARD_LIZ4", sXLIZARD_LIZ4);
		engine->RegisterEnumValue("Sample", "XLIZARD_LIZ6", sXLIZARD_LIZ6);
		engine->RegisterEnumValue("Sample", "XTURTLE_BITE3", sXTURTLE_BITE3);
		engine->RegisterEnumValue("Sample", "XTURTLE_HIDE", sXTURTLE_HIDE);
		engine->RegisterEnumValue("Sample", "XTURTLE_HITSHELL", sXTURTLE_HITSHELL);
		engine->RegisterEnumValue("Sample", "XTURTLE_IDLE1", sXTURTLE_IDLE1);
		engine->RegisterEnumValue("Sample", "XTURTLE_IDLE2", sXTURTLE_IDLE2);
		engine->RegisterEnumValue("Sample", "XTURTLE_NECK", sXTURTLE_NECK);
		engine->RegisterEnumValue("Sample", "XTURTLE_SPK1TURT", sXTURTLE_SPK1TURT);
		engine->RegisterEnumValue("Sample", "XTURTLE_SPK2TURT", sXTURTLE_SPK2TURT);
		engine->RegisterEnumValue("Sample", "XTURTLE_SPK3TURT", sXTURTLE_SPK3TURT);
		engine->RegisterEnumValue("Sample", "XTURTLE_SPK4TURT", sXTURTLE_SPK4TURT);
		engine->RegisterEnumValue("Sample", "XTURTLE_TURN", sXTURTLE_TURN);
		engine->RegisterEnumValue("Sample", "ZDOG_AGRESSIV", sZDOG_AGRESSIV);
		engine->RegisterEnumValue("Sample", "ZDOG_SNIF1", sZDOG_SNIF1);
		engine->RegisterEnumValue("Sample", "ZDOG_WAF1", sZDOG_WAF1);
		engine->RegisterEnumValue("Sample", "ZDOG_WAF2", sZDOG_WAF2);
		engine->RegisterEnumValue("Sample", "ZDOG_WAF3", sZDOG_WAF3);

		engine->SetDefaultNamespace("AREA");
		engine->RegisterEnumValue("Area", "ONEWAY", areaONEWAY);
		engine->RegisterEnumValue("Area", "HURT", areaHURT);
		engine->RegisterEnumValue("Area", "VINE", areaVINE);
		engine->RegisterEnumValue("Area", "HOOK", areaHOOK);
		engine->RegisterEnumValue("Area", "SLIDE", areaSLIDE);
		engine->RegisterEnumValue("Area", "HPOLE", areaHPOLE);
		engine->RegisterEnumValue("Area", "VPOLE", areaVPOLE);
		engine->RegisterEnumValue("Area", "FLYOFF", areaFLYOFF);
		engine->RegisterEnumValue("Area", "RICOCHET", areaRICOCHET);
		engine->RegisterEnumValue("Area", "BELTRIGHT", areaBELTRIGHT);
		engine->RegisterEnumValue("Area", "BELTLEFT", areaBELTLEFT);
		engine->RegisterEnumValue("Area", "ACCBELTRIGHT", areaBELTACCRIGHT);
		engine->RegisterEnumValue("Area", "ACCBELTLEFT", areaBELTACCLEFT);
		engine->RegisterEnumValue("Area", "STOPENEMY", areaSTOPENEMY);
		engine->RegisterEnumValue("Area", "WINDLEFT", areaWINDLEFT);
		engine->RegisterEnumValue("Area", "WINDRIGHT", areaWINDRIGHT);
		engine->RegisterEnumValue("Area", "EOL", areaEOL);
		engine->RegisterEnumValue("Area", "WARPEOL", areaWARPEOL);
		engine->RegisterEnumValue("Area", "REVERTMORPH", areaENDMORPH);
		engine->RegisterEnumValue("Area", "FLOATUP", areaFLOATUP);
		engine->RegisterEnumValue("Area", "TRIGGERROCK", areaROCKTRIGGER);
		engine->RegisterEnumValue("Area", "DIMLIGHT", areaDIMLIGHT);
		engine->RegisterEnumValue("Area", "SETLIGHT", areaSETLIGHT);
		engine->RegisterEnumValue("Area", "LIMITXSCROLL", areaLIMITXSCROLL);
		engine->RegisterEnumValue("Area", "RESETLIGHT", areaRESETLIGHT);
		engine->RegisterEnumValue("Area", "WARPSECRET", areaWARPSECRET);
		engine->RegisterEnumValue("Area", "ECHO", areaECHO);
		engine->RegisterEnumValue("Area", "ACTIVATEBOSS", areaBOSSTRIGGER);
		engine->RegisterEnumValue("Area", "JAZZLEVELSTART", areaJAZZLEVELSTART);
		engine->RegisterEnumValue("Area", "JAZZSTART", areaJAZZLEVELSTART); // Private/deprecated
		engine->RegisterEnumValue("Area", "SPAZLEVELSTART", areaSPAZLEVELSTART);
		engine->RegisterEnumValue("Area", "SPAZSTART", areaSPAZLEVELSTART); // Private/deprecated
		engine->RegisterEnumValue("Area", "MPLEVELSTART", areaMPLEVELSTART);
		engine->RegisterEnumValue("Area", "MPSTART", areaMPLEVELSTART); // Private/deprecated
		engine->RegisterEnumValue("Area", "LORILEVELSTART", areaLORILEVELSTART);
		engine->RegisterEnumValue("Area", "LORISTART", areaLORILEVELSTART); // Private/deprecated
		engine->RegisterEnumValue("Area", "WARP", areaWARP);
		engine->RegisterEnumValue("Area", "WARPTARGET", areaWARPTARGET);
		engine->RegisterEnumValue("Area", "PATH", areaAREAID);
		engine->RegisterEnumValue("Area", "AREAID", areaAREAID); // Private/deprecated
		engine->RegisterEnumValue("Area", "NOFIREZONE", areaNOFIREZONE);
		engine->RegisterEnumValue("Area", "TRIGGERZONE", areaTRIGGERZONE);

		engine->RegisterEnumValue("Area", "SUCKERTUBE", aSUCKERTUBE);
		engine->RegisterEnumValue("Area", "TEXT", aTEXT);
		engine->RegisterEnumValue("Area", "WATERLEVEL", aWATERLEVEL);
		engine->RegisterEnumValue("Area", "MORPHFROG", aMORPHFROG);
		engine->RegisterEnumValue("Area", "WATERBLOCK", aWATERBLOCK);

		engine->SetDefaultNamespace("OBJECT");
		engine->RegisterEnumValue("Object", "BLASTERBULLET", aPLAYERBULLET1);
		engine->RegisterEnumValue("Object", "BOUNCERBULLET", aPLAYERBULLET2);
		engine->RegisterEnumValue("Object", "ICEBULLET", aPLAYERBULLET3);
		engine->RegisterEnumValue("Object", "SEEKERBULLET", aPLAYERBULLET4);
		engine->RegisterEnumValue("Object", "RFBULLET", aPLAYERBULLET5);
		engine->RegisterEnumValue("Object", "TOASTERBULLET", aPLAYERBULLET6);
		engine->RegisterEnumValue("Object", "FIREBALLBULLET", aPLAYERBULLET8);
		engine->RegisterEnumValue("Object", "ELECTROBULLET", aPLAYERBULLET9);
		engine->RegisterEnumValue("Object", "BLASTERBULLETPU", aPLAYERBULLETP1);
		engine->RegisterEnumValue("Object", "BOUNCERBULLETPU", aPLAYERBULLETP2);
		engine->RegisterEnumValue("Object", "ICEBULLETPU", aPLAYERBULLETP3);
		engine->RegisterEnumValue("Object", "SEEKERBULLETPU", aPLAYERBULLETP4);
		engine->RegisterEnumValue("Object", "RFBULLETPU", aPLAYERBULLETP5);
		engine->RegisterEnumValue("Object", "TOASTERBULLETPU", aPLAYERBULLETP6);
		engine->RegisterEnumValue("Object", "FIREBALLBULLETPU", aPLAYERBULLETP8);
		engine->RegisterEnumValue("Object", "ELECTROBULLETPU", aPLAYERBULLETP9);
		engine->RegisterEnumValue("Object", "FIRESHIELDBULLET", aPLAYERBULLETC1);
		engine->RegisterEnumValue("Object", "WATERSHIELDBULLET", aPLAYERBULLETC2);
		engine->RegisterEnumValue("Object", "BUBBLESHIELDBULLET", aPLAYERBULLETC2); // Private/deprecated
		engine->RegisterEnumValue("Object", "LIGHTNINGSHIELDBULLET", aPLAYERBULLETC3);
		engine->RegisterEnumValue("Object", "PLASMASHIELDBULLET", aPLAYERBULLETC3); // Private/deprecated
		engine->RegisterEnumValue("Object", "BULLET", aBULLET);
		engine->RegisterEnumValue("Object", "SMOKERING", aCATSMOKE);
		engine->RegisterEnumValue("Object", "SHARD", aSHARD);
		engine->RegisterEnumValue("Object", "EXPLOSION", aEXPLOSION);
		engine->RegisterEnumValue("Object", "BOUNCEONCE", aBOUNCEONCE);
		engine->RegisterEnumValue("Object", "FLICKERGEM", aREDGEMTEMP);
		engine->RegisterEnumValue("Object", "LASER", aPLAYERLASER);
		engine->RegisterEnumValue("Object", "UTERUSSPIKEBALL", aUTERUSEL);
		engine->RegisterEnumValue("Object", "BIRD", aBIRD);
		engine->RegisterEnumValue("Object", "BUBBLE", aBUBBLE);
		engine->RegisterEnumValue("Object", "ICEAMMO3", aGUN3AMMO3);
		engine->RegisterEnumValue("Object", "BOUNCERAMMO3", aGUN2AMMO3);
		engine->RegisterEnumValue("Object", "SEEKERAMMO3", aGUN4AMMO3);
		engine->RegisterEnumValue("Object", "RFAMMO3", aGUN5AMMO3);
		engine->RegisterEnumValue("Object", "TOASTERAMMO3", aGUN6AMMO3);
		engine->RegisterEnumValue("Object", "TNTAMMO3", aGUN7AMMO3);
		engine->RegisterEnumValue("Object", "GUN8AMMO3", aGUN8AMMO3);
		engine->RegisterEnumValue("Object", "GUN9AMMO3", aGUN9AMMO3);
		engine->RegisterEnumValue("Object", "TURTLESHELL", aTURTLESHELL);
		engine->RegisterEnumValue("Object", "SWINGINGVINE", aSWINGVINE);
		engine->RegisterEnumValue("Object", "BOMB", aBOMB);
		engine->RegisterEnumValue("Object", "SILVERCOIN", aSILVERCOIN);
		engine->RegisterEnumValue("Object", "GOLDCOIN", aGOLDCOIN);
		engine->RegisterEnumValue("Object", "GUNCRATE", aGUNCRATE);
		engine->RegisterEnumValue("Object", "CARROTCRATE", aCARROTCRATE);
		engine->RegisterEnumValue("Object", "ONEUPCRATE", a1UPCRATE);
		engine->RegisterEnumValue("Object", "GEMBARREL", aGEMBARREL);
		engine->RegisterEnumValue("Object", "CARROTBARREL", aCARROTBARREL);
		engine->RegisterEnumValue("Object", "ONEUPBARREL", a1UPBARREL);
		engine->RegisterEnumValue("Object", "BOMBCRATE", aBOMBCRATE);
		engine->RegisterEnumValue("Object", "ICEAMMO15", aGUN3AMMO15);
		engine->RegisterEnumValue("Object", "BOUNCERAMMO15", aGUN2AMMO15);
		engine->RegisterEnumValue("Object", "SEEKERAMMO15", aGUN4AMMO15);
		engine->RegisterEnumValue("Object", "RFAMMO15", aGUN5AMMO15);
		engine->RegisterEnumValue("Object", "TOASTERAMMO15", aGUN6AMMO15);
		engine->RegisterEnumValue("Object", "TNT", aTNT);
		engine->RegisterEnumValue("Object", "AIRBOARDGENERATOR", aAIRBOARDGENERATOR);
		engine->RegisterEnumValue("Object", "FROZENSPRING", aFROZENGREENSPRING);
		engine->RegisterEnumValue("Object", "FASTFIRE", aGUNFASTFIRE);
		engine->RegisterEnumValue("Object", "SPRINGCRATE", aSPRINGCRATE);
		engine->RegisterEnumValue("Object", "REDGEM", aREDGEM);
		engine->RegisterEnumValue("Object", "GREENGEM", aGREENGEM);
		engine->RegisterEnumValue("Object", "BLUEGEM", aBLUEGEM);
		engine->RegisterEnumValue("Object", "PURPLEGEM", aPURPLEGEM);
		engine->RegisterEnumValue("Object", "SUPERGEM", aSUPERREDGEM);
		engine->RegisterEnumValue("Object", "BIRDCAGE", aBIRDCAGE);
		engine->RegisterEnumValue("Object", "GUNBARREL", aGUNBARREL);
		engine->RegisterEnumValue("Object", "GEMCRATE", aGEMCRATE);
		engine->RegisterEnumValue("Object", "MORPH", aMORPHMONITOR);
		engine->RegisterEnumValue("Object", "CARROT", aENERGYUP);
		engine->RegisterEnumValue("Object", "FULLENERGY", aFULLENERGY);
		engine->RegisterEnumValue("Object", "FIRESHIELD", aFIRESHIELD);
		engine->RegisterEnumValue("Object", "WATERSHIELD", aWATERSHIELD);
		engine->RegisterEnumValue("Object", "BUBBLESHIELD", aWATERSHIELD); // Private/deprecated
		engine->RegisterEnumValue("Object", "LIGHTNINGSHIELD", aLIGHTSHIELD);
		engine->RegisterEnumValue("Object", "PLASMASHIELD", aLIGHTSHIELD); // Private/deprecated
		engine->RegisterEnumValue("Object", "FASTFEET", aFASTFEET);
		engine->RegisterEnumValue("Object", "ONEUP", aEXTRALIFE);
		engine->RegisterEnumValue("Object", "EXTRALIFE", aEXTRALIFE); // Private/deprecated
		engine->RegisterEnumValue("Object", "EXTRALIVE", aEXTRALIFE); // Private/deprecated
		engine->RegisterEnumValue("Object", "EOLPOST", aENDOFLEVELPOST);
		engine->RegisterEnumValue("Object", "SAVEPOST", aSAVEPOST);
		engine->RegisterEnumValue("Object", "CHECKPOINT", aSAVEPOST); // Private/deprecated
		engine->RegisterEnumValue("Object", "BONUSPOST", aBONUSLEVELPOST);
		engine->RegisterEnumValue("Object", "REDSPRING", aREDSPRING);
		engine->RegisterEnumValue("Object", "GREENSPRING", aGREENSPRING);
		engine->RegisterEnumValue("Object", "BLUESPRING", aBLUESPRING);
		engine->RegisterEnumValue("Object", "INVINCIBILITY", aINVINCIBILITY);
		engine->RegisterEnumValue("Object", "EXTRATIME", aEXTRATIME);
		engine->RegisterEnumValue("Object", "FREEZER", aFREEZER);
		engine->RegisterEnumValue("Object", "FREEZEENEMIES", aFREEZER); // Private/deprecated
		engine->RegisterEnumValue("Object", "HORREDSPRING", aHREDSPRING);
		engine->RegisterEnumValue("Object", "HORGREENSPRING", aHGREENSPRING);
		engine->RegisterEnumValue("Object", "HORBLUESPRING", aHBLUESPRING);
		engine->RegisterEnumValue("Object", "BIRDMORPH", aBIRDMORPHMONITOR);
		engine->RegisterEnumValue("Object", "TRIGGERCRATE", aTRIGGERCRATE);
		engine->RegisterEnumValue("Object", "FLYCARROT", aFLYCARROT);
		engine->RegisterEnumValue("Object", "RECTREDGEM", aRECTREDGEM);
		engine->RegisterEnumValue("Object", "RECTGREENGEM", aRECTGREENGEM);
		engine->RegisterEnumValue("Object", "RECTBLUEGEM", aRECTBLUEGEM);
		engine->RegisterEnumValue("Object", "TUFTURT", aTUFTURT);
		engine->RegisterEnumValue("Object", "TUFBOSS", aTUFBOSS);
		engine->RegisterEnumValue("Object", "LABRAT", aLABRAT);
		engine->RegisterEnumValue("Object", "DRAGON", aDRAGON);
		engine->RegisterEnumValue("Object", "LIZARD", aLIZARD);
		engine->RegisterEnumValue("Object", "BEE", aBUMBEE);
		engine->RegisterEnumValue("Object", "BUMBEE", aBUMBEE); // Private/deprecated
		engine->RegisterEnumValue("Object", "RAPIER", aRAPIER);
		engine->RegisterEnumValue("Object", "SPARK", aSPARK);
		engine->RegisterEnumValue("Object", "BAT", aBAT);
		engine->RegisterEnumValue("Object", "SUCKER", aSUCKER);
		engine->RegisterEnumValue("Object", "CATERPILLAR", aCATERPILLAR);
		engine->RegisterEnumValue("Object", "CHESHIRE1", aCHESHIRE1);
		engine->RegisterEnumValue("Object", "CHESHIRE2", aCHESHIRE2);
		engine->RegisterEnumValue("Object", "HATTER", aHATTER);
		engine->RegisterEnumValue("Object", "BILSY", aBILSYBOSS);
		engine->RegisterEnumValue("Object", "SKELETON", aSKELETON);
		engine->RegisterEnumValue("Object", "DOGGYDOGG", aDOGGYDOGG);
		engine->RegisterEnumValue("Object", "NORMTURTLE", aNORMTURTLE);
		engine->RegisterEnumValue("Object", "HELMUT", aHELMUT);
		engine->RegisterEnumValue("Object", "DEMON", aDEMON);
		engine->RegisterEnumValue("Object", "DRAGONFLY", aDRAGONFLY);
		engine->RegisterEnumValue("Object", "MONKEY", aMONKEY);
		engine->RegisterEnumValue("Object", "FATCHICK", aFATCHK);
		engine->RegisterEnumValue("Object", "FENCER", aFENCER);
		engine->RegisterEnumValue("Object", "FISH", aFISH);
		engine->RegisterEnumValue("Object", "MOTH", aMOTH);
		engine->RegisterEnumValue("Object", "STEAM", aSTEAM);
		engine->RegisterEnumValue("Object", "ROTATINGROCK", aROCK);
		engine->RegisterEnumValue("Object", "BLASTERPOWERUP", aGUN1POWER);
		engine->RegisterEnumValue("Object", "BOUNCERPOWERUP", aGUN2POWER);
		engine->RegisterEnumValue("Object", "ICEPOWERUP", aGUN3POWER);
		engine->RegisterEnumValue("Object", "SEEKERPOWERUP", aGUN4POWER);
		engine->RegisterEnumValue("Object", "RFPOWERUP", aGUN5POWER);
		engine->RegisterEnumValue("Object", "TOASTERPOWERUP", aGUN6POWER);
		engine->RegisterEnumValue("Object", "LEFTPADDLE", aPINLEFTPADDLE);
		engine->RegisterEnumValue("Object", "RIGHTPADDLE", aPINRIGHTPADDLE);
		engine->RegisterEnumValue("Object", "FIVEHUNDREDBUMP", aPIN500BUMP);
		engine->RegisterEnumValue("Object", "CARROTBUMP", aPINCARROTBUMP);
		engine->RegisterEnumValue("Object", "APPLE", aAPPLE);
		engine->RegisterEnumValue("Object", "BANANA", aBANANA);
		engine->RegisterEnumValue("Object", "CHERRY", aCHERRY);
		engine->RegisterEnumValue("Object", "ORANGE", aORANGE);
		engine->RegisterEnumValue("Object", "PEAR", aPEAR);
		engine->RegisterEnumValue("Object", "PRETZEL", aPRETZEL);
		engine->RegisterEnumValue("Object", "STRAWBERRY", aSTRAWBERRY);
		engine->RegisterEnumValue("Object", "STEADYLIGHT", aSTEADYLIGHT);
		engine->RegisterEnumValue("Object", "PULZELIGHT", aPULZELIGHT);
		engine->RegisterEnumValue("Object", "PULSELIGHT", aPULZELIGHT); // Private/deprecated
		engine->RegisterEnumValue("Object", "FLICKERLIGHT", aFLICKERLIGHT);
		engine->RegisterEnumValue("Object", "QUEEN", aQUEENBOSS);
		engine->RegisterEnumValue("Object", "FLOATSUCKER", aFLOATSUCKER);
		engine->RegisterEnumValue("Object", "BRIDGE", aBRIDGE);
		engine->RegisterEnumValue("Object", "LEMON", aLEMON);
		engine->RegisterEnumValue("Object", "LIME", aLIME);
		engine->RegisterEnumValue("Object", "THING", aTHING);
		engine->RegisterEnumValue("Object", "WATERMELON", aWMELON);
		engine->RegisterEnumValue("Object", "PEACH", aPEACH);
		engine->RegisterEnumValue("Object", "GRAPES", aGRAPES);
		engine->RegisterEnumValue("Object", "LETTUCE", aLETTUCE);
		engine->RegisterEnumValue("Object", "EGGPLANT", aEGGPLANT);
		engine->RegisterEnumValue("Object", "CUCUMB", aCUCUMB);
		engine->RegisterEnumValue("Object", "CUCUMBER", aCUCUMB); // Private/deprecated
		engine->RegisterEnumValue("Object", "COKE", aCOKE);
		engine->RegisterEnumValue("Object", "SOFTDRINK", aCOKE); // Private/deprecated
		engine->RegisterEnumValue("Object", "PEPSI", aPEPSI);
		engine->RegisterEnumValue("Object", "SODAPOP", aCOKE); // Private/deprecated
		engine->RegisterEnumValue("Object", "MILK", aMILK);
		engine->RegisterEnumValue("Object", "PIE", aPIE);
		engine->RegisterEnumValue("Object", "CAKE", aCAKE);
		engine->RegisterEnumValue("Object", "DONUT", aDONUT);
		engine->RegisterEnumValue("Object", "CUPCAKE", aCUPCAKE);
		engine->RegisterEnumValue("Object", "CHIPS", aCHIPS);
		engine->RegisterEnumValue("Object", "CANDY", aCANDY1);
		engine->RegisterEnumValue("Object", "CHOCBAR", aCHOCBAR);
		engine->RegisterEnumValue("Object", "aCHOCOLATEBAR", aCHOCBAR); // Private/deprecated
		engine->RegisterEnumValue("Object", "ICECREAM", aICECREAM);
		engine->RegisterEnumValue("Object", "BURGER", aBURGER);
		engine->RegisterEnumValue("Object", "PIZZA", aPIZZA);
		engine->RegisterEnumValue("Object", "FRIES", aFRIES);
		engine->RegisterEnumValue("Object", "CHICKENLEG", aCHICKLEG);
		engine->RegisterEnumValue("Object", "SANDWICH", aSANDWICH);
		engine->RegisterEnumValue("Object", "TACO", aTACOBELL);
		engine->RegisterEnumValue("Object", "WEENIE", aWEENIE);
		engine->RegisterEnumValue("Object", "HAM", aHAM);
		engine->RegisterEnumValue("Object", "CHEESE", aCHEESE);
		engine->RegisterEnumValue("Object", "FLOATLIZARD", aFLOATLIZARD);
		engine->RegisterEnumValue("Object", "STANDMONKEY", aSTANDMONKEY);
		engine->RegisterEnumValue("Object", "DESTRUCTSCENERY", aDESTRUCTSCENERY);
		engine->RegisterEnumValue("Object", "DESTRUCTSCENERYBOMB", aDESTRUCTSCENERYBOMB);
		engine->RegisterEnumValue("Object", "TNTDESTRUCTSCENERY", aDESTRUCTSCENERYBOMB); // Private/deprecated
		engine->RegisterEnumValue("Object", "COLLAPSESCENERY", aCOLLAPSESCENERY);
		engine->RegisterEnumValue("Object", "STOMPSCENERY", aSTOMPSCENERY);
		engine->RegisterEnumValue("Object", "GEMSTOMP", aGEMSTOMP);
		engine->RegisterEnumValue("Object", "RAVEN", aRAVEN);
		engine->RegisterEnumValue("Object", "TUBETURTLE", aTUBETURTLE);
		engine->RegisterEnumValue("Object", "GEMRING", aGEMRING);
		engine->RegisterEnumValue("Object", "SMALLTREE", aROTSMALLTREE);
		engine->RegisterEnumValue("Object", "AMBIENTSOUND", aAMBIENTSOUND);
		engine->RegisterEnumValue("Object", "UTERUS", aUTERUS);
		engine->RegisterEnumValue("Object", "CRAB", aCRAB);
		engine->RegisterEnumValue("Object", "WITCH", aWITCH);
		engine->RegisterEnumValue("Object", "ROCKETTURTLE", aROCKTURT);
		engine->RegisterEnumValue("Object", "BUBBA", aBUBBA);
		engine->RegisterEnumValue("Object", "DEVILDEVAN", aDEVILDEVAN);
		engine->RegisterEnumValue("Object", "DEVANROBOT", aDEVANROBOT);
		engine->RegisterEnumValue("Object", "ROBOT", aROBOT);
		engine->RegisterEnumValue("Object", "CARROTUSPOLE", aCARROTUSPOLE);
		engine->RegisterEnumValue("Object", "PSYCHPOLE", aPSYCHPOLE);
		engine->RegisterEnumValue("Object", "DIAMONDUSPOLE", aDIAMONDUSPOLE);
		engine->RegisterEnumValue("Object", "FRUITPLATFORM", aFRUITPLATFORM);
		engine->RegisterEnumValue("Object", "BOLLPLATFORM", aBOLLPLATFORM);
		engine->RegisterEnumValue("Object", "GRASSPLATFORM", aGRASSPLATFORM);
		engine->RegisterEnumValue("Object", "PINKPLATFORM", aPINKPLATFORM);
		engine->RegisterEnumValue("Object", "SONICPLATFORM", aSONICPLATFORM);
		engine->RegisterEnumValue("Object", "SPIKEPLATFORM", aSPIKEPLATFORM);
		engine->RegisterEnumValue("Object", "SPIKEBOLL", aSPIKEBOLL);
		engine->RegisterEnumValue("Object", "GENERATOR", aGENERATOR);
		engine->RegisterEnumValue("Object", "EVA", aEVA);
		engine->RegisterEnumValue("Object", "BUBBLER", aBUBBLER);
		engine->RegisterEnumValue("Object", "TNTPOWERUP", aTNTPOWER);
		engine->RegisterEnumValue("Object", "GUN8POWERUP", aGUN8POWER);
		engine->RegisterEnumValue("Object", "GUN9POWERUP", aGUN9POWER);
		engine->RegisterEnumValue("Object", "SPIKEBOLL3D", aSPIKEBOLL3D);
		engine->RegisterEnumValue("Object", "SPRINGCORD", aSPRINGCORD);
		engine->RegisterEnumValue("Object", "BEES", aBEES);
		engine->RegisterEnumValue("Object", "COPTER", aCOPTER);
		engine->RegisterEnumValue("Object", "LASERSHIELD", aLASERSHIELD);
		engine->RegisterEnumValue("Object", "STOPWATCH", aSTOPWATCH);
		engine->RegisterEnumValue("Object", "JUNGLEPOLE", aJUNGLEPOLE);
		engine->RegisterEnumValue("Object", "WARP", areaWARP);
		engine->RegisterEnumValue("Object", "BIGROCK", aBIGROCK);
		engine->RegisterEnumValue("Object", "BIGBOX", aBIGBOX);
		engine->RegisterEnumValue("Object", "TRIGGERSCENERY", aTRIGGERSCENERY);
		engine->RegisterEnumValue("Object", "BOLLY", aSONICBOSS);
		engine->RegisterEnumValue("Object", "BUTTERFLY", aBUTTERFLY);
		engine->RegisterEnumValue("Object", "BEEBOY", aBEEBOY);
		engine->RegisterEnumValue("Object", "SNOW", aSNOW);
		engine->RegisterEnumValue("Object", "TWEEDLEBOSS", aTWEEDLEBOSS);
		engine->RegisterEnumValue("Object", "AIRBOARD", aAIRBOARD);
		engine->RegisterEnumValue("Object", "CTFBASE", aFLAG);
		engine->RegisterEnumValue("Object", "XMASNORMTURTLE", aXNORMTURTLE);
		engine->RegisterEnumValue("Object", "XMASLIZARD", aXLIZARD);
		engine->RegisterEnumValue("Object", "XMASFLOATLIZARD", aXFLOATLIZARD);
		engine->RegisterEnumValue("Object", "XMASBILSY", aXBILSYBOSS);
		engine->RegisterEnumValue("Object", "CAT", aZCAT);
		engine->RegisterEnumValue("Object", "PACMANGHOST", aZGHOST);

		engine->SetDefaultNamespace("ANIM");
		engine->RegisterEnumValue("Set", "AMMO", mAMMO);
		engine->RegisterEnumValue("Set", "BAT", mBAT);
		engine->RegisterEnumValue("Set", "BEEBOY", mBEEBOY);
		engine->RegisterEnumValue("Set", "BEES", mBEES);
		engine->RegisterEnumValue("Set", "BIGBOX", mBIGBOX);
		engine->RegisterEnumValue("Set", "BIGROCK", mBIGROCK);
		engine->RegisterEnumValue("Set", "BIGTREE", mBIGTREE);
		engine->RegisterEnumValue("Set", "BILSBOSS", mBILSBOSS);
		engine->RegisterEnumValue("Set", "BIRD", mBIRD);
		engine->RegisterEnumValue("Set", "BIRD3D", mBIRD3D);
		engine->RegisterEnumValue("Set", "BOLLPLAT", mBOLLPLAT);
		engine->RegisterEnumValue("Set", "BONUS", mBONUS);
		engine->RegisterEnumValue("Set", "BOSS", mBOSS);
		engine->RegisterEnumValue("Set", "BRIDGE", mBRIDGE);
		engine->RegisterEnumValue("Set", "BUBBA", mBUBBA);
		engine->RegisterEnumValue("Set", "BUMBEE", mBUMBEE);
		engine->RegisterEnumValue("Set", "BUTTERFLY", mBUTTERFLY);
		engine->RegisterEnumValue("Set", "CARROTPOLE", mCARROTPOLE);
		engine->RegisterEnumValue("Set", "CAT", mCAT);
		engine->RegisterEnumValue("Set", "CAT2", mCAT2);
		engine->RegisterEnumValue("Set", "CATERPIL", mCATERPIL);
		engine->RegisterEnumValue("Set", "CHUCK", mCHUCK);
		engine->RegisterEnumValue("Set", "COMMON", mCOMMON);
		engine->RegisterEnumValue("Set", "CONTINUE", mCONTINUE);
		engine->RegisterEnumValue("Set", "DEMON", mDEMON);
		engine->RegisterEnumValue("Set", "DESTSCEN", mDESTSCEN);
		engine->RegisterEnumValue("Set", "DEVAN", mDEVAN);
		engine->RegisterEnumValue("Set", "DEVILDEVAN", mDEVILDEVAN);
		engine->RegisterEnumValue("Set", "DIAMPOLE", mDIAMPOLE);
		engine->RegisterEnumValue("Set", "DOG", mDOG);
		engine->RegisterEnumValue("Set", "DOOR", mDOOR);
		engine->RegisterEnumValue("Set", "DRAGFLY", mDRAGFLY);
		engine->RegisterEnumValue("Set", "DRAGON", mDRAGON);

		engine->RegisterEnumValue("Set", "EVA", mEVA);
		engine->RegisterEnumValue("Set", "FACES", mFACES);

		engine->RegisterEnumValue("Set", "FATCHK", mFATCHK);
		engine->RegisterEnumValue("Set", "FENCER", mFENCER);
		engine->RegisterEnumValue("Set", "FISH", mFISH);
		engine->RegisterEnumValue("Set", "FLAG", mFLAG);
		engine->RegisterEnumValue("Set", "FLARE", mFLARE);
		engine->RegisterEnumValue("Set", "FONT", mFONT);
		engine->RegisterEnumValue("Set", "FROG", mFROG);
		engine->RegisterEnumValue("Set", "FRUITPLAT", mFRUITPLAT);
		engine->RegisterEnumValue("Set", "GEMRING", mGEMRING);
		engine->RegisterEnumValue("Set", "GLOVE", mGLOVE);
		engine->RegisterEnumValue("Set", "GRASSPLAT", mGRASSPLAT);
		engine->RegisterEnumValue("Set", "HATTER", mHATTER);
		engine->RegisterEnumValue("Set", "HELMUT", mHELMUT);

		engine->RegisterEnumValue("Set", "JAZZ", mJAZZ);
		engine->RegisterEnumValue("Set", "JAZZ3D", mJAZZ3D);

		engine->RegisterEnumValue("Set", "JUNGLEPOLE", mJUNGLEPOLE);
		engine->RegisterEnumValue("Set", "LABRAT", mLABRAT);
		engine->RegisterEnumValue("Set", "LIZARD", mLIZARD);
		engine->RegisterEnumValue("Set", "LORI", mLORI);
		engine->RegisterEnumValue("Set", "LORI2", mLORI2);

		engine->RegisterEnumValue("Set", "MENU", mMENU);
		engine->RegisterEnumValue("Set", "MENUFONT", mMENUFONT);

		engine->RegisterEnumValue("Set", "MONKEY", mMONKEY);
		engine->RegisterEnumValue("Set", "MOTH", mMOTH);

		engine->RegisterEnumValue("Set", "PICKUPS", mPICKUPS);
		engine->RegisterEnumValue("Set", "PINBALL", mPINBALL);
		engine->RegisterEnumValue("Set", "PINKPLAT", mPINKPLAT);
		engine->RegisterEnumValue("Set", "PSYCHPOLE", mPSYCHPOLE);
		engine->RegisterEnumValue("Set", "QUEEN", mQUEEN);
		engine->RegisterEnumValue("Set", "RAPIER", mRAPIER);
		engine->RegisterEnumValue("Set", "RAVEN", mRAVEN);
		engine->RegisterEnumValue("Set", "ROBOT", mROBOT);
		engine->RegisterEnumValue("Set", "ROCK", mROCK);
		engine->RegisterEnumValue("Set", "ROCKTURT", mROCKTURT);

		engine->RegisterEnumValue("Set", "SKELETON", mSKELETON);
		engine->RegisterEnumValue("Set", "SMALTREE", mSMALTREE);
		engine->RegisterEnumValue("Set", "SNOW", mSNOW);
		engine->RegisterEnumValue("Set", "SONCSHIP", mSONCSHIP);
		engine->RegisterEnumValue("Set", "SONICSHIP", mSONCSHIP); // Private/deprecated
		engine->RegisterEnumValue("Set", "SONICPLAT", mSONICPLAT);
		engine->RegisterEnumValue("Set", "SPARK", mSPARK);
		engine->RegisterEnumValue("Set", "SPAZ", mSPAZ);
		engine->RegisterEnumValue("Set", "SPAZ2", mSPAZ2);
		engine->RegisterEnumValue("Set", "SPAZ3D", mSPAZ3D);
		engine->RegisterEnumValue("Set", "SPIKEBOLL", mSPIKEBOLL);
		engine->RegisterEnumValue("Set", "SPIKEBOLL3D", mSPIKEBOLL3D);
		engine->RegisterEnumValue("Set", "SPIKEPLAT", mSPIKEPLAT);
		engine->RegisterEnumValue("Set", "SPRING", mSPRING);
		engine->RegisterEnumValue("Set", "STEAM", mSTEAM);

		engine->RegisterEnumValue("Set", "SUCKER", mSUCKER);
		engine->RegisterEnumValue("Set", "TUBETURT", mTUBETURT);
		engine->RegisterEnumValue("Set", "TUFBOSS", mTUFBOSS);
		engine->RegisterEnumValue("Set", "TUFTUR", mTUFTURT);
		engine->RegisterEnumValue("Set", "TURTLE", mTURTLE);
		engine->RegisterEnumValue("Set", "TWEEDLE", mTWEEDLE);
		engine->RegisterEnumValue("Set", "UTERUS", mUTERUS);
		engine->RegisterEnumValue("Set", "VINE", mVINE);
		engine->RegisterEnumValue("Set", "WARP10", mWARP10);
		engine->RegisterEnumValue("Set", "WARP100", mWARP100);
		engine->RegisterEnumValue("Set", "WARP20", mWARP20);
		engine->RegisterEnumValue("Set", "WARP50", mWARP50);

		engine->RegisterEnumValue("Set", "WITCH", mWITCH);
		engine->RegisterEnumValue("Set", "XBILSY", mXBILSY);
		engine->RegisterEnumValue("Set", "XLIZARD", mXLIZARD);
		engine->RegisterEnumValue("Set", "XTURTLE", mXTURTLE);
		engine->RegisterEnumValue("Set", "ZDOG", mZDOG);
		engine->RegisterEnumValue("Set", "ZSPARK", mZSPARK);
		engine->RegisterEnumValue("Set", "PLUS_AMMO", mZZAMMO);
		engine->RegisterEnumValue("Set", "PLUS_BETA", mZZBETA);
		engine->RegisterEnumValue("Set", "PLUS_COMMON", mZZCOMMON);
		engine->RegisterEnumValue("Set", "PLUS_CONTINUE", mZZCONTINUE);

		engine->RegisterEnumValue("Set", "PLUS_FONT", mZZFONT);
		engine->RegisterEnumValue("Set", "PLUS_MENUFONT", mZZMENUFONT);
		engine->RegisterEnumValue("Set", "PLUS_REPLACEMENTS", mZZREPLACEMENTS);
		engine->RegisterEnumValue("Set", "PLUS_RETICLES", mZZRETICLES);
		engine->RegisterEnumValue("Set", "PLUS_SCENERY", mZZSCENERY);
		engine->RegisterEnumValue("Set", "PLUS_WARP", mZZWARP);
		//engine->RegisterGlobalFunction("Set get_CUSTOM(uint8) property", asFUNCTION(getCustomSetID), asCALL_CDECL);

		engine->SetDefaultNamespace("RABBIT");
		engine->RegisterEnum("Anim");
		engine->RegisterEnumValue("Anim", "AIRBOARD", mJAZZ_AIRBOARD);
		engine->RegisterEnumValue("Anim", "AIRBOARDTURN", mJAZZ_AIRBOARDTURN);
		engine->RegisterEnumValue("Anim", "BUTTSTOMPLAND", mJAZZ_BUTTSTOMPLAND);
		engine->RegisterEnumValue("Anim", "CORPSE", mJAZZ_CORPSE);
		engine->RegisterEnumValue("Anim", "DIE", mJAZZ_DIE);
		engine->RegisterEnumValue("Anim", "DIVE", mJAZZ_DIVE);
		engine->RegisterEnumValue("Anim", "DIVEFIREQUIT", mJAZZ_DIVEFIREQUIT);
		engine->RegisterEnumValue("Anim", "DIVEFIRERIGHT", mJAZZ_DIVEFIRERIGHT);
		engine->RegisterEnumValue("Anim", "DIVEUP", mJAZZ_DIVEUP);
		engine->RegisterEnumValue("Anim", "EARBRACHIATE", mJAZZ_EARBRACHIATE);
		engine->RegisterEnumValue("Anim", "ENDOFLEVEL", mJAZZ_ENDOFLEVEL);
		engine->RegisterEnumValue("Anim", "FALL", mJAZZ_FALL);
		engine->RegisterEnumValue("Anim", "FALLBUTTSTOMP", mJAZZ_FALLBUTTSTOMP);
		engine->RegisterEnumValue("Anim", "FALLLAND", mJAZZ_FALLLAND);
		engine->RegisterEnumValue("Anim", "FIRE", mJAZZ_FIRE);
		engine->RegisterEnumValue("Anim", "FIREUP", mJAZZ_FIREUP);
		engine->RegisterEnumValue("Anim", "FIREUPQUIT", mJAZZ_FIREUPQUIT);
		engine->RegisterEnumValue("Anim", "FROG", mJAZZ_FROG);
		engine->RegisterEnumValue("Anim", "HANGFIREQUIT", mJAZZ_HANGFIREQUIT);
		engine->RegisterEnumValue("Anim", "HANGFIREREST", mJAZZ_HANGFIREREST);
		engine->RegisterEnumValue("Anim", "HANGFIREUP", mJAZZ_HANGFIREUP);
		engine->RegisterEnumValue("Anim", "HANGIDLE1", mJAZZ_HANGIDLE1);
		engine->RegisterEnumValue("Anim", "HANGIDLE2", mJAZZ_HANGIDLE2);
		engine->RegisterEnumValue("Anim", "HANGINGFIREQUIT", mJAZZ_HANGINGFIREQUIT);
		engine->RegisterEnumValue("Anim", "HANGINGFIRERIGHT", mJAZZ_HANGINGFIRERIGHT);
		engine->RegisterEnumValue("Anim", "HELICOPTER", mJAZZ_HELICOPTER);
		engine->RegisterEnumValue("Anim", "HELICOPTERFIREQUIT", mJAZZ_HELICOPTERFIREQUIT);
		engine->RegisterEnumValue("Anim", "HELICOPTERFIRERIGHT", mJAZZ_HELICOPTERFIRERIGHT);
		engine->RegisterEnumValue("Anim", "HPOLE", mJAZZ_HPOLE);
		engine->RegisterEnumValue("Anim", "HURT", mJAZZ_HURT);
		engine->RegisterEnumValue("Anim", "IDLE1", mJAZZ_IDLE1);
		engine->RegisterEnumValue("Anim", "IDLE2", mJAZZ_IDLE2);
		engine->RegisterEnumValue("Anim", "IDLE3", mJAZZ_IDLE3);
		engine->RegisterEnumValue("Anim", "IDLE4", mJAZZ_IDLE4);
		engine->RegisterEnumValue("Anim", "IDLE5", mJAZZ_IDLE5);
		engine->RegisterEnumValue("Anim", "JUMPFIREQUIT", mJAZZ_JUMPFIREQUIT);
		engine->RegisterEnumValue("Anim", "JUMPFIRERIGHT", mJAZZ_JUMPFIRERIGHT);
		engine->RegisterEnumValue("Anim", "JUMPING1", mJAZZ_JUMPING1);
		engine->RegisterEnumValue("Anim", "JUMPING2", mJAZZ_JUMPING2);
		engine->RegisterEnumValue("Anim", "JUMPING3", mJAZZ_JUMPING3);
		engine->RegisterEnumValue("Anim", "LEDGEWIGGLE", mJAZZ_LEDGEWIGGLE);
		engine->RegisterEnumValue("Anim", "LIFT", mJAZZ_LIFT);
		engine->RegisterEnumValue("Anim", "LIFTJUMP", mJAZZ_LIFTJUMP);
		engine->RegisterEnumValue("Anim", "LIFTLAND", mJAZZ_LIFTLAND);
		engine->RegisterEnumValue("Anim", "LOOKUP", mJAZZ_LOOKUP);
		engine->RegisterEnumValue("Anim", "LOOPY", mJAZZ_LOOPY);
		engine->RegisterEnumValue("Anim", "PUSH", mJAZZ_PUSH);
		engine->RegisterEnumValue("Anim", "QUIT", mJAZZ_QUIT);
		engine->RegisterEnumValue("Anim", "REV1", mJAZZ_REV1);
		engine->RegisterEnumValue("Anim", "REV2", mJAZZ_REV2);
		engine->RegisterEnumValue("Anim", "REV3", mJAZZ_REV3);
		engine->RegisterEnumValue("Anim", "RIGHTFALL", mJAZZ_RIGHTFALL);
		engine->RegisterEnumValue("Anim", "RIGHTJUMP", mJAZZ_RIGHTJUMP);
		engine->RegisterEnumValue("Anim", "ROLLING", mJAZZ_ROLLING);
		engine->RegisterEnumValue("Anim", "RUN1", mJAZZ_RUN1);
		engine->RegisterEnumValue("Anim", "RUN2", mJAZZ_RUN2);
		engine->RegisterEnumValue("Anim", "RUN3", mJAZZ_RUN3);
		engine->RegisterEnumValue("Anim", "SKID1", mJAZZ_SKID1);
		engine->RegisterEnumValue("Anim", "SKID2", mJAZZ_SKID2);
		engine->RegisterEnumValue("Anim", "SKID3", mJAZZ_SKID3);
		engine->RegisterEnumValue("Anim", "SPRING", mJAZZ_SPRING);
		engine->RegisterEnumValue("Anim", "STAND", mJAZZ_STAND);
		engine->RegisterEnumValue("Anim", "STATIONARYJUMP", mJAZZ_STATIONARYJUMP);
		engine->RegisterEnumValue("Anim", "STATIONARYJUMPEND", mJAZZ_STATIONARYJUMPEND);
		engine->RegisterEnumValue("Anim", "STATIONARYJUMPSTART", mJAZZ_STATIONARYJUMPSTART);
		engine->RegisterEnumValue("Anim", "STONED", mJAZZ_STONED);
		engine->RegisterEnumValue("Anim", "SWIMDOWN", mJAZZ_SWIMDOWN);
		engine->RegisterEnumValue("Anim", "SWIMRIGHT", mJAZZ_SWIMRIGHT);
		engine->RegisterEnumValue("Anim", "SWIMTURN1", mJAZZ_SWIMTURN1);
		engine->RegisterEnumValue("Anim", "SWIMTURN2", mJAZZ_SWIMTURN2);
		engine->RegisterEnumValue("Anim", "SWIMUP", mJAZZ_SWIMUP);
		engine->RegisterEnumValue("Anim", "SWINGINGVINE", mJAZZ_SWINGINGVINE);
		engine->RegisterEnumValue("Anim", "TELEPORT", mJAZZ_TELEPORT);
		engine->RegisterEnumValue("Anim", "TELEPORTFALL", mJAZZ_TELEPORTFALL);
		engine->RegisterEnumValue("Anim", "TELEPORTFALLING", mJAZZ_TELEPORTFALLING);
		engine->RegisterEnumValue("Anim", "TELEPORTFALLTELEPORT", mJAZZ_TELEPORTFALLTELEPORT);
		engine->RegisterEnumValue("Anim", "TELEPORTSTAND", mJAZZ_TELEPORTSTAND);
		engine->RegisterEnumValue("Anim", "VPOLE", mJAZZ_VPOLE);
	}

	void LevelScripts::RegisterStandardFunctions(asIScriptEngine* engine, asIScriptModule* module)
	{
		int r;
		r = engine->RegisterGlobalFunction("int Random()", asFUNCTIONPR(asRandom, (), int), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("int Random(int)", asFUNCTIONPR(asRandom, (int), int), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("float Random(float, float)", asFUNCTIONPR(asRandom, (float, float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		
		r = engine->RegisterGlobalFunction("void Print(const string &in)", asFUNCTION(asScript), asCALL_CDECL); RETURN_ASSERT(r >= 0);

		r = engine->RegisterGlobalFunction("uint8 get_Difficulty() property", asFUNCTION(asGetDifficulty), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("bool get_IsReforged() property", asFUNCTION(asIsReforged), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("int get_LevelWidth() property", asFUNCTION(asGetLevelWidth), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("int get_LevelHeight() property", asFUNCTION(asGetLevelHeight), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("float get_ElapsedFrames() property", asFUNCTION(asGetElapsedFrames), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("float get_AmbientLight() property", asFUNCTION(asGetAmbientLight), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("void set_AmbientLight(float) property", asFUNCTION(asSetAmbientLight), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("float get_WaterLevel() property", asFUNCTION(asGetWaterLevel), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("void set_WaterLevel(float) property", asFUNCTION(asSetWaterLevel), asCALL_CDECL); RETURN_ASSERT(r >= 0);

		r = engine->RegisterGlobalFunction("void PreloadMetadata(const string &in)", asFUNCTION(asPreloadMetadata), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("void RegisterSpawnable(int, const string &in)", asFUNCTION(asRegisterSpawnable), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("void Spawn(int, int, int)", asFUNCTION(asSpawnEvent), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("void Spawn(int, int, int, const array<uint8> &in)", asFUNCTION(asSpawnEventParams), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("void Spawn(const string &in, int, int)", asFUNCTION(asSpawnType), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("void Spawn(const string &in, int, int, const array<uint8> &in)", asFUNCTION(asSpawnTypeParams), asCALL_CDECL); RETURN_ASSERT(r >= 0);

		r = engine->RegisterGlobalFunction("void ChangeLevel(int, const string &in = string())", asFUNCTION(asChangeLevel), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("void MusicPlay(const string &in)", asFUNCTION(asMusicPlay), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("void ShowLevelText(const string &in)", asFUNCTION(asShowLevelText), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("void SetWeather(uint8, uint8)", asFUNCTION(asSetWeather), asCALL_CDECL); RETURN_ASSERT(r >= 0);

		// Game-specific classes
		ScriptActorWrapper::RegisterFactory(engine, module);
		ScriptPlayerWrapper::RegisterFactory(engine);
	}

	Actors::ActorBase* LevelScripts::CreateActorInstance(const StringView& typeName)
	{
		auto nullTerminatedTypeName = String::nullTerminatedView(typeName);

		// Create an instance of the ActorBase script class that inherits from the ScriptActorWrapper C++ class
		asITypeInfo* typeInfo = _module->GetTypeInfoByName(nullTerminatedTypeName.data());
		if (typeInfo == nullptr) {
			return nullptr;
		}

		asIScriptObject* obj = reinterpret_cast<asIScriptObject*>(_engine->CreateScriptObject(typeInfo));

		// Get the pointer to the C++ side of the ActorBase class
		ScriptActorWrapper* obj2 = *reinterpret_cast<ScriptActorWrapper**>(obj->GetAddressOfProperty(0));

		// Increase the reference count to the C++ object, as this is what will be used to control the life time of the object from the application side 
		obj2->AddRef();

		// Release the reference to the script side
		obj->Release();

		return obj2;
	}

	const SmallVectorImpl<Actors::Player*>& LevelScripts::GetPlayers() const
	{
		return _levelHandler->_players;
	}

	uint8_t LevelScripts::asGetDifficulty()
	{
		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		return (uint8_t)_this->_levelHandler->_difficulty;
	}

	bool LevelScripts::asIsReforged()
	{
		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		return (uint8_t)_this->_levelHandler->_isReforged;
	}

	int LevelScripts::asGetLevelWidth()
	{
		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		return _this->_levelHandler->_tileMap->LevelBounds().X;
	}

	int LevelScripts::asGetLevelHeight()
	{
		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		return _this->_levelHandler->_tileMap->LevelBounds().Y;
	}

	float LevelScripts::asGetElapsedFrames()
	{
		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		return _this->_levelHandler->_elapsedFrames;
	}

	float LevelScripts::asGetAmbientLight()
	{
		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		return _this->_levelHandler->_ambientLightTarget;
	}

	void LevelScripts::asSetAmbientLight(float value)
	{
		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		_this->_levelHandler->_ambientLightTarget = value;
	}

	float LevelScripts::asGetWaterLevel()
	{
		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		return _this->_levelHandler->_waterLevel;
	}

	void LevelScripts::asSetWaterLevel(float value)
	{
		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		_this->_levelHandler->_waterLevel = value;
	}

	void LevelScripts::asPreloadMetadata(const String& path)
	{
		ContentResolver::Current().PreloadMetadataAsync(path);
	}

	void LevelScripts::asRegisterSpawnable(int eventType, const String& typeName)
	{
		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));

		asITypeInfo* typeInfo = _this->_module->GetTypeInfoByName(typeName.data());
		if (typeInfo == nullptr) {
			return;
		}

		bool added = _this->_eventTypeToTypeInfo.emplace(eventType, typeInfo).second;
		if (added) {
			_this->_levelHandler->EventSpawner()->RegisterSpawnable((EventType)eventType, asRegisterSpawnableCallback);
		}
	}

	std::shared_ptr<Actors::ActorBase> LevelScripts::asRegisterSpawnableCallback(const Actors::ActorActivationDetails& details)
	{
		if (auto levelHandler = dynamic_cast<LevelHandler*>(details.LevelHandler)) {
			auto _this = levelHandler->_scripts.get();
			// Spawn() function with custom event cannot be used in OnLevelLoad(), because _scripts is not assigned yet
			if (_this != nullptr) {
				auto it = _this->_eventTypeToTypeInfo.find((int)details.Type);
				if (it != _this->_eventTypeToTypeInfo.end()) {
					asIScriptObject* obj = reinterpret_cast<asIScriptObject*>(_this->_engine->CreateScriptObject(it->second));
					ScriptActorWrapper* obj2 = *reinterpret_cast<ScriptActorWrapper**>(obj->GetAddressOfProperty(0));
					obj2->AddRef();
					obj->Release();
					obj2->OnActivated(details);
					return std::shared_ptr<Actors::ActorBase>(obj2);
				}
			}
		}
		return nullptr;
	}

	void LevelScripts::asSpawnEvent(int eventType, int x, int y)
	{
		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));

		uint8_t spawnParams[Events::EventSpawner::SpawnParamsSize] { };
		auto actor = _this->_levelHandler->EventSpawner()->SpawnEvent((EventType)eventType, spawnParams, Actors::ActorState::None, Vector3i(x, y, ILevelHandler::MainPlaneZ));
		if (actor != nullptr) {
			_this->_levelHandler->AddActor(actor);
		}
	}

	void LevelScripts::asSpawnEventParams(int eventType, int x, int y, const CScriptArray& eventParams)
	{
		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));

		uint8_t spawnParams[Events::EventSpawner::SpawnParamsSize] { };
		int size = eventParams.GetSize();
		std::memcpy(spawnParams, eventParams.At(0), size);

		auto actor = _this->_levelHandler->EventSpawner()->SpawnEvent((EventType)eventType, spawnParams, Actors::ActorState::None, Vector3i(x, y, ILevelHandler::MainPlaneZ));
		if (actor != nullptr) {
			_this->_levelHandler->AddActor(actor);
		}
	}

	void LevelScripts::asSpawnType(const String& typeName, int x, int y)
	{
		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));

		auto actor = _this->CreateActorInstance(typeName);
		if (actor == nullptr) {
			return;
		}

		uint8_t spawnParams[Events::EventSpawner::SpawnParamsSize] { };
		actor->OnActivated({
			.LevelHandler = _this->_levelHandler,
			.Pos = Vector3i(x, y, ILevelHandler::MainPlaneZ),
			.Params = spawnParams
		});
		_this->_levelHandler->AddActor(std::shared_ptr<Actors::ActorBase>(actor));
	}

	void LevelScripts::asSpawnTypeParams(const String& typeName, int x, int y, const CScriptArray& eventParams)
	{
		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));

		auto actor = _this->CreateActorInstance(typeName);
		if (actor == nullptr) {
			return;
		}

		uint8_t spawnParams[Events::EventSpawner::SpawnParamsSize] { };
		int size = eventParams.GetSize();
		std::memcpy(spawnParams, eventParams.At(0), size);

		actor->OnActivated({
			.LevelHandler = _this->_levelHandler,
			.Pos = Vector3i(x, y, ILevelHandler::MainPlaneZ),
			.Params = spawnParams
		});
		_this->_levelHandler->AddActor(std::shared_ptr<Actors::ActorBase>(actor));
	}

	void LevelScripts::asChangeLevel(int exitType, const String& path)
	{
		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		_this->_levelHandler->BeginLevelChange((ExitType)exitType, path);
	}

	void LevelScripts::asMusicPlay(const String& path)
	{
#if defined(WITH_OPENMPT)
		if (path.empty()) {
			return;
		}

		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		auto _levelHandler = _this->_levelHandler;

		if (_levelHandler->_musicPath != path) {
			_levelHandler->_music = ContentResolver::Current().GetMusic(path);
			if (_levelHandler->_music != nullptr) {
				_levelHandler->_musicPath = path;
				_levelHandler->_music->setLooping(true);
				_levelHandler->_music->setGain(PreferencesCache::MasterVolume * PreferencesCache::MusicVolume);
				_levelHandler->_music->setSourceRelative(true);
				_levelHandler->_music->play();
			}
		}
#endif
	}

	void LevelScripts::asShowLevelText(const String& text)
	{
		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		_this->_levelHandler->ShowLevelText(text);
	}

	void LevelScripts::asSetWeather(uint8_t weatherType, uint8_t intensity)
	{
		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		_this->_levelHandler->SetWeather((WeatherType)weatherType, intensity);
	}
}

#endif