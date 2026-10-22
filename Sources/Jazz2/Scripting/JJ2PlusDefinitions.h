#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../Main.h"
#include "../../nCine/Primitives/Rect.h"
#include "../../nCine/Base/Random.h"
#include "RegisterArray.h"

#include <angelscript.h>

#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>
#include <IO/Stream.h>

using namespace Death::Containers;
using namespace nCine;

namespace Jazz2::Actors
{
	class ActorBase;
	class Player;
}

namespace Jazz2::UI
{
	class HUD;
}

namespace Jazz2::Resources
{
	struct GenericGraphicResource;
}

namespace Jazz2::Tiles
{
	struct LayerDescription;
	struct TileMapLayer;
}

namespace Jazz2::Scripting
{
	class LevelScriptLoader;

	/** @brief Logs that a script called a function that isn't implemented, with where it called it from */
	void Unimplemented(const char* sourceName);
	/** @brief Opens a file a script names, from the content or the directory of the original game */
	std::unique_ptr<Death::IO::Stream> OpenScriptFile(StringView path);
	/** @brief Returns whether a file name a script gives stays inside the data directory of the game */
	bool IsScriptFileNameSafe(StringView filename);

	namespace Legacy
	{
		class AnimationTables;

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
			// The sets of `plus.j2a` as JJ2+ 6 has them, in the order of the file
			mZZAMMO,
			mZZBETA,
			mZZCOMMON,
			mZZCONTINUE,
			mZZEMOJI,
			mZZFONT,
			mZZHELP,
			mZZMENUFONT,
			mZZPLAYERSTATEICONS,
			mZZREPLACEMENTS,
			mZZRETICLES,
			mZZSCENERY,
			// A set the file has that scripts have no name for
			mZZUNNAMED,
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

			aPLAYERBULLET1 = 0x01,
			aPLAYERBULLET2 = 0x02,
			aPLAYERBULLET3 = 0x03,
			aPLAYERBULLET4 = 0x04,
			aPLAYERBULLET5 = 0x05,
			aPLAYERBULLET6 = 0x06,
			aPLAYERBULLET8 = 0x08,
			aPLAYERBULLET9 = 0x09,
			aPLAYERBULLETP1 = 0x0A,
			aPLAYERBULLETP2 = 0x0B,
			aPLAYERBULLETP3 = 0x0C,
			aPLAYERBULLETP4 = 0x0D,
			aPLAYERBULLETP5 = 0x0E,
			aPLAYERBULLETP6 = 0x0F,
			aPLAYERBULLETP8 = 0x11,
			aPLAYERBULLETP9 = 0x12,
			aPLAYERBULLETC1 = 0x13,
			aPLAYERBULLETC2 = 0x14,
			aPLAYERBULLETC3 = 0x15,
			aBULLET = 0x16,
			aCATSMOKE = 0x17,
			aSHARD = 0x18,
			aEXPLOSION = 0x19,
			aBOUNCEONCE = 0x1A,
			aREDGEMTEMP = 0x1B,
			aPLAYERLASER = 0x1C,
			aUTERUSEL = 0x1D,
			aBIRD = 0x1E,
			aBUBBLE = 0x1F,
			aGUN3AMMO3 = 0x21,
			aGUN2AMMO3 = 0x22,
			aGUN4AMMO3 = 0x23,
			aGUN5AMMO3 = 0x24,
			aGUN6AMMO3 = 0x25,
			aGUN7AMMO3 = 0x26,
			aGUN8AMMO3 = 0x27,
			aGUN9AMMO3 = 0x28,
			aTURTLESHELL = 0x29,
			aSWINGVINE = 0x2A,
			aBOMB = 0x2B,
			aSILVERCOIN = 0x2C,
			aGOLDCOIN = 0x2D,
			aGUNCRATE = 0x2E,
			aCARROTCRATE = 0x2F,
			a1UPCRATE = 0x30,
			aGEMBARREL = 0x31,
			aCARROTBARREL = 0x32,
			a1UPBARREL = 0x33,
			aBOMBCRATE = 0x34,
			aGUN3AMMO15 = 0x35,
			aGUN2AMMO15 = 0x36,
			aGUN4AMMO15 = 0x37,
			aGUN5AMMO15 = 0x38,
			aGUN6AMMO15 = 0x39,
			aTNT = 0x3A,
			aAIRBOARDGENERATOR = 0x3B,
			aFROZENGREENSPRING = 0x3C,
			aGUNFASTFIRE = 0x3D,
			aSPRINGCRATE = 0x3E,
			aREDGEM = 0x3F,
			aGREENGEM = 0x40,
			aBLUEGEM = 0x41,
			aPURPLEGEM = 0x42,
			aSUPERREDGEM = 0x43,
			aBIRDCAGE = 0x44,
			aGUNBARREL = 0x45,
			aGEMCRATE = 0x46,
			aMORPHMONITOR = 0x47,
			aENERGYUP = 0x48,
			aFULLENERGY = 0x49,
			aFIRESHIELD = 0x4A,
			aWATERSHIELD = 0x4B,
			aLIGHTSHIELD = 0x4C,
			aFASTFEET = 0x4F,
			aEXTRALIFE = 0x50,
			aENDOFLEVELPOST = 0x51,
			aSAVEPOST = 0x53,
			aBONUSLEVELPOST = 0x54,
			aREDSPRING = 0x55,
			aGREENSPRING = 0x56,
			aBLUESPRING = 0x57,
			aINVINCIBILITY = 0x58,
			aEXTRATIME = 0x59,
			aFREEZER = 0x5A,
			aHREDSPRING = 0x5B,
			aHGREENSPRING = 0x5C,
			aHBLUESPRING = 0x5D,
			aBIRDMORPHMONITOR = 0x5E,
			aTRIGGERCRATE = 0x5F,
			aFLYCARROT = 0x60,
			aRECTREDGEM = 0x61,
			aRECTGREENGEM = 0x62,
			aRECTBLUEGEM = 0x63,
			aTUFTURT = 0x64,
			aTUFBOSS = 0x65,
			aLABRAT = 0x66,
			aDRAGON = 0x67,
			aLIZARD = 0x68,
			aBUMBEE = 0x69,
			aRAPIER = 0x6A,
			aSPARK = 0x6B,
			aBAT = 0x6C,
			aSUCKER = 0x6D,
			aCATERPILLAR = 0x6E,
			aCHESHIRE1 = 0x6F,
			aCHESHIRE2 = 0x70,
			aHATTER = 0x71,
			aBILSYBOSS = 0x72,
			aSKELETON = 0x73,
			aDOGGYDOGG = 0x74,
			aNORMTURTLE = 0x75,
			aHELMUT = 0x76,
			aDEMON = 0x78,
			aDRAGONFLY = 0x7B,
			aMONKEY = 0x7C,
			aFATCHK = 0x7D,
			aFENCER = 0x7E,
			aFISH = 0x7F,
			aMOTH = 0x80,
			aSTEAM = 0x81,
			aROCK = 0x82,
			aGUN1POWER = 0x83,
			aGUN2POWER = 0x84,
			aGUN3POWER = 0x85,
			aGUN4POWER = 0x86,
			aGUN5POWER = 0x87,
			aGUN6POWER = 0x88,
			aPINLEFTPADDLE = 0x89,
			aPINRIGHTPADDLE = 0x8A,
			aPIN500BUMP = 0x8B,
			aPINCARROTBUMP = 0x8C,
			aAPPLE = 0x8D,
			aBANANA = 0x8E,
			aCHERRY = 0x8F,
			aORANGE = 0x90,
			aPEAR = 0x91,
			aPRETZEL = 0x92,
			aSTRAWBERRY = 0x93,
			aSTEADYLIGHT = 0x94,
			aPULZELIGHT = 0x95,
			aFLICKERLIGHT = 0x96,
			aQUEENBOSS = 0x97,
			aFLOATSUCKER = 0x98,
			aBRIDGE = 0x99,
			aLEMON = 0x9A,
			aLIME = 0x9B,
			aTHING = 0x9C,
			aWMELON = 0x9D,
			aPEACH = 0x9E,
			aGRAPES = 0x9F,
			aLETTUCE = 0xA0,
			aEGGPLANT = 0xA1,
			aCUCUMB = 0xA2,
			aCOKE = 0xA3,
			aPEPSI = 0xA4,
			aMILK = 0xA5,
			aPIE = 0xA6,
			aCAKE = 0xA7,
			aDONUT = 0xA8,
			aCUPCAKE = 0xA9,
			aCHIPS = 0xAA,
			aCANDY1 = 0xAB,
			aCHOCBAR = 0xAC,
			aICECREAM = 0xAD,
			aBURGER = 0xAE,
			aPIZZA = 0xAF,
			aFRIES = 0xB0,
			aCHICKLEG = 0xB1,
			aSANDWICH = 0xB2,
			aTACOBELL = 0xB3,
			aWEENIE = 0xB4,
			aHAM = 0xB5,
			aCHEESE = 0xB6,
			aFLOATLIZARD = 0xB7,
			aSTANDMONKEY = 0xB8,
			aDESTRUCTSCENERY = 0xB9,
			aDESTRUCTSCENERYBOMB = 0xBA,
			aCOLLAPSESCENERY = 0xBB,
			aSTOMPSCENERY = 0xBC,
			aGEMSTOMP = 0xBD,
			aRAVEN = 0xBE,
			aTUBETURTLE = 0xBF,
			aGEMRING = 0xC0,
			aROTSMALLTREE = 0xC1,
			aAMBIENTSOUND = 0xC2,
			aUTERUS = 0xC3,
			aCRAB = 0xC4,
			aWITCH = 0xC5,
			aROCKTURT = 0xC6,
			aBUBBA = 0xC7,
			aDEVILDEVAN = 0xC8,
			aDEVANROBOT = 0xC9,
			aROBOT = 0xCA,
			aCARROTUSPOLE = 0xCB,
			aPSYCHPOLE = 0xCC,
			aDIAMONDUSPOLE = 0xCD,
			aFRUITPLATFORM = 0xD1,
			aBOLLPLATFORM = 0xD2,
			aGRASSPLATFORM = 0xD3,
			aPINKPLATFORM = 0xD4,
			aSONICPLATFORM = 0xD5,
			aSPIKEPLATFORM = 0xD6,
			aSPIKEBOLL = 0xD7,
			aGENERATOR = 0xD8,
			aEVA = 0xD9,
			aBUBBLER = 0xDA,
			aTNTPOWER = 0xDB,
			aGUN8POWER = 0xDC,
			aGUN9POWER = 0xDD,
			aSPIKEBOLL3D = 0xDF,
			aSPRINGCORD = 0xE0,
			aBEES = 0xE1,
			aCOPTER = 0xE2,
			aLASERSHIELD = 0xE3,
			aSTOPWATCH = 0xE4,
			aJUNGLEPOLE = 0xE5,
			aBIGROCK = 0xE7,
			aBIGBOX = 0xE8,
			aTRIGGERSCENERY = 0xEA,
			aSONICBOSS = 0xEB,
			aBUTTERFLY = 0xEC,
			aBEEBOY = 0xED,
			aSNOW = 0xEE,
			aTWEEDLEBOSS = 0xF1,
			aAIRBOARD = 0xF3,
			aFLAG = 0xF4,
			aXNORMTURTLE = 0xF8,
			aXLIZARD = 0xF9,
			aXFLOATLIZARD = 0xFA,
			aXBILSYBOSS = 0xFB,
			aZCAT = 0xFC,
			aZGHOST = 0xFD,
			areaONEWAY = 0x01,
			areaHURT = 0x02,
			areaVINE = 0x03,
			areaHOOK = 0x04,
			areaSLIDE = 0x05,
			areaHPOLE = 0x06,
			areaVPOLE = 0x07,
			areaFLYOFF = 0x08,
			areaRICOCHET = 0x09,
			areaBELTRIGHT = 0x0A,
			areaBELTLEFT = 0x0B,
			areaBELTACCRIGHT = 0x0C,
			areaBELTACCLEFT = 0x0D,
			areaSTOPENEMY = 0x0E,
			areaWINDLEFT = 0x0F,
			areaWINDRIGHT = 0x10,
			areaEOL = 0x11,
			areaWARPEOL = 0x12,
			areaENDMORPH = 0x13,
			areaFLOATUP = 0x14,
			areaROCKTRIGGER = 0x15,
			areaDIMLIGHT = 0x16,
			areaSETLIGHT = 0x17,
			areaLIMITXSCROLL = 0x18,
			areaRESETLIGHT = 0x19,
			areaWARPSECRET = 0x1A,
			areaECHO = 0x1B,
			areaBOSSTRIGGER = 0x1C,
			areaJAZZLEVELSTART = 0x1D,
			areaSPAZLEVELSTART = 0x1E,
			areaMPLEVELSTART = 0x1F,
			areaLORILEVELSTART = 0x20,
			areaWARP = 0xE6,
			areaWARPTARGET = 0xF0,
			areaAREAID = 0xF2,
			areaNOFIREZONE = 0xF5,
			areaTRIGGERZONE = 0xF6,
			aSUCKERTUBE = 0xCE,
			aTEXT = 0xCF,
			aWATERLEVEL = 0xD0,
			aMORPHFROG = 0xDE,
			aWATERBLOCK = 0xE9,
			aCOUNT = 0x100};

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


		/** @brief Describes how text is rendered (spacing, animation and special-character handling) */
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

			/** @brief Horizontal wave amplitude */
			std::int32_t xAmp;
			/** @brief Vertical wave amplitude */
			std::int32_t yAmp;
			/** @brief Extra spacing between characters */
			std::int32_t spacing;
			/** @brief Whether characters use fixed (monospace) width */
			bool monospace;
			/** @brief Whether a leading hash character is skipped */
			bool skipInitialHash;

			/** @brief How the at (@) character is handled */
			ch_ at = ch_HIDE;
			/** @brief How the caret (^) character is handled */
			ch_ caret = ch_HIDE;
			/** @brief How the hash (#) character is handled */
			ch_ hash = ch_HIDE;
			/** @brief How the newline character is handled */
			ch_ newline = ch_HIDE;
			/** @brief How the pipe (|) character is handled */
			ch_ pipe = ch_HIDE;
			/** @brief How the section (§) character is handled */
			ch_ section = ch_HIDE;
			/** @brief How the tilde (~) character is handled */
			ch_ tilde = ch_HIDE;
			/** @brief Horizontal text alignment */
			align_ align = align_DEFAULT;

			/** @brief Constructs a default appearance in place */
			static void constructor(void* self);
			/** @brief Constructs an appearance from a packed mode value */
			static void constructorMode(std::uint32_t mode, void* self);

