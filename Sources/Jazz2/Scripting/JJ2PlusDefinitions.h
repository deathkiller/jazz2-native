#pragma once

#if defined(WITH_ANGELSCRIPT)

#include "../../Common.h"
#include "FindAngelScript.h"
#include "RegisterArray.h"

#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace Jazz2::Actors
{
	class Player;
}

namespace Jazz2::Scripting
{
	class LevelScriptLoader;

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
		mZZWARP,

		mCOUNT
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
		aUNKNOWN,

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
		aWATERBLOCK,

		aCOUNT
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


	struct jjTEXTAPPEARANCE {
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

		ch_ at = ch_HIDE;
		ch_ caret = ch_HIDE;
		ch_ hash = ch_HIDE;
		ch_ newline = ch_HIDE;
		ch_ pipe = ch_HIDE;
		ch_ section = ch_HIDE;
		ch_ tilde = ch_HIDE;
		align_ align = align_DEFAULT;

		static jjTEXTAPPEARANCE constructor();
		static jjTEXTAPPEARANCE constructorMode(uint32_t mode);

		jjTEXTAPPEARANCE& operator=(uint32_t other);
	};

	struct jjPALCOLOR {
		uint8_t red;
		uint8_t green;
		uint8_t blue;

		static jjPALCOLOR Create();
		static jjPALCOLOR CreateFromRgb(uint8_t red, uint8_t green, uint8_t blue);

		uint8_t getHue();
		uint8_t getSat();
		uint8_t getLight();

		void swizzle(uint32_t redc, uint32_t greenc, uint32_t bluec);
		void setHSL(int hue, uint8_t sat, uint8_t light);

		jjPALCOLOR& operator=(const jjPALCOLOR& other);
		bool operator==(const jjPALCOLOR& other);
	};

	class jjPAL
	{
	public:
		jjPAL();
		~jjPAL();

		static jjPAL* Create(jjPAL* self);

		void AddRef();

		void Release();

		void reset();
		void apply();
		bool load(const String& filename);
		void fill(uint8_t red, uint8_t green, uint8_t blue, float opacity);
		void fillTint(uint8_t red, uint8_t green, uint8_t blue, uint8_t start, uint8_t length, float opacity);
		void fillFromColor(jjPALCOLOR color, float opacity);
		void fillTintFromColor(jjPALCOLOR color, uint8_t start, uint8_t length, float opacity);
		void gradient(uint8_t red1, uint8_t green1, uint8_t blue1, uint8_t red2, uint8_t green2, uint8_t blue2, uint8_t start, uint8_t length, float opacity, bool inclusive);
		void gradientFromColor(jjPALCOLOR color1, jjPALCOLOR color2, uint8_t start, uint8_t length, float opacity, bool inclusive);
		void copyFrom(uint8_t start, uint8_t length, uint8_t start2, const jjPAL& source, float opacity);
		uint8_t findNearestColor(jjPALCOLOR color);

	private:
		int _refCount;
	};

	class jjSTREAM
	{
	public:
		jjSTREAM();
		~jjSTREAM();

		static jjSTREAM* Create();
		static jjSTREAM* CreateFromFile(const String& filename);

		void AddRef();
		void Release();

		jjSTREAM& operator=(const jjSTREAM& o);

		uint32_t getSize() const;
		bool isEmpty() const;
		bool save(const String& tilename) const;
		void clear();
		bool discard(uint32_t count);

		bool write(const String& value);
		bool write(const jjSTREAM& value);
		bool get(String& value, uint32_t count);
		bool get(jjSTREAM& value, uint32_t count);
		bool getLine(String& value, const String& delim);

		bool push(bool value);
		bool push(uint8_t value);
		bool push(int8_t value);
		bool push(uint16_t value);
		bool push(int16_t value);
		bool push(uint32_t value);
		bool push(int32_t value);
		bool push(uint64_t value);
		bool push(int64_t value);
		bool push(float value);
		bool push(double value);
		bool push(const String& value);
		bool push(const jjSTREAM& value);

		bool pop(bool& value);
		bool pop(uint8_t& value);
		bool pop(int8_t& value);
		bool pop(uint16_t& value);
		bool pop(int16_t& value);
		bool pop(uint32_t& value);
		bool pop(int32_t& value);
		bool pop(uint64_t& value);
		bool pop(int64_t& value);
		bool pop(float& value);
		bool pop(double& value);
		bool pop(String& value);
		bool pop(jjSTREAM& value);

	private:
		int _refCount;
	};

