#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../Main.h"
#include "../../nCine/Primitives/Rect.h"
#include "../../nCine/Base/Random.h"
#include "RegisterArray.h"

#include <angelscript.h>

#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;
using namespace nCine;

namespace Jazz2::Actors
{
	class Player;
}

namespace Jazz2::UI
{
	class HUD;
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

		std::int32_t xAmp;
		std::int32_t yAmp;
		std::int32_t spacing;
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

		static void constructor(void* self);
		static void constructorMode(std::uint32_t mode, void* self);

		jjTEXTAPPEARANCE& operator=(std::uint32_t other);
	};

	struct jjPALCOLOR {
		std::uint8_t red;
		std::uint8_t green;
		std::uint8_t blue;

		static void Create(void* self);
		static void CreateFromRgb(std::uint8_t red, std::uint8_t green, std::uint8_t blue, void* self);

		std::uint8_t getHue();
		std::uint8_t getSat();
		std::uint8_t getLight();

		void swizzle(std::uint32_t redc, std::uint32_t greenc, std::uint32_t bluec);
		void setHSL(std::int32_t hue, std::uint8_t sat, std::uint8_t light);

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

		jjPAL& operator=(const jjPAL& o);

		bool operator==(const jjPAL& o);

		jjPALCOLOR& getColor(std::uint8_t idx);
		const jjPALCOLOR& getConstColor(std::uint8_t idx) const;
		jjPALCOLOR& setColorEntry(std::uint8_t idx, jjPALCOLOR& value);

		void reset();
		void apply();
		bool load(const String& filename);
		void fill(std::uint8_t red, std::uint8_t green, std::uint8_t blue, float opacity);
		void fillTint(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t start, std::uint8_t length, float opacity);
		void fillFromColor(jjPALCOLOR color, float opacity);
		void fillTintFromColor(jjPALCOLOR color, std::uint8_t start, std::uint8_t length, float opacity);
		void gradient(std::uint8_t red1, std::uint8_t green1, std::uint8_t blue1, std::uint8_t red2, std::uint8_t green2, std::uint8_t blue2, std::uint8_t start, std::uint8_t length, float opacity, bool inclusive);
		void gradientFromColor(jjPALCOLOR color1, jjPALCOLOR color2, std::uint8_t start, std::uint8_t length, float opacity, bool inclusive);
		void copyFrom(std::uint8_t start, std::uint8_t length, std::uint8_t start2, const jjPAL& source, float opacity);
		std::uint8_t findNearestColor(jjPALCOLOR color);

	private:
		std::int32_t _refCount;
		jjPALCOLOR _palette[256];
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

		std::uint32_t getSize() const;
		bool isEmpty() const;
		bool save(const String& tilename) const;
		void clear();
		bool discard(std::uint32_t count);

		bool write(const String& value);
		bool write(const jjSTREAM& value);
		bool get(String& value, std::uint32_t count);
		bool get(jjSTREAM& value, std::uint32_t count);
		bool getLine(String& value, const String& delim);

		bool push(bool value);
		bool push(std::uint8_t value);
		bool push(std::int8_t value);
		bool push(std::uint16_t value);
		bool push(std::int16_t value);
		bool push(std::uint32_t value);
		bool push(std::int32_t value);
		bool push(std::uint64_t value);
		bool push(std::int64_t value);
		bool push(float value);
		bool push(double value);
		bool push(const String& value);
		bool push(const jjSTREAM& value);

		bool pop(bool& value);
		bool pop(std::uint8_t& value);
		bool pop(std::int8_t& value);
		bool pop(std::uint16_t& value);
		bool pop(std::int16_t& value);
		bool pop(std::uint32_t& value);
		bool pop(std::int32_t& value);
		bool pop(std::uint64_t& value);
		bool pop(std::int64_t& value);
		bool pop(float& value);
		bool pop(double& value);
		bool pop(String& value);
		bool pop(jjSTREAM& value);

	private:
		std::int32_t _refCount;
	};

	class jjRNG
	{
	public:
		jjRNG(std::uint64_t seed);
		~jjRNG();

		static jjRNG* Create(std::uint64_t seed);

		void AddRef();
		void Release();

		std::uint64_t operator()();

		jjRNG& operator=(const jjRNG& o);

		bool operator==(const jjRNG& o) const;

		void seed(std::uint64_t value);
		void discard(std::uint64_t count);

	private:
		const std::uint64_t DefaultInitSequence = 0xda3e39cb94b95bdbULL;

		std::int32_t _refCount;
		RandomGenerator _random;
	};

	struct jjBEHAVIOR
	{
		static jjBEHAVIOR* Create(jjBEHAVIOR* self);
		static jjBEHAVIOR* CreateFromBehavior(std::uint32_t behavior, jjBEHAVIOR* self);
		static void Destroy(jjBEHAVIOR* self);

		jjBEHAVIOR& operator=(const jjBEHAVIOR& other);
		jjBEHAVIOR& operator=(std::uint32_t other);
		jjBEHAVIOR& operator=(asIScriptFunction* other);
		jjBEHAVIOR& operator=(asIScriptObject* other);
		bool operator==(const jjBEHAVIOR& other) const;
		bool operator==(std::uint32_t other) const;
		bool operator==(const asIScriptFunction* other) const;
		operator std::uint32_t();
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

		static jjANIMFRAME* get_jjAnimFrames(std::uint32_t index);

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
		bool doesCollide(std::int32_t xPos, std::int32_t yPos, std::int32_t direction, const jjANIMFRAME* frame2, std::int32_t xPos2, std::int32_t yPos2, std::int32_t direction2, bool always) const;

	private:
		std::int32_t _refCount;
	};

	class jjANIMATION
	{
	public:
		jjANIMATION(std::uint32_t index);
		~jjANIMATION();

		void AddRef();
		void Release();

		jjANIMATION& operator=(const jjANIMATION& o);

		bool save(const String& filename, const jjPAL& palette) const;
		bool load(const String& filename, std::int32_t hotSpotX, std::int32_t hotSpotY, std::int32_t coldSpotYOffset, std::int32_t firstFrameToOverwrite);

		static jjANIMATION* get_jjAnimations(std::uint32_t index);

		std::uint16_t frameCount = 0;
		std::int16_t fps = 0;

		std::uint32_t get_firstFrame() const;
		std::uint32_t set_firstFrame(std::uint32_t index) const;

		std::uint32_t getAnimFirstFrame();

	private:
		std::int32_t _refCount;
		std::uint32_t _index;
	};

	class jjANIMSET
	{
	public:
		jjANIMSET(std::uint32_t index);
		~jjANIMSET();

		void AddRef();
		void Release();

		static jjANIMSET* get_jjAnimSets(std::uint32_t index);

		std::uint32_t convertAnimSetToUint();

		jjANIMSET* load(std::uint32_t fileSetID, const String& filename, int32_t firstAnimToOverwrite, int32_t firstFrameToOverwrite);
		jjANIMSET* allocate(const CScriptArray& frameCounts);

	private:
		std::int32_t _refCount;
		std::uint32_t _index;
	};

	struct jjCANVAS {
		UI::HUD* Hud;
		Rectf View;

		jjCANVAS(UI::HUD* hud, const Rectf& view);

		void DrawPixel(std::int32_t xPixel, std::int32_t yPixel, std::uint8_t color, std::uint32_t mode, std::uint8_t param);
		void DrawRectangle(std::int32_t xPixel, std::int32_t yPixel, std::int32_t width, std::int32_t height, std::uint8_t color, std::uint32_t mode, std::uint8_t param);
		void DrawSprite(std::int32_t xPixel, std::int32_t yPixel, std::int32_t setID, std::uint8_t animation, std::uint8_t frame, int8_t direction, std::uint32_t mode, std::uint8_t param);
		void DrawCurFrameSprite(std::int32_t xPixel, std::int32_t yPixel, std::uint32_t sprite, int8_t direction, std::uint32_t mode, std::uint8_t param);
		void DrawResizedSprite(std::int32_t xPixel, std::int32_t yPixel, std::int32_t setID, std::uint8_t animation, std::uint8_t frame, float xScale, float yScale, std::uint32_t mode, std::uint8_t param);
		void DrawResizedCurFrameSprite(std::int32_t xPixel, std::int32_t yPixel, std::uint32_t sprite, float xScale, float yScale, std::uint32_t mode, std::uint8_t param);
		void DrawTransformedSprite(std::int32_t xPixel, std::int32_t yPixel, std::int32_t setID, std::uint8_t animation, std::uint8_t frame, std::int32_t angle, float xScale, float yScale, std::uint32_t mode, std::uint8_t param);
		void DrawTransformedCurFrameSprite(std::int32_t xPixel, std::int32_t yPixel, std::uint32_t sprite, std::int32_t angle, float xScale, float yScale, std::uint32_t mode, std::uint8_t param);
		void DrawSwingingVine(std::int32_t xPixel, std::int32_t yPixel, std::uint32_t sprite, std::int32_t length, std::int32_t curvature, std::uint32_t mode, std::uint8_t param);

		void ExternalDrawTile(std::int32_t xPixel, std::int32_t yPixel, std::uint16_t tile, std::uint32_t tileQuadrant);
		void DrawTextBasicSize(std::int32_t xPixel, std::int32_t yPixel, const String& text, std::uint32_t size, std::uint32_t mode, std::uint8_t param);
		void DrawTextExtSize(std::int32_t xPixel, std::int32_t yPixel, const String& text, std::uint32_t size, const jjTEXTAPPEARANCE& appearance, std::uint8_t param1, std::uint32_t mode, std::uint8_t param);

		void drawString(std::int32_t xPixel, std::int32_t yPixel, const String& text, const jjANIMATION& animation, std::uint32_t mode, std::uint8_t param);

		void drawStringEx(std::int32_t xPixel, std::int32_t yPixel, const String& text, const jjANIMATION& animation, const jjTEXTAPPEARANCE& appearance, std::uint8_t param1, std::uint32_t spriteMode, std::uint8_t param2);

		static void jjDrawString(float xPixel, float yPixel, const String& text, const jjANIMATION& animation, std::uint32_t mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);
		static void jjDrawStringEx(float xPixel, float yPixel, const String& text, const jjANIMATION& animation, const jjTEXTAPPEARANCE& appearance, std::uint8_t param1, std::uint32_t spriteMode, std::uint8_t param2, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);
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
		std::uint32_t get_lightType() const;
		std::uint32_t set_lightType(std::uint32_t value) const;

		jjOBJ* objectHit(jjOBJ* target, std::uint32_t playerHandling);
		void blast(std::int32_t maxDistance, bool blastObjects);

		jjBEHAVIOR behavior;

		void behave1(std::uint32_t behavior, bool draw);
		void behave2(jjBEHAVIOR behavior, bool draw);
		void behave3(jjVOIDFUNCOBJ behavior, bool draw);

		static std::int32_t jjAddObject(std::uint8_t eventID, float xPixel, float yPixel, std::uint16_t creatorID, std::uint32_t creatorType, std::uint32_t behavior);
		static std::int32_t jjAddObjectEx(std::uint8_t eventID, float xPixel, float yPixel, std::uint16_t creatorID, std::uint32_t creatorType, jjVOIDFUNCOBJ behavior);

		static void jjDeleteObject(std::int32_t objectID);
		static void jjKillObject(std::int32_t objectID);

		float xOrg = 0;
		float yOrg = 0;
		float xPos = 0;
		float yPos = 0;
		float xSpeed = 0;
		float ySpeed = 0;
		float xAcc = 0;
		float yAcc = 0;
		std::int32_t counter = 0;
		std::uint32_t curFrame = 0;

		std::uint32_t determineCurFrame(bool change);

		std::int32_t age = 0;
		std::int32_t creator = 0;

		std::uint16_t get_creatorID() const;
		std::uint16_t set_creatorID(std::uint16_t value) const;
		std::uint32_t get_creatorType() const;
		std::uint32_t set_creatorType(std::uint32_t value) const;

		int16_t curAnim = 0;

		int16_t determineCurAnim(std::uint8_t setID, std::uint8_t animation, bool change);

		std::uint16_t killAnim = 0;
		std::uint8_t freeze = 0;
		std::uint8_t lightType = 0;
		std::int8_t frameID = 0;
		std::int8_t noHit = 0;

		std::uint32_t get_bulletHandling();
		std::uint32_t set_bulletHandling(std::uint32_t value);
		bool get_ricochet();
		bool set_ricochet(bool value);
		bool get_freezable();
		bool set_freezable(bool value);
		bool get_blastable();
		bool set_blastable(bool value);

		std::int8_t energy = 0;
		std::int8_t light = 0;
		std::uint8_t objType = 0;

		std::uint32_t get_playerHandling();
		std::uint32_t set_playerHandling(std::uint32_t value);
		bool get_isTarget();
		bool set_isTarget(bool value);
		bool get_triggersTNT();
		bool set_triggersTNT(bool value);
		bool get_deactivates();
		bool set_deactivates(bool value);
		bool get_scriptedCollisions();
		bool set_scriptedCollisions(bool value);

		std::int8_t state = 0;
		std::uint16_t points = 0;
		std::uint8_t eventID = 0;
		std::int8_t direction = 0;
		std::uint8_t justHit = 0;
		std::int8_t oldState = 0;
		std::int32_t animSpeed = 0;
		std::int32_t special = 0;

		std::int32_t get_var(std::uint8_t x);
		std::int32_t set_var(std::uint8_t x, std::int32_t value);

		std::uint8_t doesHurt = 0;
		std::uint8_t counterEnd = 0;
		std::int16_t objectID = 0;

		std::int32_t draw();
		std::int32_t beSolid(bool shouldCheckForStompingLocalPlayers);
		void bePlatform(float xOld, float yOld, std::int32_t width, std::int32_t height);
		void clearPlatform();
		void putOnGround(bool precise);
		bool ricochet();
		std::int32_t unfreeze(std::int32_t style);
		void deleteObject();
		void deactivate();
		void pathMovement();
		std::int32_t fireBullet(std::uint8_t eventID);
		void particlePixelExplosion(std::int32_t style);
		void grantPickup(jjPLAYER* player, std::int32_t frequency);

		std::int32_t findNearestPlayer(std::int32_t maxDistance) const;
		std::int32_t findNearestPlayerEx(std::int32_t maxDistance, std::int32_t& foundDistance) const;

		bool doesCollide(const jjOBJ* object, bool always) const;
		bool doesCollidePlayer(const jjPLAYER* object, bool always) const;

	private:
		std::int32_t _refCount;
	};

	jjOBJ* get_jjObjects(std::int32_t index);
	jjOBJ* get_jjObjectPresets(std::int8_t id);

	void jjAddParticleTileExplosion(std::uint16_t xTile, std::uint16_t yTile, std::uint16_t tile, bool collapseSceneryStyle);
	void jjAddParticlePixelExplosion(float xPixel, float yPixel, int curFrame, int direction, int mode);

	struct jjPARTICLEPIXEL
	{
		std::uint8_t size;

		std::uint8_t get_color(std::int32_t i) const;
		std::uint8_t set_color(std::int32_t i, std::uint8_t value);
	};

	struct jjPARTICLEFIRE
	{
		std::uint8_t size;
		std::uint8_t color;
		std::uint8_t colorStop;
		std::int8_t colorDelta;
	};

	struct jjPARTICLESMOKE
	{
		std::uint8_t countdown;
	};

	struct jjPARTICLEICETRAIL
	{
		std::uint8_t color;
		std::uint8_t colorStop;
		std::int8_t colorDelta;
	};

	struct jjPARTICLESPARK
	{
		std::uint8_t color;
		std::uint8_t colorStop;
		std::int8_t colorDelta;
	};

	struct jjPARTICLESTRING
	{
		String get_text() const;
		void set_text(String text);
	};

	struct jjPARTICLESNOW
	{
		std::uint8_t frame;
		std::uint8_t countup;
		std::uint8_t countdown;
		std::uint16_t frameBase;
	};

	struct jjPARTICLERAIN
	{
		std::uint8_t frame;
		std::uint16_t frameBase;
	};

	struct jjPARTICLEFLOWER
	{
		std::uint8_t size;
		std::uint8_t color;
		std::uint8_t angle;
		std::int8_t angularSpeed;
		std::uint8_t petals;
	};

	struct jjPARTICLESTAR
	{
		std::uint8_t size;
		std::uint8_t color;
		std::uint8_t angle;
		std::int8_t angularSpeed;
		std::uint8_t frame;
		std::uint8_t colorChangeCounter;
		std::uint8_t colorChangeInterval;
	};

	struct jjPARTICLELEAF
	{
		std::uint8_t frame;
		std::uint8_t countup;
		bool noclip;
		std::uint8_t height;
		std::uint16_t frameBase;
	};

	class jjPARTICLE
	{
	public:
		jjPARTICLE();
		~jjPARTICLE();

		void AddRef();

		void Release();

		float xPos;
		float yPos;
		float xSpeed;
		float ySpeed;

		std::uint8_t particleType;
		bool active;

		union {
			jjPARTICLEPIXEL pixel;
			jjPARTICLEFIRE fire;
			jjPARTICLESMOKE smoke;
			jjPARTICLEICETRAIL trail;
			jjPARTICLESPARK spark;
			jjPARTICLESTRING string;
			jjPARTICLESNOW snow;
			jjPARTICLERAIN rain;
			jjPARTICLEFLOWER flower;
			jjPARTICLESTAR star;
			jjPARTICLELEAF leaf;
		} GENERIC;


	private:
		std::int32_t _refCount;
	};

	jjPARTICLE* GetParticle(std::int32_t index);

	jjPARTICLE* AddParticle(std::int32_t particleType);

	class jjPLAYER
	{
		friend class LevelScriptLoader;

	public:
		jjPLAYER(LevelScriptLoader* levelScripts, Actors::Player* player);
		~jjPLAYER();

		jjPLAYER& operator=(const jjPLAYER& o);

		std::int32_t get_score() const;
		std::int32_t set_score(std::int32_t value);
		std::int32_t get_scoreDisplayed() const;
		std::int32_t set_scoreDisplayed(std::int32_t value);

		std::int32_t setScore(std::int32_t value);

		float get_xPos() const;
		float set_xPos(float value);
		float get_yPos() const;
		float set_yPos(float value);
		float get_xAcc() const;
		float set_xAcc(float value);
		float get_yAcc() const;
		float set_yAcc(float value);
		float get_xOrg() const;
		float set_xOrg(float value);
		float get_yOrg() const;
		float set_yOrg(float value);
		float get_xSpeed();
		float set_xSpeed(float value);
		float get_ySpeed();
		float set_ySpeed(float value);

		float jumpStrength = 0.0f;
		std::int8_t frozen = 0;

		void freeze(bool frozen);
		std::int32_t get_currTile();
		bool startSugarRush(std::int32_t time);
		std::int8_t get_health() const;
		std::int8_t set_health(std::int8_t value);

		std::int32_t warpID = 0;

		std::int32_t get_fastfire() const;
		std::int32_t set_fastfire(std::int32_t value);
		std::int8_t get_currWeapon() const;
		std::int8_t set_currWeapon(std::int8_t value);
		std::int32_t get_lives() const;
		std::int32_t set_lives(std::int32_t value);
		std::int32_t get_invincibility() const;
		std::int32_t set_invincibility(std::int32_t value);
		std::int32_t get_blink() const;
		std::int32_t set_blink(std::int32_t value);
		std::int32_t extendInvincibility(std::int32_t duration);
		std::int32_t get_food() const;
		std::int32_t set_food(std::int32_t value);
		std::int32_t get_coins() const;
		std::int32_t set_coins(std::int32_t value);
		bool testForCoins(std::int32_t numberOfCoins);
		std::int32_t get_gems(std::uint32_t type) const;
		std::int32_t set_gems(std::uint32_t type, std::int32_t value);
		bool testForGems(std::int32_t numberOfGems, std::uint32_t type);

		std::int32_t get_shieldType() const;
		std::int32_t set_shieldType(std::int32_t value);
		std::int32_t get_shieldTime() const;
		std::int32_t set_shieldTime(std::int32_t value);
		std::int32_t get_rolling() const;
		std::int32_t set_rolling(std::int32_t value);

		std::int32_t bossNumber = 0;
		std::int32_t boss = 0;
		bool bossActive = false;
		std::int8_t direction = 0;
		std::int32_t platform = 0;
		std::int32_t flag = 0;
		std::int32_t clientID = 0;

		std::int8_t get_playerID() const;
		std::int32_t get_localPlayerID() const;

		bool team = false;

		bool get_running() const;
		bool set_running(bool value);

		std::int32_t specialJump = 0;

		std::int32_t get_stoned();
		std::int32_t set_stoned(std::int32_t value);

		std::int32_t buttstomp = 0;
		std::int32_t helicopter = 0;
		std::int32_t helicopterElapsed = 0;
		std::int32_t specialMove = 0;
		std::int32_t idle = 0;

		void suckerTube(std::int32_t xSpeed, std::int32_t ySpeed, bool center, bool noclip, bool trigSample);
		void poleSpin(float xSpeed, float ySpeed, std::uint32_t delay);
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
		std::int8_t get_light() const;
		std::int8_t set_light(std::int8_t value);
		std::uint32_t get_fur() const;
		std::uint32_t set_fur(std::uint32_t value);
		void getFur(std::uint8_t& a, std::uint8_t& b, std::uint8_t& c, std::uint8_t& d) const;
		void setFur(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d);

		bool get_noFire() const;
		bool set_noFire(bool value);
		bool get_antiGrav() const;
		bool set_antiGrav(bool value);
		bool get_invisibility() const;
		bool set_invisibility(bool value);
		bool get_noclipMode() const;
		bool set_noclipMode(bool value);
		std::uint8_t get_lighting() const;
		std::uint8_t set_lighting(std::uint8_t value);
		std::uint8_t resetLight();

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

		bool get_powerup(std::uint8_t index);
		bool set_powerup(std::uint8_t index, bool value);
		std::int32_t get_ammo(std::uint8_t index) const;
		std::int32_t set_ammo(std::uint8_t index, std::int32_t value);

		bool offsetPosition(std::int32_t xPixels, std::int32_t yPixels);
		bool warpToTile(std::int32_t xTile, std::int32_t yTile, bool fast);
		bool warpToID(std::uint8_t warpID, bool fast);

		std::uint32_t morph(bool rabbitsOnly, bool morphEffect);
		std::uint32_t morphTo(std::uint32_t charNew, bool morphEffect);
		std::uint32_t revertMorph(bool morphEffect);
		std::uint32_t get_charCurr() const;

		std::uint32_t charOrig = 0;

		void kill();
		bool hurt(std::int8_t damage, bool forceHurt, jjPLAYER* attacker);

		std::uint32_t get_timerState() const;
		bool get_timerPersists() const;
		bool set_timerPersists(bool value);
		std::uint32_t timerStart(std::int32_t ticks, bool startPaused);
		std::uint32_t timerPause();
		std::uint32_t timerResume();
		std::uint32_t timerStop();
		std::int32_t get_timerTime() const;
		std::int32_t set_timerTime(std::int32_t value);
		void timerFunction(const String& functionName);
		void timerFunctionPtr(void* function);
		void timerFunctionFuncPtr(void* function);

		bool activateBoss(bool activate);
		bool limitXScroll(std::uint16_t left, std::uint16_t width);
		void cameraFreezeFF(float xPixel, float yPixel, bool centered, bool instant);
		void cameraFreezeBF(bool xUnfreeze, float yPixel, bool centered, bool instant);
		void cameraFreezeFB(float xPixel, bool yUnfreeze, bool centered, bool instant);
		void cameraFreezeBB(bool xUnfreeze, bool yUnfreeze, bool centered, bool instant);
		void cameraUnfreeze(bool instant);
		void showText(const String& text, std::uint32_t size);
		void showTextByID(std::uint32_t textID, std::uint32_t offset, std::uint32_t size);

		std::uint32_t get_fly() const;
		std::uint32_t set_fly(std::uint32_t value);

		std::int32_t fireBulletDirection(std::uint8_t gun, bool depleteAmmo, bool requireAmmo, std::uint32_t direction);
		std::int32_t fireBulletAngle(std::uint8_t gun, bool depleteAmmo, bool requireAmmo, float angle);

		std::int32_t subscreenX = 0;
		std::int32_t subscreenY = 0;

		float get_cameraX() const;
		float get_cameraY() const;
		std::int32_t get_deaths() const;

		bool get_isJailed() const;
		bool get_isZombie() const;
		std::int32_t get_lrsLives() const;
		std::int32_t get_roasts() const;
		std::int32_t get_laps() const;
		std::int32_t get_lapTimeCurrent() const;
		std::int32_t get_lapTimes(std::uint32_t index) const;
		std::int32_t get_lapTimeBest() const;
		bool get_isAdmin() const;
		bool hasPrivilege(const String& privilege, std::uint32_t moduleID) const;

		bool doesCollide(const jjOBJ* object, bool always) const;
		std::int32_t getObjectHitForce(const jjOBJ& target) const;
		bool objectHit(jjOBJ* target, std::int32_t force, std::uint32_t playerHandling);
		bool isEnemy(const jjPLAYER* victim) const;

		std::uint32_t charCurr = 0;
		std::uint16_t curAnim = 0;
		std::uint32_t curFrame = 0;
		std::uint8_t frameID = 0;

	private:
		LevelScriptLoader* _levelScriptLoader;
		Actors::Player* _player;

		void* _timerCallback;
		std::uint32_t _timerState;
		float _timerLeft;
		bool _timerPersists;
	};

	std::int32_t get_jjPlayerCount();
	std::int32_t get_jjLocalPlayerCount();

	jjPLAYER* get_jjP();
	jjPLAYER* get_jjPlayers(std::uint8_t index);
	jjPLAYER* get_jjLocalPlayers(std::uint8_t index);

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
		static jjPIXELMAP* CreateFromSize(std::uint32_t width, std::uint32_t height);
		static jjPIXELMAP* CreateFromFrame(const jjANIMFRAME* animFrame);
		static jjPIXELMAP* CreateFromLayer(std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height, std::uint32_t layer);
		static jjPIXELMAP* CreateFromLayerObject(std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height, const jjLAYER* layer);
		static jjPIXELMAP* CreateFromTexture(std::uint32_t animFrame);
		static jjPIXELMAP* CreateFromFilename(const String& filename, const jjPAL* palette, std::uint8_t threshold);

		void AddRef();
		void Release();

		jjPIXELMAP& operator=(const jjPIXELMAP& o);

		// TODO: return type std::uint8_t& instead?
		std::uint8_t GetPixel(std::uint32_t x, std::uint32_t y);

		std::uint32_t width = 0;
		std::uint32_t height = 0;

		bool saveToTile(std::uint16_t tileID, bool hFlip) const;
		bool saveToFrame(jjANIMFRAME* frame) const;
		bool saveToFile(const String& filename, const jjPAL& palette) const;

	private:
		std::int32_t _refCount;
	};

	class jjMASKMAP
	{
	public:
		jjMASKMAP();
		~jjMASKMAP();

		static jjMASKMAP* CreateFromBool(bool filled);
		static jjMASKMAP* CreateFromTile(std::uint16_t tileID);

		void AddRef();
		void Release();

		jjMASKMAP& operator=(const jjMASKMAP& o);

		// TODO: return type bool& instead?
		bool GetPixel(std::uint32_t x, std::uint32_t y);

		bool save(std::uint16_t tileID, bool hFlip) const;

	private:
		std::int32_t _refCount;
	};

	class jjLAYER
	{
	public:
		jjLAYER();
		~jjLAYER();

		static jjLAYER* CreateFromSize(std::uint32_t width, std::uint32_t height);
		static jjLAYER* CreateCopy(jjLAYER* other);

		void AddRef();
		void Release();

		jjLAYER& operator=(const jjLAYER& o);

		static jjLAYER* get_jjLayers(int32_t index);

		std::int32_t width = 0;
		std::int32_t widthReal = 0;
		std::int32_t widthRounded = 0;
		std::int32_t height = 0;
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

		std::uint32_t get_spriteMode() const;
		std::uint32_t set_spriteMode(std::uint32_t value) const;
		std::uint8_t get_spriteParam() const;
		std::uint8_t set_spriteParam(std::uint8_t value) const;

		void setXSpeed(float newspeed, bool newSpeedIsAnAutoSpeed) const;
		void setYSpeed(float newspeed, bool newSpeedIsAnAutoSpeed) const;
		float getXPosition(const jjPLAYER* play) const;
		float getYPosition(const jjPLAYER* play) const;

		std::int32_t GetTextureMode() const;
		void SetTextureMode(std::int32_t value) const;
		std::int32_t GetTexture() const;
		void SetTexture(std::int32_t value) const;

		std::int32_t rotationAngle = 0;
		std::int32_t rotationRadiusMultiplier = 0;
		bool tileHeight = false;
		bool tileWidth = false;
		bool limitVisibleRegion = false;
		bool hasTileMap = false;
		bool hasTiles = false;

		static CScriptArray* jjLayerOrderGet();
		static bool jjLayerOrderSet(const CScriptArray& order);
		static CScriptArray* jjLayersFromLevel(const String& filename, const CScriptArray& layerIDs, int32_t tileIDAdjustmentFactor);
		static bool jjTilesFromTileset(const String& filename, std::uint32_t firstTileID, std::uint32_t tileCount, const CScriptArray* paletteColorMapping);

		std::uint16_t tileGet(int xTile, int yTile);
		std::uint16_t tileSet(int xTile, int yTile, std::uint16_t newTile);
		void generateSettableTileArea(int xTile, int yTile, int width, int height);
		void generateSettableTileAreaAll();

		std::int32_t SpeedModeX;
		std::int32_t SpeedModeY;

	private:
		std::int32_t _refCount;
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

	float get_sinTable(std::uint32_t angle);
	float get_cosTable(std::uint32_t angle);
	std::uint32_t RandWord32();
	std::uint64_t unixTimeSec();
	std::uint64_t unixTimeMs();

	bool jjRegexIsValid(const String& expression);
	bool jjRegexMatch(const String& text, const String& expression, bool ignoreCase);
	bool jjRegexMatchWithResults(const String& text, const String& expression, CScriptArray& results, bool ignoreCase);
	bool jjRegexSearch(const String& text, const String& expression, bool ignoreCase);
	bool jjRegexSearchWithResults(const String& text, const String& expression, CScriptArray& results, bool ignoreCase);
	String jjRegexReplace(const String& text, const String& expression, const String& replacement, bool ignoreCase);

	std::int32_t GetFPS();

	bool isAdmin();

	String getLevelFileName();
	String getCurrLevelName();
	void setCurrLevelName(const String& in);
	String get_jjTilesetFileName();

	std::int32_t get_gameState();

	// TODO

	std::int32_t getBorderWidth();
	std::int32_t getBorderHeight();
	bool getSplitscreenType();
	bool setSplitscreenType();

	// TODO

	std::int32_t get_teamScore(std::int32_t color);
	std::int32_t GetMaxHealth();
	std::int32_t GetStartHealth();

	// TODO

	void jjDrawPixel(float xPixel, float yPixel, std::uint8_t color, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);

	void jjDrawRectangle(float xPixel, float yPixel, std::int32_t width, std::int32_t height, std::uint8_t color, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);

	void jjDrawSprite(float xPixel, float yPixel, std::int32_t setID, std::uint8_t animation, std::uint8_t frame, std::int8_t direction, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);

	void jjDrawSpriteFromCurFrame(float xPixel, float yPixel, std::uint32_t sprite, std::int8_t direction, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);

	void jjDrawResizedSprite(float xPixel, float yPixel, std::int32_t setID, std::uint8_t animation, std::uint8_t frame, float xScale, float yScale, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);

	void jjDrawResizedSpriteFromCurFrame(float xPixel, float yPixel, std::uint32_t sprite, float xScale, float yScale, spriteType mode, std::uint8_t param, int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);

	void jjDrawRotatedSprite(float xPixel, float yPixel, std::int32_t setID, std::uint8_t animation, std::uint8_t frame, std::int32_t angle, float xScale, float yScale, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);

	void jjDrawRotatedSpriteFromCurFrame(float xPixel, float yPixel, std::uint32_t sprite, std::int32_t angle, float xScale, float yScale, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);

	void jjDrawSwingingVineSpriteFromCurFrame(float xPixel, float yPixel, std::uint32_t sprite, std::int32_t length, std::int32_t curvature, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);

	void jjDrawTile(float xPixel, float yPixel, std::uint16_t tile, std::uint32_t tileQuadrant, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);

	void jjDrawString(float xPixel, float yPixel, const String& text, std::uint32_t size, std::uint32_t mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);

	void jjDrawStringEx(float xPixel, float yPixel, const String& text, std::uint32_t size, const jjTEXTAPPEARANCE& appearance, std::uint8_t param1, spriteType spriteMode, std::uint8_t param2, int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);

	std::int32_t jjGetStringWidth(const String& text, std::uint32_t size, const jjTEXTAPPEARANCE& style);

	void jjSetDarknessColor(jjPALCOLOR color);
	void jjSetFadeColors(std::uint8_t red, std::uint8_t green, std::uint8_t blue);
	void jjSetFadeColorsFromPalette(std::uint8_t paletteColorID);
	void jjSetFadeColorsFromPalcolor(jjPALCOLOR color);
	jjPALCOLOR jjGetFadeColors();
	void jjUpdateTexturedBG();
	std::int32_t get_jjTexturedBGTexture(jjPALCOLOR color);
	std::int32_t set_jjTexturedBGTexture(std::int32_t texture);
	std::int32_t get_jjTexturedBGStyle();
	std::int32_t set_jjTexturedBGStyle(std::int32_t style);
	bool get_jjTexturedBGUsed(jjPALCOLOR color);
	bool set_jjTexturedBGUsed(bool used);
	bool get_jjTexturedBGStars(bool used);
	bool set_jjTexturedBGStars(bool used);
	float get_jjTexturedBGFadePositionX();
	float set_jjTexturedBGFadePositionX(float value);
	float get_jjTexturedBGFadePositionY();
	float set_jjTexturedBGFadePositionY(float value);

	bool getEnabledTeam(std::uint8_t team);

	bool getKeyDown(std::uint8_t key);
	std::int32_t getCursorX();
	std::int32_t getCursorY();

	void playSample(float xPixel, float yPixel, std::int32_t sample, std::int32_t volume, std::int32_t frequency);
	std::int32_t playLoopedSample(float xPixel, float yPixel, std::int32_t sample, std::int32_t volume, std::int32_t frequency);
	void playPrioritySample(std::int32_t sample);
	bool isSampleLoaded(std::int32_t sample);
	bool loadSample(std::int32_t sample, const String& filename);

	bool getUseLayer8Speeds();
	bool setUseLayer8Speeds(bool value);

	// TODO

	std::int32_t GetEvent(std::uint16_t tx, std::uint16_t ty);
	std::int32_t GetEventParamWrapper(std::uint16_t tx, std::uint16_t ty, std::int32_t offset, std::int32_t length);
	void SetEventByte(std::uint16_t tx, std::uint16_t ty, std::uint8_t newEventId);
	void SetEventParam(std::uint16_t tx, std::uint16_t ty, int8_t offset, std::int8_t length, std::int32_t newValue);
	std::int8_t GetTileType(std::uint16_t tile);
	std::int8_t SetTileType(std::uint16_t tile, std::uint16_t value);

	// TODO

	std::uint16_t jjGetStaticTile(std::uint16_t tileID);
	std::uint16_t jjTileGet(std::uint8_t layer, std::int32_t xTile, std::int32_t yTile);
	std::uint16_t jjTileSet(std::uint8_t layer, std::int32_t xTile, std::int32_t yTile, std::uint16_t newTile);
	void jjGenerateSettableTileArea(std::uint8_t layer, std::int32_t xTile, std::int32_t yTile, std::int32_t width, std::int32_t height);

	// TODO

	bool jjMaskedPixel(std::int32_t xPixel, std::int32_t yPixel);
	bool jjMaskedPixelLayer(std::int32_t xPixel, std::int32_t yPixel, std::uint8_t layer);
	bool jjMaskedHLine(std::int32_t xPixel, std::int32_t lineLength, std::int32_t yPixel);
	bool jjMaskedHLineLayer(std::int32_t xPixel, std::int32_t lineLength, std::int32_t yPixel, std::uint8_t layer);
	bool jjMaskedVLine(std::int32_t xPixel, std::int32_t yPixel, std::int32_t lineLength);
	bool jjMaskedVLineLayer(std::int32_t xPixel, std::int32_t yPixel, std::int32_t lineLength, std::uint8_t layer);
	bool jjMaskedTopVLine(std::int32_t xPixel, std::int32_t yPixel, std::int32_t lineLength);
	bool jjMaskedTopVLineLayer(std::int32_t xPixel, std::int32_t yPixel, std::int32_t lineLength, std::uint8_t layer);

	// TODO

	void jjSetModPosition(std::int32_t order, std::int32_t row, bool reset);
	void jjSlideModChannelVolume(std::int32_t channel, float volume, std::int32_t milliseconds);
	std::int32_t jjGetModOrder();
	std::int32_t jjGetModRow();
	std::int32_t jjGetModTempo();
	void jjSetModTempo(std::uint8_t speed);
	std::int32_t jjGetModSpeed();
	void jjSetModSpeed(std::uint8_t speed);

	std::uint32_t getCustomSetID(std::uint8_t index);
}

#endif