			jjTEXTAPPEARANCE& operator=(std::uint32_t other);
		};

		/** @brief A single RGB palette color */
		struct jjPALCOLOR {
			/** @brief Red component */
			std::uint8_t red;
			/** @brief Green component */
			std::uint8_t green;
			/** @brief Blue component */
			std::uint8_t blue;
			/**
			 * @brief Alpha component, only colors of 32-bit images use it in JJ2+, left 0 otherwise
			 *
			 * It also makes the color 4 bytes, which the x64 calling convention of MSVC passes and returns in a register
			 * as AngelScript does - a color of 3 bytes is passed by a pointer to it there, and a script's color then
			 * arrives as an address.
			 */
			std::uint8_t alpha;

			/** @brief Constructs a black color in place */
			static void Create(void* self);
			/** @brief Constructs a color from red, green and blue components in place */
			static void CreateFromRgb(std::uint8_t red, std::uint8_t green, std::uint8_t blue, void* self);
			/** @brief Constructs a color from red, green, blue and alpha components in place */
			static void CreateFromRgba(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha, void* self);

			/** @brief Returns the hue component of the color */
			std::uint8_t getHue();
			/** @brief Returns the saturation component of the color */
			std::uint8_t getSat();
			/** @brief Returns the lightness component of the color */
			std::uint8_t getLight();

			/** @brief Reorders the color channels by the given source channels */
			void swizzle(std::uint32_t redc, std::uint32_t greenc, std::uint32_t bluec);
			/** @brief Sets the color from hue, saturation and lightness */
			void setHSL(std::int32_t hue, std::uint8_t sat, std::uint8_t light);

			jjPALCOLOR& operator=(const jjPALCOLOR& other);
			bool operator==(const jjPALCOLOR& other);
		};

		/**
		 * @brief A 256-color palette
		 *
		 * Reference-counted wrapper around 256 @ref jjPALCOLOR entries, mirroring the original JJ2+ palette object.
		 * Scripts use it to read and edit individual colors and to fill, tint, gradient or copy ranges, then `apply()`
		 * the result to the screen (or `reset()` back to the level default). Backs the global `jjPalette`.
		 */
		class jjPAL
		{
		public:
			/** @brief Creates a new instance */
			jjPAL();
			~jjPAL();

			/** @brief Returns a new instance */
			static jjPAL* Create();

			/** @brief Increments the reference count */
			void AddRef();

			/** @brief Decrements the reference count */
			void Release();

			jjPAL& operator=(const jjPAL& o);

			bool operator==(const jjPAL& o);

			/** @brief Returns the color at the specified palette index */
			jjPALCOLOR& getColor(std::uint8_t idx);
			/** @brief Returns the read-only color at the specified palette index */
			const jjPALCOLOR& getConstColor(std::uint8_t idx) const;
			/** @brief Sets the color at the specified palette index */
			jjPALCOLOR& setColorEntry(std::uint8_t idx, jjPALCOLOR& value);

			/** @brief Resets the palette to the level default */
			void reset();
			/** @brief Applies the palette so it takes effect on screen */
			void apply();
			/** @brief Loads the palette from a file */
			bool load(const String& filename);
			/** @brief Fills the whole palette with the given color, blended by opacity */
			void fill(std::uint8_t red, std::uint8_t green, std::uint8_t blue, float opacity);
			/** @brief Tints a range of palette entries with the given color */
			void fillTint(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t start, std::uint8_t length, float opacity);
			/** @brief Fills the whole palette with the given color, blended by opacity */
			void fillFromColor(jjPALCOLOR color, float opacity);
			/** @brief Tints a range of palette entries with the given color */
			void fillTintFromColor(jjPALCOLOR color, std::uint8_t start, std::uint8_t length, float opacity);
			/** @brief Writes a color gradient into a range of palette entries */
			void gradient(std::uint8_t red1, std::uint8_t green1, std::uint8_t blue1, std::uint8_t red2, std::uint8_t green2, std::uint8_t blue2, std::uint8_t start, std::uint8_t length, float opacity, bool inclusive);
			/** @brief Writes a color gradient into a range of palette entries */
			void gradientFromColor(jjPALCOLOR color1, jjPALCOLOR color2, std::uint8_t start, std::uint8_t length, float opacity, bool inclusive);
			/** @brief Copies a range of entries from another palette, blended by opacity */
			void copyFrom(std::uint8_t start, std::uint8_t length, std::uint8_t start2, const jjPAL& source, float opacity);
			/** @brief Returns the palette index closest to the given color */
			std::uint8_t findNearestColor(jjPALCOLOR color);
			/** @brief Returns the palette index closest to the given color but the transparent one, index 0 */
			std::uint8_t FindNearestOpaqueColor(const jjPALCOLOR& color) const;

		private:
			std::int32_t _refCount;
			jjPALCOLOR _palette[256];
		};

		/**
		 * @brief A binary data stream for serialization and file I/O
		 *
		 * Reference-counted growable byte buffer that scripts use to serialize values. Typed `push`/`pop` and `get`
		 * helpers append and consume primitives and strings from either end, and the whole stream can be saved to or
		 * loaded from a file. Also used as the payload of networking packets sent via `jjSendPacket`.
		 */
		class jjSTREAM
		{
		public:
			/** @brief Creates a new instance */
			jjSTREAM();
			~jjSTREAM();

			/** @brief Returns a new empty instance */
			static jjSTREAM* Create();
			/** @brief Returns a new instance populated from a file */
			static jjSTREAM* CreateFromFile(const String& filename);

			/** @brief Increments the reference count */
			void AddRef();
			/** @brief Decrements the reference count */
			void Release();

			jjSTREAM& operator=(const jjSTREAM& o);

			/** @brief Returns the size of the stream in bytes */
			std::uint32_t getSize() const;
			/** @brief Returns whether the stream contains no data */
			bool isEmpty() const;
			/** @brief Saves the stream contents to a file */
			bool save(const String& tilename) const;
			/** @brief Removes all data from the stream */
			void clear();
			/** @brief Discards the given number of bytes from the front of the stream */
			bool discard(std::uint32_t count);

			/** @brief Appends the raw bytes of a string to the stream */
			bool write(const String& value);
			/** @brief Appends the contents of another stream */
			bool write(const jjSTREAM& value);
			/** @brief Reads the given number of bytes from the front into a string */
			bool get(String& value, std::uint32_t count);
			/** @brief Reads the given number of bytes from the front into another stream */
			bool get(jjSTREAM& value, std::uint32_t count);
			/** @brief Reads bytes from the front up to a delimiter into a string */
			bool getLine(String& value, const String& delim);

			/** @brief Appends a boolean value to the stream */
			bool push(bool value);
			/** @brief Appends an unsigned 8-bit value to the stream */
			bool push(std::uint8_t value);
			/** @brief Appends a signed 8-bit value to the stream */
			bool push(std::int8_t value);
			/** @brief Appends an unsigned 16-bit value to the stream */
			bool push(std::uint16_t value);
			/** @brief Appends a signed 16-bit value to the stream */
			bool push(std::int16_t value);
			/** @brief Appends an unsigned 32-bit value to the stream */
			bool push(std::uint32_t value);
			/** @brief Appends a signed 32-bit value to the stream */
			bool push(std::int32_t value);
			/** @brief Appends an unsigned 64-bit value to the stream */
			bool push(std::uint64_t value);
			/** @brief Appends a signed 64-bit value to the stream */
			bool push(std::int64_t value);
			/** @brief Appends a single-precision float to the stream */
			bool push(float value);
			/** @brief Appends a double-precision float to the stream */
			bool push(double value);
			/** @brief Appends a length-prefixed string to the stream */
			bool push(const String& value);
			/** @brief Appends the contents of another stream */
			bool push(const jjSTREAM& value);

			/** @brief Reads a boolean value from the front of the stream */
			bool pop(bool& value);
			/** @brief Reads an unsigned 8-bit value from the front of the stream */
			bool pop(std::uint8_t& value);
			/** @brief Reads a signed 8-bit value from the front of the stream */
			bool pop(std::int8_t& value);
			/** @brief Reads an unsigned 16-bit value from the front of the stream */
			bool pop(std::uint16_t& value);
			/** @brief Reads a signed 16-bit value from the front of the stream */
			bool pop(std::int16_t& value);
			/** @brief Reads an unsigned 32-bit value from the front of the stream */
			bool pop(std::uint32_t& value);
			/** @brief Reads a signed 32-bit value from the front of the stream */
			bool pop(std::int32_t& value);
			/** @brief Reads an unsigned 64-bit value from the front of the stream */
			bool pop(std::uint64_t& value);
			/** @brief Reads a signed 64-bit value from the front of the stream */
			bool pop(std::int64_t& value);
			/** @brief Reads a single-precision float from the front of the stream */
			bool pop(float& value);
			/** @brief Reads a double-precision float from the front of the stream */
			bool pop(double& value);
			/** @brief Reads a length-prefixed string from the front of the stream */
			bool pop(String& value);
			/** @brief Reads stream contents from the front into another stream */
			bool pop(jjSTREAM& value);

		private:
			std::int32_t _refCount;
			SmallVector<std::uint8_t, 0> _data;
			// Reads advance this instead of erasing from the front, so reading a large stream stays linear
			std::uint32_t _readPos;

			const std::uint8_t* PeekFront(std::uint32_t count) const;
			bool Take(void* target, std::uint32_t count);
			void Append(const void* source, std::uint32_t count);

			template<class T>
			bool PushValue(const T& value) {
				Append(&value, sizeof(T));
				return true;
			}

			template<class T>
			bool PopValue(T& value) {
				return Take(&value, sizeof(T));
			}
		};

		/** @brief A seedable pseudo-random number generator */
		class jjRNG
		{
		public:
			/** @brief Creates a new instance seeded with the given value */
			jjRNG(std::uint64_t seed);
			~jjRNG();

			/** @brief Returns a new instance seeded with the given value */
			static jjRNG* Create(std::uint64_t seed);

			/** @brief Increments the reference count */
			void AddRef();
			/** @brief Decrements the reference count */
			void Release();

			std::uint64_t operator()();

			jjRNG& operator=(const jjRNG& o);

			bool operator==(const jjRNG& o) const;

			/** @brief Reseeds the generator with the given value */
			void seed(std::uint64_t value);
			/** @brief Advances the generator past the given number of outputs */
			void discard(std::uint64_t count);

		private:
			const std::uint64_t DefaultInitSequence = 0xda3e39cb94b95bdbULL;

			std::int32_t _refCount;
			RandomGenerator _random;
		};

		/**
		 * @brief Holds the behavior assigned to an object: a built-in id, a script function, or a script object
		 *
		 * Tagged-union-style value assigned to @ref jjOBJ::behavior that selects what drives an object each frame:
		 * a built-in behavior id, a custom script function, or a script object implementing the behavior interface.
		 * Assignment and conversion operators accept each form, and the held function/object reference is reference-
		 * counted so it survives as long as the behavior does.
		 */
		struct jjBEHAVIOR
		{
			/** @brief Constructs an empty behavior in place */
			static jjBEHAVIOR* Create(jjBEHAVIOR* self);
			/** @brief Constructs a behavior from a built-in behavior id in place */
			static jjBEHAVIOR* CreateFromBehavior(std::uint32_t behavior, jjBEHAVIOR* self);
			/** @brief Destroys a behavior in place */
			static void Destroy(jjBEHAVIOR* self);

			// Releases the held function/object reference. Runs both when AngelScript destroys a value (via Destroy)
			// and when an embedding object (jjOBJ::behavior) is destroyed.
			~jjBEHAVIOR();

			/** @brief Creates a new instance */
			jjBEHAVIOR() = default;
			// Copy must add a reference to the held function/object (rule of three, since the destructor releases)
			jjBEHAVIOR(const jjBEHAVIOR& other);

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

			// A behavior is one of: a built-in behavior id (BEHAVIOR::Behavior), a custom script behavior function
			// (jjVOIDFUNCOBJ), or a script object implementing jjBEHAVIORINTERFACE. The function/object hold an
			// AngelScript reference (managed in Create/Destroy/operator=).
			std::uint32_t behaviorId = 0;
			asIScriptFunction* function = nullptr;
			asIScriptObject* object = nullptr;
		};

		/**
		 * @brief A single frame of a sprite animation
		 *
		 * One entry of the frame table, `jjAnimFrames[index]`, with its image, its size and its hotspot, coldspot and
		 * gunspot. There's only one instance of an entry, as in JJ2+, so what a script changes stays - and the sprites
		 * the game draws of the frame show it too, once the script returns (see @ref AnimationTables).
		 */
		class jjANIMFRAME
		{
			friend class AnimationTables;
			friend class jjPIXELMAP;
			friend class jjCOLORMAP;

		public:
			/** @brief Creates a new instance, the entry at the given index of the frame table */
			explicit jjANIMFRAME(std::uint32_t index);
			~jjANIMFRAME();

			/** @brief Increments the reference count */
			void AddRef();
			/** @brief Decrements the reference count */
			void Release();

			/** @brief Copies the image, the size and the spots of another frame */
			jjANIMFRAME& operator=(const jjANIMFRAME& o);

			/** @brief Returns the animation frame at the specified global index */
			static jjANIMFRAME* get_jjAnimFrames(std::uint32_t index);

			/** @brief Horizontal hotspot offset */
			std::int16_t hotSpotX = 0;
			/** @brief Vertical hotspot offset */
			std::int16_t hotSpotY = 0;
			/** @brief Horizontal coldspot offset */
			std::int16_t coldSpotX = 0;
			/** @brief Vertical coldspot offset */
			std::int16_t coldSpotY = 0;
			/** @brief Horizontal gun muzzle offset */
			std::int16_t gunSpotX = 0;
			/** @brief Vertical gun muzzle offset */
			std::int16_t gunSpotY = 0;
			/** @brief Frame width in pixels */
			std::uint16_t width = 0;
			/** @brief Frame height in pixels */
			std::uint16_t height = 0;

			/** @brief Returns whether the frame is drawn translucent */
			bool get_transparent() const;
			/** @brief Sets whether the frame is drawn translucent */
			bool set_transparent(bool value);
			/** @brief Returns whether the frame has colors of its own instead of palette indices, see @ref jjCOLORMAP */
			bool get_is32Bit() const;
			/** @brief Returns whether this frame collides with another frame at the given positions */
			bool doesCollide(std::int32_t xPos, std::int32_t yPos, std::int32_t direction, const jjANIMFRAME* frame2, std::int32_t xPos2, std::int32_t yPos2, std::int32_t direction2, bool always) const;

		private:
			std::int32_t _refCount;
			std::uint32_t _index;
			// Palette indices, width * height of them, 0 is transparent
			std::unique_ptr<std::uint8_t[]> _pixels;
			// Colors with alpha of a 32-bit frame, width * height of them, or null - `_pixels` has the nearest palette indices
			// of them then, for what reads the frame as indices (collisions, jjPIXELMAP)
			std::unique_ptr<jjPALCOLOR[]> _colors;
			bool _transparent;
			// Whether the frame is in the list of frames AnimationTables::Flush() looks at
			bool _watched;
			// Index of the animation whose sprite sheet has the image of the frame, read on first use, -1 once read
			std::int32_t _source;
			// Index of the animation the frame was made for, which draws it best
			std::uint32_t _owner;
			// Distinguishes every change of the frame from any other, 0 for the frame as the game has it
			std::uint32_t _version;
			// The spots as AnimationTables::Flush() saw them last, the ones a script writes are told apart by them
			std::int16_t _seenSpots[6];
		};

		/**
		 * @brief A sprite animation consisting of a sequence of frames
		 *
		 * One entry of the animation table, `jjAnimations[index]`: the number of frames, the playback speed and the
		 * index of the first frame in the frame table. There's only one instance of an entry, as in JJ2+.
		 */
		class jjANIMATION
		{
			friend class AnimationTables;

		public:
			/** @brief Creates a new instance, the entry at the given index of the animation table */
			explicit jjANIMATION(std::uint32_t index);
			~jjANIMATION();

			/** @brief Increments the reference count */
			void AddRef();
			/** @brief Decrements the reference count */
			void Release();

			/** @brief Copies the frame count, the speed and the first frame of another animation */
			jjANIMATION& operator=(const jjANIMATION& o);

			/** @brief Saves the animation to a file using the given palette */
			bool save(const String& filename, const jjPAL& palette) const;
			/** @brief Loads animation frames from a file, overwriting starting at the given frame */
			bool load(const String& filename, std::int32_t hotSpotX, std::int32_t hotSpotY, std::int32_t coldSpotYOffset, std::int32_t firstFrameToOverwrite);

			/** @brief Returns the animation at the specified global index */
			static jjANIMATION* get_jjAnimations(std::uint32_t index);

			/** @brief Number of frames in the animation */
			std::uint16_t frameCount = 0;
			/** @brief Playback speed in frames per second */
			std::int16_t fps = 0;

			/** @brief Returns the global index of the first frame */
			std::uint32_t get_firstFrame() const;
			/** @brief Sets the global index of the first frame */
			std::uint32_t set_firstFrame(std::uint32_t index);

			/** @brief Returns the global index of the first frame of the animation */
			std::uint32_t getAnimFirstFrame();

		private:
			std::int32_t _refCount;
			std::uint32_t _index;
			std::uint32_t _firstFrame;
			// Whether the animation is in the list of animations AnimationTables::Flush() looks at
			bool _watched;
			// The frame count as AnimationTables::Flush() saw it last
			std::uint16_t _seenFrameCount;
			// Index of the graphics of the game the animation is shown in (see AnimationTables), -1 if it's in none
			std::int32_t _native;
			// The sprite sheet a script draws the animation with when it isn't in the graphics of the game, made on
			// first use, and what it was made of
			std::unique_ptr<Resources::GenericGraphicResource> _graphics;
			std::uint64_t _graphicsSignature;
		};

		/**
		 * @brief A set of related sprite animations
		 *
		 * One entry of the set table, `jjAnimSets[setID]`, with the index of its first @ref jjANIMATION. A set of the
		 * original is always loaded, a custom one (`ANIM::CUSTOM[n]`) once a script loads or allocates it.
		 */
		class jjANIMSET
		{
			friend class AnimationTables;

		public:
			/** @brief Creates a new instance, the entry of the given set ID */
			explicit jjANIMSET(std::uint32_t index);
			~jjANIMSET();

			/** @brief Increments the reference count */
			void AddRef();
			/** @brief Decrements the reference count */
			void Release();

			/** @brief Returns the animation set at the specified global index */
			static jjANIMSET* get_jjAnimSets(std::uint32_t index);

			/** @brief Returns the index of the first animation of the set */
			std::uint32_t convertAnimSetToUint();

			/** @brief Loads animations from a file into this set, overwriting starting at the given animation */
			jjANIMSET* load(std::uint32_t fileSetID, const String& filename, int32_t firstAnimToOverwrite, int32_t firstFrameToOverwrite);
			/** @brief Allocates animations in this set with the given per-animation frame counts */
			jjANIMSET* allocate(const CScriptArray& frameCounts);

			/** @brief Index of the first animation of the set in the animation table, 0 if the set isn't loaded */
			std::uint32_t firstAnim = 0;

		private:
			std::int32_t _refCount;
			std::uint32_t _index;
			// Whether the set of the original was looked up already (see AnimationTables::ResolveSet()), and how many
			// animations it has
			bool _resolved;
			std::uint32_t _animCount = 0;
			// Whether the set is in the list of sets AnimationTables::Flush() looks at, and firstAnim as it saw it last
			bool _watched;
			std::uint32_t _seenFirstAnim;
		};

		/**
		 * @brief Drawing surface used by scripts to render onto the HUD
		 *
		 * Short-lived surface handed to draw callbacks, bound to a @ref UI::HUD and a view rectangle. Its methods draw
		 * pixels, rectangles, tiles, sprites (plain, resized, rotated, vine) and text into that frame's HUD layer.
		 * Mirrors the original JJ2+ `jjCANVAS` passed to `onDraw*` hooks.
		 */
		struct jjCANVAS {
			/** @brief The HUD this canvas draws onto */
			UI::HUD* Hud;
			/** @brief The visible view rectangle */
			Rectf View;
			/** @brief Counts the draws into the HUD in this frame, each is drawn over the ones before it, as in JJ2+ */
			std::uint16_t* DrawCount;

			/** @brief Creates a new instance bound to the given HUD and view, @p drawCount is shared by the canvases of the frame */
			jjCANVAS(UI::HUD* hud, const Rectf& view, std::uint16_t* drawCount);

			/** @brief Returns the HUD layer of the next draw, above every draw before it */
			std::uint16_t NextLayer();

			/** @brief Draws a single colored pixel */
			void DrawPixel(std::int32_t xPixel, std::int32_t yPixel, std::uint8_t color, std::uint32_t mode, std::uint8_t param);
			/** @brief Draws a filled colored rectangle */
			void DrawRectangle(std::int32_t xPixel, std::int32_t yPixel, std::int32_t width, std::int32_t height, std::uint8_t color, std::uint32_t mode, std::uint8_t param);
			/** @brief Draws a sprite frame from an animation set */
			void DrawSprite(std::int32_t xPixel, std::int32_t yPixel, std::int32_t setID, std::uint8_t animation, std::uint8_t frame, int8_t direction, std::uint32_t mode, std::uint8_t param);
			/** @brief Draws the current frame of the given sprite */
			void DrawCurFrameSprite(std::int32_t xPixel, std::int32_t yPixel, std::uint32_t sprite, int8_t direction, std::uint32_t mode, std::uint8_t param);
			/** @brief Draws a sprite frame scaled by the given factors */
			void DrawResizedSprite(std::int32_t xPixel, std::int32_t yPixel, std::int32_t setID, std::uint8_t animation, std::uint8_t frame, float xScale, float yScale, std::uint32_t mode, std::uint8_t param);
			/** @brief Draws the current frame of the given sprite scaled by the given factors */
			void DrawResizedCurFrameSprite(std::int32_t xPixel, std::int32_t yPixel, std::uint32_t sprite, float xScale, float yScale, std::uint32_t mode, std::uint8_t param);
			/** @brief Draws a sprite frame rotated and scaled */
			void DrawTransformedSprite(std::int32_t xPixel, std::int32_t yPixel, std::int32_t setID, std::uint8_t animation, std::uint8_t frame, std::int32_t angle, float xScale, float yScale, std::uint32_t mode, std::uint8_t param);
			/** @brief Draws the current frame of the given sprite rotated and scaled */
			void DrawTransformedCurFrameSprite(std::int32_t xPixel, std::int32_t yPixel, std::uint32_t sprite, std::int32_t angle, float xScale, float yScale, std::uint32_t mode, std::uint8_t param);
			/** @brief Draws a swinging vine sprite */
			void DrawSwingingVine(std::int32_t xPixel, std::int32_t yPixel, std::uint32_t sprite, std::int32_t length, std::int32_t curvature, std::uint32_t mode, std::uint8_t param);

			/** @brief Draws a single tile from the tileset */
			void ExternalDrawTile(std::int32_t xPixel, std::int32_t yPixel, std::uint16_t tile, std::uint32_t tileQuadrant);
			/** @brief Draws text using the given built-in font size */
			void DrawTextBasicSize(std::int32_t xPixel, std::int32_t yPixel, const String& text, std::uint32_t size, std::uint32_t mode, std::uint8_t param);
			/** @brief Draws text with a custom appearance using the given built-in font size */
			void DrawTextExtSize(std::int32_t xPixel, std::int32_t yPixel, const String& text, std::uint32_t size, const jjTEXTAPPEARANCE& appearance, std::uint8_t param1, std::uint32_t mode, std::uint8_t param);

			/** @brief Draws text using the given animation as a font */
			void drawString(std::int32_t xPixel, std::int32_t yPixel, const String& text, const jjANIMATION& animation, std::uint32_t mode, std::uint8_t param);

			/** @brief Draws text with a custom appearance using the given animation as a font */
			void drawStringEx(std::int32_t xPixel, std::int32_t yPixel, const String& text, const jjANIMATION& animation, const jjTEXTAPPEARANCE& appearance, std::uint8_t param1, std::uint32_t spriteMode, std::uint8_t param2);

			/** @brief Draws text directly into the level using the given animation as a font */
			static void jjDrawString(float xPixel, float yPixel, const String& text, const jjANIMATION& animation, std::uint32_t mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);
			/** @brief Draws text with a custom appearance directly into the level using the given animation as a font */
			static void jjDrawStringEx(float xPixel, float yPixel, const String& text, const jjANIMATION& animation, const jjTEXTAPPEARANCE& appearance, std::uint8_t param1, std::uint32_t spriteMode, std::uint8_t param2, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);
			/** @brief Returns the rendered width of the text in pixels */
			static int jjGetStringWidth(const String& text, const jjANIMATION& animation, const jjTEXTAPPEARANCE& style);
		};

		class jjOBJ;
		class jjPLAYER;
		// Native actor that hosts a script-controlled jjOBJ (defined in JJ2PlusDefinitions.cpp)
		class ScriptLegacyObject;
		// Drives an object of the game by a behavior of the script (see JJ2PlusObjects.h)
		class ScriptObjectHost;

		using jjVOIDFUNCOBJ = void(*)(jjOBJ* obj);

		/**
		 * @brief A level object (actor) controllable from scripts
		 *
		 * Reference-counted script view of an in-level object (enemy, pickup, projectile, etc.), mirroring the original
		 * JJ2+ `jjOBJ`. Exposes position, speed, acceleration, animation, energy, lighting and the handling flags that
		 * govern collisions with players, bullets and blasts, plus its @ref jjBEHAVIOR. Static helpers spawn, delete
		 * and kill objects; live ones are reached through `jjObjects[id]`.
		 */
		class jjOBJ
		{
			friend class ScriptLegacyObject;
			friend class ScriptObjectHost;
			friend class jjPLAYER;
			friend class Scripting::LevelScriptLoader;

		public:
			/** @brief Creates a new instance */
			jjOBJ();
			~jjOBJ();

			/** @brief Increments the reference count */
			void AddRef();

			/** @brief Decrements the reference count */
			void Release();

			/** @brief Returns whether the object is currently active */
			bool get_isActive() const;
			/** @brief Returns the light type emitted by the object */
			std::uint32_t get_lightType() const;
			/** @brief Sets the light type emitted by the object */
			std::uint32_t set_lightType(std::uint32_t value) const;

			/** @brief Handles a collision against the target object and returns the object that was hit */
			jjOBJ* objectHit(jjOBJ* target, std::uint32_t playerHandling);
			/** @brief Destroys nearby objects (and optionally tiles) within the given distance */
			void blast(std::int32_t maxDistance, bool blastObjects);

			/** @brief The behavior driving this object each frame */
			jjBEHAVIOR behavior;

			/** @brief Runs the given built-in behavior id once, optionally drawing the object */
			void behave1(std::uint32_t behavior, bool draw);
			/** @brief Runs the given behavior once, optionally drawing the object */
			void behave2(jjBEHAVIOR behavior, bool draw);
			/** @brief Runs the given behavior function once, optionally drawing the object */
			void behave3(jjVOIDFUNCOBJ behavior, bool draw);

			/** @brief Spawns a new object with a built-in behavior and returns its object id */
			static std::int32_t jjAddObject(std::uint8_t eventID, float xPixel, float yPixel, std::uint16_t creatorID, std::uint32_t creatorType, std::uint32_t behavior);
			/** @brief Spawns a new object with a custom behavior function and returns its object id */
			static std::int32_t jjAddObjectEx(std::uint8_t eventID, float xPixel, float yPixel, std::uint16_t creatorID, std::uint32_t creatorType, jjVOIDFUNCOBJ behavior);

			/** @brief Deletes the object with the given id */
			static void jjDeleteObject(std::int32_t objectID);
			/** @brief Kills the object with the given id */
			static void jjKillObject(std::int32_t objectID);

			/** @brief Original (spawn) horizontal position in pixels */
			float xOrg = 0;
			/** @brief Original (spawn) vertical position in pixels */
			float yOrg = 0;
			/** @brief Current horizontal position in pixels */
			float xPos = 0;
			/** @brief Current vertical position in pixels */
			float yPos = 0;
			/** @brief Horizontal speed in pixels per frame */
			float xSpeed = 0;
			/** @brief Vertical speed in pixels per frame */
			float ySpeed = 0;
			/** @brief Horizontal acceleration in pixels per frame */
			float xAcc = 0;
			/** @brief Vertical acceleration in pixels per frame */
			float yAcc = 0;
			/** @brief General-purpose counter used by behaviors */
			std::int32_t counter = 0;
			/** @brief Current sprite frame index */
			std::uint32_t curFrame = 0;

			/** @brief Advances and returns the current animation frame, optionally changing it */
			std::uint32_t determineCurFrame(bool change);

			/** @brief Number of frames the object has existed */
			std::int32_t age = 0;
			/** @brief Object id of the creator */
			std::int32_t creator = 0;

			/** @brief Returns the id of the object that created this one */
			std::uint16_t get_creatorID() const;
			/** @brief Sets the id of the object that created this one */
			std::uint16_t set_creatorID(std::uint16_t value) const;
			/** @brief Returns the type of the creator */
			std::uint32_t get_creatorType() const;
			/** @brief Sets the type of the creator */
			std::uint32_t set_creatorType(std::uint32_t value) const;

			/** @brief Current animation index */
			int16_t curAnim = 0;

			/** @brief Selects and returns the current animation, optionally changing it */
			int16_t determineCurAnim(std::uint8_t setID, std::uint8_t animation, bool change);
			/** @brief The same for a set given by `ANIM::Set`, which can be a custom one */
			int16_t determineCurAnimOfSet(std::int32_t setID, std::uint8_t animation, bool change);

			/** @brief Animation played when the object is killed */
			std::uint16_t killAnim = 0;
			/** @brief Number of frames the object remains frozen */
			std::uint8_t freeze = 0;
			/** @brief Light type emitted by the object */
			std::uint8_t lightType = 0;
			/** @brief Current frame id within the animation */
			std::int8_t frameID = 0;
			/** @brief Number of frames during which the object cannot be hit */
			std::int8_t noHit = 0;

			/** @brief Returns how the object responds to bullets */
			std::uint32_t get_bulletHandling();
			/** @brief Sets how the object responds to bullets */
			std::uint32_t set_bulletHandling(std::uint32_t value);
			/** @brief Returns whether bullets ricochet off the object */
			bool get_ricochet();
			/** @brief Sets whether bullets ricochet off the object */
			bool set_ricochet(bool value);
			/** @brief Returns whether the object can be frozen */
			bool get_freezable();
			/** @brief Sets whether the object can be frozen */
			bool set_freezable(bool value);
			/** @brief Returns whether the object can be destroyed by a blast */
			bool get_blastable();
			/** @brief Sets whether the object can be destroyed by a blast */
			bool set_blastable(bool value);

			/** @brief Remaining health of the object */
			std::int8_t energy = 0;
			/** @brief Brightness of the light emitted by the object */
			std::int8_t light = 0;
			/** @brief Object category/type */
			std::uint8_t objType = 0;

			/** @brief Returns how the object responds to player contact */
			std::uint32_t get_playerHandling();
			/** @brief Sets how the object responds to player contact */
			std::uint32_t set_playerHandling(std::uint32_t value);
			/** @brief Returns whether the object can be targeted */
			bool get_isTarget();
			/** @brief Sets whether the object can be targeted */
			bool set_isTarget(bool value);
			/** @brief Returns whether the object triggers nearby TNT */
			bool get_triggersTNT();
			/** @brief Sets whether the object triggers nearby TNT */
			bool set_triggersTNT(bool value);
			/** @brief Returns whether the object deactivates when off screen */
			bool get_deactivates();
			/** @brief Sets whether the object deactivates when off screen */
			bool set_deactivates(bool value);
			/** @brief Returns whether collisions are handled by the script */
			bool get_scriptedCollisions();
			/** @brief Sets whether collisions are handled by the script */
			bool set_scriptedCollisions(bool value);

			/** @brief Current behavior state */
			std::int8_t state = 0;
			/** @brief Score points awarded when the object is destroyed */
			std::uint16_t points = 0;
			/** @brief Event id the object was spawned from */
			std::uint8_t eventID = 0;
			/** @brief Facing direction of the object */
			std::int8_t direction = 0;
			/** @brief Number of frames since the object was last hit */
			std::uint8_t justHit = 0;
			/** @brief Previous behavior state */
			std::int8_t oldState = 0;
			/** @brief Animation playback speed */
			std::int32_t animSpeed = 0;
			/** @brief General-purpose value used by behaviors */
			std::int32_t special = 0;

			/** @brief Returns the value of a custom per-object variable */
			std::int32_t get_var(std::uint8_t x);
			/** @brief Sets the value of a custom per-object variable */
			std::int32_t set_var(std::uint8_t x, std::int32_t value);

			/** @brief Amount of damage the object deals on contact */
			std::uint8_t doesHurt = 0;
			/** @brief Counter value at which the object's counter is considered complete */
			std::uint8_t counterEnd = 0;
			/** @brief Unique id of the object */
			std::int16_t objectID = 0;

			/** @brief Draws the object this frame */
			std::int32_t draw();
			/** @brief Makes the object behave as a solid obstacle */
			std::int32_t beSolid(bool shouldCheckForStompingLocalPlayers);
			/** @brief Makes the object behave as a moving platform */
			void bePlatform(float xOld, float yOld, std::int32_t width, std::int32_t height);
			/** @brief Stops the object from acting as a platform */
			void clearPlatform();
			/** @brief Snaps the object onto the ground below it */
			void putOnGround(bool precise);
			/** @brief Bounces the object as a ricocheting bullet */
			bool ricochet();
			/** @brief Unfreezes the object with the given shatter style */
			std::int32_t unfreeze(std::int32_t style);
			/** @brief Removes the object from the level */
			void deleteObject();
			/** @brief Deactivates the object */
			void deactivate();
			/** @brief Advances the object along its predefined path */
			void pathMovement();
			/** @brief Fires a bullet from the object and returns the new bullet's id */
			std::int32_t fireBullet(std::uint8_t eventID);
			/** @brief Spawns a pixel-explosion particle effect for the object */
			void particlePixelExplosion(std::int32_t style);
			/** @brief Grants the object as a pickup to the given player */
			void grantPickup(jjPLAYER* player, std::int32_t frequency);

			/** @brief Returns the id of the nearest player within the given distance */
			std::int32_t findNearestPlayer(std::int32_t maxDistance) const;
			/** @brief Returns the id of the nearest player within the given distance and outputs the distance */
			std::int32_t findNearestPlayerEx(std::int32_t maxDistance, std::int32_t& foundDistance) const;

			/** @brief Returns whether this object collides with another object */
			bool doesCollide(const jjOBJ* object, bool always) const;
			/** @brief Returns whether this object collides with the given player */
			bool doesCollidePlayer(const jjPLAYER* object, bool always) const;

		private:
			std::int32_t _refCount;
			// The hosting actor while this is a live script-controlled object (null once despawned); set by the wrapper
			ScriptLegacyObject* _wrapper = nullptr;

			// Script-facing per-object state. The native engine doesn't act on most of these for script-spawned objects,
			// but scripts read/write them constantly (state machines lean on var[]), so they're stored for round-trip use.
			static constexpr std::int32_t MaxVars = 16;
			std::int32_t _vars[MaxVars] = {};
			// Mutable: set_creatorID/set_creatorType are declared const (to match the JJ2+ registration) but still store
			mutable std::uint16_t _creatorID = 0;
			mutable std::uint32_t _creatorType = 0;
			std::uint32_t _bulletHandling = 0;
			std::uint32_t _playerHandling = 0;
			bool _ricochetProp = false;
			bool _freezable = true;
			bool _blastable = true;
			bool _isTarget = false;
			bool _triggersTNT = false;
			bool _deactivates = true;
			bool _scriptedCollisions = false;

			// Whether a script set how the object is handled, the game handles it itself otherwise (see ScriptObjectHost::OnCollision())
			bool _bulletHandlingSet = false;
			bool _playerHandlingSet = false;

			// The object of the game this is the script view of (see LevelScriptLoader::GetObjectById()), none for a template
			// or an object a script controls entirely (see _wrapper)
			std::weak_ptr<Actors::ActorBase> _actor;
			// While a behavior of the script drives the object of the game
			ScriptObjectHost* _host = nullptr;
			// The animation of its own a script gave the object of the game, which shows its frames in place of the ones of
			// its own animation, 0 for none (see ScriptObjectHost::PullFromActor())
			std::int32_t _skinAnim = 0;
			// The properties as the object of the game had them last, the ones a script writes are told apart by them
			struct SyncedState {
				float XPos, YPos, XSpeed, YSpeed;
				std::int8_t Direction, Energy, State;
				// The animation of the animation table the object shows by itself, 0 for none of the original
				std::int32_t NativeAnim;
				std::int16_t CurAnim;
				std::uint32_t CurFrame;
			};
			SyncedState _synced = {};

			// Takes over what an object gets of its template (jjObjectPresets), all but what makes it this object
			void CopyTemplate(const jjOBJ& preset);
		};

		/** @brief Per-particle data for a pixel particle */
		struct jjPARTICLEPIXEL
		{
			/** @brief Size of the pixel cluster */
			std::uint8_t size;

			/** @brief Returns the color of the pixel at the given index */
			std::uint8_t get_color(std::int32_t i) const;
			/** @brief Sets the color of the pixel at the given index */
			std::uint8_t set_color(std::int32_t i, std::uint8_t value);
		};

		/** @brief Per-particle data for a fire particle */
		struct jjPARTICLEFIRE
		{
			/** @brief Size of the particle */
			std::uint8_t size;
			/** @brief Current color index */
			std::uint8_t color;
			/** @brief Final color index the particle fades toward */
			std::uint8_t colorStop;
			/** @brief Per-frame change applied to the color */
			std::int8_t colorDelta;
		};

		/** @brief Per-particle data for a smoke particle */
		struct jjPARTICLESMOKE
		{
			/** @brief Frames remaining before the particle expires */
			std::uint8_t countdown;
		};

		/** @brief Per-particle data for an ice trail particle */
		struct jjPARTICLEICETRAIL
		{
			/** @brief Current color index */
			std::uint8_t color;
			/** @brief Final color index the particle fades toward */
			std::uint8_t colorStop;
			/** @brief Per-frame change applied to the color */
			std::int8_t colorDelta;
		};

		/** @brief Per-particle data for a spark particle */
		struct jjPARTICLESPARK
		{
			/** @brief Current color index */
			std::uint8_t color;
			/** @brief Final color index the particle fades toward */
			std::uint8_t colorStop;
			/** @brief Per-frame change applied to the color */
			std::int8_t colorDelta;
		};

		/** @brief Per-particle data for a text (score) particle */
		struct jjPARTICLESTRING
		{
			/** @brief Returns the displayed text */
			String get_text() const;
			/** @brief Sets the displayed text */
			void set_text(String text);
		};

		/** @brief Per-particle data for a snow particle */
		struct jjPARTICLESNOW
		{
			/** @brief Current animation frame */
			std::uint8_t frame;
			/** @brief Counter advancing the animation */
			std::uint8_t countup;
			/** @brief Frames remaining before the particle expires */
			std::uint8_t countdown;
			/** @brief Base animation frame index */
			std::uint16_t frameBase;
		};

		/** @brief Per-particle data for a rain particle */
		struct jjPARTICLERAIN
		{
			/** @brief Current animation frame */
			std::uint8_t frame;
			/** @brief Base animation frame index */
			std::uint16_t frameBase;
		};

		/** @brief Per-particle data for a flower particle */
		struct jjPARTICLEFLOWER
		{
			/** @brief Size of the particle */
			std::uint8_t size;
			/** @brief Color index */
			std::uint8_t color;
			/** @brief Current rotation angle */
			std::uint8_t angle;
			/** @brief Rotation speed */
			std::int8_t angularSpeed;
			/** @brief Number of petals */
			std::uint8_t petals;
		};

		/** @brief Per-particle data for a star particle */
		struct jjPARTICLESTAR
		{
			/** @brief Size of the particle */
			std::uint8_t size;
			/** @brief Color index */
			std::uint8_t color;
			/** @brief Current rotation angle */
			std::uint8_t angle;
			/** @brief Rotation speed */
			std::int8_t angularSpeed;
			/** @brief Current animation frame */
			std::uint8_t frame;
			/** @brief Counter toward the next color change */
			std::uint8_t colorChangeCounter;
			/** @brief Number of frames between color changes */
			std::uint8_t colorChangeInterval;
		};

		/** @brief Per-particle data for a leaf particle */
		struct jjPARTICLELEAF
		{
			/** @brief Current animation frame */
			std::uint8_t frame;
			/** @brief Counter advancing the animation */
			std::uint8_t countup;
			/** @brief Whether the particle ignores collision */
			bool noclip;
			/** @brief Height at which the particle settles */
			std::uint8_t height;
			/** @brief Base animation frame index */
			std::uint16_t frameBase;
		};

		/** @brief A visual particle in the level */
		class jjPARTICLE
		{
		public:
			/** @brief Creates a new instance */
			jjPARTICLE();
			~jjPARTICLE();

			/** @brief Increments the reference count */
			void AddRef();

			/** @brief Decrements the reference count */
			void Release();

			/** @brief Horizontal position in pixels */
			float xPos;
			/** @brief Vertical position in pixels */
			float yPos;
			/** @brief Horizontal speed in pixels per frame */
			float xSpeed;
			/** @brief Vertical speed in pixels per frame */
			float ySpeed;

			/** @brief Type of the particle */
			std::uint8_t particleType;
			/** @brief Whether the particle is currently active */
			bool active;

			/** @brief Type-specific particle data, interpreted according to particleType */
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

		/**
		 * @brief A player in the level
		 *
		 * Primary script interface to a player, wrapping an engine @ref Actors::Player and mirroring the original JJ2+
		 * `jjPLAYER`. Exposes position, speed, health, lives, score, weapons/ammo, the active character and a large set
		 * of state queries and actions (freeze, morph, sugar rush, warps, etc.). The level loader owns one backing
		 * instance per player (see @ref LevelScriptLoader::GetPlayerBackingStore), reached from script via `jjPlayers`.
		 */
		class jjPLAYER
		{
			friend class LevelScriptLoader;
			friend class jjLAYER;	// jjLAYER::getXPosition/getYPosition read the player's camera

		public:
			/** @brief Creates a new instance wrapping the given engine player */
			jjPLAYER(LevelScriptLoader* levelScripts, Actors::Player* player);
			~jjPLAYER();

			jjPLAYER& operator=(const jjPLAYER& o);

			/** @brief Returns the player's score */
			std::int32_t get_score() const;
			/** @brief Sets the player's score */
			std::int32_t set_score(std::int32_t value);
			/** @brief Returns the score currently shown on the HUD */
			std::int32_t get_scoreDisplayed() const;
			/** @brief Sets the score currently shown on the HUD */
			std::int32_t set_scoreDisplayed(std::int32_t value);

			/** @brief Sets the player's score */
			std::int32_t setScore(std::int32_t value);

			/** @brief Returns the horizontal position in pixels */
			float get_xPos() const;
			/** @brief Sets the horizontal position in pixels */
			float set_xPos(float value);
			/** @brief Returns the vertical position in pixels */
			float get_yPos() const;
			/** @brief Sets the vertical position in pixels */
			float set_yPos(float value);
			/** @brief Returns the horizontal acceleration */
			float get_xAcc() const;
			/** @brief Sets the horizontal acceleration */
			float set_xAcc(float value);
			/** @brief Returns the vertical acceleration */
			float get_yAcc() const;
			/** @brief Sets the vertical acceleration */
			float set_yAcc(float value);
			/** @brief Returns the original (spawn) horizontal position */
			float get_xOrg() const;
			/** @brief Sets the original (spawn) horizontal position */
			float set_xOrg(float value);
			/** @brief Returns the original (spawn) vertical position */
			float get_yOrg() const;
			/** @brief Sets the original (spawn) vertical position */
			float set_yOrg(float value);
			/** @brief Returns the horizontal speed */
			float get_xSpeed();
			/** @brief Sets the horizontal speed */
			float set_xSpeed(float value);
			/** @brief Returns the vertical speed */
			float get_ySpeed();
			/** @brief Sets the vertical speed */
			float set_ySpeed(float value);

			/** @brief Strength of the player's jump */
			float jumpStrength = 0.0f;
			/** @brief Number of frames the player remains frozen */
			std::int8_t frozen = 0;

			/** @brief Freezes or unfreezes the player */
			void freeze(bool frozen);
			/** @brief Returns the tile the player currently occupies */
			std::int32_t get_currTile();
			/** @brief Starts a sugar rush lasting the given number of ticks */
			bool startSugarRush(std::int32_t time);
			/** @brief Returns the player's health */
			std::int8_t get_health() const;
			/** @brief Sets the player's health */
			std::int8_t set_health(std::int8_t value);

			/** @brief Id of the warp the player is using */
			std::int32_t warpID = 0;

			/** @brief Returns the player's fast-fire level */
			std::int32_t get_fastfire() const;
			/** @brief Sets the player's fast-fire level */
			std::int32_t set_fastfire(std::int32_t value);
			/** @brief Returns the currently selected weapon */
			std::int8_t get_currWeapon() const;
			/** @brief Sets the currently selected weapon */
			std::int8_t set_currWeapon(std::int8_t value);

			//std::int32_t get_lives() const;
			//std::int32_t set_lives(std::int32_t value);
			/** @brief Number of remaining lives */
			std::int32_t lives = 0;

			/** @brief Returns the remaining invincibility duration */
			std::int32_t get_invincibility() const;
			/** @brief Sets the remaining invincibility duration */
			std::int32_t set_invincibility(std::int32_t value);
			/** @brief Returns the remaining blink (post-hit flicker) duration */
			std::int32_t get_blink() const;
			/** @brief Sets the remaining blink (post-hit flicker) duration */
			std::int32_t set_blink(std::int32_t value);
			/** @brief Extends the player's invincibility by the given duration */
			std::int32_t extendInvincibility(std::int32_t duration);
			/** @brief Returns the player's food count */
			std::int32_t get_food() const;
			/** @brief Sets the player's food count */
			std::int32_t set_food(std::int32_t value);
			/** @brief Returns the player's coin count */
			std::int32_t get_coins() const;
			/** @brief Sets the player's coin count */
			std::int32_t set_coins(std::int32_t value);
			/** @brief Spends the given number of coins if the player has enough */
			bool testForCoins(std::int32_t numberOfCoins);
			/** @brief Returns the player's gem count of the given type */
			std::int32_t get_gems(std::uint32_t type) const;
			/** @brief Sets the player's gem count of the given type */
			std::int32_t set_gems(std::uint32_t type, std::int32_t value);
			/** @brief Spends the given number of gems of a type if the player has enough */
			bool testForGems(std::int32_t numberOfGems, std::uint32_t type);

			/** @brief Returns the active shield type */
			std::int32_t get_shieldType() const;
			/** @brief Sets the active shield type */
			std::int32_t set_shieldType(std::int32_t value);
			/** @brief Returns the remaining shield duration */
			std::int32_t get_shieldTime() const;
			/** @brief Sets the remaining shield duration */
			std::int32_t set_shieldTime(std::int32_t value);
			/** @brief Returns the remaining roll duration */
			std::int32_t get_rolling() const;
			/** @brief Sets the remaining roll duration */
			std::int32_t set_rolling(std::int32_t value);

			/** @brief Index of the boss assigned to the player */
			std::int32_t bossNumber = 0;
			/** @brief Object id of the player's boss */
			std::int32_t boss = 0;
			/** @brief Whether the player's boss is active */
			bool bossActive = false;
			/** @brief Facing direction of the player */
			std::int8_t direction = 0;
			/** @brief Object id of the platform the player is standing on */
			std::int32_t platform = 0;
			/** @brief Capture-the-flag flag state */
			std::int32_t flag = 0;
			/** @brief Network client id of the player */
			std::int32_t clientID = 0;

			/** @brief Returns the player's id */
			std::int8_t get_playerID() const;
			/** @brief Returns the player's local id */
			std::int32_t get_localPlayerID() const;

			/** @brief Team the player belongs to */
			bool team = false;

			/** @brief Returns whether the player is running */
			bool get_running() const;
			/** @brief Sets whether the player is running */
			bool set_running(bool value);

			/** @brief Remaining special-jump duration */
			std::int32_t specialJump = 0;

			/** @brief Returns the remaining stoned (dizzy) duration */
			std::int32_t get_stoned();
			/** @brief Sets the remaining stoned (dizzy) duration */
			std::int32_t set_stoned(std::int32_t value);

			/** @brief Remaining buttstomp duration */
			std::int32_t buttstomp = 0;
			/** @brief Remaining helicopter (copter) duration */
			std::int32_t helicopter = 0;
			/** @brief Elapsed helicopter (copter) time */
			std::int32_t helicopterElapsed = 0;
			/** @brief Active special move */
			std::int32_t specialMove = 0;
			/** @brief Number of frames the player has been idle */
			std::int32_t idle = 0;

			/** @brief Moves the player through a sucker tube at the given speed */
			void suckerTube(std::int32_t xSpeed, std::int32_t ySpeed, bool center, bool noclip, bool trigSample);
			/** @brief Spins the player off a pole at the given speed */
			void poleSpin(float xSpeed, float ySpeed, std::uint32_t delay);
			/** @brief Bounces the player off a spring at the given speed */
			void spring(float xSpeed, float ySpeed, bool keepZeroSpeeds, bool sample);

			/** @brief Whether the player is local to this client */
			bool isLocal = true;
			/** @brief Whether the player is currently active */
			bool isActive = true;

			/** @brief Returns whether the player is still connecting */
			bool get_isConnecting() const;
			/** @brief Returns whether the player is idle */
			bool get_isIdle() const;
			/** @brief Returns whether the player is out of the game */
			bool get_isOut() const;
			/** @brief Returns whether the player is spectating */
			bool get_isSpectating() const;
			/** @brief Returns whether the player is currently in the game */
			bool get_isInGame() const;

			/** @brief Returns the player's display name */
			String get_name() const;
			/** @brief Returns the player's name without formatting codes */
			String get_nameUnformatted() const;
			/** @brief Sets the player's display name */
			bool setName(const String& name);
			/** @brief Returns the brightness of the light around the player */
			std::int8_t get_light() const;
			/** @brief Sets the brightness of the light around the player */
			std::int8_t set_light(std::int8_t value);
			/** @brief Returns the packed fur (recolor) value */
			std::uint32_t get_fur() const;
			/** @brief Sets the packed fur (recolor) value */
			std::uint32_t set_fur(std::uint32_t value);
			/** @brief Outputs the player's four fur (recolor) palette indices */
			void getFur(std::uint8_t& a, std::uint8_t& b, std::uint8_t& c, std::uint8_t& d) const;
			/** @brief Sets the player's four fur (recolor) palette indices */
			void setFur(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d);

			/** @brief Returns whether the player is prevented from firing */
			bool get_noFire() const;
			/** @brief Sets whether the player is prevented from firing */
			bool set_noFire(bool value);
			/** @brief Returns whether anti-gravity is active */
			bool get_antiGrav() const;
			/** @brief Sets whether anti-gravity is active */
			bool set_antiGrav(bool value);
			/** @brief Returns whether the player is invisible */
			bool get_invisibility() const;
			/** @brief Sets whether the player is invisible */
			bool set_invisibility(bool value);
			/** @brief Returns whether noclip mode is active */
			bool get_noclipMode() const;
			/** @brief Sets whether noclip mode is active */
			bool set_noclipMode(bool value);
			/** @brief Returns the player's lighting level */
			std::uint8_t get_lighting() const;
			/** @brief Sets the player's lighting level */
			std::uint8_t set_lighting(std::uint8_t value);
			/** @brief Resets the player's lighting to the default level */
			std::uint8_t resetLight();

			/** @brief Returns whether the left key is pressed */
			bool get_playerKeyLeftPressed();
			/** @brief Returns whether the right key is pressed */
			bool get_playerKeyRightPressed();
			/** @brief Returns whether the up key is pressed */
			bool get_playerKeyUpPressed();
			/** @brief Returns whether the down key is pressed */
			bool get_playerKeyDownPressed();
			/** @brief Returns whether the fire key is pressed */
			bool get_playerKeyFirePressed();
			/** @brief Returns whether the weapon-select key is pressed */
			bool get_playerKeySelectPressed();
			/** @brief Returns whether the jump key is pressed */
			bool get_playerKeyJumpPressed();
			/** @brief Returns whether the run key is pressed */
			bool get_playerKeyRunPressed();
			/** @brief Sets whether the left key is pressed */
			bool set_playerKeyLeftPressed(bool value);
			/** @brief Sets whether the right key is pressed */
			bool set_playerKeyRightPressed(bool value);
			/** @brief Sets whether the up key is pressed */
			bool set_playerKeyUpPressed(bool value);
			/** @brief Sets whether the down key is pressed */
			bool set_playerKeyDownPressed(bool value);
			/** @brief Sets whether the fire key is pressed */
			bool set_playerKeyFirePressed(bool value);
			/** @brief Sets whether the weapon-select key is pressed */
			bool set_playerKeySelectPressed(bool value);
			/** @brief Sets whether the jump key is pressed */
			bool set_playerKeyJumpPressed(bool value);
			/** @brief Sets whether the run key is pressed */
			bool set_playerKeyRunPressed(bool value);

			/** @brief Returns whether the player has the powerup for the given weapon */
			bool get_powerup(std::uint8_t index);
			/** @brief Sets whether the player has the powerup for the given weapon */
			bool set_powerup(std::uint8_t index, bool value);
			/** @brief Returns the ammo count for the given weapon */
			std::int32_t get_ammo(std::uint8_t index) const;
			/** @brief Sets the ammo count for the given weapon */
			std::int32_t set_ammo(std::uint8_t index, std::int32_t value);

			/** @brief Moves the player by the given pixel offset */
			bool offsetPosition(std::int32_t xPixels, std::int32_t yPixels);
			/** @brief Warps the player to the given tile coordinates */
			bool warpToTile(std::int32_t xTile, std::int32_t yTile, bool fast);
			/** @brief Warps the player to the warp target with the given id */
			bool warpToID(std::uint8_t warpID, bool fast);

			/** @brief Morphs the player into a random character */
			std::uint32_t morph(bool rabbitsOnly, bool morphEffect);
			/** @brief Morphs the player into the given character */
			std::uint32_t morphTo(std::uint32_t charNew, bool morphEffect);
			/** @brief Reverts the player to the original character */
			std::uint32_t revertMorph(bool morphEffect);
			/** @brief Returns the player's current character */
			std::uint32_t get_charCurr() const;

			/** @brief Returns the player's original character, the one @ref revertMorph() turns it back into */
			std::uint32_t get_charOrig() const;
			/** @brief Sets the player's original character */
			std::uint32_t set_charOrig(std::uint32_t value);

			/** @brief Kills the player */
			void kill();
			/** @brief Damages the player and returns whether the hit was applied */
			bool hurt(std::int8_t damage, bool forceHurt, jjPLAYER* attacker);

			/** @brief Returns the state of the player's timer */
			std::uint32_t get_timerState() const;
			/** @brief Returns whether the timer persists across levels */
			bool get_timerPersists() const;
			/** @brief Sets whether the timer persists across levels */
			bool set_timerPersists(bool value);
			/** @brief Starts the player's timer for the given number of ticks */
			std::uint32_t timerStart(std::int32_t ticks, bool startPaused);
			/** @brief Pauses the player's timer */
			std::uint32_t timerPause();
			/** @brief Resumes the player's timer */
			std::uint32_t timerResume();
			/** @brief Stops the player's timer */
			std::uint32_t timerStop();
			/** @brief Returns the time remaining on the player's timer */
			std::int32_t get_timerTime() const;
			/** @brief Sets the time remaining on the player's timer */
			std::int32_t set_timerTime(std::int32_t value);
			/** @brief Sets the script function called when the timer ends, by name */
			void timerFunction(const String& functionName);
			/** @brief Sets the script function called when the timer ends, by pointer */
			void timerFunctionPtr(void* function);
			/** @brief Sets the script function called when the timer ends, by function pointer */
			void timerFunctionFuncPtr(void* function);

			/** @brief Activates or deactivates the player's boss */
			bool activateBoss(bool activate);
			/** @brief Limits horizontal camera scrolling to the given region */
			bool limitXScroll(std::uint16_t left, std::uint16_t width);
			/** @brief Freezes the camera at the given fixed position */
			void cameraFreezeFF(float xPixel, float yPixel, bool centered, bool instant);
			/** @brief Freezes the camera, leaving the horizontal axis tracking the player */
			void cameraFreezeBF(bool xUnfreeze, float yPixel, bool centered, bool instant);
			/** @brief Freezes the camera, leaving the vertical axis tracking the player */
			void cameraFreezeFB(float xPixel, bool yUnfreeze, bool centered, bool instant);
			/** @brief Freezes the camera, leaving both axes optionally tracking the player */
			void cameraFreezeBB(bool xUnfreeze, bool yUnfreeze, bool centered, bool instant);
			/** @brief Unfreezes the camera */
			void cameraUnfreeze(bool instant);
			/** @brief Shows the given text to the player */
			void showText(const String& text, std::uint32_t size);
			/** @brief Shows a string from the level text bank to the player */
			void showTextByID(std::uint32_t textID, std::uint32_t offset, std::uint32_t size);

			/** @brief Returns the remaining fly (airboard) duration */
			std::uint32_t get_fly() const;
			/** @brief Sets the remaining fly (airboard) duration */
			std::uint32_t set_fly(std::uint32_t value);

			/** @brief Fires a bullet in the given direction and returns the new bullet's id */
			std::int32_t fireBulletDirection(std::uint8_t gun, bool depleteAmmo, bool requireAmmo, std::uint32_t direction);
			/** @brief Fires a bullet at the given angle and returns the new bullet's id */
			std::int32_t fireBulletAngle(std::uint8_t gun, bool depleteAmmo, bool requireAmmo, float angle);

			/** @brief Horizontal position of the player's split-screen subscreen */
			std::int32_t subscreenX = 0;
			/** @brief Vertical position of the player's split-screen subscreen */
			std::int32_t subscreenY = 0;

			/** @brief Returns the camera's horizontal position */
			float get_cameraX() const;
			/** @brief Returns the camera's vertical position */
			float get_cameraY() const;
			/** @brief Returns the number of times the player has died */
			std::int32_t get_deaths() const;

			/** @brief Returns whether the player is jailed */
			bool get_isJailed() const;
			/** @brief Returns whether the player is a zombie */
			bool get_isZombie() const;
			/** @brief Returns the player's remaining lives in last-rabbit-standing mode */
			std::int32_t get_lrsLives() const;
			/** @brief Returns the number of players this player has roasted */
			std::int32_t get_roasts() const;
			/** @brief Returns the number of laps the player has completed */
			std::int32_t get_laps() const;
			/** @brief Returns the elapsed time of the current lap */
			std::int32_t get_lapTimeCurrent() const;
			/** @brief Returns the recorded time of the lap at the given index */
			std::int32_t get_lapTimes(std::uint32_t index) const;
			/** @brief Returns the player's best lap time */
			std::int32_t get_lapTimeBest() const;
			/** @brief Returns whether the player is a server administrator */
			bool get_isAdmin() const;
			/** @brief Returns whether the player holds the given privilege */
			bool hasPrivilege(const String& privilege, std::uint32_t moduleID) const;

			/** @brief Returns whether the player collides with the given object */
			bool doesCollide(const jjOBJ* object, bool always) const;
			/** @brief Returns the force with which the player would hit the given object */
			std::int32_t getObjectHitForce(const jjOBJ& target) const;
			/** @brief Handles the player hitting the target object */
			bool objectHit(jjOBJ* target, std::int32_t force, std::uint32_t playerHandling);
			/** @brief Returns whether the given player is an enemy of this player */
			bool isEnemy(const jjPLAYER* victim) const;

			/** @brief Returns how the player is drawn (`SPRITE::Mode`) */
			std::uint32_t get_spriteMode() const;
			/** @brief Sets how the player is drawn (`SPRITE::Mode`) */
			std::uint32_t set_spriteMode(std::uint32_t value);
			/** @brief Returns the parameter of the sprite mode */
			std::uint8_t get_spriteParam() const;
			/** @brief Sets the parameter of the sprite mode */
			std::uint8_t set_spriteParam(std::uint8_t value);

			/** @brief The player's current character */
			std::uint32_t charCurr = 0;
			/** @brief Current animation index */
			std::uint16_t curAnim = 0;
			/** @brief Current sprite frame index */
			std::uint32_t curFrame = 0;
			/** @brief Current frame id within the animation */
			std::uint8_t frameID = 0;

		private:
			LevelScriptLoader* _levelScriptLoader;
			Actors::Player* _player;

			void* _timerCallback;
			std::uint32_t _timerState;
			float _timerLeft;
			bool _timerPersists;

			bool _backingStoreDirty = false;

			// SPRITE::NORMAL and its parameter
			std::uint32_t _spriteMode = 0;
			std::uint8_t _spriteParam = 0;

			void SyncPropertiesToBackingStore();
			void SyncPropertiesFromBackingStore();
		};

		/**
		 * @brief Settings for a weapon type
		 *
		 * Holds the tunable parameters of one of the game's weapon slots: ammo limits and multipliers, firing style and
		 * spread, what the weapon is replaced by (shield, bubbles) and where its ammo comes from (crates, birds), and
		 * whether it and its powered-up form are allowed. Indexed by weapon type and edited from script.
		 */
		class jjWEAPON
		{
		public:
			/** @brief Whether the weapon has infinite ammo */
			bool infinite;
			/** @brief Whether the weapon's ammo replenishes over time */
			bool replenishes;
			/** @brief Whether the weapon is replaced by a shield pickup */
			bool replacedByShield;
			/** @brief Whether the weapon is replaced by bubbles underwater */
			bool replacedByBubbles;
			/** @brief Whether ammo for the weapon comes from gun crates */
			bool comesFromGunCrates;
			/** @brief Whether the weapon aims gradually */
			bool gradualAim;
			/** @brief Ammo multiplier applied to pickups */
			int multiplier;
			/** @brief Maximum ammo the player can carry */
			int maximum;
			/** @brief Gems lost when firing the weapon */
			int gemsLost;
			/** @brief Gems lost when firing the powered-up weapon */
			int gemsLostPowerup;
			/** @brief Firing style of the weapon */
			std::int8_t style;
			/** @brief Spread pattern of the weapon's bullets */
			int spread;
			/** @brief Whether the weapon uses its default firing sound */
			bool defaultSample;
			/** @brief Whether the weapon is allowed */
			bool allowed;
			/** @brief Whether the powered-up weapon is allowed */
			bool allowedPowerup;
			/** @brief Whether ammo for the weapon comes from birds */
			bool comesFromBirds;
			/** @brief Whether ammo for the powered-up weapon comes from birds */
			bool comesFromBirdsPowerup;
		};

		/** @brief Movement and ability settings for a playable character */
		class jjCHARACTER
		{
		public:
			/** @brief Air-jump ability */
			int airJump;
			/** @brief Ground-jump ability */
			int groundJump;
			/** @brief Maximum number of double jumps */
			int doubleJumpCountMax;
			/** @brief Horizontal speed of a double jump */
			float doubleJumpXSpeed;
			/** @brief Vertical speed of a double jump */
			float doubleJumpYSpeed;
			/** @brief Maximum helicopter (copter) duration */
			int helicopterDurationMax;
			/** @brief Horizontal helicopter (copter) speed */
			float helicopterXSpeed;
			/** @brief Vertical helicopter (copter) speed */
			float helicopterYSpeed;
			/** @brief Whether the character can hurt enemies on contact */
			bool canHurt;
			/** @brief Whether the character can run */
			bool canRun;
			/** @brief Whether the character cycles through morph boxes */
			bool morphBoxCycle;
		};

		class jjLAYER;
		class jjCOLORMAP;

		/**
		 * @brief A rectangular buffer of palette-indexed pixels
		 *
		 * Reference-counted, editable image of 8-bit palette indices that scripts read and write per pixel. It can be
		 * built from a tile, an animation frame, a texture, a region of a tile layer, an image file or a blank size,
		 * and saved back into a tile, an animation frame or an image file. The bridge for procedural graphics editing.
		 */
		class jjPIXELMAP
		{
		public:
			/** @brief Creates a new instance */
			jjPIXELMAP();
			~jjPIXELMAP();

			/** @brief Returns a new instance built from a single tile */
			static jjPIXELMAP* CreateFromTile(std::uint16_t tileID, const jjPAL* palette);
			/** @brief Returns a new instance of the given size */
			static jjPIXELMAP* CreateFromSize(std::uint32_t width, std::uint32_t height);
			/** @brief Returns a new instance with the same size and image as another one */
			static jjPIXELMAP* CreateFromOther(const jjPIXELMAP& other);
			/** @brief Returns a new instance built from an animation frame */
			static jjPIXELMAP* CreateFromFrame(const jjANIMFRAME* animFrame, const jjPAL* palette);
			/** @brief Returns a new instance built from a region of a tile layer */
			static jjPIXELMAP* CreateFromLayer(std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height, std::uint32_t layer, const jjPAL* palette);
			/** @brief Returns a new instance built from a region of the given layer */
			static jjPIXELMAP* CreateFromLayerObject(std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height, const jjLAYER* layer, const jjPAL* palette);
			/** @brief Returns a new instance built from a texture */
			static jjPIXELMAP* CreateFromTexture(std::uint32_t texture, const jjPAL* palette);
			/** @brief Returns a new instance built from an image file, mapped onto the given palette */
			static jjPIXELMAP* CreateFromFilename(const String& filename, const jjPAL* palette, std::uint8_t threshold);
			/** @brief Returns a new instance of a color map in the nearest colors of a palette, a pixel of no alpha is transparent */
			static jjPIXELMAP* CreateFromColorMap(const jjCOLORMAP& other, const jjPAL* palette);

			/** @brief Increments the reference count */
			void AddRef();
			/** @brief Decrements the reference count */
			void Release();

			jjPIXELMAP& operator=(const jjPIXELMAP& o);

			/** @brief Returns the pixel at the given position, a scratch one if it's out of the map */
			std::uint8_t* GetPixel(std::uint32_t x, std::uint32_t y);

			/** @brief Width in pixels */
			std::uint32_t width = 0;
			/** @brief Height in pixels */
			std::uint32_t height = 0;

			/** @brief Saves the pixel map into a tile */
			bool saveToTile(std::uint16_t tileID, bool hFlip) const;
			/** @brief Saves the pixel map into an animation frame */
			bool saveToFrame(jjANIMFRAME* frame) const;
			/** @brief Saves the pixel map to an image file using the given palette */
			bool saveToFile(const String& filename, const jjPAL& palette) const;
			/** @brief Makes the pixel map, 256 × 256 pixels, the texture of a layer, of `jjLayers[8]` without one */
			bool makeTexture(jjLAYER* layer) const;

			/** @brief Extends (positive) or crops (negative) each edge by a number of pixels */
			jjPIXELMAP& addBorders(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom, std::uint8_t borderColor);
			/** @brief Crops the map to the given rectangle */
			jjPIXELMAP& crop(std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height);
			/** @brief Flips the image across one or both axes (`SPRITE::Direction`) */
			jjPIXELMAP& flip(std::int32_t directions);
			/** @brief Replaces every pixel by the value at its index of a mapping of at least 256 entries */
			jjPIXELMAP& recolor(const CScriptArray& colorMapping);
			/** @brief Resizes the image by nearest-neighbor sampling */
			jjPIXELMAP& resize(std::uint32_t width, std::uint32_t height);
			/** @brief Rotates the image by 90 degrees clockwise */
			jjPIXELMAP& rotate();
			/** @brief Trims empty rows and columns of the given color from all edges, returning how many of them */
			jjPIXELMAP& trim(std::uint32_t& left, std::uint32_t& top, std::uint32_t& right, std::uint32_t& bottom, std::uint8_t trimColor);
			/** @brief Trims empty rows and columns of the given color from all edges */
			jjPIXELMAP& trimBasic(std::uint8_t trimColor);

		private:
			std::int32_t _refCount;
			std::unique_ptr<std::uint8_t[]> _pixels;
			// The pixel GetPixel() gives for a position out of the map
			std::uint8_t _outOfRange = 0;

			// Returns a new transparent map, or nullptr with a script exception if it would be too large
			static jjPIXELMAP* Create(std::uint32_t width, std::uint32_t height);
			// Returns a new map of a rectangle of the tiles of a level layer, by its index in the tile map
			static jjPIXELMAP* CreateFromEngineLayer(std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height, std::int32_t layerIndex, const jjPAL* palette);
			// Replaces the image by the given rectangle of it, the part out of the image is filled with `fill`
			void Reframe(std::int64_t left, std::int64_t top, std::uint32_t newWidth, std::uint32_t newHeight, std::uint8_t fill);
		};

		/**
		 * @brief A rectangular image of colors with alpha, `jjCOLORMAP` of JJ2+
		 *
		 * The true color counterpart of @ref jjPIXELMAP. A palette index of an image of the game becomes the color the
		 * palette has for it, index 0 is transparent. Saved into a tile or a frame, its colors become the nearest colors
		 * of the palette, the game draws them indexed (see @ref saveToTile()).
		 */
		class jjCOLORMAP
		{
			friend class jjPIXELMAP;

		public:
			/** @brief Creates a new instance */
			jjCOLORMAP();
			~jjCOLORMAP();

			/** @brief Returns a new transparent instance of the given size */
			static jjCOLORMAP* CreateFromSize(std::uint32_t width, std::uint32_t height);
			/** @brief Returns a new instance of a pixel map in the colors of a palette */
			static jjCOLORMAP* CreateFromPixelMap(const jjPIXELMAP& other, const jjPAL& palette);
			/** @brief Returns a new instance with the same image as another one */
			static jjCOLORMAP* CreateFromOther(const jjCOLORMAP& other);
			/** @brief Returns a new instance of an animation frame in the colors of a palette */
			static jjCOLORMAP* CreateFromFrame(const jjANIMFRAME* animFrame, const jjPAL& palette);
			/** @brief Returns a new instance of a texture of a textured background in the colors of a palette */
			static jjCOLORMAP* CreateFromTexture(std::uint32_t texture, const jjPAL& palette);
			/** @brief Returns a new instance of a tile in the colors of a palette, a tile in true color in its own */
			static jjCOLORMAP* CreateFromTile(std::uint16_t tileID, const jjPAL& palette);
			/** @brief Returns a new instance of a region of a level layer by its number in the colors of a palette */
			static jjCOLORMAP* CreateFromLayer(std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height, std::uint32_t layer, const jjPAL& palette);
			/** @brief Returns a new instance of a region of a layer in the colors of a palette */
			static jjCOLORMAP* CreateFromLayerObject(std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height, const jjLAYER& layer, const jjPAL& palette);
			/** @brief Returns a new instance of an image file */
			static jjCOLORMAP* CreateFromFilename(const String& filename);

			/** @brief Increments the reference count */
			void AddRef();
			/** @brief Decrements the reference count */
			void Release();

			jjCOLORMAP& operator=(const jjCOLORMAP& o);

			/** @brief Returns the pixel at the given position, a scratch one if it's out of the map */
			jjPALCOLOR* GetPixel(std::uint32_t x, std::uint32_t y);

			/** @brief Width in pixels */
			std::uint32_t width = 0;
			/** @brief Height in pixels */
			std::uint32_t height = 0;

			/**
			 * @brief Saves the color map into a tile
			 *
			 * The colors become the nearest ones of `jjPalette`, a pixel of any alpha but none is opaque - the game draws the
			 * tiles of a level indexed, so they follow the palette as it changes.
			 */
			bool saveToTile(std::uint16_t tileID, bool hFlip) const;
			/** @brief Saves the color map into an animation frame, its colors become the nearest ones of `jjPalette`, as @ref saveToTile() */
			bool saveToFrame(jjANIMFRAME* frame) const;
			/** @brief Saves the color map to an image file, not supported */
			bool saveToFile(const String& filename, bool allowCompression) const;

			/** @brief Extends (positive) or crops (negative) each edge by a number of pixels */
			jjCOLORMAP& addBorders(std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom, jjPALCOLOR borderColor);
			/** @brief Crops the map to the given rectangle */
			jjCOLORMAP& crop(std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height);
			/** @brief Flips the image across one or both axes (`SPRITE::Direction`) */
			jjCOLORMAP& flip(std::int32_t directions);
			/** @brief Resizes the image by nearest-neighbor sampling */
			jjCOLORMAP& resize(std::uint32_t width, std::uint32_t height);
			/** @brief Rotates the image by 90 degrees clockwise */
			jjCOLORMAP& rotate();
			/** @brief Swaps the components of every color, see `jjPALCOLOR::swizzle()` */
			jjCOLORMAP& swizzle(std::uint32_t red, std::uint32_t green, std::uint32_t blue, std::uint32_t alpha);
			/** @brief Trims the rows and columns of no more alpha than given from all edges, returning how many of them */
			jjCOLORMAP& trim(std::uint32_t& left, std::uint32_t& top, std::uint32_t& right, std::uint32_t& bottom, std::uint8_t trimAlpha);
			/** @brief Trims the rows and columns of no more alpha than given from all edges */
			jjCOLORMAP& trimBasic(std::uint8_t trimAlpha);

			/** @brief Returns the palette indices of the image, the nearest colors of a palette, 0 where there's no alpha */
			std::unique_ptr<std::uint8_t[]> ToIndices(const jjPAL& palette) const;

		private:
			std::int32_t _refCount;
			std::unique_ptr<jjPALCOLOR[]> _pixels;
			// The pixel GetPixel() gives for a position out of the map
			jjPALCOLOR _outOfRange = {};

			// Returns a new transparent map, or nullptr with a script exception if it would be too large
			static jjCOLORMAP* Create(std::uint32_t width, std::uint32_t height);
			// Returns a new map of palette indices in the colors of a palette, index 0 transparent
			static jjCOLORMAP* CreateFromIndices(const std::uint8_t* indices, std::uint32_t width, std::uint32_t height, const jjPAL& palette);
			// Replaces the image by the given rectangle of it, the part out of the image is filled with `fill`
			void Reframe(std::int64_t left, std::int64_t top, std::uint32_t newWidth, std::uint32_t newHeight, jjPALCOLOR fill);
		};

		/** @brief A rectangular collision mask of solid/empty pixels */
		class jjMASKMAP
		{
		public:
			/** @brief Size of the mask, which is always the one of a tile */
			static constexpr std::uint32_t Size = 32;

			/** @brief Creates a new instance */
			jjMASKMAP();
			~jjMASKMAP();

			/** @brief Returns a new instance filled entirely solid or empty */
			static jjMASKMAP* CreateFromBool(bool filled);
			/** @brief Returns a new instance built from a tile's collision mask */
			static jjMASKMAP* CreateFromTile(std::uint16_t tileID);

			/** @brief Increments the reference count */
			void AddRef();
			/** @brief Decrements the reference count */
			void Release();

			jjMASKMAP& operator=(const jjMASKMAP& o);

			/** @brief Returns the pixel at the given position, a scratch one if it's out of the mask */
			bool* GetPixel(std::uint32_t x, std::uint32_t y);

			/** @brief Saves the mask into a tile's collision mask */
			bool save(std::uint16_t tileID, bool hFlip) const;

		private:
			std::int32_t _refCount;
			bool _mask[Size * Size];
			// The pixel GetPixel() gives for a position out of the map
			bool _outOfRange = false;
		};

		/**
		 * @brief A tilemap layer of the level
		 *
		 * Reference-counted proxy for one of the level's parallax tile layers, mirroring the original JJ2+ `jjLAYER`.
		 * Exposes the layer's size, scrolling speeds and offsets, parallax/auto-speed and texture-background modes, and
		 * per-tile get/set access. The level loader keeps one proxy per engine layer and pushes writable fields back
		 * each frame (see @ref LevelScriptLoader::GetLayerProxy); reached from script via `jjLayers`.
		 */
		class jjLAYER
		{
		public:
			/** @brief Creates a new instance */
			jjLAYER();
			~jjLAYER();

			/** @brief Returns a new layer of the given size */
			static jjLAYER* CreateFromSize(std::uint32_t width, std::uint32_t height);
			/** @brief Returns a new layer copied from another */
			static jjLAYER* CreateCopy(jjLAYER* other);

			/** @brief Increments the reference count */
			void AddRef();
			/** @brief Decrements the reference count */
			void Release();

			jjLAYER& operator=(const jjLAYER& o);

			/** @brief Returns the level layer at the specified index */
			static jjLAYER* get_jjLayers(int32_t index);

			/** @brief Layer width in tiles */
			std::int32_t width = 0;
			/** @brief Actual (untruncated) layer width in tiles */
			std::int32_t widthReal = 0;
			/** @brief Layer width rounded up for tiling */
			std::int32_t widthRounded = 0;
			/** @brief Layer height in tiles */
			std::int32_t height = 0;
			/** @brief Horizontal parallax scrolling speed */
			float xSpeed = 0;
			/** @brief Vertical parallax scrolling speed */
			float ySpeed = 0;
			/** @brief Horizontal automatic scrolling speed */
			float xAutoSpeed = 0;
			/** @brief Vertical automatic scrolling speed */
			float yAutoSpeed = 0;
			/** @brief Horizontal scrolling offset */
			float xOffset = 0;
			/** @brief Vertical scrolling offset */
			float yOffset = 0;
			/** @brief Horizontal inner parallax speed */
			float xInnerSpeed = 0;
			/** @brief Vertical inner parallax speed */
			float yInnerSpeed = 0;
			/** @brief Horizontal inner automatic speed */
			float xInnerAutoSpeed = 0;
			/** @brief Vertical inner automatic speed */
			float yInnerAutoSpeed = 0;

			/** @brief Returns the sprite blend mode used by the layer */
			std::uint32_t get_spriteMode() const;
			/** @brief Sets the sprite blend mode used by the layer */
			std::uint32_t set_spriteMode(std::uint32_t value) const;
			/** @brief Returns the parameter for the layer's sprite blend mode */
			std::uint8_t get_spriteParam() const;
			/** @brief Sets the parameter for the layer's sprite blend mode */
			std::uint8_t set_spriteParam(std::uint8_t value) const;

			/** @brief Sets the horizontal scrolling speed, optionally as an auto-speed */
			void setXSpeed(float newspeed, bool newSpeedIsAnAutoSpeed) const;
			/** @brief Sets the vertical scrolling speed, optionally as an auto-speed */
			void setYSpeed(float newspeed, bool newSpeedIsAnAutoSpeed) const;
			/** @brief Returns the layer's horizontal position for the given player's camera */
			float getXPosition(const jjPLAYER* play) const;
			/** @brief Returns the layer's vertical position for the given player's camera */
			float getYPosition(const jjPLAYER* play) const;

			/** @brief Where a textured layer is drawn, `SURFACE::Surface` of JJ2+ */
			enum TextureSurfaceStyles : std::int32_t {
				Untextured,		/**< Drawn as tiles */
				Legacy,			/**< Over the whole view, as layer 8 of the original game */
				FullScreen,		/**< Over the whole view */
				InnerWindow,	/**< Only where the layer is, as a window into one over the whole view */
				InnerLayer		/**< Only where the layer is, the horizon and the vanishing point a fraction of the layer */
			};

			/**
			 * @brief Parameters of one texture style of a layer, `jjLAYERWARPHORIZON` and the others of JJ2+
			 *
			 * What they mean depends on the style - the fade position of a warp horizon, a tunnel and a cylinder, the pivot of
			 * a menu, the amplitudes of a wave, the vanishing point and the top of a reflection - but as in JJ2+, they're
			 * the same values for all the styles, a value set for one is the value of the others too (see @ref SetStyleValue()),
			 * only the tint color is a reflection's own. The fade color and the stars, which each style takes in its own way
			 * too, are of the layer as well (see @ref ApplyTexture()).
			 */
			struct TextureStyleParams {
				/** @brief Layer the parameters are of */
				jjLAYER* Owner = nullptr;
				/** @brief Horizontal value, as a fraction of the view (of the layer for @ref InnerLayer), or the horizontal amplitude */
				float X = 0.5f;
				/** @brief Vertical value, as a fraction of the view (of the layer for @ref InnerLayer), or the vertical amplitude */
				float Y = 0.5f;
				/** @brief Whether it fades into the fade color */
				bool Fade = true;
				/** @brief Palette entry a reflection is tinted with */
				std::uint8_t TintColor = 192;
			};

			/** @brief Parameters of `TEXTURE::WARPHORIZON` */
			TextureStyleParams warpHorizon;
			/** @brief Parameters of `TEXTURE::TUNNEL` */
			TextureStyleParams tunnel;
			/** @brief Parameters of `TEXTURE::MENU` */
			TextureStyleParams menu;
			/** @brief Parameters of `TEXTURE::TILEMENU` */
			TextureStyleParams tileMenu;
			/** @brief Parameters of `TEXTURE::WAVE` */
			TextureStyleParams wave;
			/** @brief Parameters of `TEXTURE::CYLINDER` */
			TextureStyleParams cylinder;
			/** @brief Parameters of `TEXTURE::REFLECTION` */
			TextureStyleParams reflection;

			/** @brief Returns the textured-background mode of the layer */
			std::int32_t GetTextureMode() const;
			/** @brief Sets the textured-background mode of the layer */
			void SetTextureMode(std::int32_t value);
			/** @brief Returns the texture used by the layer */
			std::int32_t GetTexture() const;
			/** @brief Sets the texture used by the layer */
			void SetTexture(std::int32_t value);
			/** @brief Returns where the layer is drawn textured, see @ref TextureSurfaceStyles */
			std::int32_t GetTextureSurface() const;
			/** @brief Sets where the layer is drawn textured, see @ref TextureSurfaceStyles */
			void SetTextureSurface(std::int32_t value);
			/**
			 * @brief Makes the layer of the level drawn as its texture properties say
			 *
			 * A style the game can't draw (`TEXTURE::MENU`) is drawn as a warp horizon, a wave is drawn over the whole view
			 * whatever its surface.
			 */
			void ApplyTexture();
			/**
			 * @brief Sets how a textured layer is drawn over what is behind it by the sprite mode a script set
			 *
			 * Returns `false` if the mode isn't one a textured layer can be drawn with, it's drawn as it is then.
			 */
			bool SetTextureBlend(Tiles::LayerDescription& description) const;

			/** @brief Returns a value of the parameters of a texture style */
			template<float TextureStyleParams::*Member>
			static float GetStyleValue(const TextureStyleParams* params) {
				return params->*Member;
			}
			/** @brief Sets a value of the parameters of a texture style, of all of them as in JJ2+ */
			template<float TextureStyleParams::*Member>
			static float SetStyleValue(TextureStyleParams* params, float value) {
				if (params->*Member != value) {
					jjLAYER* owner = params->Owner;
					for (TextureStyleParams* style : { &owner->warpHorizon, &owner->tunnel, &owner->menu, &owner->tileMenu, &owner->wave, &owner->cylinder, &owner->reflection }) {
						style->*Member = value;
					}
					owner->ApplyTexture();
				}
				return value;
			}
			/** @brief Returns whether a texture style fades */
			static bool GetStyleFade(const TextureStyleParams* params) {
				return params->Fade;
			}
			/** @brief Sets whether a texture style fades, all of them as in JJ2+ */
			static bool SetStyleFade(TextureStyleParams* params, bool value) {
				if (params->Fade != value) {
					jjLAYER* owner = params->Owner;
					for (TextureStyleParams* style : { &owner->warpHorizon, &owner->tunnel, &owner->menu, &owner->tileMenu, &owner->wave, &owner->cylinder, &owner->reflection }) {
						style->Fade = value;
					}
					owner->ApplyTexture();
				}
				return value;
			}
			/** @brief Returns the palette entry a reflection is tinted with */
			static std::uint8_t GetStyleTintColor(const TextureStyleParams* params) {
				return params->TintColor;
			}
			/** @brief Sets the palette entry a reflection is tinted with */
			static std::uint8_t SetStyleTintColor(TextureStyleParams* params, std::uint8_t value) {
				if (params->TintColor != value) {
					params->TintColor = value;
					params->Owner->ApplyTexture();
				}
				return value;
			}
			/** @brief Returns a component of the fade color, or what the style takes it as */
			template<std::int32_t Index>
			static std::uint8_t GetTextureComponent(const TextureStyleParams* params) {
				return params->Owner->_textureColor[Index];
			}
			/** @brief Sets a component of the fade color, or what the style takes it as */
			template<std::int32_t Index>
			static std::uint8_t SetTextureComponent(TextureStyleParams* params, std::uint8_t value) {
				params->Owner->SetTextureColor(Index, value);
				return value;
			}
			/** @brief Returns the speed of a wave, the blue of the fade color as a signed byte */
			static std::int8_t GetWaveSpeed(const TextureStyleParams* params) {
				return std::int8_t(params->Owner->_textureColor[2]);
			}
			/** @brief Sets the speed of a wave, the blue of the fade color as a signed byte */
			static std::int8_t SetWaveSpeed(TextureStyleParams* params, std::int8_t value) {
				params->Owner->SetTextureColor(2, std::uint8_t(value));
				return value;
			}
			/** @brief Returns the stars of a warp horizon, or what the style takes them as */
			static bool GetTextureStars(const TextureStyleParams* params) {
				return params->Owner->_textureStars;
			}
			/** @brief Sets the stars of a warp horizon, or what the style takes them as */
			static bool SetTextureStars(TextureStyleParams* params, bool value) {
				if (params->Owner->_textureStars != value) {
					params->Owner->_textureStars = value;
					params->Owner->ApplyTexture();
				}
				return value;
			}
			/** @brief Returns the fade color */
			static jjPALCOLOR GetFadeColor(const TextureStyleParams* params);
			/** @brief Sets the fade color */
			static void SetFadeColor(TextureStyleParams* params, jjPALCOLOR color);

			/** @brief Rotation angle of the layer */
			std::int32_t rotationAngle = 0;
			/** @brief Radius multiplier applied when rotating the layer */
			std::int32_t rotationRadiusMultiplier = 0;
			/** @brief Whether the layer tiles vertically */
			bool tileHeight = false;
			/** @brief Whether the layer tiles horizontally */
			bool tileWidth = false;
			/** @brief Whether the layer's visible region is limited */
			bool limitVisibleRegion = false;
			/** @brief Whether the layer has a tile map */
			bool hasTileMap = false;
			/** @brief Whether the layer contains any tiles */
			bool hasTiles = false;

			/** @brief Returns the draw order of all layers */
			static CScriptArray* jjLayerOrderGet();
			/** @brief Sets the draw order of all layers */
			static bool jjLayerOrderSet(const CScriptArray& order);
			/** @brief Loads the given layers from another level file */
			static CScriptArray* jjLayersFromLevel(const String& filename, const CScriptArray& layerIDs, int32_t tileIDAdjustmentFactor);
			/** @brief Imports tiles from another tileset into the current one */
			static bool jjTilesFromTileset(const String& filename, std::uint32_t firstTileID, std::uint32_t tileCount, const CScriptArray* paletteColorMapping);
			/** @brief Imports tiles from another tileset as 32-bit tiles, in the colors of a palette or of the tileset's own */
			static bool jjTilesFromTileset32(const String& filename, std::uint32_t firstTileID, std::uint32_t tileCount, const jjPAL* palette);

			/** @brief Returns the tile at the given tile coordinates */
			std::uint16_t tileGet(int xTile, int yTile);
			/** @brief Sets the tile at the given tile coordinates */
			std::uint16_t tileSet(int xTile, int yTile, std::uint16_t newTile);
			/** @brief Marks a rectangular tile area as runtime-settable */
			void generateSettableTileArea(int xTile, int yTile, int width, int height);
			/** @brief Marks the whole layer as runtime-settable */
			void generateSettableTileAreaAll();

			/** @brief Horizontal speed mode of the layer */
			std::int32_t SpeedModeX = 0;
			/** @brief Vertical speed mode of the layer */
			std::int32_t SpeedModeY = 0;

		private:
			std::int32_t _refCount;
			// The engine tile-map layer this proxy is bound to (-1 = a standalone layer not part of the level). The loader
			// caches one proxy per level layer and pushes the writable fields into the engine each frame (see SyncLayerProperties).
			std::int32_t _layerIndex = -1;
			// Last-synced offsets, used to tell a script write apart from the engine's auto-scroll (which advances OffsetX/Y
			// itself): if the proxy value still equals this, the engine's value is authoritative; otherwise the script changed it.
			float _syncedOffsetX = 0.0f;
			float _syncedOffsetY = 0.0f;
			// The sprite mode of a standalone layer, and the parameter of any layer - it's kept even while the mode doesn't use it,
			// as in JJ2+. The mode of a level layer is its renderer type, see get_spriteMode(), unless a script set one, some
			// modes are only like the renderer type they're drawn with.
			std::uint32_t _spriteMode = 0;
			std::uint8_t _spriteParam = 0;
			bool _hasSpriteMode = false;
			// The texture of the layer as scripts see it, the layer of the level is drawn as it says (see ApplyTexture()) - a
			// style the game can't draw is still kept as the script set it. The fade color and the stars are shared by the
			// styles, as in JJ2+, each takes them in its own way.
			std::int32_t _textureStyle = 0;
			std::int32_t _texture = 0;
			std::int32_t _textureSurface = Untextured;
			std::uint8_t _textureColor[3] = {};
			bool _textureStars = false;
			// The image jjPIXELMAP::makeTexture() gave the layer (TEXTURE::CUSTOM) as the script made it, see SetCustomTexture()
			std::unique_ptr<std::uint8_t[]> _customTexture;

			// Sets a component of the fade color and makes the layer drawn with it
			void SetTextureColor(std::int32_t index, std::uint8_t value);
			// Gives the layer of the level the image the script made, or the one it has again after the style changed - a sky, a
			// tunnel or a cylinder of JJ2+ is drawn from the upper half of the palette, index | 128 (measured), a wave and the
			// other styles as it is. Returns `false` if the layer isn't one of the level.
			bool SetCustomTexture(const std::uint8_t* pixels);
			// Takes the texture properties of the layer of the level as it's drawn now, when the proxy is made
			void InitializeTexture(const Tiles::TileMapLayer& layer);
			// Points the parameters of the texture styles to this layer
			void BindTextureParams();

			friend class LevelScriptLoader;
			friend class jjPIXELMAP;	// A pixel map can be made of the tiles of a layer
			friend class jjCOLORMAP;
		};

		/**
		 * @brief A tile of the level, static or animated
		 *
		 * There's one instance for every tile ID, `jjTiles[id]`, which a script reads the frames and the speed of an
		 * animated tile through, and changes them through `jjAnimatedTiles[id]`. A static tile is its only frame.
		 */
		class jjTILE
		{
		public:
			/** @brief Creates a new instance for a tile ID, with @ref TILE::ANIMATED for an animated tile and without flips */
			explicit jjTILE(std::uint16_t tileID);

			/** @brief Increments the reference count */
			void AddRef();
			/** @brief Decrements the reference count */
			void Release();

			/** @brief Returns the tile for a tile ID of `jjTiles`, or `nullptr` if the level has no such tile */
			static jjTILE* get_jjTiles(std::uint16_t tileID);
			/** @brief Returns the animated tile of the given index, or `nullptr` if the level has no such animated tile */
			static jjTILE* get_jjAnimatedTiles(std::uint16_t tileID);

			/** @brief Returns the frames of the tile as it plays them, with the wait and ping-pong frames */
			CScriptArray* getFrames() const;
			/** @brief Replaces the frames of an animated tile, returns `false` for a static one or no frames */
			bool setFrames(const CScriptArray& frames, bool pingPong, std::uint16_t wait, std::uint16_t randomWait, std::uint16_t pingPongWait);
			/** @brief Returns the frames per second of an animated tile, 0 for a static one */
			std::uint8_t get_fps() const;
			/** @brief Sets the frames per second of an animated tile */
			std::uint8_t set_fps(std::uint8_t value);
			/** @brief Returns the tile ID, as `jjTiles` takes it */
			std::uint16_t get_tileID() const;

		private:
			std::int32_t _refCount;
			std::uint16_t _tileID;
		};

		/** @brief Controls which parts of a player are drawn and how */
		struct jjPLAYERDRAW
		{
			/** @brief Whether the player's name is drawn */
			bool name;
			/** @brief Whether the player's sprite is drawn */
			bool sprite;
			/** @brief Whether the sugar rush effect is drawn */
			bool sugarRush;
			/** @brief Whether the gun muzzle flash is drawn */
			bool gunFlash;
			/** @brief Whether the invincibility effect is drawn */
			bool invincibility;
			/** @brief Whether the motion trail is drawn */
			bool trail;
			/** @brief Whether morphing explosions are drawn */
			bool morphingExplosions;
			/** @brief Whether the airboard bouncing motion is drawn */
			bool airboardBouncingMotion;
			/** @brief Whether the airboard puff effect is drawn */
			bool airboardPuff;
			/** @brief Sprite blend mode used to draw the player */
			std::int32_t spriteMode;
			/** @brief Parameter for the sprite blend mode */
			std::uint8_t spriteParam;
			/** @brief Light type emitted by the player */
			std::int32_t lightType;
			/** @brief Brightness of the light around the player */
			std::int8_t light;
			/** @brief Layer the player is drawn on */
			std::int32_t layer;
			/** @brief Current sprite frame index */
			std::uint32_t curFrame;
			/** @brief Rotation angle applied when drawing */
			std::int32_t angle;
			/** @brief Horizontal draw offset */
			float xOffset;
			/** @brief Vertical draw offset */
			float yOffset;
			/** @brief Horizontal draw scale */
			float xScale;
			/** @brief Vertical draw scale */
			float yScale;
			/** @brief Capture-the-flag flag state */
			std::int32_t flag;

			/** @brief Returns whether the given shield effect is drawn */
			bool get_shield(std::int32_t shield) const;
			/** @brief Sets whether the given shield effect is drawn */
			bool set_shield(std::int32_t shield, bool enable);
			/** @brief Returns the player this draw configuration belongs to */
			jjPLAYER* get_player() const;
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
	}

	using namespace Legacy;

	/** @brief Returns the level object at the given index */
	jjOBJ* get_jjObjects(std::int32_t index);
	/** @brief Returns the object preset for the given event id */
	jjOBJ* get_jjObjectPresets(std::int8_t id);

	/** @brief Spawns a tile-explosion particle effect at the given tile */
	void jjAddParticleTileExplosion(std::uint16_t xTile, std::uint16_t yTile, std::uint16_t tile, bool collapseSceneryStyle);
	/** @brief Spawns a pixel-explosion particle effect at the given position */
	void jjAddParticlePixelExplosion(float xPixel, float yPixel, int curFrame, int direction, int mode);

	/** @brief Returns the particle at the given index */
	jjPARTICLE* GetParticle(std::int32_t index);

	/** @brief Spawns and returns a new particle of the given type */
	jjPARTICLE* AddParticle(std::int32_t particleType);

	/** @brief Returns the total number of players */
	std::int32_t get_jjPlayerCount();
	/** @brief Returns the number of local players */
	std::int32_t get_jjLocalPlayerCount();

	/** @brief Returns the primary local player */
	jjPLAYER* get_jjP();
	/** @brief Returns the player at the given index */
	jjPLAYER* get_jjPlayers(std::uint8_t index);
	/** @brief Returns the local player at the given index */
	jjPLAYER* get_jjLocalPlayers(std::uint8_t index);

	/** @brief Runs MLLE (level editor) level setup */
	bool mlleSetup();
	/** @brief Spawns MLLE off-grid objects */
	void mlleSpawnOffgrids();
	/** @brief Spawns MLLE off-grid objects for the local player */
	void mlleSpawnOffgridsLocal();
	/** @brief Returns the layer of the given name, as MLLE named it, or `nullptr` */
	Legacy::jjLAYER* mlleGetLayer(const String& name);

	/** @brief Returns the sine of the given table angle */
	float get_sinTable(std::uint32_t angle);
	/** @brief Returns the cosine of the given table angle */
	float get_cosTable(std::uint32_t angle);
	/** @brief Returns a random 32-bit word */
	std::uint32_t RandWord32();
	/** @brief Returns the current Unix time in seconds */
	std::uint64_t unixTimeSec();
	/** @brief Returns the current Unix time in milliseconds */
	std::uint64_t unixTimeMs();

	/** @brief Returns whether the given regular expression is valid */
	bool jjRegexIsValid(const String& expression);
	/** @brief Returns whether the text fully matches the regular expression */
	bool jjRegexMatch(const String& text, const String& expression, bool ignoreCase);
	/** @brief Returns whether the text fully matches the regular expression, outputting captured groups */
	bool jjRegexMatchWithResults(const String& text, const String& expression, CScriptArray& results, bool ignoreCase);
	/** @brief Returns whether the regular expression is found anywhere in the text */
	bool jjRegexSearch(const String& text, const String& expression, bool ignoreCase);
	/** @brief Returns whether the regular expression is found anywhere in the text, outputting captured groups */
	bool jjRegexSearchWithResults(const String& text, const String& expression, CScriptArray& results, bool ignoreCase);
	/** @brief Returns the text with regular expression matches replaced */
	String jjRegexReplace(const String& text, const String& expression, const String& replacement, bool ignoreCase);

	/** @brief Returns the current frames per second */
	std::int32_t GetFPS();

	/** @brief Returns whether the local player is a server administrator */
	bool isAdmin();

	/** @brief Returns the file name of the current level */
	String getLevelFileName();
	/** @brief Returns the display name of the current level */
	String getCurrLevelName();
	/** @brief Sets the display name of the current level */
	void setCurrLevelName(const String& in);
	/** @brief Returns the file name of the current tileset */
	String get_jjTilesetFileName();

	/** @brief Returns the current game state */
	std::int32_t get_gameState();

	// TODO

	std::int32_t getBorderWidth();
	/** @brief Returns the height of the screen border */
	std::int32_t getBorderHeight();
	/** @brief Returns whether split-screen is forced */
	bool getSplitscreenType();
	/** @brief Sets whether split-screen is forced */
	bool setSplitscreenType(bool value);

	// TODO

	std::int32_t get_teamScore(std::int32_t color);
	/** @brief Returns the maximum player health */
	std::int32_t GetMaxHealth();
	/** @brief Returns the starting player health */
	std::int32_t GetStartHealth();

	// TODO

	void jjSetDarknessColor(jjPALCOLOR color);
	/** @brief Sets the fade color from red, green and blue components */
	void jjSetFadeColors(std::uint8_t red, std::uint8_t green, std::uint8_t blue);
	/** @brief Sets the fade color from a palette index */
	void jjSetFadeColorsFromPalette(std::uint8_t paletteColorID);
	/** @brief Sets the fade color from a palette color */
	void jjSetFadeColorsFromPalcolor(jjPALCOLOR color);
	/** @brief Returns the current fade color */
	jjPALCOLOR jjGetFadeColors();
	/** @brief Recomputes the textured background */
	void jjUpdateTexturedBG();
	/** @brief Returns the texture used by the textured background */
	std::int32_t get_jjTexturedBGTexture();
	/** @brief Sets the texture used by the textured background */
	std::int32_t set_jjTexturedBGTexture(std::int32_t texture);
	/** @brief Returns the textured background style */
	std::int32_t get_jjTexturedBGStyle();
	/** @brief Sets the textured background style */
	std::int32_t set_jjTexturedBGStyle(std::int32_t style);
	/** @brief Returns whether the textured background is used */
	bool get_jjTexturedBGUsed();
	/** @brief Sets whether the textured background is used */
	bool set_jjTexturedBGUsed(bool used);
	/** @brief Returns whether stars are drawn in the textured background */
	bool get_jjTexturedBGStars();
	/** @brief Sets whether stars are drawn in the textured background */
	bool set_jjTexturedBGStars(bool used);
	/** @brief Returns the horizontal fade position of the textured background */
	float get_jjTexturedBGFadePositionX();
	/** @brief Sets the horizontal fade position of the textured background */
	float set_jjTexturedBGFadePositionX(float value);
	/** @brief Returns the vertical fade position of the textured background */
	float get_jjTexturedBGFadePositionY();
	/** @brief Sets the vertical fade position of the textured background */
	float set_jjTexturedBGFadePositionY(float value);

	/** @brief Returns whether the given team is enabled */
	bool getEnabledTeam(std::uint8_t team);

	/** @brief Returns whether the given key is currently held down */
	bool getKeyDown(std::uint8_t key);
	/** @brief Returns the cursor's horizontal position */
	std::int32_t getCursorX();
	/** @brief Returns the cursor's vertical position */
	std::int32_t getCursorY();

	/** @brief Plays a sound sample at the given position */
	void playSample(float xPixel, float yPixel, std::int32_t sample, std::int32_t volume, std::int32_t frequency);
	/** @brief Plays a looped sound sample on a channel and returns the channel */
	std::int32_t playLoopedSample(float xPixel, float yPixel, std::int32_t sample, std::int32_t channel, std::int32_t volume, std::int32_t frequency);
	/** @brief Plays a sound sample with priority, ignoring position */
	void playPrioritySample(std::int32_t sample);
	/** @brief Returns whether the given sample is loaded */
	bool isSampleLoaded(std::int32_t sample);
	/** @brief Loads a sound sample from a file */
	bool loadSample(std::int32_t sample, const String& filename);

	/** @brief Returns whether layer 8 uses its own scrolling speeds */
	bool getUseLayer8Speeds();
	/** @brief Sets whether layer 8 uses its own scrolling speeds */
	bool setUseLayer8Speeds(bool value);

	/** @brief Returns the weapon settings at the given index */
	jjWEAPON* get_jjWEAPON(int index);

	/** @brief Returns the character settings at the given index */
	jjCHARACTER* get_jjCHARACTER(int index);

	/** @brief Returns the event id at the given tile coordinates */
	std::int32_t GetEvent(std::uint16_t tx, std::uint16_t ty);
	/** @brief Returns a field of the event at the given tile coordinates */
	std::int32_t GetEventParamWrapper(std::uint16_t tx, std::uint16_t ty, std::int32_t offset, std::int32_t length);
	/** @brief Sets the event id at the given tile coordinates */
	void SetEventByte(std::uint16_t tx, std::uint16_t ty, std::uint8_t newEventId);
	/** @brief Sets a field of the event at the given tile coordinates */
	void SetEventParam(std::uint16_t tx, std::uint16_t ty, int8_t offset, std::int8_t length, std::int32_t newValue);
	/** @brief Returns the collision type of the given tile */
	std::int8_t GetTileType(std::uint16_t tile);
	/** @brief Sets the collision type of the given tile */
	std::int8_t SetTileType(std::uint16_t tile, std::uint16_t value);
	/** @brief Returns whether the given tile uses 32-bit colors, `jjTileIs32Bit[]` */
	bool GetTileIs32Bit(std::uint16_t tile);

	// TODO

	std::uint16_t jjGetStaticTile(std::uint16_t tileID);
	/** @brief Returns the tile at the given coordinates on the given layer */
	std::uint16_t jjTileGet(std::uint8_t layer, std::int32_t xTile, std::int32_t yTile);
	/** @brief Sets the tile at the given coordinates on the given layer */
	std::uint16_t jjTileSet(std::uint8_t layer, std::int32_t xTile, std::int32_t yTile, std::uint16_t newTile);
	/** @brief Marks a rectangular tile area on the given layer as runtime-settable */
	void jjGenerateSettableTileArea(std::uint8_t layer, std::int32_t xTile, std::int32_t yTile, std::int32_t width, std::int32_t height);

	// TODO

	bool jjMaskedPixel(std::int32_t xPixel, std::int32_t yPixel);
	/** @brief Returns whether the given pixel is masked (solid) on the given layer */
	bool jjMaskedPixelLayer(std::int32_t xPixel, std::int32_t yPixel, std::uint8_t layer);
	/** @brief Returns whether any pixel along the horizontal line is masked (solid) */
	bool jjMaskedHLine(std::int32_t xPixel, std::int32_t lineLength, std::int32_t yPixel);
	/** @brief Returns whether any pixel along the horizontal line is masked (solid) on the given layer */
	bool jjMaskedHLineLayer(std::int32_t xPixel, std::int32_t lineLength, std::int32_t yPixel, std::uint8_t layer);
	/** @brief Returns whether any pixel along the vertical line is masked (solid) */
	bool jjMaskedVLine(std::int32_t xPixel, std::int32_t yPixel, std::int32_t lineLength);
	/** @brief Returns whether any pixel along the vertical line is masked (solid) on the given layer */
	bool jjMaskedVLineLayer(std::int32_t xPixel, std::int32_t yPixel, std::int32_t lineLength, std::uint8_t layer);
	/** @brief Returns whether the topmost pixel along the vertical line is masked (solid) */
	bool jjMaskedTopVLine(std::int32_t xPixel, std::int32_t yPixel, std::int32_t lineLength);
	/** @brief Returns whether the topmost pixel along the vertical line is masked (solid) on the given layer */
	bool jjMaskedTopVLineLayer(std::int32_t xPixel, std::int32_t yPixel, std::int32_t lineLength, std::uint8_t layer);

	// TODO

	void jjSetModPosition(std::int32_t order, std::int32_t row, bool reset);
	/** @brief Slides a music channel's volume to the given level over time */
	void jjSlideModChannelVolume(std::int32_t channel, float volume, std::int32_t milliseconds);
	/** @brief Returns the current music order */
	std::int32_t jjGetModOrder();
	/** @brief Returns the current music row */
	std::int32_t jjGetModRow();
	/** @brief Returns the current music tempo */
	std::int32_t jjGetModTempo();
	/** @brief Sets the music tempo */
	void jjSetModTempo(std::uint8_t speed);
	/** @brief Returns the current music speed */
	std::int32_t jjGetModSpeed();
	/** @brief Sets the music speed */
	void jjSetModSpeed(std::uint8_t speed);

	/** @brief Returns the custom animation set id at the given index */
	std::uint32_t getCustomSetID(std::uint8_t index);
}

#endif