	struct jjBEHAVIOR
	{
		static jjBEHAVIOR* Create(jjBEHAVIOR* self);
		static jjBEHAVIOR* CreateFromBehavior(uint32_t behavior, jjBEHAVIOR* self);
		static void Destroy(jjBEHAVIOR* self);

		jjBEHAVIOR& operator=(const jjBEHAVIOR& other);
		jjBEHAVIOR& operator=(uint32_t other);
		jjBEHAVIOR& operator=(asIScriptFunction* other);
		jjBEHAVIOR& operator=(asIScriptObject* other);
		bool operator==(const jjBEHAVIOR& other) const;
		bool operator==(uint32_t other) const;
		bool operator==(const asIScriptFunction* other) const;
		operator uint32_t();
		operator asIScriptFunction* ();
		operator asIScriptObject* ();
	};

	class jjANIMFRAME
	{
	public:
		jjANIMFRAME();
		~jjANIMFRAME();

		void AddRef();
		void Release();

		jjANIMFRAME& operator=(const jjANIMFRAME& o);

		static jjANIMFRAME* get_jjAnimFrames(uint32_t index);

		int16_t hotSpotX = 0;
		int16_t hotSpotY = 0;
		int16_t coldSpotX = 0;
		int16_t coldSpotY = 0;
		int16_t gunSpotX = 0;
		int16_t gunSpotY = 0;
		int16_t width = 0;
		int16_t height = 0;

		bool get_transparent() const;
		bool set_transparent(bool value) const;
		bool doesCollide(int32_t xPos, int32_t yPos, int32_t direction, const jjANIMFRAME* frame2, int32_t xPos2, int32_t yPos2, int32_t direction2, bool always) const;

	private:
		int _refCount;
	};

	class jjANIMATION
	{
	public:
		jjANIMATION(uint32_t index);
		~jjANIMATION();

		void AddRef();
		void Release();

		jjANIMATION& operator=(const jjANIMATION& o);

		bool save(const String& filename, const jjPAL& palette) const;
		bool load(const String& filename, int32_t hotSpotX, int32_t hotSpotY, int32_t coldSpotYOffset, int32_t firstFrameToOverwrite);

		static jjANIMATION* get_jjAnimations(uint32_t index);

		uint16_t frameCount = 0;
		int16_t fps = 0;

		uint32_t get_firstFrame() const;
		uint32_t set_firstFrame(uint32_t index) const;

		uint32_t getAnimFirstFrame();

	private:
		int _refCount;
		uint32_t _index;
	};

	class jjANIMSET
	{
	public:
		jjANIMSET(uint32_t index);
		~jjANIMSET();

		void AddRef();
		void Release();

		static jjANIMSET* get_jjAnimSets(uint32_t index);

		uint32_t convertAnimSetToUint();

		jjANIMSET* load(uint32_t fileSetID, const String& filename, int32_t firstAnimToOverwrite, int32_t firstFrameToOverwrite);
		jjANIMSET* allocate(const CScriptArray& frameCounts);

	private:
		int _refCount;
		uint32_t _index;
	};

	struct jjCANVAS {

		void DrawPixel(int32_t xPixel, int32_t yPixel, uint8_t color, uint32_t mode, uint8_t param);
		void DrawRectangle(int32_t xPixel, int32_t yPixel, int32_t width, int32_t height, uint8_t color, uint32_t mode, uint8_t param);
		void DrawSprite(int32_t xPixel, int32_t yPixel, int32_t setID, uint8_t animation, uint8_t frame, int8_t direction, uint32_t mode, uint8_t param);
		void DrawCurFrameSprite(int32_t xPixel, int32_t yPixel, uint32_t sprite, int8_t direction, uint32_t mode, uint8_t param);
		void DrawResizedSprite(int32_t xPixel, int32_t yPixel, int32_t setID, uint8_t animation, uint8_t frame, float xScale, float yScale, uint32_t mode, uint8_t param);
		void DrawResizedCurFrameSprite(int32_t xPixel, int32_t yPixel, uint32_t sprite, float xScale, float yScale, uint32_t mode, uint8_t param);
		void DrawTransformedSprite(int32_t xPixel, int32_t yPixel, int32_t setID, uint8_t animation, uint8_t frame, int32_t angle, float xScale, float yScale, uint32_t mode, uint8_t param);
		void DrawTransformedCurFrameSprite(int32_t xPixel, int32_t yPixel, uint32_t sprite, int32_t angle, float xScale, float yScale, uint32_t mode, uint8_t param);
		void DrawSwingingVine(int32_t xPixel, int32_t yPixel, uint32_t sprite, int32_t length, int32_t curvature, uint32_t mode, uint8_t param);

