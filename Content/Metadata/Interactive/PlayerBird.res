{
	"Version": {
		"Target": "Jazz² Resurrection"
	},

	"Animations": {
		"Fly": {
			"Path": "BirdyYellow/fly.aura",
			"States": [ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 32, 64, 128, 129, 256, 512, 1024, 4096, 8192, 16384, 16385, 32768, 65536, 65537, 65538, 65539, 131072, 262144, 262145, 262146, 262147, 524288 ],
			"FrameRate": 6.25
		},
		"Charge": {
			"Path": "BirdyYellow/charge_ver.aura",
			"FrameRate": 6.25,
			"States": [ 546308096 ]
		},
		"ChargeUp": {
			"Path": "BirdyYellow/charge_diag_upright.aura",
			"FrameRate": 6.25,
			"States": [ 547356672 ]
		},
		"ChargeDown": {
			"Path": "BirdyYellow/charge_diag_downright_placeholder.aura",
			"FrameRate": 6.25,
			"States": [ 548405248 ]
		},

		"Perch": {
			"Path": "BirdyYellow/idle.aura",
			"FrameRate": 12.5,
			"States": [ 537919488 ]
		},
		"PerchTurnLeft": {
			"Path": "BirdyYellow/idle_turn_head_left.aura",
			"FrameRate": 12.5,
			"States": [ 538968064 ]
		},
		"PerchLookLeft": {
			"Path": "BirdyYellow/idle_look_left.aura",
			"FrameRate": 12.5,
			"States": [ 540016640 ]
		},
		"PerchTurnLeftBack": {
			"Path": "BirdyYellow/idle_turn_head_left_back.aura",
			"FrameRate": 16.67,
			"States": [ 541065216 ]
		},
		"PerchTurnRight": {
			"Path": "BirdyYellow/idle_turn_head_right.aura",
			"FrameRate": 8.34,
			"States": [ 542113792 ]
		},
		"PerchLookRight": {
			"Path": "BirdyYellow/idle_look_right.aura",
			"FrameRate": 12.5,
			"States": [ 543162368 ]
		},
		"PerchTurnRightBack": {
			"Path": "BirdyYellow/idle_turn_head_right_back.aura",
			"FrameRate": 16.67,
			"States": [ 544210944 ]
		},
		"PerchWorm": {
			"Path": "BirdyYellow/idle_worm.aura",
			"FrameRate": 1.92,
			"States": [ 545259520 ]
		},

		"Hurt": {
			"Path": "BirdyYellow/hurt.aura",
			"FrameRate": 5.0,
			"States": [ 2048 ]
		},
		"Death": {
			"Path": "BirdyYellow/die.aura",
			"FrameRate": 3,
			"States": [ 1073741839 ]
		},
		"EndOfLevel": {
			"Path": "BirdyYellow/fly.aura",
			"Flags": 1,
			"FrameRate": 6.25,
			"States": [ 1073741846 ]
		},

		"Corpse": {
			"Path": "BirdyYellow/corpse.aura",
			"States": [ 536870912 ]
		},
		"Feathers": {
			"Path": "BirdyYellow/feather_blue_and_yellow.aura",
			"States": [ 536870960 ]
		},

		"SugarRush": {
			"Path": "Common/sugar_rush_stars.aura",
			"States": [ 536870913 ]
		},
		"Shield": {
			"Path": "Common/player_shield.aura",
			"States": [ 536870928 ]
		},
		"ShieldFire": {
			"Path": "Common/shield_fire.aura",
			"States": [ 536870929 ]
		},
		"ShieldWater": {
			"Path": "Common/shield_water.aura",
			"States": [ 536870930 ]
		},
		"ShieldLightning": {
			"Path": "Common/shield_lightning.aura",
			"States": [ 536870931 ]
		},

		"WeaponFlare": {
			"Path": "Weapon/flare_hor_2.aura",
			"States": [ 536870950 ]
		}
	},

	"Sounds": {
		"ChangeWeapon": {
			"Paths": [ "UI/weapon_change.wav" ]
		},
		"EndOfLevel1": {
			"Paths": [ "Common/char_revup.wav" ]
		},
		"WarpIn": {
			"Paths": [ "Common/warp_in.wav" ]
		},
		"WarpOut": {
			"Paths": [ "Common/warp_out.wav" ]
		},
		"Hurt": {
			"Paths": [ "Birdy/fly_1.wav", "Birdy/fly_2.wav" ]
		},
		"HurtSoft": {
			"Paths": [ "Birdy/fly_1.wav", "Birdy/fly_2.wav" ]
		},
		"Die": {
			"Paths": [ "Common/gunsm1.wav" ]
		},
		"PickupAmmo": {
			"Paths": [ "Pickup/ammo.wav" ]
		},
		"PickupCoin": {
			"Paths": [ "Pickup/coin.wav" ]
		},
		"PickupGem": {
			"Paths": [ "Pickup/gem.wav" ]
		},
		"PickupOneUp": {
			"Paths": [ "Pickup/1up.wav" ]
		},
		"PickupDrink": {
			"Paths": [ "Pickup/food_drink_1.wav", "Pickup/food_drink_2.wav", "Pickup/food_drink_3.wav", "Pickup/food_drink_4.wav" ]
		},
		"PickupFood": {
			"Paths": [ "Pickup/food_edible_1.wav", "Pickup/food_edible_2.wav", "Pickup/food_edible_3.wav", "Pickup/food_edible_4.wav" ]
		},
		"PickupMaxCarrot": {
			"Paths": [ "Pickup/food_edible_1.wav" ]
		},
		"WeaponBlaster": {
			"Paths": [ "Weapon/bullet_blaster_jazz_4.wav" ]
		},
		"WeaponToaster": {
			"Paths": [ "Weapon/toaster.wav" ]
		},
		"WeaponThunderbolt": {
			"Paths": [ "Unknown/unknown_bonus1.wav" ]
		},
		"WeaponThunderboltStart": {
			"Paths": [ "Cinematics/opening_shot.wav" ]
		},
		"WeaponThunderboltEnd": {
			"Paths": [ "Pickup/shield_lightning_bullet_1.wav" ]
		},
		"BonusWarpNotEnoughCoins": {
			"Paths": [ "Object/bonus_not_enough_coins.wav" ]
		}
	}
}
