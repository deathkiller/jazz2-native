{
	"Version": {
		"Target": "Jazz2² Resurrection"
	},

	"Animations": {
		"Idle": {
			"Path": "Frog/idle.aura",
			"States": [ 0, 64, 128 ],
			"FrameRate": 8
		},
		"Walk": {
			"Path": "Frog/run.aura",
			"States": [ 1, 2, 3, 17, 18, 19, 129, 145 ],
			"FrameRate": 12
		},

		"Jump": {
			"Path": "Frog/jump_start.aura",
			"States": [ 4, 5, 6, 7, 262144, 262145, 262146, 262147 ],
			"Flags": 1,
			"FrameRate": 30
		},
		"Fall": {
			"Path": "Frog/fall.aura",
			"States": [ 8, 9, 10, 11, 65536, 65537, 65538, 65539 ],
			"FrameRate": 18
		},

		"Crouch": {
			"Path": "Frog/crouch.aura",
			"Flags": 1,
			"FrameRate": 40,
			"States": [ 32 ]
		},

		"Shoot": {
			"Path": "Frog/tongue_hor.aura",
			"States": [ 16, 144, 48 ],
			"FrameRate": 8
		},

		"LookupShoot": {
			"Path": "Frog/tongue_ver.aura",
			"States": [ 80 ],
			"FrameRate": 8
		},
		"Hurt": {
			"Path": "Frog/hurt.aura",
			"FrameRate": 6,
			"States": [ 2048, 1073741839 ]
		},
		"Corpse": {
			"Path": "Frog/hurt.aura",
			"States": [ 536870912 ]
		},
		"FallToIdle": {
			"Path": "Frog/fall_land.aura",
			"States": [ 1073741826 ],
			"FrameRate": 28
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
		
		"TransformFromJazz": {
			"Path": "Jazz/transform_frog.aura",
			"States": [ 1610612736 ],
			"FrameRate": 3
		},
		"TransformFromSpaz": {
			"Path": "Spaz/transform_frog.aura",
			"States": [ 1610612737 ],
			"FrameRate": 3
		},
		"TransformFromLori": {
			"Path": "Lori/transform_frog.aura",
			"States": [ 1610612738 ],
			"FrameRate": 3
		},
		
		"TransformToJazz": {
			"_Path": "Jazz/transform_frog_end.aura",
			"States": [ 1610612739 ],
			"FrameRate": 3
		},
		"TransformToSpaz": {
			"_Path": "Spaz/transform_frog_end.aura",
			"States": [ 1610612740 ],
			"FrameRate": 3
		},
		"TransformToLori": {
			"_Path": "Lori/transform_frog_end.aura",
			"States": [ 1610612741 ],
			"FrameRate": 3
		},
		"WarpInFreefall": {
			"Path": "Frog/jump_start.aura",
			"States": [ 1073741847 ]
		},
		"WarpOutFreefall": {
			"Path": "Frog/fall.aura",
			"States": [ 1073741848 ]
		},
		"WarpIn": {
			"Path": "Jazz/warp_in.aura",
			"States": [ 1073741843 ],
			"FrameOffset": 2,
		},
		"WarpOut": {
			"Path": "Jazz/warp_out.aura",
			"States": [ 1073741844 ],
			"FrameCount": 5,
		},
		"WarpInFreefall": {
			"Path": "Jazz/warp_in.aura",
			"States": [ 1073741843 ],
			"FrameOffset": 2,
		},
		"WarpOutFreefall": {
			"Path": "Jazz/warp_out.aura",
			"States": [ 1073741844 ],
			"FrameCount": 5,
		}
	},

	"Sounds": {
		"Tongue": {
			"Paths": [ "Frog/tongue.wav" ]
		},
		"Transform": {
			"Paths": [ "Frog/transform.wav" ]
		},
		"Jump": {
			"Paths": [ "Frog/noise_1.wav" ]
		},
		"Land": {
			"Paths": [ "Frog/noise_3.wav", "Frog/noise_5.wav", "Frog/noise_6.wav" ]
		},
		"Hurt": {
			"Paths": [ "Frog/noise_4.wav" ]
		},
		"HurtSoft": {
			"Paths": [ "Frog/noise_4.wav" ]
		},
		"EndOfLevel": {
			"Paths": [ "Jazz/level_complete.wav" ]
		},
		"EndOfLevel2": {
			"Paths": [ "Weapon/ricochet_bullet_3.wav" ]
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
		"WarpIn": {
			"Paths": [ "Common/warp_in.wav" ]
		},
		"WarpOut": {
			"Paths": [ "Common/warp_out.wav" ]
		},
		"BonusWarpNotEnoughCoins": {
			"Paths": [ "Object/bonus_not_enough_coins.wav" ]
		}
	}
}