		void ExternalDrawTile(int32_t xPixel, int32_t yPixel, uint16_t tile, uint32_t tileQuadrant);
		void DrawTextBasicSize(int32_t xPixel, int32_t yPixel, const String& text, uint32_t size, uint32_t mode, uint8_t param);
		void DrawTextExtSize(int32_t xPixel, int32_t yPixel, const String& text, uint32_t size, const jjTEXTAPPEARANCE& appearance, uint8_t param1, uint32_t mode, uint8_t param);

		void drawString(int32_t xPixel, int32_t yPixel, const String& text, const jjANIMATION& animation, uint32_t mode, uint8_t param);

		void drawStringEx(int32_t xPixel, int32_t yPixel, const String& text, const jjANIMATION& animation, const jjTEXTAPPEARANCE& appearance, uint8_t param1, uint32_t spriteMode, uint8_t param2);

		static void jjDrawString(float xPixel, float yPixel, const String& text, const jjANIMATION& animation, uint32_t mode, uint8_t param, int8_t layerZ, uint8_t layerXY, int8_t playerID);
		static void jjDrawStringEx(float xPixel, float yPixel, const String& text, const jjANIMATION& animation, const jjTEXTAPPEARANCE& appearance, uint8_t param1, uint32_t spriteMode, uint8_t param2, int8_t layerZ, uint8_t layerXY, int8_t playerID);
		static int jjGetStringWidth(const String& text, const jjANIMATION& animation, const jjTEXTAPPEARANCE& style);
	};

	class jjOBJ;
	class jjPLAYER;

	using jjVOIDFUNCOBJ = void(*)(jjOBJ* obj);

	class jjOBJ
	{
	public:
		jjOBJ();
		~jjOBJ();

		void AddRef();

		void Release();

		bool get_isActive() const;
		uint32_t get_lightType() const;
		uint32_t set_lightType(uint32_t value) const;

		jjOBJ* objectHit(jjOBJ* target, uint32_t playerHandling);
		void blast(int32_t maxDistance, bool blastObjects);

		jjBEHAVIOR behavior;

		void behave1(uint32_t behavior, bool draw);
		void behave2(jjBEHAVIOR behavior, bool draw);
		void behave3(jjVOIDFUNCOBJ behavior, bool draw);

		static int32_t jjAddObject(uint8_t eventID, float xPixel, float yPixel, uint16_t creatorID, uint32_t creatorType, uint32_t behavior);
		static int32_t jjAddObjectEx(uint8_t eventID, float xPixel, float yPixel, uint16_t creatorID, uint32_t creatorType, jjVOIDFUNCOBJ behavior);

		static void jjDeleteObject(int32_t objectID);
		static void jjKillObject(int32_t objectID);

		float xOrg = 0;
		float yOrg = 0;
		float xPos = 0;
		float yPos = 0;
		float xSpeed = 0;
		float ySpeed = 0;
		float xAcc = 0;
		float yAcc = 0;
		int32_t counter = 0;
		uint32_t curFrame = 0;

		uint32_t determineCurFrame(bool change);

		int32_t age = 0;
		int32_t creator = 0;

		uint16_t get_creatorID() const;
		uint16_t set_creatorID(uint16_t value) const;
		uint32_t get_creatorType() const;
		uint32_t set_creatorType(uint32_t value) const;

		int16_t curAnim = 0;

		int16_t determineCurAnim(uint8_t setID, uint8_t animation, bool change);

		uint16_t killAnim = 0;
		uint8_t freeze = 0;
		uint8_t lightType = 0;
		int8_t frameID = 0;
		int8_t noHit = 0;

		uint32_t get_bulletHandling();
		uint32_t set_bulletHandling(uint32_t value);
		bool get_ricochet();
		bool set_ricochet(bool value);
		bool get_freezable();
		bool set_freezable(bool value);
		bool get_blastable();
		bool set_blastable(bool value);

		int8_t energy = 0;
		int8_t light = 0;
		uint8_t objType = 0;

		uint32_t get_playerHandling();
		uint32_t set_playerHandling(uint32_t value);
		bool get_isTarget();
		bool set_isTarget(bool value);
		bool get_triggersTNT();
		bool set_triggersTNT(bool value);
		bool get_deactivates();
		bool set_deactivates(bool value);
		bool get_scriptedCollisions();
		bool set_scriptedCollisions(bool value);

		int8_t state = 0;
		uint16_t points = 0;
		uint8_t eventID = 0;
		int8_t direction = 0;
		uint8_t justHit = 0;
		int8_t oldState = 0;
		int32_t animSpeed = 0;
		int32_t special = 0;

		int32_t get_var(uint8_t x);
		int32_t set_var(uint8_t x, int32_t value);

		uint8_t doesHurt = 0;
		uint8_t counterEnd = 0;
		int16_t objectID = 0;

		int32_t draw();
		int32_t beSolid(bool shouldCheckForStompingLocalPlayers);
		void bePlatform(float xOld, float yOld, int32_t width, int32_t height);
		void clearPlatform();
		void putOnGround(bool precise);
		bool ricochet();
		int32_t unfreeze(int32_t style);
		void deleteObject();
		void deactivate();
		void pathMovement();
		int32_t fireBullet(uint8_t eventID);
		void particlePixelExplosion(int32_t style);
		void grantPickup(jjPLAYER* player, int32_t frequency);

		int findNearestPlayer(int maxDistance) const;
		int findNearestPlayerEx(int maxDistance, int& foundDistance) const;

		bool doesCollide(const jjOBJ* object, bool always) const;
		bool doesCollidePlayer(const jjPLAYER* object, bool always) const;

	private:
		int _refCount;
	};

	jjOBJ* get_jjObjects(int index);

	jjOBJ* get_jjObjectPresets(int8_t id);

	class jjPLAYER
	{
	public:
		jjPLAYER(LevelScriptLoader* levelScripts, int playerIndex);
		jjPLAYER(LevelScriptLoader* levelScripts, Actors::Player* player);
		~jjPLAYER();

		void AddRef();
		void Release();

		jjPLAYER& operator=(const jjPLAYER& o);

		int32_t score = 0;
		int32_t lastScoreDisplay = 0;

		int32_t setScore(int32_t value);

		float xPos = 0.0f;
		float yPos = 0.0f;
		float xAcc = 0.0f;
		float yAcc = 0.0f;
		float xOrg = 0.0f;
		float yOrg = 0.0f;

		float get_xSpeed();
		float set_xSpeed(float value);
		float get_ySpeed();
		float set_ySpeed(float value);

		float jumpStrength = 0.0f;
		int8_t frozen = 0;

		void freeze(bool frozen);
		int32_t get_currTile();
		bool startSugarRush(int32_t time);
		int8_t get_health() const;
		int8_t set_health(int8_t value);

		int32_t warpID = 0;
		int32_t fastfire = 0;

		int8_t get_currWeapon() const;
		int8_t set_currWeapon(int8_t value);

		int32_t lives = 1;
		int32_t invincibility = 0;
		int32_t blink = 0;

		int32_t extendInvincibility(int32_t duration);

		int32_t food = 0;
		int32_t coins = 0;

		bool testForCoins(int32_t numberOfCoins);
		int32_t get_gems(uint32_t type) const;
		int32_t set_gems(uint32_t type, int32_t value);
		bool testForGems(int32_t numberOfGems, uint32_t type);

		int32_t shieldType = 0;
		int32_t shieldTime = 0;
		int32_t rolling = 0;
		int32_t bossNumber = 0;
		int32_t boss = 0;
		bool bossActive = false;
		int8_t direction = 0;
		int32_t platform = 0;
		int32_t flag = 0;
		int32_t clientID = 0;
		int8_t playerID = 0;
		int32_t localPlayerID = 0;
		bool team = false;
		bool run = false;
		int32_t specialJump = 0;

		int32_t get_stoned();
		int32_t set_stoned(int32_t value);

		int32_t buttstomp = 0;
		int32_t helicopter = 0;
		int32_t helicopterElapsed = 0;
		int32_t specialMove = 0;
		int32_t idle = 0;

		void suckerTube(int32_t xSpeed, int32_t ySpeed, bool center, bool noclip, bool trigSample);
		void poleSpin(float xSpeed, float ySpeed, uint32_t delay);
		void spring(float xSpeed, float ySpeed, bool keepZeroSpeeds, bool sample);

		bool isLocal = true;
		bool isActive = true;

		bool get_isConnecting() const;
		bool get_isIdle() const;
		bool get_isOut() const;
		bool get_isSpectating() const;
		bool get_isInGame() const;

		String get_name() const;
		String get_nameUnformatted() const;
		bool setName(const String& name);
		int8_t get_light() const;
		int8_t set_light(int8_t value);
		uint32_t get_fur() const;
		uint32_t set_fur(uint32_t value);

		bool get_noFire() const;
		bool set_noFire(bool value);
		bool get_antiGrav() const;
		bool set_antiGrav(bool value);
		bool get_invisibility() const;
		bool set_invisibility(bool value);
		bool get_noclipMode() const;
		bool set_noclipMode(bool value);
		uint8_t get_lighting() const;
		uint8_t set_lighting(uint8_t value);
		uint8_t resetLight();

		bool get_playerKeyLeftPressed();
		bool get_playerKeyRightPressed();
		bool get_playerKeyUpPressed();
		bool get_playerKeyDownPressed();
		bool get_playerKeyFirePressed();
		bool get_playerKeySelectPressed();
		bool get_playerKeyJumpPressed();
		bool get_playerKeyRunPressed();
		void set_playerKeyLeftPressed(bool value);
		void set_playerKeyRightPressed(bool value);
		void set_playerKeyUpPressed(bool value);
		void set_playerKeyDownPressed(bool value);
		void set_playerKeyFirePressed(bool value);
		void set_playerKeySelectPressed(bool value);
		void set_playerKeyJumpPressed(bool value);
		void set_playerKeyRunPressed(bool value);

		bool get_powerup(uint8_t index);
		bool set_powerup(uint8_t index, bool value);
		int32_t get_ammo(uint8_t index) const;
		int32_t set_ammo(uint8_t index, int32_t value);

		bool offsetPosition(int32_t xPixels, int32_t yPixels);
		bool warpToTile(int32_t xTile, int32_t yTile, bool fast);
		bool warpToID(uint8_t warpID, bool fast);

		uint32_t morph(bool rabbitsOnly, bool morphEffect);
		uint32_t morphTo(uint32_t charNew, bool morphEffect);
		uint32_t revertMorph(bool morphEffect);
		uint32_t get_charCurr() const;

		uint32_t charOrig = 0;

		void kill();
		bool hurt(int8_t damage, bool forceHurt, jjPLAYER* attacker);

		uint32_t get_timerState() const;
		bool get_timerPersists() const;
		bool set_timerPersists(bool value);
		uint32_t timerStart(int32_t ticks, bool startPaused);
		uint32_t timerPause();
		uint32_t timerResume();
		uint32_t timerStop();
		int32_t get_timerTime() const;
		int32_t set_timerTime(int32_t value);
		void timerFunction(const String& functionName);
		void timerFunctionPtr(void* function);
		void timerFunctionFuncPtr(void* function);

		bool activateBoss(bool activate);
		bool limitXScroll(uint16_t left, uint16_t width);
		void cameraFreezeFF(float xPixel, float yPixel, bool centered, bool instant);
		void cameraFreezeBF(bool xUnfreeze, float yPixel, bool centered, bool instant);
		void cameraFreezeFB(float xPixel, bool yUnfreeze, bool centered, bool instant);
		void cameraFreezeBB(bool xUnfreeze, bool yUnfreeze, bool centered, bool instant);
		void cameraUnfreeze(bool instant);
		void showText(const String& text, uint32_t size);
		void showTextByID(uint32_t textID, uint32_t offset, uint32_t size);

		uint32_t get_fly() const;
		uint32_t set_fly(uint32_t value);

		int32_t fireBulletDirection(uint8_t gun, bool depleteAmmo, bool requireAmmo, uint32_t direction);
		int32_t fireBulletAngle(uint8_t gun, bool depleteAmmo, bool requireAmmo, float angle);

		int32_t subscreenX = 0;
		int32_t subscreenY = 0;

		float get_cameraX() const;
		float get_cameraY() const;
		int32_t get_deaths() const;

		bool get_isJailed() const;
		bool get_isZombie() const;
		int32_t get_lrsLives() const;
		int32_t get_roasts() const;
		int32_t get_laps() const;
		int32_t get_lapTimeCurrent() const;
		int32_t get_lapTimes(uint32_t index) const;
		int32_t get_lapTimeBest() const;
		bool get_isAdmin() const;
		bool hasPrivilege(const String& privilege, uint32_t moduleID) const;

		bool doesCollide(const jjOBJ* object, bool always) const;
		int getObjectHitForce(const jjOBJ& target) const;
		bool objectHit(jjOBJ* target, int force, uint32_t playerHandling);
		bool isEnemy(const jjPLAYER* victim) const;

		uint32_t charCurr = 0;
		uint16_t curAnim = 0;
		uint32_t curFrame = 0;
		uint8_t frameID = 0;

	private:
		int _refCount;
		LevelScriptLoader* _levelScriptLoader;
		Actors::Player* _player;
	};

	int32_t get_jjPlayerCount();
	int32_t get_jjLocalPlayerCount();

	jjPLAYER* get_jjP();
	jjPLAYER* get_jjPlayers(uint8_t index);
	jjPLAYER* get_jjLocalPlayers(uint8_t index);

	class jjWEAPON
	{

	};

	class jjCHARACTER
	{

	};

	class jjLAYER;

	class jjPIXELMAP
	{
	public:
		jjPIXELMAP();
		~jjPIXELMAP();

		static jjPIXELMAP* CreateFromTile();
		static jjPIXELMAP* CreateFromSize(uint32_t width, uint32_t height);
		static jjPIXELMAP* CreateFromFrame(const jjANIMFRAME* animFrame);
		static jjPIXELMAP* CreateFromLayer(uint32_t left, uint32_t top, uint32_t width, uint32_t height, uint32_t layer);
		static jjPIXELMAP* CreateFromLayerObject(uint32_t left, uint32_t top, uint32_t width, uint32_t height, const jjLAYER* layer);
		static jjPIXELMAP* CreateFromTexture(uint32_t animFrame);
		static jjPIXELMAP* CreateFromFilename(const String& filename, const jjPAL* palette, uint8_t threshold);

		void AddRef();
		void Release();

		jjPIXELMAP& operator=(const jjPIXELMAP& o);

		// TODO: return type uint8_t& instead?
		uint8_t GetPixel(uint32_t x, uint32_t y);

		uint32_t width = 0;
		uint32_t height = 0;

		bool saveToTile(uint16_t tileID, bool hFlip) const;
		bool saveToFrame(jjANIMFRAME* frame) const;
		bool saveToFile(const String& filename, const jjPAL& palette) const;

	private:
		int _refCount;
	};

	class jjMASKMAP
	{
	public:
		jjMASKMAP();
		~jjMASKMAP();

		static jjMASKMAP* CreateFromBool(bool filled);
		static jjMASKMAP* CreateFromTile(uint16_t tileID);

		void AddRef();
		void Release();

		jjMASKMAP& operator=(const jjMASKMAP& o);

		// TODO: return type bool& instead?
		bool GetPixel(uint32_t x, uint32_t y);

		bool save(uint16_t tileID, bool hFlip) const;

	private:
		int _refCount;
	};

	class jjLAYER
	{
	public:
		jjLAYER();
		~jjLAYER();

		static jjLAYER* CreateFromSize(uint32_t width, uint32_t height, jjLAYER* self);
		static jjLAYER* CreateCopy(jjLAYER* other, jjLAYER* self);

		void AddRef();
		void Release();

		jjLAYER& operator=(const jjLAYER& o);

		static jjLAYER* get_jjLayers(int32_t index);

		int32_t width = 0;
		int32_t widthReal = 0;
		int32_t widthRounded = 0;
		int32_t height = 0;
		float xSpeed = 0;
		float ySpeed = 0;
		float xAutoSpeed = 0;
		float yAutoSpeed = 0;
		float xOffset = 0;
		float yOffset = 0;
		float xInnerSpeed = 0;
		float yInnerSpeed = 0;
		float xInnerAutoSpeed = 0;
		float yInnerAutoSpeed = 0;

		uint32_t get_spriteMode() const;
		uint32_t set_spriteMode(uint32_t value) const;
		uint8_t get_spriteParam() const;
		uint8_t set_spriteParam(uint8_t value) const;

		void setXSpeed(float newspeed, bool newSpeedIsAnAutoSpeed) const;
		void setYSpeed(float newspeed, bool newSpeedIsAnAutoSpeed) const;
		float getXPosition(const jjPLAYER* play) const;
		float getYPosition(const jjPLAYER* play) const;

		int32_t rotationAngle = 0;
		int32_t rotationRadiusMultiplier = 0;
		bool tileHeight = false;
		bool tileWidth = false;
		bool limitVisibleRegion = false;
		bool hasTileMap = false;
		bool hasTiles = false;

		static CScriptArray* jjLayerOrderGet();
		static bool jjLayerOrderSet(const CScriptArray& order);
		static CScriptArray* jjLayersFromLevel(const String& filename, const CScriptArray& layerIDs, int32_t tileIDAdjustmentFactor);
		static bool jjTilesFromTileset(const String& filename, uint32_t firstTileID, uint32_t tileCount, const CScriptArray* paletteColorMapping);

	private:
		int _refCount;
	};

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

	bool mlleSetup();

	float get_sinTable(uint32_t angle);
	float get_cosTable(uint32_t angle);
	uint32_t RandWord32();
	uint64_t unixTimeSec();
	uint64_t unixTimeMs();

	int32_t GetFPS();

	bool isAdmin();

	int32_t GetDifficulty();
	int32_t SetDifficulty(int32_t value);

	String getLevelFileName();
	String getCurrLevelName();
	void setCurrLevelName(const String& in);
	String get_jjTilesetFileName();

	int32_t get_gameState();

	// TODO

	void jjPrint(const String& text, bool timestamp);
	void jjDebug(const String& text, bool timestamp);
	void jjChat(const String& text, bool teamchat);
	void jjConsole(const String& text, bool sendToAll);
	void jjSpy(const String& text);

	// TODO

	int32_t getBorderWidth();
	int32_t getBorderHeight();
	bool getSplitscreenType();
	bool setSplitscreenType();

	// TODO

	int32_t get_teamScore(int32_t color);
	int32_t GetMaxHealth();
	int32_t GetStartHealth();

	// TODO

	float get_layerXOffset(uint8_t id);
	float set_layerXOffset(uint8_t id, float value);
	float get_layerYOffset(uint8_t id);
	float set_layerYOffset(uint8_t id, float value);
	int get_layerWidth(uint8_t id);
	int get_layerRealWidth(uint8_t id);
	int get_layerRoundedWidth(uint8_t id);
	int get_layerHeight(uint8_t id);
	float get_layerXSpeed(uint8_t id);
	float set_layerXSpeed(uint8_t id, float value);
	float get_layerYSpeed(uint8_t id);
	float set_layerYSpeed(uint8_t id, float value);
	float get_layerXAutoSpeed(uint8_t id);
	float set_layerXAutoSpeed(uint8_t id, float value);
	float get_layerYAutoSpeed(uint8_t id);
	float set_layerYAutoSpeed(uint8_t id, float value);
	bool get_layerHasTiles(uint8_t id);
	bool set_layerHasTiles(uint8_t id, bool value);
	bool get_layerTileHeight(uint8_t id);
	bool set_layerTileHeight(uint8_t id, bool value);
	bool get_layerTileWidth(uint8_t id);
	bool set_layerTileWidth(uint8_t id, bool value);
	bool get_layerLimitVisibleRegion(uint8_t id);
	bool set_layerLimitVisibleRegion(uint8_t id, bool value);

	void setLayerXSpeedSeamlessly(uint8_t id, float newspeed, bool newSpeedIsAnAutoSpeed);
	void setLayerYSpeedSeamlessly(uint8_t id, float newspeed, bool newSpeedIsAnAutoSpeed);

	// TODO

	void jjDrawPixel(float xPixel, float yPixel, uint8_t color, spriteType mode, uint8_t param, int8_t layerZ, uint8_t layerXY, int8_t playerID);

	void jjDrawRectangle(float xPixel, float yPixel, int32_t width, int32_t height, uint8_t color, spriteType mode, uint8_t param, int8_t layerZ, uint8_t layerXY, int8_t playerID);

	void jjDrawSprite(float xPixel, float yPixel, int32_t setID, uint8_t animation, uint8_t frame, int8_t direction, spriteType mode, uint8_t param, int8_t layerZ, uint8_t layerXY, int8_t playerID);

	void jjDrawSpriteFromCurFrame(float xPixel, float yPixel, uint32_t sprite, int8_t direction, spriteType mode, uint8_t param, int8_t layerZ, uint8_t layerXY, int8_t playerID);

	void jjDrawResizedSprite(float xPixel, float yPixel, int32_t setID, uint8_t animation, uint8_t frame, float xScale, float yScale, spriteType mode, uint8_t param, int8_t layerZ, uint8_t layerXY, int8_t playerID);

	void jjDrawResizedSpriteFromCurFrame(float xPixel, float yPixel, uint32_t sprite, float xScale, float yScale, spriteType mode, uint8_t param, int8_t layerZ, uint8_t layerXY, int8_t playerID);

	void jjDrawRotatedSprite(float xPixel, float yPixel, int32_t setID, uint8_t animation, uint8_t frame, int32_t angle, float xScale, float yScale, spriteType mode, uint8_t param, int8_t layerZ, uint8_t layerXY, int8_t playerID);

	void jjDrawRotatedSpriteFromCurFrame(float xPixel, float yPixel, uint32_t sprite, int32_t angle, float xScale, float yScale, spriteType mode, uint8_t param, int8_t layerZ, uint8_t layerXY, int8_t playerID);

	void jjDrawSwingingVineSpriteFromCurFrame(float xPixel, float yPixel, uint32_t sprite, int32_t length, int32_t curvature, spriteType mode, uint8_t param, int8_t layerZ, uint8_t layerXY, int8_t playerID);

	void jjDrawTile(float xPixel, float yPixel, uint16_t tile, uint32_t tileQuadrant, int8_t layerZ, uint8_t layerXY, int8_t playerID);

	void jjDrawString(float xPixel, float yPixel, const String& text, uint32_t size, uint32_t mode, uint8_t param, int8_t layerZ, uint8_t layerXY, int8_t playerID);

	void jjDrawStringEx(float xPixel, float yPixel, const String& text, uint32_t size, const jjTEXTAPPEARANCE& appearance, uint8_t param1, spriteType spriteMode, uint8_t param2, int8_t layerZ, uint8_t layerXY, int8_t playerID);

	int32_t jjGetStringWidth(const String& text, uint32_t size, const jjTEXTAPPEARANCE& style);

	bool isNumberedASFunctionEnabled(uint8_t id);
	bool setNumberedASFunctionEnabled(uint8_t id, bool value);
	void reenableAllNumberedASFunctions();

	float getWaterLevel();
	float getWaterLevel2();
	float setWaterLevel(float value, bool instant);
	float get_waterChangeSpeed();
	float set_waterChangeSpeed(float value);
	int32_t get_waterLayer();
	int32_t set_waterLayer(int32_t value);
	void setWaterGradient(uint8_t red1, uint8_t green1, uint8_t blue1, uint8_t red2, uint8_t green2, uint8_t blue2);
	// TODO: void setWaterGradientFromColors(jjPALCOLOR color1, jjPALCOLOR color2)
	void setWaterGradientToTBG();
	void resetWaterGradient();

	void triggerRock(uint8_t id);

	void cycleTo(const String& filename, bool warp, bool fast);

	bool getEnabledTeam(uint8_t team);

	bool getKeyDown(uint8_t key);
	int32_t getCursorX();
	int32_t getCursorY();

	void playSample(float xPixel, float yPixel, int32_t sample, int32_t volume, int32_t frequency);
	int32_t playLoopedSample(float xPixel, float yPixel, int32_t sample, int32_t volume, int32_t frequency);
	void playPrioritySample(int32_t sample);
	bool isSampleLoaded(int32_t sample);
	bool loadSample(int32_t sample, const String& filename);

	bool getUseLayer8Speeds();
	bool setUseLayer8Speeds(bool value);

	// TODO

	int32_t GetEvent(uint16_t tx, uint16_t ty);
	int32_t GetEventParamWrapper(uint16_t tx, uint16_t ty, int32_t offset, int32_t length);
	void SetEventByte(uint16_t tx, uint16_t ty, uint8_t newEventId);
	void SetEventParam(uint16_t tx, uint16_t ty, int8_t offset, int8_t length, int32_t newValue);
	int8_t GetTileType(uint16_t tile);
	int8_t SetTileType(uint16_t tile, uint16_t value);

	// TODO

	uint16_t jjGetStaticTile(uint16_t tileID);
	uint16_t jjTileGet(uint8_t layer, int32_t xTile, int32_t yTile);
	uint16_t jjTileSet(uint8_t layer, int32_t xTile, int32_t yTile, uint16_t newTile);
	void jjGenerateSettableTileArea(uint8_t layer, int32_t xTile, int32_t yTile, int32_t width, int32_t height);

	// TODO

	bool jjMaskedPixel(int32_t xPixel, int32_t yPixel);
	bool jjMaskedPixelLayer(int32_t xPixel, int32_t yPixel, uint8_t layer);
	bool jjMaskedHLine(int32_t xPixel, int32_t lineLength, int32_t yPixel);
	bool jjMaskedHLineLayer(int32_t xPixel, int32_t lineLength, int32_t yPixel, uint8_t layer);
	bool jjMaskedVLine(int32_t xPixel, int32_t yPixel, int32_t lineLength);
	bool jjMaskedVLineLayer(int32_t xPixel, int32_t yPixel, int32_t lineLength, uint8_t layer);
	bool jjMaskedTopVLine(int32_t xPixel, int32_t yPixel, int32_t lineLength);
	bool jjMaskedTopVLineLayer(int32_t xPixel, int32_t yPixel, int32_t lineLength, uint8_t layer);

	// TODO

	void jjSetModPosition(int32_t order, int32_t row, bool reset);
	void jjSlideModChannelVolume(int32_t channel, float volume, int32_t milliseconds);
	int32_t jjGetModOrder();
	int32_t jjGetModRow();
	int32_t jjGetModTempo();
	void jjSetModTempo(uint8_t speed);
	int32_t jjGetModSpeed();
	void jjSetModSpeed(uint8_t speed);

	uint32_t getCustomSetID(uint8_t index);
}

#endif